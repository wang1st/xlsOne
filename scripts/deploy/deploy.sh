#!/usr/bin/env bash
#
# xlsOne 一键部署脚本
# 交互式选择平台、版本，输入 z-pulse.cn root 密码后自动：
#   1. 更新 cpp/CMakeLists.txt、site/api/version.json、站点页面缓存戳
#   2. 构建（Linux）或定位（Windows/macOS）安装包
#   3. 上传安装包与站点文件到服务器
#
# 用法：bash scripts/deploy/deploy.sh
#

set -euo pipefail

# -----------------------------------------------------------------------------
# 颜色与 UI
# -----------------------------------------------------------------------------
RESET='\033[0m'
BOLD='\033[1m'
DIM='\033[2m'
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
CYAN='\033[36m'
WHITE='\033[37m'
BG_BLUE='\033[44m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SITE_DIR="${PROJECT_ROOT}/site"
CPP_DIR="${PROJECT_ROOT}/cpp"

SERVER="z-pulse.cn"
SERVER_USER="root"
REMOTE_ROOT="/var/www/z-pulse.cn"
REMOTE_BACKEND_DIR="/opt/xlsone-activation"

# -----------------------------------------------------------------------------
# UI 函数
# -----------------------------------------------------------------------------
clear_screen() {
    printf '\033[2J\033[H'
}

print_header() {
    clear_screen
    printf "${BLUE}╔════════════════════════════════════════════════════════════════╗${RESET}\n" >&2
    printf "${BLUE}║${RESET}                                                                ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}║${RESET}  ${BOLD}${CYAN}xlsOne${RESET}  一键部署脚本                                        ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}║${RESET}  ${DIM}自动更新版本信息并上传安装包到 z-pulse.cn${RESET}                    ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}║${RESET}                                                                ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}╚════════════════════════════════════════════════════════════════╝${RESET}\n\n" >&2
}

print_step() {
    local step="$1"
    local total="$2"
    local msg="$3"
    printf "\n${BLUE}[${step}/${total}]${RESET} ${BOLD}%s${RESET}\n" "$msg" >&2
    printf "${DIM}%s${RESET}\n" "$(printf '─%.0s' $(seq 1 64))" >&2
}

print_info() {
    printf "${CYAN}ℹ${RESET}  %s\n" "$1" >&2
}

print_success() {
    printf "${GREEN}✓${RESET}  %s\n" "$1" >&2
}

print_warning() {
    printf "${YELLOW}⚠${RESET}  %s\n" "$1" >&2
}

print_error() {
    printf "${RED}✗${RESET}  %s\n" "$1" >&2
}

print_box() {
    local title="$1"
    shift
    local lines=("$@")
    local width=64
    printf "\n${BLUE}┌%s┐${RESET}\n" "$(printf '─%.0s' $(seq 1 $width))" >&2
    printf "${BLUE}│${RESET} ${BOLD}%-${width}s${RESET} ${BLUE}│${RESET}\n" "$title" >&2
    printf "${BLUE}├%s┤${RESET}\n" "$(printf '─%.0s' $(seq 1 $width))" >&2
    for line in "${lines[@]}"; do
        printf "${BLUE}│${RESET} %-${width}s ${BLUE}│${RESET}\n" "$line" >&2
    done
    printf "${BLUE}└%s┘${RESET}\n" "$(printf '─%.0s' $(seq 1 $width))" >&2
}

# -----------------------------------------------------------------------------
# 依赖检查
# -----------------------------------------------------------------------------
check_deps() {
    local missing=()
    for cmd in sshpass scp tar python3 sed; do
        if ! command -v "$cmd" &>/dev/null; then
            missing+=("$cmd")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        print_error "缺少必要工具：${missing[*]}"
        print_info "请安装后重试，例如：sudo apt-get install sshpass sed tar python3"
        exit 1
    fi
}

# -----------------------------------------------------------------------------
# 平台选择
# -----------------------------------------------------------------------------
select_platform() {
    local choice
    while true; do
        print_box "请选择要部署的平台" \
            "[1] Linux AMD64 (.deb)" \
            "[2] Linux ARM64 (.deb)" \
            "[3] Windows AMD64 (.msi + .zip)" \
            "[4] macOS (.dmg)" \
            "[5] 全部平台（仅上传本地已存在的包）" \
            "[q] 退出"
        printf "\n请输入选项 [1-5/q]: " >&2
        read -r choice
        case "$choice" in
            1) echo "linux_amd64"; return ;;
            2) echo "linux_arm64"; return ;;
            3) echo "windows"; return ;;
            4) echo "macos"; return ;;
            5) echo "all"; return ;;
            q|Q) exit 0 ;;
            *) print_warning "无效选项，请重新输入" ;;
        esac
    done
}

