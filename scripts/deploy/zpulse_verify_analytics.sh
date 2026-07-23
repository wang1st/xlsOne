#!/usr/bin/env bash
set -euo pipefail

db=/var/lib/goatcounter/goatcounter.sqlite3
gc=/usr/local/bin/goatcounter

printf 'service=%s\n' "$(systemctl is-active goatcounter)"
printf 'listener=%s\n' "$(ss -lnt | awk '$4 == "127.0.0.1:8082" {print $4; exit}')"
printf 'homepage_status=%s\n' "$(curl -sS -o /tmp/zpulse-home-check -w '%{http_code}' https://z-pulse.cn/)"
printf 'tracker_status=%s\n' "$(curl -sS -o /dev/null -w '%{http_code}' https://z-pulse.cn/analytics/count.js)"
grep -q 'data-goatcounter="https://z-pulse.cn/analytics/count"' /tmp/zpulse-home-check
rm -f /tmp/zpulse-home-check

printf '%s\n' 'database_integrity:'
runuser -u goatcounter -- "$gc" db query -db="sqlite+$db" -format=csv 'PRAGMA integrity_check'
printf '%s\n' 'recorded_stats:'
runuser -u goatcounter -- "$gc" db query -db="sqlite+$db" -format=csv \
    'SELECT count(*) AS aggregate_rows, coalesce(sum(total), 0) AS accepted_hits FROM hit_counts'
printf 'verified_backups=' 
find /var/backups/goatcounter -maxdepth 1 -type f -name 'goatcounter-*.sqlite3.gz' | wc -l
latest=$(find /var/backups/goatcounter -maxdepth 1 -type f -name 'goatcounter-*.sqlite3.gz' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)
test -n "$latest"
gzip -t "$latest"

test "$(systemctl is-active goatcounter)" = active
ss -lnt | grep -q '127.0.0.1:8082'
! ss -lnt | grep -Eq '(0\.0\.0\.0|\[::\]):8082'
