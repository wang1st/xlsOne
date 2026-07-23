#!/usr/bin/env bash
set -u
systemctl is-active goatcounter || true
ss -ltnp | grep 8082 || true
for path in / /status /count.js /count /analytics /analytics/ /analytics/status /analytics/count.js /analytics/count; do
    code=$(curl -sS -o /dev/null -w '%{http_code}' -H 'Host: z-pulse.cn' "http://127.0.0.1:8082$path" || true)
    printf '%-28s %s\n' "$path" "$code"
done
journalctl -u goatcounter -n 30 --no-pager || true
