#!/usr/bin/env bash
set -euo pipefail

web_root=/var/www/z-pulse.cn
nginx_conf=/etc/nginx/conf.d/default.conf
stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup_dir="/root/xlsone-backups/seo-phase1-${stamp}"
indexnow_key=40ade2df5b7b75d1de1b7dbd54d14852

declare -A uploads=(
  [index.html]='/tmp/zpulse-seo-index.html'
  [xlsone/download.html]='/tmp/zpulse-seo-download.html'
  [support/index.html]='/tmp/zpulse-seo-support.html'
  [privacy/index.html]='/tmp/zpulse-seo-privacy.html'
  [robots.txt]='/tmp/zpulse-seo-robots.txt'
  [sitemap.xml]='/tmp/zpulse-seo-sitemap.xml'
  ["${indexnow_key}.txt"]='/tmp/zpulse-seo-indexnow.txt'
)

for rel in "${!uploads[@]}"; do
  src=${uploads[$rel]}
  test -s "$src"
  if [ "$rel" != "${indexnow_key}.txt" ]; then
    test -f "$web_root/$rel"
  fi
done

grep -q '<loc>https://z-pulse.cn/</loc>' "${uploads[sitemap.xml]}"
test "$(grep -c '<loc>' "${uploads[sitemap.xml]}")" -eq 4
grep -q 'Disallow: /analytics/' "${uploads[robots.txt]}"
test "$(tr -d '\r\n' < "${uploads[${indexnow_key}.txt]}")" = "$indexnow_key"

mkdir -p "$backup_dir"
cp -a "$nginx_conf" "$backup_dir/default.conf.before"
for rel in "${!uploads[@]}"; do
  dst="$web_root/$rel"
  backup="$backup_dir/$rel"
  if [ -f "$dst" ]; then
    mkdir -p "$(dirname "$backup")"
    cp -a "$dst" "$backup"
  fi
done

old_copy="$web_root/小红书推广软文.html"
if [ -f "$old_copy" ]; then
  cp -a "$old_copy" "$backup_dir/小红书推广软文.html"
fi

python3 - "$nginx_conf" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

if "listen 443 ssl default_server;\n    server_name z-pulse.cn;" not in text:
    text = text.replace(
        "listen 443 ssl default_server;\n",
        "listen 443 ssl default_server;\n    server_name z-pulse.cn;\n",
        1,
    )

old_xlsone = """    location = /xlsone {
        return 301 /xlsone/;
    }

"""
text = text.replace(old_xlsone, "", 1)

seo_marker = "    # Privacy-friendly self-hosted website analytics\n"
if "    # SEO canonical redirects\n" not in text:
    if seo_marker not in text:
        raise SystemExit("SEO insertion marker not found")
    seo_block = """    # SEO canonical redirects
    location = /xlsone {
        return 301 https://z-pulse.cn/;
    }

    location = /xlsone/ {
        return 301 https://z-pulse.cn/;
    }

    location = /products/xlsone/ {
        return 301 https://z-pulse.cn/;
    }

    location = /products/xlsone/download.html {
        return 301 https://z-pulse.cn/xlsone/download.html;
    }

    location = /xlsone/buy.html {
        return 301 https://z-pulse.cn/support/;
    }

    location = /小红书推广软文.html {
        return 410;
    }

"""
    text = text.replace(seo_marker, seo_block + seo_marker, 1)

www_marker = "# ========================================\n# HTTP → HTTPS 重定向 (z-pulse.cn)\n"
if "server_name www.z-pulse.cn;\n    return 301 https://z-pulse.cn$request_uri;" not in text:
    if www_marker not in text:
        raise SystemExit("www redirect insertion marker not found")
    www_block = """# HTTPS canonical host redirect
server {
    listen 443 ssl;
    server_name www.z-pulse.cn;
    ssl_certificate /etc/letsencrypt/live/z-pulse.cn/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/z-pulse.cn/privkey.pem;
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;
    return 301 https://z-pulse.cn$request_uri;
}
"""
    text = text.replace(www_marker, www_block + www_marker, 1)

text = text.replace(
    "return 301 https://$host$request_uri;",
    "return 301 https://z-pulse.cn$request_uri;",
    1,
)

def add_noindex_to_locations(config: str, spec: str) -> str:
    needle = f"    location {spec} {{"
    offset = 0
    while True:
        start = config.find(needle, offset)
        if start < 0:
            return config
        brace = config.find("{", start)
        depth = 0
        end = None
        for i in range(brace, len(config)):
            if config[i] == "{":
                depth += 1
            elif config[i] == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        if end is None:
            raise SystemExit(f"unterminated location block: {spec}")
        block = config[start:end]
        if 'X-Robots-Tag "noindex, nofollow"' not in block:
            line_end = config.find("\n", start)
            header = '        add_header X-Robots-Tag "noindex, nofollow" always;\n'
            config = config[: line_end + 1] + header + config[line_end + 1 :]
            end += len(header)
        offset = end

for location_spec in (
    "= /api/version",
    "/api/",
    "/activation/",
    "/downloads/",
    "= /offline",
    "= /xlsone/offline",
    "= /xlsone/license-console",
    "/xlsone/license-console/",
    "= /analytics",
    "/analytics/",
):
    text = add_noindex_to_locations(text, location_spec)

path.write_text(text, encoding="utf-8")
PY

if ! nginx -t; then
  cp -a "$backup_dir/default.conf.before" "$nginx_conf"
  nginx -t
  exit 1
fi

for rel in "${!uploads[@]}"; do
  src=${uploads[$rel]}
  dst="$web_root/$rel"
  staged="${dst}.seo-new-$$"
  cp "$src" "$staged"
  if [ -f "$dst" ]; then
    chmod --reference="$dst" "$staged"
    chown --reference="$dst" "$staged"
  else
    chmod 0644 "$staged"
    chown --reference="$web_root/index.html" "$staged"
  fi
  mv -f "$staged" "$dst"
done

rm -f -- "$old_copy"
systemctl reload nginx
rm -f "${uploads[@]}"

printf 'seo phase 1 deployed\nbackup=%s\n' "$backup_dir"
