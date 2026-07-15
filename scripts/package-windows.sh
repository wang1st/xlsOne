#!/usr/bin/env bash
#
# xlsOne Windows 安装包一键打包脚本
# 生成 .msi (WiX) 和 .zip 两种安装包
#
# 本脚本会自动探测运行环境：
#   - Windows / WSL / MSYS / Git Bash -> 直接调用 PowerShell 打包脚本
#   - Linux + 配置 WINDOWS_BUILD_HOST -> 通过 SSH 在远程 Windows 机器上构建
#   - 其他情况 -> 输出手动打包指引
#
# 用法：bash scripts/package-windows.sh [选项]
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CPP_DIR="${PROJECT_ROOT}/cpp"
PS_SCRIPT="${CPP_DIR}/scripts/package_windows_full.ps1"
BUILD_DIR="${CPP_DIR}/build-windows-cn-release"

# 远程构建默认配置（可通过环境变量覆盖）
WINDOWS_BUILD_HOST="${WINDOWS_BUILD_HOST:-}"
WINDOWS_BUILD_USER="${WINDOWS_BUILD_USER:-root}"
WINDOWS_BUILD_DIR="${WINDOWS_BUILD_DIR:-C:\\xlsone}"
WINDOWS_BUILD_PASSWORD="${WINDOWS_BUILD_PASSWORD:-}"

# 选项
CLEAN_FLAG=""
SIGN_ARGS=""
DRY_RUN=0

# -----------------------------------------------------------------------------
# UI 函数
# -----------------------------------------------------------------------------
print_header() {
    printf "${BLUE}╔════════════════════════════════════════════════════════════════╗${RESET}\n" >&2
    printf "${BLUE}║${RESET}                                                                ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}║${RESET}  ${BOLD}${CYAN}xlsOne${RESET}  Windows 安装包一键打包                          ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}║${RESET}  ${DIM}生成 .msi 与 .zip 安装包${RESET}                                  ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}║${RESET}                                                                ${BLUE}║${RESET}\n" >&2
    printf "${BLUE}╚════════════════════════════════════════════════════════════════╝${RESET}\n\n" >&2
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

# -----------------------------------------------------------------------------
# 环境检测
# -----------------------------------------------------------------------------
detect_os_family() {
    case "$(uname -s)" in
        CYGWIN*|MINGW*|MSYS*|Windows_NT)
            echo "windows"
            ;;
        Linux)
            # 检测 WSL
            if [[ -f /proc/sys/fs/binfmt_misc/WSLInterop ]] || \
               [[ "$(uname -r)" == *[Mm]icrosoft* ]] || \
               [[ -n "${WSL_DISTRO_NAME:-}" ]]; then
                echo "wsl"
            else
                echo "linux"
            fi
            ;;
        Darwin)
            echo "macos"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

find_powershell() {
    local os
    os="$1"
    if [[ "$os" == "wsl" ]]; then
        # WSL 优先用 Windows 的 powershell.exe
        if command -v powershell.exe &>/dev/null; then
            echo "powershell.exe"
            return
        fi
    fi
    if command -v pwsh &>/dev/null; then
        echo "pwsh"
    elif command -v powershell &>/dev/null; then
        echo "powershell"
    elif command -v powershell.exe &>/dev/null; then
        echo "powershell.exe"
    else
        echo ""
    fi
}