# -----------------------------------------------------------------------------
# 版本号输入
# -----------------------------------------------------------------------------
input_version() {
    local current_version
    current_version="$(grep -oP 'VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "${CPP_DIR}/CMakeLists.txt" | head -1)"
    local version
    while true; do
        printf "\n请输入版本号 [当前: ${CYAN}%s${RESET}]: " "$current_version" >&2
        read -r version
        if [[ -z "$version" ]]; then
            version="$current_version"
        fi
        if [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            echo "$version"
            return
        fi
        print_warning "版本号格式应为 x.y.z，例如 1.0.7"
    done
}

# -----------------------------------------------------------------------------
# 升级说明输入
# -----------------------------------------------------------------------------
input_changelog() {
    local changelog
    printf "\n请输入升级说明 [默认: 修复了某些已知错误]: " >&2
    read -r changelog
    if [[ -z "$changelog" ]]; then
        changelog="修复了某些已知错误"
    fi
    echo "$changelog"
}

# -----------------------------------------------------------------------------
# 密码输入
# -----------------------------------------------------------------------------
input_password() {
    local password
    printf "\n请输入 ${YELLOW}%s@%s${RESET} 的密码（输入不显示）: " "$SERVER_USER" "$SERVER" >&2
    read -rs password
    printf "\n" >&2
    if [[ -z "$password" ]]; then
        print_error "密码不能为空"
        exit 1
    fi
    echo "$password"
}

# -----------------------------------------------------------------------------
# 更新版本信息
# -----------------------------------------------------------------------------
update_version_files() {
    local version="$1"
    local changelog="$2"
    local major minor patch
    IFS='.' read -r major minor patch <<< "$version"

    print_info "更新 cpp/CMakeLists.txt: $version"
    python3 - "$version" "${CPP_DIR}/CMakeLists.txt" <<'PYEOF'
import re
import sys
from pathlib import Path
version, path = sys.argv[1:3]
p = Path(path)
text = p.read_text(encoding='utf-8')
text = re.sub(
    r'(project\([^)]*?VERSION\s+)[0-9]+\.[0-9]+\.[0-9]+',
    r'\g<1>' + version,
    text,
    count=1,
    flags=re.DOTALL
)
p.write_text(text, encoding='utf-8')
PYEOF

    print_info "更新站点页面 CSS/JS 缓存戳: v${version}-1"
    find "$SITE_DIR" -name "*.html" -type f -print0 | xargs -0 \
        sed -i -E "s/\?v=[0-9]+\.[0-9]+\.[0-9]+-[0-9]+/?v=${version}-1/g"

    print_info "更新 site/api/version.json"
    python3 - "$version" "$changelog" "$SITE_DIR/api/version.json" <<'PYEOF'
import json
import sys
from pathlib import Path

version, changelog, path = sys.argv[1:4]
api_path = Path(path)

data = json.loads(api_path.read_text(encoding='utf-8')) if api_path.exists() else {
    "latest_version": version,
    "changelog": changelog,
    "downloads": {},
    "checksums": {}
}

old_version = data.get("latest_version", version)
data["latest_version"] = version
data["changelog"] = changelog

new_downloads = {}
for key, url in data.get("downloads", {}).items():
    new_downloads[key] = url.replace(old_version, version)

# 确保 macos 字段存在
template = "https://z-pulse.cn/downloads/xlsOne-{}-{}.deb"
if "linux_arm64" not in new_downloads:
    new_downloads["linux_arm64"] = f"https://z-pulse.cn/downloads/xlsOne-{version}-linux-arm64.deb"
if "linux_amd64" not in new_downloads:
    new_downloads["linux_amd64"] = f"https://z-pulse.cn/downloads/xlsOne-{version}-linux-amd64.deb"
if "windows_amd64" not in new_downloads:
    new_downloads["windows_amd64"] = f"https://z-pulse.cn/downloads/xlsone-{version}-windows-amd64.msi"
if "windows_amd64_zip" not in new_downloads:
    new_downloads["windows_amd64_zip"] = f"https://z-pulse.cn/downloads/xlsone-{version}-windows-amd64.zip"
if "macos" not in new_downloads:
    new_downloads["macos"] = f"https://z-pulse.cn/downloads/xlsOne-{version}-macos.dmg"

data["downloads"] = new_downloads

# checksums 的 key 也随版本更新
new_checksums = {}
for fname, checksum in data.get("checksums", {}).items():
    new_fname = fname.replace(old_version, version)
    new_checksums[new_fname] = checksum

# 保留已知包的旧 checksum 不会丢失，新的包需要后续计算再写入
data["checksums"] = new_checksums

api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding='utf-8')
PYEOF

    print_success "版本信息更新完成"
}

