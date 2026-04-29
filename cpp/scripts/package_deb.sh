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
