#!/usr/bin/env bash
set -u

systemctl is-active goatcounter || true
systemctl --no-pager -l status goatcounter | tail -n 12 || true
printf '%s\n' '--- backup script ---'
sed -n '1,220p' /usr/local/sbin/backup-goatcounter
printf '%s\n' '--- backup files ---'
find /var/backups/goatcounter -maxdepth 1 -type f -printf '%f %s bytes\n' | sort
tmp=$(find /var/backups/goatcounter -maxdepth 1 -type f -name '.goatcounter-*.sqlite3.tmp' | head -n 1)
if [ -n "$tmp" ]; then
    printf '%s\n' '--- temp integrity ---'
    /usr/local/bin/goatcounter db query -db="sqlite+$tmp" -format=csv 'PRAGMA integrity_check' || true
fi
printf '%s\n' '--- endpoint ---'
curl -sS -o /dev/null -w '%{http_code}\n' -H 'Host: z-pulse.cn' http://127.0.0.1:8082/analytics/count.js || true
