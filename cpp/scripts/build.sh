#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${1:-dev}"

cd "${root}"

cmake --preset "${preset}"
cmake --build --preset "${preset}"
ctest --preset "${preset}"
