#!/usr/bin/env bash
set -euo pipefail

web_root=/var/www/z-pulse.cn
incoming=/root/xlsone-index-gsc-new.html
verification_incoming=/root/googlede3d88d210fda816.html.new
verification_live="$web_root/googlede3d88d210fda816.html"
backup_root=/root/xlsone-backups
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
backup_dir="$backup_root/google-verification-$timestamp"

test -f "$incoming"
test -f "$verification_incoming"
grep -q 'name="google-site-verification"' "$incoming"
test "$(tr -d '\r\n' < "$verification_incoming")" = 'google-site-verification: googlede3d88d210fda816.html'

mkdir -p "$backup_dir"
cp -a "$web_root/index.html" "$backup_dir/index.html"
if test -f "$verification_live"; then
  cp -a "$verification_live" "$backup_dir/googlede3d88d210fda816.html"
fi
install -o root -g root -m 0644 "$incoming" "$web_root/index.html"
install -o root -g root -m 0644 "$verification_incoming" "$verification_live"
rm -f "$incoming" "$verification_incoming"

nginx -t
systemctl reload nginx

grep -q 'nU3G3dTZNxh3Qr5aru5kQs_39JndxTeh4XViZl6mrO4' "$web_root/index.html"
test "$(tr -d '\r\n' < "$verification_live")" = 'google-site-verification: googlede3d88d210fda816.html'
printf 'google_verification_deployed backup=%s\n' "$backup_dir"