# -----------------------------------------------------------------------------
# 本地 / WSL 构建
# -----------------------------------------------------------------------------
run_local_build() {
    local ps
    ps="$(find_powershell "$(detect_os_family)")"
    if [[ -z "$ps" ]]; then
        print_error "未找到 PowerShell，无法运行打包脚本"
        print_info "请安装 PowerShell 后重试：https://aka.ms/powershell"
        exit 1
    fi

    if [[ ! -f "$PS_SCRIPT" ]]; then
        print_error "未找到 PowerShell 打包脚本: $PS_SCRIPT"
        exit 1
    fi

    local ps_script_path="$PS_SCRIPT"
    # WSL 调用 Windows 版 powershell.exe 时需要 Windows 路径
    if [[ "$ps" == "powershell.exe" ]] && command -v wslpath &>/dev/null; then
        ps_script_path="$(wslpath -w "$PS_SCRIPT")"
    fi

    print_info "调用 PowerShell 打包脚本: $ps_script_path"
    print_info "PowerShell: $ps"

    local args=()
    args+=("-ExecutionPolicy" "Bypass" "-File" "$ps_script_path" "-Domestic")
    if [[ -n "$CLEAN_FLAG" ]]; then
        args+=("-Clean")
    fi
    if [[ -n "$SIGN_ARGS" ]]; then
        # shellcheck disable=SC2086
        args+=($SIGN_ARGS)
    fi

    if [[ "$DRY_RUN" -eq 1 ]]; then
        print_info "[DRY-RUN] 将执行命令:"
        echo "$ps" "${args[@]}"
        return
    fi

    "$ps" "${args[@]}"

    print_success "Windows 安装包构建完成"
    if [[ -d "$BUILD_DIR" ]]; then
        find "$BUILD_DIR" -maxdepth 1 \( -name '*.msi' -o -name '*.zip' \) -print | while read -r f; do
            print_info "产物: $f"
        done
    fi
}

# -----------------------------------------------------------------------------
# 远程 SSH 构建（Linux 无 WSL 时的备选）
# -----------------------------------------------------------------------------
run_remote_build() {
    if [[ -z "$WINDOWS_BUILD_HOST" ]]; then
        return 1
    fi

    for cmd in ssh scp; do
        if ! command -v "$cmd" &>/dev/null; then
            print_error "远程构建需要 $cmd，请先安装"
            exit 1
        fi
    done

    print_info "远程 Windows 构建机: ${WINDOWS_BUILD_USER}@${WINDOWS_BUILD_HOST}"
    print_info "远程仓库目录: ${WINDOWS_BUILD_DIR}"

    local ssh_prefix=()
    local scp_prefix=()
    if [[ -n "$WINDOWS_BUILD_PASSWORD" ]]; then
        if ! command -v sshpass &>/dev/null; then
            print_error "远程构建配置了密码，需要 sshpass"
            exit 1
        fi
        ssh_prefix=(sshpass -e)
        scp_prefix=(sshpass -e)
        export SSHPASS="$WINDOWS_BUILD_PASSWORD"
    fi

    local target="${WINDOWS_BUILD_USER}@${WINDOWS_BUILD_HOST}"
    local ssh_opts="-o StrictHostKeyChecking=no -o ConnectTimeout=10"

    if [[ "$DRY_RUN" -eq 1 ]]; then
        print_info "[DRY-RUN] 将执行远程构建:"
        echo "ssh ${ssh_opts} ${target} \"cd ${WINDOWS_BUILD_DIR} && git pull && powershell -ExecutionPolicy Bypass -File cpp\\\\scripts\\\\package_windows_full.ps1 -Domestic ${CLEAN_FLAG}\""
        echo "scp ${target}:\"${WINDOWS_BUILD_DIR}\\\\cpp\\\\build-windows-cn-release\\\\*.msi\" ${BUILD_DIR}/"
        echo "scp ${target}:\"${WINDOWS_BUILD_DIR}\\\\cpp\\\\build-windows-cn-release\\\\*.zip\" ${BUILD_DIR}/"
        return
    fi

    mkdir -p "$BUILD_DIR"

    print_info "同步远程仓库代码..."
    "${ssh_prefix[@]}" ssh $ssh_opts "$target" \
        "cd ${WINDOWS_BUILD_DIR} && git pull" || {
        print_error "远程代码同步失败，请检查 WINDOWS_BUILD_DIR 路径和仓库"
        exit 1
    }

    print_info "在远程机器上构建 Windows 安装包..."
    "${ssh_prefix[@]}" ssh $ssh_opts "$target" \
        "cd ${WINDOWS_BUILD_DIR} && powershell -ExecutionPolicy Bypass -File cpp\\scripts\\package_windows_full.ps1 -Domestic ${CLEAN_FLAG}" || {
        print_error "远程构建失败"
        exit 1
    }

    print_info "下载构建产物..."
    "${scp_prefix[@]}" scp $ssh_opts \
        "${target}:${WINDOWS_BUILD_DIR}\\cpp\\build-windows-cn-release\\*.msi" \
        "${BUILD_DIR}/" || print_warning "未下载到 .msi"
    "${scp_prefix[@]}" scp $ssh_opts \
        "${target}:${WINDOWS_BUILD_DIR}\\cpp\\build-windows-cn-release\\*.zip" \
        "${BUILD_DIR}/" || print_warning "未下载到 .zip"

    print_success "远程构建完成，产物已下载到: $BUILD_DIR"
}

