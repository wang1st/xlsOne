#!/usr/bin/env bash
set -euo pipefail

gc=/usr/local/bin/goatcounter
db='sqlite+/var/lib/goatcounter/goatcounter.sqlite3'

printf '%s\n' '--- integrity ---'
runuser -u goatcounter -- "$gc" db query -db="$db" -format=csv 'PRAGMA integrity_check'
printf '%s\n' '--- tables ---'
runuser -u goatcounter -- "$gc" db query -db="$db" -format=csv "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
printf '%s\n' '--- aggregate counts ---'
runuser -u goatcounter -- "$gc" db query -db="$db" -format=csv 'PRAGMA table_info(hit_counts)'
runuser -u goatcounter -- "$gc" db query -db="$db" -format=csv 'SELECT count(*) AS rows FROM hit_counts'
printf '%s\n' '--- recorded paths ---'
runuser -u goatcounter -- "$gc" db query -db="$db" -format=csv 'PRAGMA table_info(paths)'
runuser -u goatcounter -- "$gc" db query -db="$db" -format=csv "SELECT path, title, event FROM paths WHERE path LIKE '%deployment%' ORDER BY path"