# -----------------------------------------------------------------------------
# 构建或定位安装包
# -----------------------------------------------------------------------------
locate_or_build_package() {
    local platform="$1"
    local version="$2"
    local package_path=""

    case "$platform" in
        linux_amd64)
            package_path="${CPP_DIR}/build-linux-release/xlsone-${version}-linux-amd64.deb"
            if [[ -f "$package_path" ]]; then
                print_info "找到已构建的 Linux AMD64 包: $package_path"
            else
                printf "\n"
                print_warning "未找到 Linux AMD64 包: $package_path"
                printf "是否立即构建？${YELLOW}(需要 ninja)${RESET} [y/N]: " >&2
                local build
                read -r build
                if [[ "$build" =~ ^[yY]$ ]]; then
                    if [[ ! -d "${CPP_DIR}/build-linux-release" ]]; then
                        print_error "未找到 build-linux-release 目录，请先配置 CMake 构建"
                        exit 1
                    fi
                    (cd "${CPP_DIR}/build-linux-release" && ninja && ninja package)
                    if [[ ! -f "$package_path" ]]; then
                        print_error "构建后仍未找到包: $package_path"
                        exit 1
                    fi
                else
                    print_info "跳过 Linux AMD64 包"
                    return
                fi
            fi
            normalize_package_name "linux_amd64" "$package_path"
            return
            ;;
        linux_arm64)
            package_path="${CPP_DIR}/build-linux-release/xlsone-${version}-linux-arm64.deb"
            if [[ -f "$package_path" ]]; then
                print_info "找到已构建的 Linux ARM64 包: $package_path"
            else
                print_warning "Linux ARM64 包需交叉编译或在 ARM64 设备上构建"
                print_info "请手动构建后放到: $package_path"
                printf "是否继续不上传此包？ [Y/n]: " >&2
                local cont
                read -r cont
                if [[ "$cont" =~ ^[nN]$ ]]; then
                    exit 0
                fi
                return
            fi
            normalize_package_name "linux_arm64" "$package_path"
            return
            ;;
        windows)
            package_path="${CPP_DIR}/build-windows-cn-release/xlsone-${version}-windows-amd64.msi"
            local zip_path="${CPP_DIR}/build-windows-cn-release/xlsone-${version}-windows-amd64.zip"
            if [[ -f "$package_path" && -f "$zip_path" ]]; then
                print_info "找到 Windows 安装包: $package_path"
                print_info "找到 Windows 便携包: $zip_path"
                normalize_package_name "windows" "$package_path"
                normalize_package_name "windows_zip" "$zip_path"
                return
            else
                print_warning "未找到 Windows 包，期望路径："
                print_info "  $package_path"
                print_info "  $zip_path"
                printf "是否继续不上传 Windows 包？ [Y/n]: " >&2
                local cont
                read -r cont
                if [[ "$cont" =~ ^[nN]$ ]]; then
                    exit 0
                fi
                return
            fi
            ;;
        macos)
            package_path="${CPP_DIR}/build-macos-release/xlsOne-${version}-macos.dmg"
            if [[ -f "$package_path" ]]; then
                print_info "找到 macOS 安装包: $package_path"
            else
                print_warning "未找到 macOS 包，期望路径: $package_path"
                printf "是否继续不上传 macOS 包？ [Y/n]: " >&2
                local cont
                read -r cont
                if [[ "$cont" =~ ^[nN]$ ]]; then
                    exit 0
                fi
                return
            fi
            normalize_package_name "macos" "$package_path"
            return
            ;;
    esac

    if [[ -n "$package_path" && -f "$package_path" ]]; then
        echo "$package_path"
    fi
}

