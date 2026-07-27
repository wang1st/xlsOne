#!/usr/bin/env bash
#
# Deploy site files to z-pulse.cn using sshpass with password.
# Only syncs production files, excludes dev/node artifacts.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SITE_DIR="${PROJECT_ROOT}/site"
SECRETS_FILE="${HOME}/secrets.json"

SERVER="z-pulse.cn"
SERVER_USER="root"
REMOTE_ROOT="/var/www/z-pulse.cn"

PASSWORD="$(python3 - "$SECRETS_FILE" <<'PY'
import json
import sys
from pathlib import Path
data = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
print(data['ZPULSE_SERVER_ROOT_PASSWORD'])
PY
)"

if ! command -v sshpass &>/dev/null; then
    echo "sshpass is required. Install with: brew install sshpass"
    exit 1
fi

echo "Testing SSH connection..."
export SSHPASS="$PASSWORD"
sshpass -e ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 \
    "${SERVER_USER}@${SERVER}" 'echo OK'

echo "Syncing site files to ${SERVER_USER}@${SERVER}:${REMOTE_ROOT} ..."
rsync -avz \
    -e "sshpass -e ssh -o StrictHostKeyChecking=no" \
    --exclude='node_modules/' \
    --exclude='.DS_Store' \
    --exclude='.htmlhintrc.json' \
    --exclude='.stylelintrc.json' \
    --exclude='package.json' \
    --exclude='package-lock.json' \
    --exclude='README.md' \
    --exclude='nginx.conf' \
    "${SITE_DIR}/" "${SERVER_USER}@${SERVER}:${REMOTE_ROOT}/"

echo "Deployment complete."
