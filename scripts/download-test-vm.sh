#!/usr/bin/env bash
#
# download-test-vm.sh — 一键下载 VirtualBox + Microsoft Dev VM (VirtualBox 版)
#
# 用法:
#   chmod +x download-test-vm.sh
#   ./download-test-vm.sh [--win10|--win11] [--dir <目录>]
#
# 默认下载 Windows 10 开发者 VM。加 --win11 下载 Windows 11 版。
# 默认保存到 ./test-vm/ 目录。
#
set -euo pipefail

# ── 参数解析 ──────────────────────────────────────────────
WIN_VERSION="10"
OUTPUT_DIR="./test-vm"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --win10) WIN_VERSION="10"; shift ;;
        --win11) WIN_VERSION="11"; shift ;;
        --dir)   OUTPUT_DIR="$2"; shift 2 ;;
        -h|--help)
            echo "用法: $0 [--win10|--win11] [--dir <目录>]"
            echo ""
            echo "选项:"
            echo "  --win10    下载 Windows 10 开发者 VM (默认)"
            echo "  --win11    下载 Windows 11 开发者 VM"
            echo "  --dir DIR  保存目录 (默认: ./test-vm)"
            exit 0 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

# ── 颜色输出 ──────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ── 检查依赖 ──────────────────────────────────────────────
check_dep() {
    if ! command -v "$1" &>/dev/null; then
        error "缺少依赖: $1 — 请先安装 (如: sudo apt install $2)"
        exit 1
    fi
}

check_dep curl "curl"
check_dep wget "wget"

echo ""
echo "=============================================="
echo "  VirtualBox + Microsoft Dev VM 一键下载"
echo "  目标系统: Windows $WIN_VERSION"
echo "  保存目录: $(pwd)"
echo "=============================================="
echo ""

# ── 1. 下载 VirtualBox ───────────────────────────────────
info "正在获取 VirtualBox 最新版本号..."

VBOX_VERSION=$(curl -sL "https://download.virtualbox.org/virtualbox/LATEST-STABLE.TXT" 2>/dev/null | tr -d '[:space:]')

if [[ -z "$VBOX_VERSION" ]]; then
    warn "无法自动获取版本号，使用默认 7.1.6"
    VBOX_VERSION="7.1.6"
fi

ok "VirtualBox 最新稳定版: $VBOX_VERSION"

# 判断 Linux 发行版，选择对应包
VBOX_FILE=""
VBOX_URL=""

if [[ -f /etc/os-release ]]; then
    . /etc/os-release
    DISTRO="${ID:-}"
    info "检测到发行版: $DISTRO (${VERSION_ID:-unknown})"
else
    DISTRO="unknown"
fi

case "$DISTRO" in
    ubuntu|debian|linuxmint|pop)
        VBOX_FILE="virtualbox-${VBOX_VERSION}-164728~Ubuntu~noble_amd64.deb"
        # 尝试多个 Ubuntu 版本代号
        for CODENAME in noble jammy focal; do
            VBOX_URL="https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/virtualbox-${VBOX_VERSION}-164728~Ubuntu~${CODENAME}_amd64.deb"
            if curl -sI "$VBOX_URL" | grep -q "200 OK"; then
                VBOX_FILE="virtualbox-${VBOX_VERSION}-164728~Ubuntu~${CODENAME}_amd64.deb"
                break
            fi
        done
        # 如果精确匹配失败，用通用 URL
        if ! curl -sI "$VBOX_URL" | grep -q "200 OK"; then
            info "精确匹配失败，使用版本目录索引查找 .deb 包..."
            VBOX_DEB=$(curl -sL "https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/" | grep -oP 'virtualbox-[^"]*\.deb' | grep -i ubuntu | head -1)
            if [[ -n "$VBOX_DEB" ]]; then
                VBOX_FILE="$VBOX_DEB"
                VBOX_URL="https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/${VBOX_DEB}"
            fi
        fi
        ;;
    fedora|rhel|centos|rocky|alma)
        VBOX_FILE="VirtualBox-${VBOX_VERSION}-164728_fedora40.x86_64.rpm"
        for REL in 40 39 38; do
            VBOX_URL="https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/VirtualBox-${VBOX_VERSION}-164728_fedora${REL}.x86_64.rpm"
            if curl -sI "$VBOX_URL" | grep -q "200 OK"; then
                VBOX_FILE="VirtualBox-${VBOX_VERSION}-164728_fedora${REL}.x86_64.rpm"
                break
            fi
        done
        if ! curl -sI "$VBOX_URL" | grep -q "200 OK"; then
            VBOX_RPM=$(curl -sL "https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/" | grep -oP 'VirtualBox-[^"]*\.rpm' | grep -i fedora | head -1)
            if [[ -n "$VBOX_RPM" ]]; then
                VBOX_FILE="$VBOX_RPM"
                VBOX_URL="https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/${VBOX_RPM}"
            fi
        fi
        ;;
    arch|manjaro)
        info "Arch 系发行版 — 建议直接: sudo pacman -S virtualbox"
        info "跳过 VirtualBox 下载"
        VBOX_FILE=""
        ;;
    *)
        warn "未识别的发行版 '$DISTRO' — 下载通用 .run 安装包"
        VBOX_FILE="VirtualBox-${VBOX_VERSION}-164728-Linux_amd64.run"
        VBOX_URL="https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/${VBOX_FILE}"
        ;;
