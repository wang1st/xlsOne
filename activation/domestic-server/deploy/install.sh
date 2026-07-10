#!/usr/bin/env bash
# xlsOne 国内激活 API 安装脚本（在 z-pulse.cn 服务器上运行，CentOS 7）
# 前置：已通过 SFTP 将 server.js / keygen.py / public/ / .env / deploy/ 上传到 /opt/xlsone-activation
set -euo pipefail

APP_DIR=/opt/xlsone-activation
PORT=8787
NODE_VER=16.20.2
NODE_PREFIX=/opt/node${NODE_VER%%.*}
NODE_BIN="$NODE_PREFIX/bin/node"

# 载入 .env 中的密钥（由 SFTP 上传，权限 600）
if [ -f "$APP_DIR/.env" ]; then set -a; . "$APP_DIR/.env"; set +a; fi
SEED_HEX="${ED25519_PRIVATE_KEY:-}"
ADMIN_PATH="${ADMIN_PATH:-/xlsone/license-console}"

echo "==> [1/6] 安装 Node.js $NODE_VER (CentOS7/glibc2.17 兼容)"
if [ ! -x "$NODE_BIN" ]; then
  cd /tmp
  URL="https://nodejs.org/dist/v${NODE_VER}/node-v${NODE_VER}-linux-x64.tar.xz"
  echo "    下载 $URL"
  curl -fsSL "$URL" -o node.tar.xz
  tar -xf node.tar.xz
  rm -rf "$NODE_PREFIX"
  mv "node-v${NODE_VER}-linux-x64" "$NODE_PREFIX"
  rm -f node.tar.xz
  "$NODE_BIN" --version
else
  echo "    已存在: $($NODE_BIN --version)"
fi

echo "==> [2/6] 确保 python3 cryptography（用于从 seed 生成 PEM，可选）"
if python3 -c "import cryptography" 2>/dev/null; then
  echo "    cryptography 已安装"
else
  echo "    尝试 pip3 install cryptography ..."
  pip3 install --quiet cryptography 2>&1 | tail -3 || echo "    [warn] cryptography 安装失败，PEM 需手动生成"
fi

echo "==> [3/6] 确认 Ed25519 PEM 存在"
mkdir -p "$APP_DIR/data"
if [ -f "$APP_DIR/data/ed25519.pem" ]; then
  echo "    PEM 已存在（通常由部署脚本从本地预生成后上传）"
else
  # 服务端回退：若已安装 cryptography，则本地从 seed 生成
  if [ -n "$SEED_HEX" ] && python3 -c "import cryptography" 2>/dev/null; then
    SEED_HEX="${SEED_HEX//[^0-9a-fA-F]/}"
    echo "    从 seed 生成 PEM ..."
    SEED_HEX="$SEED_HEX" python3 "$APP_DIR/keygen.py" > "$APP_DIR/data/ed25519.pem"
    chmod 600 "$APP_DIR/data/ed25519.pem"
    echo "    PEM 已生成"
  else
    echo "    [warn] ed25519.pem 缺失且服务端无法生成（缺 cryptography）。"
    echo "           激活接口将在首次请求时失败。请手动上传 data/ed25519.pem 后重启服务。"
  fi
fi

echo "==> [4/6] 安装 systemd 服务"
cp "$APP_DIR/deploy/xlsone-activation.service" /etc/systemd/system/xlsone-activation.service
chmod 644 /etc/systemd/system/xlsone-activation.service
if [ -f "$APP_DIR/.env" ]; then chmod 600 "$APP_DIR/.env"; fi
systemctl daemon-reload
systemctl enable xlsone-activation
systemctl restart xlsone-activation
sleep 2
if systemctl is-active --quiet xlsone-activation; then
  echo "    服务已启动 ($(curl -fsS http://127.0.0.1:$PORT/api/health || echo 'health 检查失败'))"
