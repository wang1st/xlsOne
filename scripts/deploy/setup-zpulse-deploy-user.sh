#!/bin/bash
# One-time z-pulse.cn server setup. Run as root on the production server.

set -euo pipefail
umask 027
PATH="/usr/sbin:/usr/bin:/sbin:/bin"
export PATH

DEPLOY_USER="xlsone-deploy"
EXPECTED_HOME="/home/$DEPLOY_USER"
UPLOAD_ROOT="/srv/xlsone-upload"
PROMOTE_SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/zpulse-promote.sh"
PROMOTE_TARGET="/usr/local/sbin/xlsone-promote"
SUDOERS_FILE="/etc/sudoers.d/xlsone-deploy"

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this setup script as root." >&2
    exit 1
fi

for bootstrap_command in mktemp ssh-keygen; do
    if ! command -v "$bootstrap_command" >/dev/null 2>&1; then
        echo "Missing required server command: $bootstrap_command" >&2
        exit 1
    fi
done

public_key="${ZPULSE_DEPLOY_PUBLIC_KEY:-}"
if [[ -z "$public_key" ]]; then
    IFS= read -r public_key
fi
if [[ "$public_key" == *$'\n'* || "$public_key" == *$'\r'* || ! "$public_key" =~ ^ssh-ed25519[[:space:]][A-Za-z0-9+/=]+([[:space:]][^[:cntrl:]]*)?$ ]]; then
    echo "ZPULSE_DEPLOY_PUBLIC_KEY must contain one Ed25519 OpenSSH public key." >&2
    exit 1
fi

public_key_tmp="$(mktemp)"
printf '%s\n' "$public_key" > "$public_key_tmp"
chmod 0600 "$public_key_tmp"
if ! ssh-keygen -l -f "$public_key_tmp" >/dev/null 2>&1; then
    rm -f "$public_key_tmp"
    echo "ZPULSE_DEPLOY_PUBLIC_KEY is not a valid OpenSSH public key." >&2
    exit 1
fi
rm -f "$public_key_tmp"

[[ -f "$PROMOTE_SOURCE" ]] || {
    echo "Missing $PROMOTE_SOURCE; copy both deployment scripts to the server." >&2
    exit 1
}
missing_commands=()
for command_name in sha256sum flock visudo ssh-keygen realpath stat awk install cp find; do
    command -v "$command_name" >/dev/null 2>&1 || missing_commands+=("$command_name")
done
[[ -x /usr/bin/python3 ]] || missing_commands+=("/usr/bin/python3")
if [[ ${#missing_commands[@]} -ne 0 ]]; then
    printf 'Missing required server command: %s\n' "${missing_commands[@]}" >&2
    exit 1
fi
bash -n "$PROMOTE_SOURCE"

if ! id "$DEPLOY_USER" >/dev/null 2>&1; then
    useradd --create-home --home-dir "$EXPECTED_HOME" --shell /bin/bash "$DEPLOY_USER"
fi
usermod --shell /bin/bash "$DEPLOY_USER"
usermod --groups "" "$DEPLOY_USER"
passwd --lock "$DEPLOY_USER" >/dev/null 2>&1 || true

home_dir="$(getent passwd "$DEPLOY_USER" | cut -d: -f6)"
[[ "$home_dir" == "$EXPECTED_HOME" && -d "$home_dir" && ! -L "$home_dir" ]] || {
    echo "Unsafe home directory for $DEPLOY_USER: $home_dir" >&2
    exit 1
}
# The deploy account must not be able to replace its authorized_keys with a
# less-restricted key. sshd can read this root-owned public file, while the
# account can only traverse the directories.
chown -R root:root "$home_dir"
chmod 0755 "$home_dir"
install -d -o root -g root -m 0755 "$home_dir/.ssh"
authorized_keys="$home_dir/.ssh/authorized_keys"
key_options="no-agent-forwarding,no-port-forwarding,no-pty,no-user-rc,no-X11-forwarding"
printf '%s %s\n' "$key_options" "$public_key" > "$authorized_keys"
chown root:root "$authorized_keys"
chmod 0644 "$authorized_keys"
if command -v restorecon >/dev/null 2>&1; then
    restorecon -RF "$home_dir/.ssh"
fi

install -d -o root -g root -m 0755 "$UPLOAD_ROOT"
deploy_group="$(id -gn "$DEPLOY_USER")"
install -d -o "$DEPLOY_USER" -g "$deploy_group" -m 0750 "$UPLOAD_ROOT/incoming"
install -d -o root -g root -m 0700 "$UPLOAD_ROOT/processing"
install -o root -g root -m 0755 "$PROMOTE_SOURCE" "$PROMOTE_TARGET"

sudoers_tmp="$(mktemp)"
trap 'rm -f "$sudoers_tmp"' EXIT
cat > "$sudoers_tmp" <<'EOF'
Defaults:xlsone-deploy !requiretty
xlsone-deploy ALL=(root) NOPASSWD: /usr/local/sbin/xlsone-promote run-* *
EOF
chmod 0440 "$sudoers_tmp"
visudo -cf "$sudoers_tmp"
install -o root -g root -m 0440 "$sudoers_tmp" "$SUDOERS_FILE"
visudo -cf "$SUDOERS_FILE"

echo "Configured $DEPLOY_USER with a restricted non-root SSH shell and the fixed root promotion command."