esac

if [[ -n "$VBOX_FILE" && -n "$VBOX_URL" ]]; then
    if [[ -f "$VBOX_FILE" ]]; then
        ok "VirtualBox 已存在: $VBOX_FILE (跳过下载)"
    else
        info "下载 VirtualBox: $VBOX_URL"
        wget -c --show-progress -O "$VBOX_FILE" "$VBOX_URL" || {
            warn "精确版本 URL 失败，尝试目录索引..."
            # 最后兜底：从目录列表找 .deb/.rpm
            if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
                VBOX_DEB=$(curl -sL "https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/" | grep -oP 'virtualbox-[^"]*\.deb' | head -1)
                if [[ -n "$VBOX_DEB" ]]; then
                    VBOX_FILE="$VBOX_DEB"
                    wget -c --show-progress -O "$VBOX_FILE" "https://download.virtualbox.org/virtualbox/${VBOX_VERSION}/${VBOX_DEB}"
                fi
            fi
        }
        [[ -f "$VBOX_FILE" ]] && ok "VirtualBox 下载完成: $VBOX_FILE" || error "VirtualBox 下载失败"
    fi
fi

echo ""

# ── 2. 下载 Microsoft Dev VM ─────────────────────────────
info "正在获取 Microsoft 开发者 VM 下载链接 (Windows $WIN_VERSION)..."

# 微软 Dev VM 页面
DEV_VM_PAGE="https://developer.microsoft.com/en-us/windows/downloads/virtual-machines/"

# 获取页面内容，提取 VirtualBox 版本的下载链接
PAGE_HTML=$(curl -sL "$DEV_VM_PAGE" 2>/dev/null)

if [[ -z "$PAGE_HTML" ]]; then
    error "无法获取微软开发者 VM 页面"
    error "请手动访问: $DEV_VM_PAGE"
    error "下载 VirtualBox 版本的 .ova 文件"
    exit 1
fi

# 提取 VirtualBox .ova 链接
# 微软页面格式: <a href="...ova">VirtualBox</a>
OVA_URL=""

if [[ "$WIN_VERSION" == "11" ]]; then
    # Windows 11 的链接
    OVA_URL=$(echo "$PAGE_HTML" | grep -oP 'https://[^"]*\.ova[^"]*' | head -1)
else
    # Windows 10 — 页面上可能有多个 .ova，取第一个或按标签关联
    OVA_URL=$(echo "$PAGE_HTML" | grep -oP 'https://[^"]*\.ova[^"]*' | head -1)
fi

if [[ -z "$OVA_URL" ]]; then
    # 尝试另一种提取方式 — 有些页面用 data 属性
    OVA_URL=$(echo "$PAGE_HTML" | grep -oiP 'href="([^"]*virtualbox[^"]*\.ova[^"]*)"' | grep -oP 'https?://[^"]*' | head -1)
fi

if [[ -z "$OVA_URL" ]]; then
    # 最后尝试 — 抓所有 .ova 链接
    OVA_URL=$(echo "$PAGE_HTML" | grep -oiP 'https?://[^"'\''<>\s]+\.ova' | head -1)
fi

