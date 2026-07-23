#!/usr/bin/env bash
set -euo pipefail

GC_BIN_SOURCE=/tmp/goatcounter-v2.7.0
GC_BIN=/usr/local/bin/goatcounter
GC_DATA=/var/lib/goatcounter
GC_DB="$GC_DATA/goatcounter.sqlite3"
GC_BACKUPS=/var/backups/goatcounter
GC_SERVICE=/etc/systemd/system/goatcounter.service
NGINX_CONF=/etc/nginx/conf.d/default.conf
EXPECTED_BIN_SHA256=bb753bad7cdf9d75b8f1a5183a570dcdcc8705f2419ab40698f4c752796d6567
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
backup_dir="/root/xlsone-backups/analytics-$timestamp"

echo '== preflight =='
test -f "$GC_BIN_SOURCE"
actual_sha=$(sha256sum "$GC_BIN_SOURCE" | awk '{print $1}')
test "$actual_sha" = "$EXPECTED_BIN_SHA256"
nginx -t
mkdir -p "$backup_dir"
cp -a "$NGINX_CONF" "$backup_dir/default.conf.before"
if [ -f "$GC_SERVICE" ]; then
    cp -a "$GC_SERVICE" "$backup_dir/goatcounter.service.before"
fi
if [ -f "$GC_BIN" ]; then
    cp -a "$GC_BIN" "$backup_dir/goatcounter.bin.before"
fi

echo '== install binary and restricted account =='
getent group goatcounter >/dev/null || groupadd --system goatcounter
id goatcounter >/dev/null 2>&1 || useradd --system --gid goatcounter \
    --home-dir "$GC_DATA" --shell /sbin/nologin goatcounter
install -d -o goatcounter -g goatcounter -m 0750 "$GC_DATA"
install -d -o root -g root -m 0700 "$GC_BACKUPS"
install -o root -g root -m 0755 "$GC_BIN_SOURCE" "$GC_BIN"

echo '== initialize database =='
if [ ! -f "$GC_DB" ]; then
    admin_password=$(openssl rand -hex 24)
    umask 077
    printf '%s\n' "$admin_password" > /root/.goatcounter-initial-password
    runuser -u goatcounter -- "$GC_BIN" db create site \
        -db="sqlite+$GC_DB" \
        -createdb=true \
        -vhost z-pulse.cn \
        -user.email privacy@z-pulse.cn \
        -user.password "$admin_password"
    unset admin_password
fi
chown -R goatcounter:goatcounter "$GC_DATA"
chmod 0750 "$GC_DATA"
chmod 0640 "$GC_DB" 2>/dev/null || true

echo '== install systemd unit =='
cat > "$GC_SERVICE" <<'UNIT'
[Unit]
Description=GoatCounter analytics for z-pulse.cn
After=network.target

[Service]
Type=simple
User=goatcounter
Group=goatcounter
WorkingDirectory=/var/lib/goatcounter
ExecStart=/usr/local/bin/goatcounter serve -listen=127.0.0.1:8082 -tls=http -public-port=443 -base-path=/analytics -automigrate -db=sqlite+/var/lib/goatcounter/goatcounter.sqlite3
Restart=on-failure
RestartSec=5
PrivateTmp=true
NoNewPrivileges=true
ProtectSystem=full
ProtectHome=true

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable goatcounter >/dev/null
systemctl restart goatcounter

for attempt in $(seq 1 20); do
    if curl -fsS -H 'Host: z-pulse.cn' \
        http://127.0.0.1:8082/analytics/count.js >/dev/null; then
        break
    fi
    if [ "$attempt" -eq 20 ]; then
        systemctl --no-pager -l status goatcounter || true
        journalctl -u goatcounter -n 80 --no-pager || true
        exit 1
    fi
    sleep 1
done

echo '== install consistent SQLite backup =='
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
cat > /etc/cron.d/goatcounter-backup <<'CRON'
23 3 * * * root /usr/local/sbin/backup-goatcounter >/dev/null 2>&1
CRON
chmod 0644 /etc/cron.d/goatcounter-backup
/usr/local/sbin/backup-goatcounter

echo '== add nginx reverse proxy =='
python3 - "$NGINX_CONF" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
if "location /analytics/" not in text:
    marker = "    location / {\n"
    if marker not in text:
        raise SystemExit("nginx insertion marker not found")
    block = """    # Privacy-friendly self-hosted website analytics
    location = /analytics {
        return 301 /analytics/;
    }

    location /analytics/ {
        proxy_pass http://127.0.0.1:8082;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 60s;
    }

"""
    text = text.replace(marker, block + marker, 1)
    path.write_text(text, encoding="utf-8")
PY

if ! nginx -t; then
    cp -a "$backup_dir/default.conf.before" "$NGINX_CONF"
    nginx -t
    systemctl reload nginx
    echo 'nginx update failed and was rolled back' >&2
    exit 1
fi
systemctl reload nginx

echo '== verification =='
systemctl is-active goatcounter
curl -fsS -o /dev/null -H 'Host: z-pulse.cn' http://127.0.0.1:8082/analytics/count.js
curl -kfsS -o /dev/null -H 'Host: z-pulse.cn' https://127.0.0.1/analytics/count.js
echo "backup_dir=$backup_dir"
echo 'admin_password_file=/root/.goatcounter-initial-password'
