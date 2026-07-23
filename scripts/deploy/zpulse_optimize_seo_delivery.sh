#!/usr/bin/env bash
set -euo pipefail

nginx_conf=/etc/nginx/conf.d/default.conf
stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup_dir="/root/xlsone-backups/seo-delivery-${stamp}"
mkdir -p "$backup_dir"
cp -a "$nginx_conf" "$backup_dir/default.conf.before"

python3 - "$nginx_conf" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

text = text.replace(
    "listen 443 ssl default_server;",
    "listen 443 ssl http2 default_server;",
    1,
)

www_start = text.find("server_name www.z-pulse.cn;")
if www_start >= 0:
    listen_start = text.rfind("listen 443 ssl;", 0, www_start)
    if listen_start >= 0:
        text = text[:listen_start] + text[listen_start:].replace(
            "listen 443 ssl;", "listen 443 ssl http2;", 1
        )

marker = "    index index.html;\n"
if "    # SEO delivery performance\n" not in text:
    if marker not in text:
        raise SystemExit("server index marker not found")
    performance = """    # SEO delivery performance
    gzip on;
    gzip_vary on;
    gzip_min_length 1024;
    gzip_comp_level 5;
    gzip_types text/plain text/css application/json application/javascript text/xml application/xml application/xml+rss image/svg+xml;

    location /css/ {
        try_files $uri =404;
        add_header Cache-Control "public, max-age=2592000, immutable";
    }

    location /images/ {
        try_files $uri =404;
        add_header Cache-Control "public, max-age=2592000, immutable";
    }

"""
    text = text.replace(marker, marker + performance, 1)

path.write_text(text, encoding="utf-8")
PY

if ! nginx -t; then
  cp -a "$backup_dir/default.conf.before" "$nginx_conf"
  nginx -t
  exit 1
fi

systemctl reload nginx
printf 'delivery optimization deployed\nbackup=%s\n' "$backup_dir"
