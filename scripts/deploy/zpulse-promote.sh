#!/bin/bash
# Promote a pre-validated GitHub Actions payload into the z-pulse.cn live tree.
# Install this file as /usr/local/sbin/xlsone-promote, owned by root and mode 0755.

set -euo pipefail
umask 022
PATH="/usr/sbin:/usr/bin:/sbin:/bin"
export PATH
cd /

UPLOAD_ROOT="/srv/xlsone-upload"
INCOMING_ROOT="$UPLOAD_ROOT/incoming"
PROCESSING_ROOT="$UPLOAD_ROOT/processing"
SITE_ROOT="/var/www/z-pulse.cn"
LIVE_STAGING_ROOT="/var/www/.xlsone-download-incoming"
BACKEND_MANIFEST_DIR="/opt/xlsone-activation/site/api"
BACKUP_ROOT="/var/lib/xlsone-deploy/backups"
DOWNLOAD_BASE_URL="https://z-pulse.cn/downloads"

die() {
    echo "xlsone-promote: $*" >&2
    exit 1
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
    die "usage: xlsone-promote <release-id> <version> [--replace-same-version]"
fi

release_id="$1"
version="$2"
replace_same_version=0
if [[ $# -eq 3 ]]; then
    [[ "$3" == "--replace-same-version" ]] || die "invalid promotion option"
    replace_same_version=1
fi

[[ "$release_id" =~ ^run-[0-9]+-[0-9]+$ ]] || die "invalid release id"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid version"

processing_release=""
incoming_release=""
backup=""
metadata_transaction_active=0
release_committed=0
staged_paths=()
published_paths=()

cleanup_on_exit() {
    local status=$?
    local rollback_performed=0
    local rollback_ok=1
    trap - EXIT HUP INT TERM
    set +e
    if [[ "$metadata_transaction_active" -eq 1 ]]; then
        rollback_performed=1
        if ! rollback_metadata; then
            rollback_ok=0
            echo "xlsone-promote: metadata rollback was incomplete; restore $backup manually" >&2
        fi
    fi
    if (( rollback_ok == 1 && (release_committed == 0 || rollback_performed == 1) )); then
        for published_path in "${published_paths[@]+"${published_paths[@]}"}"; do
            package_backup="$backup/packages/$(basename -- "$published_path")"
            if [[ -f "$package_backup" && ! -L "$package_backup" ]]; then
                restore_tmp="${published_path}.${release_id}.rollback.tmp"
                if install -m 0644 "$package_backup" "$restore_tmp"; then
                    mv -- "$restore_tmp" "$published_path" || rollback_ok=0
                else
                    rollback_ok=0
                fi
                rm -f -- "$restore_tmp"
            else
                rm -f -- "$published_path"
            fi
        done
    fi
    if (( rollback_ok == 1 && release_committed == 0 )) && [[ -n "$backup" ]]; then
        rm -rf -- "$backup"
    fi
    if [[ -n "$processing_release" ]]; then
        rm -rf -- "$processing_release"
    fi
    if [[ -n "$incoming_release" ]]; then
        rm -rf -- "$incoming_release"
    fi
    for staged_path in "${staged_paths[@]+"${staged_paths[@]}"}"; do
        rm -f -- "$staged_path"
    done
    exit "$status"
}
trap cleanup_on_exit EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

[[ -d "$INCOMING_ROOT" && ! -L "$INCOMING_ROOT" ]] || die "unsafe incoming root"
incoming_release="$INCOMING_ROOT/$release_id"
[[ -e "$incoming_release" || -L "$incoming_release" ]] || die "release upload does not exist"

# Copy into a new root-only snapshot. Moving/chmodding the upload is not enough:
# the upload user could retain a writable directory file descriptor across mv.
exec 9>"/run/lock/xlsone-promote.lock"
flock -x 9
install -d -o root -g root -m 0700 "$PROCESSING_ROOT"
[[ -d "$PROCESSING_ROOT" && ! -L "$PROCESSING_ROOT" ]] || die "unsafe processing root"
chmod 0700 "$PROCESSING_ROOT"
processing_release="$PROCESSING_ROOT/$release_id"
[[ ! -e "$processing_release" && ! -L "$processing_release" ]] || die "release is already being processed"
install -d -o root -g root -m 0700 "$processing_release"
cp -a -- "$incoming_release" "$processing_release/payload"

release_dir="$processing_release/payload"
[[ -d "$release_dir" && ! -L "$release_dir" ]] || die "release payload must be a directory"
if [[ -n "$(find "$release_dir" -type l -print -quit)" ]]; then
    die "release payload must not contain symbolic links"
fi
if [[ -n "$(find "$release_dir" ! -type d ! -type f -print -quit)" ]]; then
    die "release payload must contain only directories and regular files"
fi
if [[ -n "$(find "$release_dir" -type f -links +1 -print -quit)" ]]; then
    die "release payload must not contain hard-linked files"
fi
chown -R root:root "$processing_release"
chmod -R u+rwX,go-rwx "$processing_release"

release_dir="$(realpath -e "$release_dir")"
case "$release_dir/" in
    "$processing_release"/*) ;;
    *) die "processing directory escapes upload root" ;;
esac

payload_downloads="$release_dir/downloads"
payload_manifest="$release_dir/api/version.json"
payload_checksums="$payload_downloads/checksums.txt"

required_packages=(
    "xlsone-${version}-windows-amd64.msi"
    "xlsone-${version}-windows-amd64.zip"
    "xlsOne-${version}-macos-universal.dmg"
    "xlsOne-${version}-linux-amd64.deb"
    "xlsOne-${version}-linux-arm64.deb"
)

[[ -f "$payload_manifest" ]] || die "missing api/version.json"
[[ -f "$payload_checksums" ]] || die "missing downloads/checksums.txt"
[[ "$(find "$release_dir" -mindepth 1 -maxdepth 1 | wc -l)" -eq 2 ]] || die "payload root must contain only api and downloads"
[[ -d "$release_dir/api" && ! -L "$release_dir/api" ]] || die "missing api directory"
[[ -d "$payload_downloads" && ! -L "$payload_downloads" ]] || die "missing downloads directory"
[[ "$(find "$release_dir/api" -mindepth 1 -maxdepth 1 | wc -l)" -eq 1 ]] || die "api must contain only version.json"
[[ "$(find "$payload_downloads" -mindepth 1 -maxdepth 1 | wc -l)" -eq 6 ]] || die "downloads must contain exactly five packages and checksums.txt"
[[ "$(find "$payload_downloads" -mindepth 1 -maxdepth 1 -type f | wc -l)" -eq 6 ]] || die "downloads entries must all be regular files"
[[ "$(stat -c %s "$payload_manifest")" -le 1048576 ]] || die "version.json is unexpectedly large"
[[ "$(stat -c %s "$payload_checksums")" -le 65536 ]] || die "checksums.txt is unexpectedly large"

for package in "${required_packages[@]}"; do
    [[ -f "$payload_downloads/$package" ]] || die "missing package: $package"
done

/usr/bin/python3 -I - "$payload_manifest" "$payload_checksums" "$version" "$DOWNLOAD_BASE_URL" "${required_packages[@]}" <<'PY'
import hashlib
import json
import re
import sys
from pathlib import Path

manifest_path = Path(sys.argv[1])
checksums_path = Path(sys.argv[2])
version = sys.argv[3]
base_url = sys.argv[4].rstrip("/")
packages = sys.argv[5:]

def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result

manifest = json.loads(
    manifest_path.read_text(encoding="utf-8"),
    object_pairs_hook=reject_duplicate_keys,
)
if set(manifest) != {"latest_version", "changelog", "downloads", "checksums"}:
    raise SystemExit("manifest fields do not match the release contract")
if manifest.get("latest_version") != version:
    raise SystemExit("manifest version mismatch")
if not isinstance(manifest.get("changelog"), str):
    raise SystemExit("manifest changelog must be a string")

expected_downloads = {
    "windows_amd64": f"{base_url}/xlsone-{version}-windows-amd64.msi",
    "windows_amd64_zip": f"{base_url}/xlsone-{version}-windows-amd64.zip",
    "macos": f"{base_url}/xlsOne-{version}-macos-universal.dmg",
    "linux_amd64": f"{base_url}/xlsOne-{version}-linux-amd64.deb",
    "linux_arm64": f"{base_url}/xlsOne-{version}-linux-arm64.deb",
}
if manifest.get("downloads") != expected_downloads:
    raise SystemExit("manifest download URLs do not match the release payload")

parsed_checksums = {}
lines = checksums_path.read_text(encoding="utf-8").splitlines()
if len(lines) != len(packages):
    raise SystemExit("checksum file must contain exactly five lines")
for line in lines:
    try:
        digest, filename = line.split(maxsplit=1)
    except ValueError as exc:
        raise SystemExit("invalid checksum line") from exc
    if filename.startswith("*"):
        filename = filename[1:]
    if not re.fullmatch(r"[0-9A-Fa-f]{64}", digest):
        raise SystemExit("invalid SHA-256 digest")
    if filename not in packages or filename in parsed_checksums:
        raise SystemExit("checksum filename is unexpected or duplicated")
    parsed_checksums[filename] = digest.lower()

if set(parsed_checksums) != set(packages):
    raise SystemExit("checksum file does not describe exactly the five release packages")
if manifest.get("checksums") != parsed_checksums:
    raise SystemExit("manifest checksums differ from checksums.txt")

for package in packages:
    digest = hashlib.sha256()
    with (checksums_path.parent / package).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != parsed_checksums[package]:
        raise SystemExit(f"package checksum mismatch: {package}")
PY

live_downloads="$SITE_ROOT/downloads"
live_api="$SITE_ROOT/api"
backup="$BACKUP_ROOT/$release_id"

[[ -d "$SITE_ROOT" && ! -L "$SITE_ROOT" ]] || die "unsafe site root"
install -d -o root -g root -m 0755 "$live_downloads" "$live_api" "$BACKEND_MANIFEST_DIR" "$BACKUP_ROOT"
install -d -o root -g root -m 0700 "$LIVE_STAGING_ROOT"
for directory in "$live_downloads" "$live_api" "$BACKEND_MANIFEST_DIR" "$BACKUP_ROOT" "$LIVE_STAGING_ROOT"; do
    [[ -d "$directory" && ! -L "$directory" ]] || die "unsafe deployment directory: $directory"
done
[[ "$(stat -c %d "$live_downloads")" == "$(stat -c %d "$LIVE_STAGING_ROOT")" ]] || die "live staging must be on the downloads filesystem"
[[ ! -e "$backup" && ! -L "$backup" ]] || die "backup directory already exists"
install -d -o root -g root -m 0700 "$backup"

for existing_manifest in "$live_api/version.json" "$BACKEND_MANIFEST_DIR/version.json"; do
    if [[ -e "$existing_manifest" || -L "$existing_manifest" ]]; then
        [[ -f "$existing_manifest" && ! -L "$existing_manifest" ]] || die "unsafe existing metadata: $existing_manifest"
    fi
done

deployment_mode="$(/usr/bin/python3 -I - "$version" "$live_api/version.json" "$BACKEND_MANIFEST_DIR/version.json" <<'PY'
import json
import re
import sys
from pathlib import Path

new_version = sys.argv[1]
paths = [Path(value) for value in sys.argv[2:]]
versions = []
for path in paths:
    if not path.exists():
        continue
    manifest = json.loads(path.read_text(encoding="utf-8"))
    value = manifest.get("latest_version")
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value):
        raise SystemExit(f"invalid existing version manifest: {path}")
    versions.append(value)

if len(set(versions)) > 1:
    raise SystemExit("public and backend version manifests disagree")
if not versions:
    print("first")
else:
    current = tuple(int(part) for part in versions[0].split("."))
    requested = tuple(int(part) for part in new_version.split("."))
    if requested < current:
        raise SystemExit(f"version downgrade rejected: {new_version} < {versions[0]}")
    print("same" if requested == current else "upgrade")
PY
)"
[[ "$deployment_mode" == "first" || "$deployment_mode" == "same" || "$deployment_mode" == "upgrade" ]] || die "could not determine deployment mode"
if (( replace_same_version == 1 )) && [[ "$deployment_mode" != "same" ]]; then
    die "--replace-same-version requires the requested version to match production"
fi
if (( replace_same_version == 1 )); then
    install -d -o root -g root -m 0700 "$backup/packages"
fi

for package in "${required_packages[@]}"; do
    source_path="$payload_downloads/$package"
    target_path="$live_downloads/$package"
    expected_hash="$(awk -v target="$package" '{ name=$2; sub(/^\*/, "", name); if (name == target) print tolower($1) }' "$payload_checksums")"
    [[ "$expected_hash" =~ ^[0-9a-f]{64}$ ]] || die "missing trusted checksum for: $package"
    [[ "$(sha256sum "$source_path" | awk '{print $1}')" == "$expected_hash" ]] || die "source checksum changed after validation: $package"

    if [[ -e "$target_path" || -L "$target_path" ]]; then
        [[ -f "$target_path" && ! -L "$target_path" ]] || die "unsafe existing target: $target_path"
        actual_hash="$(sha256sum "$target_path" | awk '{print $1}')"
        if [[ "$actual_hash" == "$expected_hash" ]]; then
            [[ "$(stat -c %u "$target_path")" -eq 0 ]] || die "published package is not root-owned: $package"
            [[ "$(stat -c %h "$target_path")" -eq 1 ]] || die "published package must not be hard-linked: $package"
            [[ "$(stat -c %a "$target_path")" == "644" ]] || die "published package mode must be 0644: $package"
            continue
        fi
        if [[ "$deployment_mode" == "same" ]]; then
            (( replace_same_version == 1 )) || die "published version package has a different hash: $package"
            install -m 0644 "$target_path" "$backup/packages/$package"
        fi
    fi

    staged_path="$LIVE_STAGING_ROOT/${package}.${release_id}.tmp"
    staged_paths+=("$staged_path")
    [[ ! -e "$staged_path" && ! -L "$staged_path" ]] || die "staging file already exists: $staged_path"
    install -m 0644 "$source_path" "$staged_path"
    [[ "$(sha256sum "$staged_path" | awk '{print $1}')" == "$expected_hash" ]] || die "staged checksum mismatch: $package"
    published_paths+=("$target_path")
    mv -- "$staged_path" "$target_path"
done

# Recheck every live package immediately before switching metadata. Files are
# root-owned, single-link and non-writable by the web server after this point.
for package in "${required_packages[@]}"; do
    target_path="$live_downloads/$package"
    expected_hash="$(awk -v target="$package" '{ name=$2; sub(/^\*/, "", name); if (name == target) print tolower($1) }' "$payload_checksums")"
    [[ -f "$target_path" && ! -L "$target_path" ]] || die "live package disappeared: $package"
    [[ "$(stat -c %u "$target_path")" -eq 0 ]] || die "live package is not root-owned: $package"
    [[ "$(stat -c %h "$target_path")" -eq 1 ]] || die "live package must not be hard-linked: $package"
    [[ "$(stat -c %a "$target_path")" == "644" ]] || die "live package mode must be 0644: $package"
    [[ "$(sha256sum "$target_path" | awk '{print $1}')" == "$expected_hash" ]] || die "live package changed before metadata switch: $package"
done

backup_metadata() {
    local source_path="$1"
    local backup_path="$2"
    if [[ -e "$source_path" || -L "$source_path" ]]; then
        [[ -f "$source_path" && ! -L "$source_path" ]] || die "unsafe existing metadata: $source_path"
        install -m 0644 "$source_path" "$backup_path"
    fi
}

backup_metadata "$live_downloads/checksums.txt" "$backup/checksums.txt"
backup_metadata "$live_api/version.json" "$backup/version.json"
backup_metadata "$BACKEND_MANIFEST_DIR/version.json" "$backup/backend-version.json"

rollback_metadata() {
    local failed=0
    if [[ -f "$backup/checksums.txt" ]]; then
        if install -m 0644 "$backup/checksums.txt" "$checksums_rollback_tmp"; then
            mv -- "$checksums_rollback_tmp" "$live_downloads/checksums.txt" || failed=1
        else
            failed=1
        fi
    else
        rm -f -- "$live_downloads/checksums.txt" || failed=1
    fi
    if [[ -f "$backup/backend-version.json" ]]; then
        if install -m 0644 "$backup/backend-version.json" "$backend_rollback_tmp"; then
            mv -- "$backend_rollback_tmp" "$BACKEND_MANIFEST_DIR/version.json" || failed=1
        else
            failed=1
        fi
    else
        rm -f -- "$BACKEND_MANIFEST_DIR/version.json" || failed=1
    fi
    if [[ -f "$backup/version.json" ]]; then
        if install -m 0644 "$backup/version.json" "$manifest_rollback_tmp"; then
            mv -- "$manifest_rollback_tmp" "$live_api/version.json" || failed=1
        else
            failed=1
        fi
    else
        rm -f -- "$live_api/version.json" || failed=1
    fi
    return "$failed"
}

checksums_tmp="$live_downloads/.checksums.${release_id}.tmp"
backend_tmp="$BACKEND_MANIFEST_DIR/.version.${release_id}.tmp"
manifest_tmp="$live_api/.version.${release_id}.tmp"
checksums_rollback_tmp="$live_downloads/.checksums.${release_id}.rollback.tmp"
backend_rollback_tmp="$BACKEND_MANIFEST_DIR/.version.${release_id}.rollback.tmp"
manifest_rollback_tmp="$live_api/.version.${release_id}.rollback.tmp"
staged_paths+=(
    "$checksums_tmp"
    "$backend_tmp"
    "$manifest_tmp"
    "$checksums_rollback_tmp"
    "$backend_rollback_tmp"
    "$manifest_rollback_tmp"
)

metadata_transaction_active=1

install -m 0644 "$payload_checksums" "$checksums_tmp"
mv -- "$checksums_tmp" "$live_downloads/checksums.txt"

install -m 0644 "$payload_manifest" "$backend_tmp"
mv -- "$backend_tmp" "$BACKEND_MANIFEST_DIR/version.json"

# Publish the public manifest last, after every referenced package is live.
install -m 0644 "$payload_manifest" "$manifest_tmp"
mv -- "$manifest_tmp" "$live_api/version.json"
release_committed=1
metadata_transaction_active=0

echo "PROMOTED_VERSION=$version"