# -----------------------------------------------------------------------------
# 规范化安装包文件名，使其与 version.json 中的 URL 一致
# -----------------------------------------------------------------------------
normalize_package_name() {
    local platform="$1"
    local src_path="$2"
    local version_json="$SITE_DIR/api/version.json"

    local json_key=""
    case "$platform" in
        linux_amd64) json_key="linux_amd64" ;;
        linux_arm64) json_key="linux_arm64" ;;
        windows) json_key="windows_amd64" ;;
        windows_zip) json_key="windows_amd64_zip" ;;
        macos) json_key="macos" ;;
        *) echo "$src_path"; return ;;
    esac

    local expected_fname
    expected_fname="$(python3 - "$json_key" "$version_json" <<'PYEOF'
import json
import sys
from pathlib import Path
from urllib.parse import urlparse

key, path = sys.argv[1:3]
data = json.loads(Path(path).read_text(encoding='utf-8'))
url = data.get("downloads", {}).get(key, "")
print(urlparse(url).path.split('/')[-1])
PYEOF
    )"

    local src_fname
    src_fname="$(basename "$src_path")"
    if [[ "$src_fname" == "$expected_fname" ]]; then
        echo "$src_path"
        return
    fi

    local dest_dir="/tmp/xlsone-deploy-packages"
    mkdir -p "$dest_dir"
    local dest_path="${dest_dir}/${expected_fname}"
    cp -f "$src_path" "$dest_path"
    print_info "规范化文件名: $src_fname -> $expected_fname"
    echo "$dest_path"
}

# -----------------------------------------------------------------------------
# 计算并写入 checksum
# -----------------------------------------------------------------------------
update_checksum() {
    local package_path="$1"
    local version_json="$SITE_DIR/api/version.json"
    local fname
    fname="$(basename "$package_path")"
    local checksum
    checksum="$(sha256sum "$package_path" | awk '{print $1}')"
    print_info "计算 checksum: $fname = $checksum"
    python3 - "$fname" "$checksum" "$version_json" <<'PYEOF'
import json
import sys
from pathlib import Path

fname, checksum, path = sys.argv[1:4]
api_path = Path(path)
data = json.loads(api_path.read_text(encoding='utf-8'))
data.setdefault("checksums", {})[fname] = checksum
api_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding='utf-8')
PYEOF
}

