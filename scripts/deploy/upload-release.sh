#!/usr/bin/env bash
# scripts/deploy/upload-release.sh
# 发布新版本：复制安装包到 downloads/ 目录，更新 version.json
set -euo pipefail

VERSION="${1:?Usage: $0 <version>}"
DEB_FILE="${2:?Usage: $0 <version> <path-to-deb>}"

SITE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/site"
DOWNLOADS="${SITE_DIR}/downloads"
API_FILE="${SITE_DIR}/api/version.json"

echo "=== Uploading xlsOne ${VERSION} ==="

# 复制安装包
cp -v "${DEB_FILE}" "${DOWNLOADS}/xlsOne-${VERSION}-linux-arm64.deb"

echo "Package copied to downloads/"

echo "Done. Next steps:"
echo "  1. Update ${API_FILE} with latest_version: \"${VERSION}\""
echo "  2. Update download URLs in version.json"
echo "  3. Update site/products/xlsone/download.html with new version"
echo "  4. Run: bash scripts/deploy/upload-site.sh (to push to server)"
