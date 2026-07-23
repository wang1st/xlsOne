#!/usr/bin/env bash
set -euo pipefail

web_root=/var/www/z-pulse.cn
stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup_dir="/root/xlsone-backups/site-analytics-${stamp}"

declare -A uploads=(
  [index.html]='/tmp/zpulse-analytics-index.html'
  [xlsone/download.html]='/tmp/zpulse-analytics-download.html'
  [support/index.html]='/tmp/zpulse-analytics-support.html'
  [privacy/index.html]='/tmp/zpulse-analytics-privacy.html'
)

for rel in "${!uploads[@]}"; do
  src=${uploads[$rel]}
  test -s "$src"
  grep -q 'data-goatcounter="https://z-pulse.cn/analytics/count"' "$src"
  grep -qi '<!DOCTYPE html>' "$src"
  test -f "$web_root/$rel"
done

grep -q '831261@qq.com' "${uploads[privacy/index.html]}"
grep -q 'data-goatcounter-click="purchase_ifdian"' "${uploads[support/index.html]}"
grep -q 'data-goatcounter-click="download_windows_msi"' "${uploads[xlsone/download.html]}"

mkdir -p "$backup_dir"

for rel in "${!uploads[@]}"; do
  src=${uploads[$rel]}
  dst="$web_root/$rel"
  backup="$backup_dir/$rel"
  staged="${dst}.analytics-new-$$"

  mkdir -p "$(dirname "$backup")"
  cp -a "$dst" "$backup"
  cp "$src" "$staged"
  chmod --reference="$dst" "$staged"
  chown --reference="$dst" "$staged"
  mv -f "$staged" "$dst"
done

nginx -t
rm -f "${uploads[@]}"

printf 'site deployed\nbackup=%s\n' "$backup_dir"
