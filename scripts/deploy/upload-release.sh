#!/usr/bin/env bash
# scripts/deploy/upload-release.sh
# 发布新版本：复制安装包到 downloads/，更新 version.json
set -euo pipefail

VERSION="${1:?Usage: $0 <version> <path-to-deb>}"
DEB_FILE="${2:?Usage: $0 <version> <path-to-deb>}"
CHANGELOG="${3:-${VERSION} 版本发布}"

SITE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/site"
DOWNLOADS="${SITE_DIR}/downloads"
API_FILE="${SITE_DIR}/api/version.json"

echo "=== Uploading xlsOne ${VERSION} ==="

# 复制安装包
cp -v "${DEB_FILE}" "${DOWNLOADS}/xlsOne-${VERSION}-linux-arm64.deb"

# 更新 version.json
cat > "${API_FILE}" << EOF
{
  "latest_version": "${VERSION}",
  "changelog": "${CHANGELOG}",
  "downloads": {
    "macos": "https://z-pulse.cn/downloads/xlsOne-${VERSION}-macos-arm64.dmg",
    "windows": "https://z-pulse.cn/downloads/xlsOne-${VERSION}-win64.exe",
    "linux": "https://z-pulse.cn/downloads/xlsOne-${VERSION}-linux-arm64.deb"
  }
}
EOF

echo "version.json updated"
echo ""
echo "Done. Next steps:"
echo "  1. Upload macOS/Windows packages to downloads/"
echo "  2. Commit: git add site/ && git commit -m 'release: v${VERSION}'"
echo "  3. Tag: git tag v${VERSION}"
echo "  4. Deploy: bash scripts/deploy/upload-site.sh"