if [[ -z "$OVA_URL" ]]; then
    warn "无法自动提取 .ova 链接（微软页面可能用了 JS 动态渲染）"
    echo ""
    echo "请手动操作:"
    echo "  1. 浏览器打开: $DEV_VM_PAGE"
    echo "  2. 选择 'VirtualBox' 标签"
    echo -e "  3. ${GREEN}Windows ${WIN_VERSION}${NC} — 点击下载 .ova 文件"
    echo "  4. 将 .ova 文件放到: $(pwd)/"
    echo ""
    echo "  或者用已知链接（可能过期，注意检查）:"

    # 已知的微软 Dev VM 下载模式（通常会重定向到 CDN）
    # 这些 URL 可能随版本更新而变化
    if [[ "$WIN_VERSION" == "11" ]]; then
        echo "  Win11: https://aka.ms/windev/VirtualBox"
    else
        echo "  Win10: https://aka.ms/windev/VirtualBox"
    fi
    echo ""
    echo "  可以尝试: wget -c \"\$(curl -sIL -o /dev/null -w '%{url_effective}' 'https://aka.ms/windev/VirtualBox')\""
else
    OVA_FILE=$(basename "$OVA_URL" | sed 's/[?].*//')

    if [[ -f "$OVA_FILE" ]]; then
        ok "Dev VM 已存在: $OVA_FILE (跳过下载)"
    else
        info "Dev VM 文件名: $OVA_FILE"
        info "下载链接: $OVA_URL"
        info "开始下载 (文件约 20GB，请耐心等待)..."
        echo ""

        # 微软的下载链接通常有重定向，用 wget 跟随
        wget -c --show-progress -O "$OVA_FILE" "$OVA_URL" || {
            warn "直接下载失败，尝试跟随重定向..."
            # 获取最终 URL
            FINAL_URL=$(curl -sIL -o /dev/null -w '%{url_effective}' "$OVA_URL" 2>/dev/null)
            if [[ -n "$FINAL_URL" && "$FINAL_URL" != "$OVA_URL" ]]; then
                info "重定向到: $FINAL_URL"
                wget -c --show-progress -O "$OVA_FILE" "$FINAL_URL"
            else
                error "下载失败，请手动访问: $DEV_VM_PAGE"
            fi
        }

        [[ -f "$OVA_FILE" ]] && ok "Dev VM 下载完成: $OVA_FILE" || error "Dev VM 下载失败"
    fi
fi

echo ""

# ── 3. 汇总 ─────────────────────────────────────────────
echo "=============================================="
echo "  下载完成"
echo "=============================================="
echo ""
echo "保存目录: $(pwd)"
echo ""
echo "文件列表:"
ls -lh *.deb *.rpm *.run *.ova 2>/dev/null || echo "(无文件)"
echo ""

# ── 4. 使用说明 ──────────────────────────────────────────
echo "=============================================="
echo "  后续步骤"
echo "=============================================="
echo ""
echo "1. 安装 VirtualBox:"
if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" || "$DISTRO" == "linuxmint" || "$DISTRO" == "pop" ]]; then
    echo "   sudo dpkg -i virtualbox-*.deb"
    echo "   sudo apt install -f   # 补依赖"
elif [[ "$DISTRO" == "fedora" || "$DISTRO" == "rhel" || "$DISTRO" == "centos" || "$DISTRO" == "rocky" || "$DISTRO" == "alma" ]]; then
    echo "   sudo dnf install VirtualBox-*.rpm"
elif [[ "$DISTRO" == "arch" || "$DISTRO" == "manjaro" ]]; then
    echo "   sudo pacman -S virtualbox"
else
    echo "   chmod +x VirtualBox-*.run && sudo ./VirtualBox-*.run"
fi
echo ""
echo "2. 导入 Dev VM:"
echo "   VirtualBox → 文件 → 导入虚拟电脑 → 选择 .ova 文件"
echo ""
echo "3. 启动 VM 后拍快照（干净状态）:"
echo "   菜单 → 机器 → 拍摄快照 → 命名 'clean'"
echo ""
echo "4. 设置共享文件夹（把 Windows 构建产物传进去）:"
echo "   菜单 → 设备 → 共享文件夹 → 添加 D:\\xlsone\\cpp\\build-windows-cn-release"
echo ""
echo "5. 在 VM 内安装测试 MSI:"
echo "   从共享文件夹运行 xlsone-1.0.4-windows-amd64.msi"
echo ""
echo "6. 测试完毕还原快照:"
echo "   菜单 → 机器 → 恢复到快照 → 选 'clean'"
echo ""
ok "完成！"
