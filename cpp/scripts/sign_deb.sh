#!/usr/bin/env bash
set -euo pipefail

# Sign a .deb for Kylin/UOS GUI installer using kylin-signtool.
# The installer (kylin-installer / KylinUpdateManager) refuses to install
# unsigned local .deb packages on hardened Kylin systems.
#
# Usage:
#   sign_deb.sh <path-to.deb> [private-key.pem] [certificate.pem]
#
# If the key/cert are omitted, the script falls back to /etc/kylinsign/kylinsign.conf.
# For automated builds, set XLSONE_KYLIN_KEY and XLSONE_KYLIN_CERT in the environment.

deb="${1:?Usage: $0 <path-to.deb> [private-key.pem] [certificate.pem]}"
key="${2:-${XLSONE_KYLIN_KEY:-}}"
cert="${3:-${XLSONE_KYLIN_CERT:-}}"

if ! command -v kylinsigntool >/dev/null 2>&1; then
    echo "Error: kylinsigntool not found. Install kylin-signtool or sign on a Kylin machine." >&2
    exit 1
fi

if [ -n "$key" ] && [ -n "$cert" ]; then
    echo "Signing $deb with provided key/cert ..."
    kylinsigntool -a "$deb" -p "$key" -c "$cert"
elif [ -f /etc/kylinsign/kylinsign.conf ]; then
    if grep -qE '^\s*(PrivateKey|UserCert)\s*=' /etc/kylinsign/kylinsign.conf 2>/dev/null; then
        echo "Signing $deb using /etc/kylinsign/kylinsign.conf ..."
        kylinsigntool -a "$deb"
    else
        echo "Error: /etc/kylinsign/kylinsign.conf exists but does not contain PrivateKey/UserCert." >&2
        exit 1
    fi
else
    echo "Error: No Kylin signing key/certificate available." >&2
    echo "To pass kylin-installer verification you need a Kylin-issued developer certificate." >&2
    echo "As a workaround, install from the terminal with: sudo dpkg -i $deb" >&2
    exit 1
fi

if kylinsigntool -v "$deb"; then
    echo "Signature verified: $deb"
else
    echo "Warning: Signature verification failed for $deb" >&2
    exit 1
fi