# -----------------------------------------------------------------------------
# 上传到服务器
# -----------------------------------------------------------------------------
upload_to_server() {
    local version="$1"
    local password="$2"
    shift 2
    local packages=("$@")

    print_info "准备上传站点文件..."

    # 打包站点文件
    local site_tar="/tmp/xlsone-deploy-site-${version}.tar.gz"
    (cd "$SITE_DIR" && tar -czf "$site_tar" \
        index.html \
        xlsone/index.html \
        xlsone/download.html \
        xlsone/buy.html \
        products/xlsone/index.html \
        products/xlsone/download.html \
        support/index.html \
        privacy/index.html \
        api/version.json \
        css/style.css \
        robots.txt \
        sitemap.xml \
        downloads/checksums.txt \
        2>/dev/null || true)

    # 上传并解压站点文件
    SSHPASS="$password" sshpass -e scp -o StrictHostKeyChecking=no -o ConnectTimeout=10 \
        "$site_tar" "${SERVER_USER}@${SERVER}:/tmp/" 2>&1 | while read -r line; do
        print_info "scp: $line"
    done

    local remote_tar="/tmp/$(basename "$site_tar")"
    SSHPASS="$password" sshpass -e ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 \
        "${SERVER_USER}@${SERVER}" "
            set -e
            cd ${REMOTE_ROOT}
            tar -xzf ${remote_tar}
            rm -f ${remote_tar}
            mkdir -p ${REMOTE_BACKEND_DIR}/site/api
            cp -f ${REMOTE_ROOT}/api/version.json ${REMOTE_BACKEND_DIR}/site/api/version.json
            echo 'SITE_UPLOADED'
        " 2>&1 | while read -r line; do
            if [[ "$line" == "SITE_UPLOADED" ]]; then
                print_success "站点文件上传完成"
            else
                print_info "remote: $line"
            fi
        done

    rm -f "$site_tar"

    # 上传安装包
    if [[ ${#packages[@]} -gt 0 ]]; then
        print_info "准备上传 ${#packages[@]} 个安装包..."
        for pkg in "${packages[@]}"; do
            if [[ -f "$pkg" ]]; then
                local fname
                fname="$(basename "$pkg")"
                print_info "上传 $fname ..."
                SSHPASS="$password" sshpass -e scp -o StrictHostKeyChecking=no -o ConnectTimeout=10 \
                    "$pkg" "${SERVER_USER}@${SERVER}:${REMOTE_ROOT}/downloads/" 2>&1 | while read -r line; do
                    print_info "scp: $line"
                done
                print_success "$fname 上传完成"
            fi
        done
    else
        print_warning "没有需要上传的安装包"
    fi
}

# -----------------------------------------------------------------------------
# 重新生成 downloads/checksums.txt
# -----------------------------------------------------------------------------
regenerate_checksums_txt() {
    local version_json="$SITE_DIR/api/version.json"
    local checksums_txt="$SITE_DIR/downloads/checksums.txt"

    print_info "重新生成 downloads/checksums.txt"
    python3 - "$version_json" "$checksums_txt" <<'PYEOF'
import json
import sys
from pathlib import Path

version_json, checksums_txt = sys.argv[1:3]
data = json.loads(Path(version_json).read_text(encoding='utf-8'))
checksums = data.get("checksums", {})

lines = []
for fname in sorted(checksums.keys()):
    lines.append(f"{checksums[fname]}  {fname}")

Path(checksums_txt).write_text("\n".join(lines) + ("\n" if lines else ""), encoding='utf-8')
PYEOF
    print_success "checksums.txt 已生成"
}

# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------
main() {
    print_header
    check_deps

    local platform
    platform="$(select_platform)"
    local version
    version="$(input_version)"
    local changelog
    changelog="$(input_changelog)"

    print_box "部署摘要" \
        "平台: ${platform}" \
        "版本: ${version}" \
        "说明: ${changelog}" \
        "服务器: ${SERVER_USER}@${SERVER}"

    printf "\n确认开始部署？ [y/N]: " >&2
    local confirm
    read -r confirm
    if [[ ! "$confirm" =~ ^[yY]$ ]]; then
        print_info "已取消部署"
        exit 0
    fi

    local password
    password="$(input_password)"

    # 测试连接
    print_info "测试服务器连接..."
    if ! SSHPASS="$password" sshpass -e ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 \
        "${SERVER_USER}@${SERVER}" 'echo OK' >/dev/null 2>&1; then
        print_error "无法连接到服务器，请检查密码和网络"
        exit 1
    fi
    print_success "服务器连接成功"

    # 步骤 1: 更新版本信息
    print_step 1 4 "更新版本信息"
    update_version_files "$version" "$changelog"

    # 步骤 2: 定位/构建安装包
    print_step 2 4 "定位或构建安装包"
    local packages=()
    case "$platform" in
        all)
            for p in linux_amd64 linux_arm64 windows macos; do
                local paths
                paths="$(locate_or_build_package "$p" "$version")"
                if [[ -n "$paths" ]]; then
                    while IFS= read -r line; do
                        [[ -n "$line" ]] && packages+=("$line")
                    done <<< "$paths"
                fi
            done
            ;;
        *)
            local paths
            paths="$(locate_or_build_package "$platform" "$version")"
            if [[ -n "$paths" ]]; then
                while IFS= read -r line; do
                    [[ -n "$line" ]] && packages+=("$line")
                done <<< "$paths"
            fi
            ;;
    esac

    # 步骤 3: 计算 checksum
    print_step 3 4 "计算安装包 checksum"
    if [[ ${#packages[@]} -gt 0 ]]; then
        for pkg in "${packages[@]}"; do
            update_checksum "$pkg"
        done
        regenerate_checksums_txt
    else
        print_warning "没有本地包需要计算 checksum"
    fi

    # 步骤 4: 上传
    print_step 4 4 "上传到服务器"
    upload_to_server "$version" "$password" "${packages[@]}"

    # 完成
    printf "\n${GREEN}╔════════════════════════════════════════════════════════════════╗${RESET}\n" >&2
    printf "${GREEN}║${RESET}                                                                ${GREEN}║${RESET}\n" >&2
    printf "${GREEN}║${RESET}  ${BOLD}部署完成！${RESET}                                                    ${GREEN}║${RESET}\n" >&2
    printf "${GREEN}║${RESET}  版本: ${CYAN}%s${RESET}                                                  ${GREEN}║${RESET}\n" "$version" >&2
    printf "${GREEN}║${RESET}  平台: ${CYAN}%s${RESET}                                                  ${GREEN}║${RESET}\n" "$platform" >&2
    printf "${GREEN}║${RESET}  请访问 https://z-pulse.cn 查看效果                            ${GREEN}║${RESET}\n" >&2
    printf "${GREEN}║${RESET}                                                                ${GREEN}║${RESET}\n" >&2
    printf "${GREEN}╚════════════════════════════════════════════════════════════════╝${RESET}\n\n" >&2
}

main "$@"
