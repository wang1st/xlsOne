#!/usr/bin/env bash
set -euo pipefail

cat > /usr/local/sbin/backup-goatcounter <<'BACKUP'
#!/usr/bin/env bash
set -euo pipefail
umask 077
db=/var/lib/goatcounter/goatcounter.sqlite3
dest=/var/backups/goatcounter
stamp=$(date -u +%Y%m%dT%H%M%SZ)
tmp="$dest/.goatcounter-$stamp.sqlite3.tmp"
final="$dest/goatcounter-$stamp.sqlite3"
mkdir -p "$dest"
find "$dest" -maxdepth 1 -type f -name '.goatcounter-*.sqlite3.tmp' -delete

was_active=false
if systemctl is-active --quiet goatcounter; then
    was_active=true
    systemctl stop goatcounter
fi
restart_service() {
    if [ "$was_active" = true ]; then
        systemctl start goatcounter
    fi
}
trap restart_service EXIT

runuser -u goatcounter -- /usr/local/bin/goatcounter db query \
    -db="sqlite+$db" -format=csv 'PRAGMA wal_checkpoint(TRUNCATE)' >/dev/null
cp "$db" "$tmp"
restart_service
was_active=false
trap - EXIT

/usr/local/bin/goatcounter db query -db="sqlite+$tmp" -format=csv \
    'PRAGMA integrity_check' | tr -d '\r' | grep -qx 'ok'
mv "$tmp" "$final"
gzip -f "$final"
gzip -t "$final.gz"
find "$dest" -maxdepth 1 -type f -name 'goatcounter-*.sqlite3.gz' -mtime +14 -delete
BACKUP

chmod 0750 /usr/local/sbin/backup-goatcounter
/usr/local/sbin/backup-goatcounter
systemctl is-active --quiet goatcounter
for attempt in $(seq 1 20); do
    if curl -fsS -o /dev/null -H 'Host: z-pulse.cn' http://127.0.0.1:8082/analytics/count.js 2>/dev/null; then
        break
    fi
    if [ "$attempt" -eq 20 ]; then
        exit 1
    fi
    sleep 1
done
latest=$(find /var/backups/goatcounter -maxdepth 1 -type f -name 'goatcounter-*.sqlite3.gz' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-)
test -n "$latest"
gzip -t "$latest"
printf 'verified_backup=%s\n' "$latest"
