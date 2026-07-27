#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out_dir="${XLSONE_GOLDEN_OUT:-${root}/tmp/golden}"
mkdir -p "${out_dir}"

inputs=("$@")
if [ "${#inputs[@]}" -eq 0 ]; then
  while IFS= read -r file; do
    inputs+=("${file}")
  done < <(find "${root}/samples/monthly-report-sample-v1.1" -maxdepth 1 \
    -type f -name "0[1-4]-*.xlsx" | sort)
fi

if [ "${#inputs[@]}" -eq 0 ]; then
  echo "No workbook inputs found. Pass paths explicitly or restore samples/monthly-report-sample-v1.1." >&2
  exit 2
fi

"${root}/cpp/scripts/build.sh" dev
export CLANG_MODULE_CACHE_PATH="${out_dir}/swift-module-cache"
swift build --package-path "${root}" --scratch-path "${out_dir}/swift-build" --product xlsOneSnapshot
swift_tool="${out_dir}/swift-build/debug/xlsOneSnapshot"
"${swift_tool}" --output "${out_dir}/swift.json" "${inputs[@]}"
"${root}/cpp/build/tools/xlsone_snapshot" --output "${out_dir}/cpp.json" "${inputs[@]}"
python3 "${root}/cpp/scripts/compare_snapshots.py" "${out_dir}/swift.json" "${out_dir}/cpp.json"