else
  echo "    [error] 服务未启动，查看: journalctl -u xlsone-activation -n 50"
  exit 1
fi

echo "==> [5/6] 在 z-pulse.cn 主站 nginx 上添加 /api 与 /offline 代理（无需 DNS 即可用）"
python3 - <<'PY'
import os, re

cfg = '/etc/nginx/conf.d/default.conf'
s = open(cfg, encoding='utf-8').read()

ADMIN_PATH = os.environ.get('ADMIN_PATH', '/xlsone/license-console')
PORT = os.environ.get('PORT', '8787')

def remove_location_block(text, path):
    # 删除形如 "location = PATH { ... }" 的块（无嵌套花括号）
    pattern = r'^[ \t]*location\s*=\s*' + re.escape(path) + r'\s*\{[^\}]*?\}\n*'
    return re.sub(pattern, '', text, flags=re.MULTILINE)

# 移除旧的、容易被猜到的管理后台路径
s = remove_location_block(s, '/admin')
s = remove_location_block(s, '/xlsone/admin')

need = []
if 'location /api/' not in s:
    need.append('''    # xlsOne 激活 API 代理（由 install.sh 注入）
    location /api/ {
        proxy_pass http://127.0.0.1:8787;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 60s;
    }
''')
if 'location = /offline' not in s:
    need.append('''    location = /offline {
        proxy_pass http://127.0.0.1:8787/offline;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
''')
if 'location = /xlsone/offline' not in s:
    need.append('''    location = /xlsone/offline {
        proxy_pass http://127.0.0.1:8787/offline;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
''')
if f'location = {ADMIN_PATH}' not in s:
    need.append(f'''    location = {ADMIN_PATH} {{
        proxy_pass http://127.0.0.1:{PORT}{ADMIN_PATH};
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }}
''')
if 'location = /xlsone {' not in s:
    need.append('''    location = /xlsone {
        return 301 /xlsone/;
    }
''')
if 'location /activation/' not in s:
    need.append('''    # xlsOne 安装包下载代理（由 install.sh 注入）
    # 在 api.z-pulse.cn 的 DNS/证书就绪前，下载页与安装包经此命名空间暴露：
    #   https://z-pulse.cn/activation/downloads/  ->  Node /downloads/
    #   https://z-pulse.cn/activation/downloads/<file>  ->  Node 数据目录文件
    location /activation/ {
        proxy_pass http://127.0.0.1:8787/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 60s;
    }
''')
if not need:
    print('    代理已存在，跳过')
else:
    # 在 443 server 块的 `location / {` 之前插入
    marker = '    location / {'
    if marker in s:
        s = s.replace(marker, ''.join(need) + '\n' + marker, 1)
        open(cfg, 'w', encoding='utf-8').write(s)
        print('    已注入 %d 个 location 块' % len(need))
    else:
        print('    [warn] 未找到注入点，请手动在 443 server 块添加 /api 代理')
PY

echo "==> [6/6] 校验并重载 nginx"
if nginx -t 2>&1; then
  systemctl reload nginx
  echo "    nginx 已重载"
else
  echo "    [error] nginx 配置校验失败，请检查 default.conf"
fi

echo
echo "==================== 完成 ===================="
echo "后端已在以下地址可用（无需 DNS）："
echo "  https://z-pulse.cn/api/health"
echo "  https://z-pulse.cn/api/activate/windows"
echo "  https://z-pulse.cn/xlsone/offline"
echo "  https://z-pulse.cn/activation/downloads/   (安装包下载页)"
echo
echo "如需使用独立子域名 api.z-pulse.cn，还需两步："
echo "  1) 在域名解析后台添加 A 记录： api.z-pulse.cn -> 47.97.115.235"
echo "  2) 服务器执行： certbot --nginx -d api.z-pulse.cn"
echo "     并将 deploy/nginx-api.z-pulse.cn.conf 复制到 /etc/nginx/conf.d/ 后 reload"
echo "=============================================="
