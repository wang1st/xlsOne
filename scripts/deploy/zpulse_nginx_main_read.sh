#!/usr/bin/env bash
set -u

echo '== /etc/nginx/conf.d/default.conf =='
nl -ba /etc/nginx/conf.d/default.conf | sed -n '1,240p'

echo '== main certificate SANs =='
openssl x509 -in /etc/letsencrypt/live/z-pulse.cn/fullchain.pem -noout -subject -issuer -dates -ext subjectAltName 2>/dev/null || true
