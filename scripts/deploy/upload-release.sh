#!/usr/bin/env bash
# scripts/deploy/upload-release.sh
# 发布新的 Linux .deb 版本：复制安装包到 downloads/，更新 version.json 与 checksums.txt。
# 多架构（amd64 / arm64）自动识别，其他平台（Windows/macOS）的下载链接和 checksum 保持不变。
set -euo pipefail

VERSION="${1:?Usage: $0 <version> <path-to-deb> [changelog]}"
DEB_FILE="${2:?Usage: $0 <version> <path-to-deb> [changelog]}"
CHANGELOG="${3:-${VERSION} 版本发布}"

if [[ ! -f "${DEB_FILE}" ]]; then
    echo "Error: deb file not found: ${DEB_FILE}" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SITE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)/site"
DOWNLOADS="${SITE_DIR}/downloads"
API_FILE="${SITE_DIR}/api/version.json"
CHECKSUMS_FILE="${DOWNLOADS}/checksums.txt"

mkdir -p "${DOWNLOADS}"

# 从 deb 包中读取架构（amd64 / arm64）
ARCH="$(dpkg-deb -f "${DEB_FILE}" Architecture)"
if [[ -z "${ARCH}" ]]; then
    echo "Error: could not detect architecture from ${DEB_FILE}" >&2
    exit 1
fi

DEST_NAME="xlsOne-${VERSION}-linux-${ARCH}.deb"
DEST_PATH="${DOWNLOADS}/${DEST_NAME}"

echo "=== Uploading xlsOne ${VERSION} linux-${ARCH} ==="

cp -v "${DEB_FILE}" "${DEST_PATH}"

# 计算并追加 checksum
CHECKSUM="$(sha256sum "${DEST_PATH}" | awk '{print $1}')"
echo "${CHECKSUM}  ${DEST_NAME}" >> "${CHECKSUMS_FILE}"
echo "checksum: ${CHECKSUM}"

# 更新 version.json：只更新 linux_${ARCH} 的下载链接和对应 checksum，其余保持不变
python3 - "${VERSION}" "${CHANGELOG}" "${ARCH}" "${DEST_NAME}" "${CHECKSUM}" "${API_FILE}" << 'PYEOF'
import json
import sys
from pathlib import Path

version, changelog, arch, dest_name, checksum, api_file = sys.argv[1:7]
api_path = Path(api_file)

if api_path.exists():
    data = json.loads(api_path.read_text(encoding='utf-8'))
else:
    data = {
        "latest_version": version,
        "changelog": changelog,
        "downloads": {},
        "checksums": {}
    }

data["latest_version"] = version
data["changelog"] = changelog
data.setdefault("downloads", {})[f"linux_{arch}"] = f"https://z-pulse.cn/downloads/{dest_name}"
data.setdefault("checksums", {})[dest_name] = checksum

api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding='utf-8')
PYEOF

echo "version.json updated"
echo ""
echo "Done. Next steps:"
echo "  1. Upload macOS/Windows packages to ${DOWNLOADS}/ and update their checksums if applicable"
echo "  2. Commit: git add site/ cpp/CMakeLists.txt && git commit -m 'release: v${VERSION}'"
echo "  3. Tag: git tag v${VERSION}"
echo "  4. Deploy site"
