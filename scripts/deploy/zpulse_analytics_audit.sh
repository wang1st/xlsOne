#!/usr/bin/env bash
set -u

echo '== identity =='
id
hostname
date -u '+%Y-%m-%dT%H:%M:%SZ'

echo '== operating system =='
cat /etc/os-release 2>/dev/null || true
uname -a

echo '== capacity =='
df -h / /var /var/www 2>/dev/null || true
free -m 2>/dev/null || true

echo '== runtime =='
command -v docker >/dev/null && docker --version || echo 'docker: absent'
docker compose version 2>/dev/null || echo 'docker compose: absent'
command -v podman >/dev/null && podman --version || echo 'podman: absent'
command -v node >/dev/null && node --version || echo 'node: absent'
command -v npm >/dev/null && npm --version || echo 'npm: absent'

echo '== services =='
systemctl is-active nginx 2>/dev/null || true
systemctl is-active docker 2>/dev/null || true
systemctl is-active xlsone-activation 2>/dev/null || true
systemctl --no-pager --type=service --state=running 2>/dev/null | grep -E 'nginx|docker|postgres|mariadb|mysql|xlsone' || true

echo '== nginx and tls =='
nginx -v 2>&1 || true
nginx -t 2>&1 || true
command -v certbot >/dev/null && certbot --version 2>&1 || echo 'certbot: absent'
find /etc/nginx/conf.d -maxdepth 1 -type f -printf '%f\n' 2>/dev/null | sort
find /etc/letsencrypt/live -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null | sort

echo '== listening tcp ports =='
ss -ltnp 2>/dev/null | sed -n '1,80p'

echo '== website =='
stat -c '%A %U:%G %n' /var/www/z-pulse.cn 2>/dev/null || true
du -sh /var/www/z-pulse.cn /var/log/nginx 2>/dev/null || true
ls -lh /var/log/nginx/z-pulse.cn-access.log* 2>/dev/null | tail -n 12 || true

echo '== dns =='
getent hosts z-pulse.cn 2>/dev/null || true
getent hosts analytics.z-pulse.cn 2>/dev/null || echo 'analytics.z-pulse.cn: no record'

echo '== package managers =='
command -v yum || true
command -v dnf || true
command -v apt-get || true

echo '== existing analytics =='
find /opt /srv /var/lib -maxdepth 2 -iname '*umami*' -o -iname '*goaccess*' 2>/dev/null | head -n 40 || true
