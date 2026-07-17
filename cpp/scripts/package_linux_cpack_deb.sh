#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-linux-release}"

case "${preset}" in
  linux-release) build_dir="${root}/build-linux-release" ;;
  uos-x86_64-release) build_dir="${root}/build-uos-x86_64-release" ;;
  uos-arm64-release) build_dir="${root}/build-uos-arm64-release" ;;
  uos-loongarch64-release) build_dir="${root}/build-uos-loongarch64-release" ;;
  release) build_dir="${root}/build-release" ;;
  *) build_dir="${2:-${root}/build-${preset}}" ;;
esac

cd "${root}"

cmake --preset "${preset}"
cmake --build --preset "${preset}"
if ctest --list-presets | grep -q "\"${preset}\""; then
  ctest --preset "${preset}"
else
  echo "No ctest preset named ${preset}; skipping local test run for this package preset."
fi
cpack --config "${build_dir}/CPackConfig.cmake" -G DEB

# Sign the resulting .deb for Kylin/UOS GUI installer if a signing key is available.
# Without a valid Kylin signature, kylin-installer may refuse to install the package.
deb_file=$(ls -t "${root}"/xlsone-*.deb 2>/dev/null | head -1)
if [ -n "${deb_file:-}" ]; then
    if [ -n "${XLSONE_KYLIN_KEY:-}" ] && [ -n "${XLSONE_KYLIN_CERT:-}" ]; then
        echo "Kylin signing key/cert provided; signing ${deb_file} ..."
        "${root}/scripts/sign_deb.sh" "$deb_file" "$XLSONE_KYLIN_KEY" "$XLSONE_KYLIN_CERT" || true
    elif [ -f /etc/kylinsign/kylinsign.conf ] && grep -qE '^\s*(PrivateKey|UserCert)\s*=' /etc/kylinsign/kylinsign.conf 2>/dev/null; then
        echo "Kylin signing config found; signing ${deb_file} ..."
        "${root}/scripts/sign_deb.sh" "$deb_file" || true
    else
        echo "No Kylin signing key available; ${deb_file} is unsigned."
        echo "kylin-installer may refuse to install it. Use: sudo dpkg -i ${deb_file}"
    fi

    # Collect into the repo-level .build/ dir so deploy.sh can find it.
    mkdir -p "${root}/../.build"
    cp -f "$deb_file" "${root}/../.build/"
    echo "Collected: ${root}/../.build/$(basename "$deb_file")"
fi
