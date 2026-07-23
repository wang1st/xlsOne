#!/usr/bin/env bash
set -u

echo '== effective nginx routing/logging =='
nginx -T 2>/dev/null | grep -nE '^[[:space:]]*(listen|server_name|root|access_log|error_log|proxy_pass)' || true

echo '== nginx log directory =='
ls -lah /var/log/nginx 2>/dev/null || true

echo '== recent access samples (paths only) =='
for f in /var/log/nginx/*access*.log; do
    [ -f "$f" ] || continue
    echo "-- $f --"
    tail -n 5 "$f" | awk '{print $4, $6, $7, $9}'
done

echo '== logrotate =='
cat /etc/logrotate.d/nginx 2>/dev/null || true
