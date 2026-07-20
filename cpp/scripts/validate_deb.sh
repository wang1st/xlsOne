#!/usr/bin/env bash
set -euo pipefail

DEB="${1:?Usage: $0 <path-to.deb>}"

run_privileged() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "SKIP: root privileges unavailable for: $*" >&2
        return 0
    fi
}

echo "========================================="
echo "  xlsOne DEB Package Validation"
echo "========================================="
echo ""

echo "== deb info =="
dpkg-deb -I "$DEB"
echo ""

echo "== control fields =="
dpkg-deb -f "$DEB" Package Version Architecture Depends Maintainer Description Section Priority
echo ""

echo "== contents (first 50) =="
dpkg-deb -c "$DEB" | head -50 || true
echo ""

echo "== dpkg simulate install =="
run_privileged dpkg --dry-run -i "$DEB" 2>&1 || true
echo ""

echo "== apt simulate install =="
run_privileged apt install --simulate --no-install-recommends "$(realpath "$DEB")" 2>&1 || true
echo ""

echo "== check maintainer format =="
MAINTAINER=$(dpkg-deb -f "$DEB" Maintainer)
if echo "$MAINTAINER" | grep -qE '.+ <.+@.+>'; then
    echo "OK: Maintainer format is valid RFC 5322"
else
    echo "WARN: Maintainer '$MAINTAINER' does not match 'Name <email>' format"
fi
echo ""

echo "== check architecture =="
ARCH=$(dpkg-deb -f "$DEB" Architecture)
HOST_ARCH=$(dpkg --print-architecture)
if [ "$ARCH" = "all" ]; then
    echo "OK: Architecture 'all'"
elif [ "$ARCH" = "$HOST_ARCH" ]; then
    echo "OK: Architecture '$ARCH' matches host '$HOST_ARCH'"
else
    echo "WARN: Architecture '$ARCH' does not match host '$HOST_ARCH'"
fi
echo ""

echo "== check postinst is non-interactive =="
if dpkg-deb --fsys-tarfile "$DEB" | tar xO ./postinst 2>/dev/null | grep -nE 'read |dialog|whiptail|zenity|sudo '; then
    echo "WARN: postinst contains potentially interactive commands"
else
    echo "OK: no interactive commands found"
fi
echo ""

echo "== check md5sums present =="
if dpkg-deb --ctrl-tarfile "$DEB" | tar -tf - | grep -q '^./md5sums$'; then
    echo "OK: md5sums present"
else
    echo "WARN: md5sums missing (Kylin installer may reject the package)"
fi
echo ""

echo "== check Kylin signature =="
if command -v kylinsigntool >/dev/null 2>&1; then
    if kylinsigntool -v "$DEB" >/dev/null 2>&1; then
        echo "OK: Kylin signature verified"
    else
        echo "WARN: Kylin signature missing or invalid (kylin-installer may refuse to install)"
    fi
else
    echo "SKIP: kylinsigntool not available"
fi
echo ""

echo "========================================="
echo "  Validation complete"
echo "========================================="
