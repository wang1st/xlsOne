#!/usr/bin/env bash
set -euo pipefail

base=https://z-pulse.cn
key=40ade2df5b7b75d1de1b7dbd54d14852

nginx -t >/dev/null
test "$(systemctl is-active nginx)" = active
test "$(systemctl is-active goatcounter)" = active

for path in / /xlsone/download.html /support/ /privacy/ /robots.txt /sitemap.xml "/${key}.txt"; do
  test "$(curl -sS -o /dev/null -w '%{http_code}' "$base$path")" = 200
done

redirect() {
  url=$1
  expected=$2
  test "$(curl -sS -o /dev/null -w '%{http_code}' "$url")" = 301
  test "$(curl -sSI "$url" | tr -d '\r' | awk -F': ' 'tolower($1) == "location" {print $2}')" = "$expected"
}

redirect "$base/xlsone/" "$base/"
redirect "$base/products/xlsone/" "$base/"
redirect "$base/products/xlsone/download.html" "$base/xlsone/download.html"
redirect "$base/xlsone/buy.html" "$base/support/"
redirect "https://www.z-pulse.cn/seo-check?x=1" "$base/seo-check?x=1"

test "$(curl -sS -o /dev/null -w '%{http_code}' "$base/%E5%B0%8F%E7%BA%A2%E4%B9%A6%E6%8E%A8%E5%B9%BF%E8%BD%AF%E6%96%87.html")" = 410
test "$(curl -fsS "$base/${key}.txt" | tr -d '\r\n')" = "$key"
test "$(curl -fsS "$base/sitemap.xml" | grep -c '<loc>')" -eq 4
curl -fsS "$base/robots.txt" | grep -q 'Disallow: /analytics/'

for path in /analytics/ /api/version /downloads/not-found.bin; do
  curl -sSI "$base$path" | tr -d '\r' | grep -qi '^X-Robots-Tag: noindex, nofollow$'
done

curl -sSI -H 'Accept-Encoding: gzip' "$base/" | tr -d '\r' | grep -qi '^Content-Encoding: gzip$'
curl -sSI "$base/css/style.css?v=1.0.6-1" | tr -d '\r' | grep -qi '^Cache-Control: public, max-age=2592000, immutable$'
grep -q 'listen 443 ssl http2 default_server;' /etc/nginx/conf.d/default.conf

printf '%s\n' 'seo_phase1_server_verification=ok'