# -----------------------------------------------------------------------------
# 帮助信息
# -----------------------------------------------------------------------------
show_help() {
    cat <<EOF
xlsOne Windows 安装包一键打包脚本

用法:
  bash scripts/package-windows.sh [选项]

选项:
  -c, --clean          清理构建目录后重新构建
  -d, --dry-run        只显示将要执行的命令，不实际运行
  -s, --sign FILE      使用 PFX 证书代码签名（需同时提供 -p 密码）
  -p, --password PASS  代码签名证书密码
  -h, --help           显示本帮助

环境变量（Linux 远程构建时使用）:
  WINDOWS_BUILD_HOST       远程 Windows 构建机 IP/域名
  WINDOWS_BUILD_USER       远程用户名（默认: root）
  WINDOWS_BUILD_DIR        远程仓库路径（默认: C:\\xlsone）
  WINDOWS_BUILD_PASSWORD   远程用户密码（留空则使用 SSH key）

说明:
  本脚本优先在 Windows/WSL 本地执行 PowerShell 打包脚本。
  在纯 Linux 上，需要配置 WINDOWS_BUILD_HOST 通过 SSH 调用远程 Windows。
  生成的安装包位于: cpp/build-windows-cn-release/
EOF
}

# -----------------------------------------------------------------------------
# 参数解析
# -----------------------------------------------------------------------------
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -c|--clean)
                CLEAN_FLAG="-Clean"
                shift
                ;;
            -d|--dry-run)
                DRY_RUN=1
                shift
                ;;
            -s|--sign)
                if [[ -z "${2:-}" ]]; then
                    print_error "--sign 需要指定 PFX 证书文件路径"
                    exit 1
                fi
                SIGN_ARGS="-Sign -CertFile \"$2\""
                shift 2
                ;;
            -p|--password)
                if [[ -z "${2:-}" ]]; then
                    print_error "--password 需要指定证书密码"
                    exit 1
                fi
                SIGN_ARGS="${SIGN_ARGS} -CertPassword \"$2\""
                shift 2
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
    done
}

# -----------------------------------------------------------------------------
# 主流程
# -----------------------------------------------------------------------------
main() {
    parse_args "$@"

    print_header

    local os
    os="$(detect_os_family)"
    print_info "检测到运行环境: $os"

    case "$os" in
        windows|wsl)
            run_local_build
            ;;
        linux|macos|unknown)
            if [[ -n "$WINDOWS_BUILD_HOST" ]]; then
                run_remote_build
            else
                print_warning "当前系统 ($os) 无法直接生成 Windows 安装包"
                print_info "可选方案："
                print_info "  1. 在 Windows 或 WSL 中运行本脚本"
                print_info "  2. 配置远程 Windows 构建机后重试："
                cat <<EOF >&2

export WINDOWS_BUILD_HOST=192.168.1.100
export WINDOWS_BUILD_USER=admin
export WINDOWS_BUILD_DIR='C:\\xlsone'
# 如使用密码登录：
export WINDOWS_BUILD_PASSWORD='your-password'

EOF
                print_info "  3. 手动在 Windows 上运行："
                print_info "     powershell -ExecutionPolicy Bypass -File cpp\\scripts\\package_windows_full.ps1 -Domestic"
                exit 1
            fi
            ;;
    esac
}

main "$@"
