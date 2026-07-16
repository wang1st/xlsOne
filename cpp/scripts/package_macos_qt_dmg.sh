#!/usr/bin/env bash
set -euo pipefail

# One-click DMG builder for xlsOne Qt on macOS.
#
# Builds the Qt/C++ port, bundles Qt frameworks with macdeployqt, and produces
# a drag-and-drop .dmg ready for distribution.
#
# Usage:
#   ./cpp/scripts/package_macos_qt_dmg.sh [options] [build-dir]
#
# Options:
#   --obfuscate              Enable compile-time secret obfuscation.
#                            Requires XLSONE_LICENSE_PUBLIC_KEY env var.
#   --signed                 Enable code signing.
#                            Requires --team-id or XLSONE_DEVELOPMENT_TEAM.
#   --team-id <id>           Apple Developer Team ID for signing.
#   --notarize               Submit the DMG for Apple notarization (signed only).
#                            Requires APPLE_ID and APP_SPECIFIC_PASSWORD.
#   --version <version>      Version string used in filenames and bundle plist.
#                            Default: value from site/api/version.json or CMake project version.
#   --output <path>          Output DMG path.
#                            Default: ./xlsOne-<version>-macos-<arch>.dmg
#   --arch <arch>            Target architecture: host, arm64, x86_64, or universal.
#                            Default: host.
#   --build-dir <path>       CMake build directory. Default: ./build-macos-release
#   -h, --help               Show this help message.

usage() {
    cat <<'EOF'
Build the xlsOne Qt app and package it into a .dmg.

Usage:
  ./cpp/scripts/package_macos_qt_dmg.sh [options] [build-dir]

Options:
  --obfuscate            Enable compile-time secret obfuscation (requires
                         XLSONE_LICENSE_PUBLIC_KEY environment variable).
  --domestic             Use domestic (China) service endpoints
                         (api.z-pulse.cn instead of api.xlsone.com).
  --signed               Enable code signing (requires --team-id or
                         XLSONE_DEVELOPMENT_TEAM environment variable).
  --team-id <id>         Apple Developer Team ID.
  --notarize             Submit the DMG for notarization (signed builds only).
                         Requires APPLE_ID and APP_SPECIFIC_PASSWORD.
  --version <version>    Version string used in the DMG name and bundle plist.
                         Default: site/api/version.json latest_version or "0.0.0".
  --output <path>        Output DMG path.
                         Default: ./xlsOne-<version>-macos-<arch>.dmg
  --arch <arch>          Target architecture: host, arm64, x86_64, or universal.
                         universal builds arm64 and x86_64 bundles, then merges
                         all Mach-O files with lipo.
  --build-dir <path>     CMake build directory. Default: ./build-macos-release
  --app-only             Build/deploy the .app bundle but skip DMG creation.
  -h, --help             Show this help message.

Environment:
  XLSONE_LICENSE_PUBLIC_KEY   64-hex-char Ed25519 public key (for --obfuscate).
  XLSONE_DEVELOPMENT_TEAM     Default Team ID for signed builds.
  XLSONE_QMAKE_ARM64          Optional qmake path for arm64 Qt.
  XLSONE_QMAKE_X86_64         Optional qmake path for x86_64 Qt.
  APPLE_ID                    Apple ID for notarization.
  APP_SPECIFIC_PASSWORD       App-specific password for notarization.

Examples:
  # Unsigned DMG for local testing
  ./cpp/scripts/package_macos_qt_dmg.sh

  # Domestic (China) DMG
  ./cpp/scripts/package_macos_qt_dmg.sh --domestic

  # Signed DMG
  ./cpp/scripts/package_macos_qt_dmg.sh --signed --team-id ABCDE12345

  # Obfuscated + signed + notarized release DMG
  ./cpp/scripts/package_macos_qt_dmg.sh --obfuscate --signed --team-id ABCDE12345 --notarize

  # Universal DMG for both Intel and Apple Silicon Macs
  ./cpp/scripts/package_macos_qt_dmg.sh --arch universal
EOF
}

ORIG_DIR="$(pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"      # cpp/ directory
REPO_ROOT="$(cd "$PROJECT_ROOT/.." && pwd)"       # repository root

# ---- Defaults ----
BUILD_DIR=""
OUTPUT_DMG=""
VERSION=""
OBFUSCATE=0
DOMESTIC=0
SIGNED_BUILD=0
TEAM_ID="${XLSONE_DEVELOPMENT_TEAM:-}"
NOTARIZE=0
REQUESTED_ARCH="host"
APP_ONLY=0

# ---- Parse arguments ----
while [ $# -gt 0 ]; do
    case "$1" in
        --obfuscate)
            OBFUSCATE=1
            shift
            ;;
        --domestic)
            DOMESTIC=1
            shift
            ;;
        --signed)
            SIGNED_BUILD=1
            shift
            ;;
        --team-id)
            TEAM_ID="${2:-}"
            shift 2
            ;;
        --notarize)
            NOTARIZE=1
            shift
            ;;
        --version)
            VERSION="${2:-}"
            shift 2
            ;;
        --output)
            OUTPUT_DMG="${2:-}"
            shift 2
            ;;
        --arch)
            REQUESTED_ARCH="${2:-}"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="${2:-}"
            shift 2
            ;;
        --app-only)
            APP_ONLY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
        *)
            if [ -z "$BUILD_DIR" ]; then
                BUILD_DIR="$1"
            else
                echo "Unexpected argument: $1" >&2
                usage
                exit 1
            fi
            shift
            ;;
    esac
done

HOST_ARCH="$(uname -m)"
case "$REQUESTED_ARCH" in
    host|"")
        BUILD_ARCH="$HOST_ARCH"
        ;;
    arm64|aarch64)
        BUILD_ARCH="arm64"
        ;;
    x86_64|amd64)
        BUILD_ARCH="x86_64"
        ;;
    universal)
        BUILD_ARCH="universal"
        ;;
    *)
        echo "Unsupported --arch value: $REQUESTED_ARCH" >&2
        echo "Supported values: host, arm64, x86_64, universal" >&2
        exit 2
        ;;
esac

# Resolve build directory
if [ -z "$BUILD_DIR" ]; then
    if [ "$BUILD_ARCH" = "$HOST_ARCH" ]; then
        BUILD_DIR="$PROJECT_ROOT/build-macos-release"
    else
        BUILD_DIR="$PROJECT_ROOT/build-macos-${BUILD_ARCH}-release"
    fi
elif [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ORIG_DIR/$BUILD_DIR"
fi
BUILD_DIR="$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$BUILD_DIR")"

# Resolve version
if [ -z "$VERSION" ]; then
    if [ -f "$REPO_ROOT/site/api/version.json" ]; then
        VERSION="$(python3 -c "import json,sys; print(json.load(open('$REPO_ROOT/site/api/version.json')).get('latest_version',''))" 2>/dev/null || echo "")"
    fi
    if [ -z "$VERSION" ]; then
        VERSION="0.0.0"
    fi
fi

# Architecture tag
case "$BUILD_ARCH" in
    x86_64)  PKG_ARCH="amd64" ;;
    arm64)   PKG_ARCH="arm64" ;;
    universal) PKG_ARCH="universal" ;;
    *)       PKG_ARCH="$BUILD_ARCH" ;;
esac

# Resolve output DMG
if [ -z "$OUTPUT_DMG" ]; then
    OUTPUT_DMG="$PROJECT_ROOT/xlsOne-${VERSION}-macos-${PKG_ARCH}.dmg"
elif [[ "$OUTPUT_DMG" != /* ]]; then
    OUTPUT_DMG="$ORIG_DIR/$OUTPUT_DMG"
fi
OUTPUT_DMG_DIR="$(dirname "$OUTPUT_DMG")"
OUTPUT_DMG="$(cd "$OUTPUT_DMG_DIR" 2>/dev/null && pwd || echo "$OUTPUT_DMG_DIR")/$(basename "$OUTPUT_DMG")"

# Validate signing / notarization options
if [ "$OBFUSCATE" -eq 1 ] && [ -z "${XLSONE_LICENSE_PUBLIC_KEY:-}" ]; then
    echo "--obfuscate requires XLSONE_LICENSE_PUBLIC_KEY environment variable." >&2
    exit 2
fi

if [ "$SIGNED_BUILD" -eq 1 ] && [ -z "$TEAM_ID" ]; then
    echo "Signed build requires --team-id or XLSONE_DEVELOPMENT_TEAM." >&2
    exit 2
fi

if [ "$NOTARIZE" -eq 1 ] && [ "$SIGNED_BUILD" -eq 0 ]; then
    echo "--notarize requires --signed." >&2
    exit 2
fi

if [ "$NOTARIZE" -eq 1 ] && { [ -z "${APPLE_ID:-}" ] || [ -z "${APP_SPECIFIC_PASSWORD:-}" ]; }; then
    echo "Notarization requires APPLE_ID and APP_SPECIFIC_PASSWORD environment variables." >&2
    exit 2
fi

cd "$PROJECT_ROOT"

echo "========================================="
echo "  xlsOne macOS DMG Packager (Qt)"
echo "========================================="
echo ""
echo "Build dir:    $BUILD_DIR"
echo "Version:      $VERSION"
echo "Architecture: $BUILD_ARCH"
echo "Output DMG:   $OUTPUT_DMG"
echo ""

# ---------------------------------------------------------------------------
# Check build dependencies
# ---------------------------------------------------------------------------
echo "==> Checking build dependencies ..."
MISSING_TOOLS=""
for cmd in cmake ninja python3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "   ✗ $cmd not found"
        MISSING_TOOLS="$MISSING_TOOLS $cmd"
    fi
done

if [ -n "$MISSING_TOOLS" ]; then
    echo "" >&2
    echo "Missing tools:$MISSING_TOOLS" >&2
    echo "Install Qt for macOS (e.g. via Homebrew or aqtinstall) and ensure" >&2
    echo "cmake, ninja, qmake and macdeployqt are on PATH." >&2
    exit 1
fi
echo "   ✓ All build dependencies found"
echo ""

INSTALL_README_NAME="安装前必看.txt"

write_install_readme() {
    local target_file="$1"
    cat > "$target_file" <<'EOF'
xlsOne 安装前必看

感谢安装 xlsOne。这个 DMG 是 macOS Universal 安装包，同时支持 Intel Mac 和 Apple Silicon Mac。

安装步骤：

1. 打开 DMG 后，把 xlsOneQt.app 拖到 Applications（应用程序）文件夹。
2. 安装完成后，从“应用程序”文件夹启动 xlsOne；不要直接在 DMG 里运行。
3. 第一次启动如果 macOS 提示“无法验证开发者”或阻止打开：
   - 打开“系统设置” > “隐私与安全性”；
   - 在页面底部找到 xlsOne 的安全提示；
   - 点击“仍要打开”，然后再次确认打开。
4. 如果仍无法打开，可在 Finder 中进入“应用程序”，按住 Control 键点击 xlsOneQt.app，选择“打开”。

内部测试包补充说明：

如果这是未公证的测试包，macOS 可能会显示“已损坏，无法打开”。请确认安装包来自官方或内部可信渠道后，再打开“终端”执行：

xattr -dr com.apple.quarantine "/Applications/xlsOneQt.app"
open "/Applications/xlsOneQt.app"

安装成功后，可以推出并删除这个 DMG 文件。
EOF
}

prepare_dmg_source_dir() {
    local app_bundle="$1"
    local source_dir="$2"
    local include_applications_link="${3:-1}"
    local app_name
    app_name="$(basename "$app_bundle")"

    rm -rf "$source_dir"
    mkdir -p "$source_dir"
    ditto "$app_bundle" "$source_dir/$app_name"
    write_install_readme "$source_dir/$INSTALL_README_NAME"

    if [ "$include_applications_link" -eq 1 ]; then
        ln -s /Applications "$source_dir/Applications"
    fi
}

if [ "$BUILD_ARCH" = "universal" ]; then
    UNIVERSAL_ROOT="$BUILD_DIR"
    ARM_BUILD_DIR="$UNIVERSAL_ROOT/arm64"
    X86_BUILD_DIR="$UNIVERSAL_ROOT/x86_64"
    UNIVERSAL_APP_DIR="$UNIVERSAL_ROOT/universal"
    APP_BUNDLE="$UNIVERSAL_APP_DIR/xlsOneQt.app"
    ARM_APP="$ARM_BUILD_DIR/app/xlsOneQt.app"
    X86_APP="$X86_BUILD_DIR/app/xlsOneQt.app"

    child_args=(--version "$VERSION" --app-only)
    if [ "$OBFUSCATE" -eq 1 ]; then
        child_args+=(--obfuscate)
    fi
    if [ "$DOMESTIC" -eq 1 ]; then
        child_args+=(--domestic)
    fi

    echo "==> Building arm64 app bundle ..."
    "$SCRIPT_DIR/package_macos_qt_dmg.sh" "${child_args[@]}" --arch arm64 --build-dir "$ARM_BUILD_DIR"

    echo ""
    echo "==> Building x86_64 app bundle ..."
    "$SCRIPT_DIR/package_macos_qt_dmg.sh" "${child_args[@]}" --arch x86_64 --build-dir "$X86_BUILD_DIR"

    if [ ! -d "$ARM_APP" ] || [ ! -d "$X86_APP" ]; then
        echo "Error: expected single-architecture app bundles were not produced." >&2
        echo "arm64:  $ARM_APP" >&2
        echo "x86_64: $X86_APP" >&2
        exit 1
    fi

    echo ""
    echo "==> Merging app bundles into a universal app ..."
    rm -rf "$UNIVERSAL_APP_DIR"
    mkdir -p "$UNIVERSAL_APP_DIR"
    ditto "$ARM_APP" "$APP_BUNDLE"

    MERGE_TMP="$(mktemp -d "$UNIVERSAL_ROOT/lipo.XXXXXX")"
    trap 'rm -rf "$MERGE_TMP"' EXIT

    copy_or_extract_arch() {
        local input_file="$1"
        local arch="$2"
        local output_file="$3"
        local archs
        archs="$(lipo -archs "$input_file")"
        if [ "$archs" = "$arch" ]; then
            cp "$input_file" "$output_file"
        else
            lipo "$input_file" -extract "$arch" -output "$output_file"
        fi
    }

    merged_count=0
    while IFS= read -r -d '' arm_file; do
        rel_path="${arm_file#$APP_BUNDLE/}"
        x86_file="$X86_APP/$rel_path"

        if ! lipo -archs "$arm_file" >/dev/null 2>&1; then
            continue
        fi
        if [ ! -f "$x86_file" ] || ! lipo -archs "$x86_file" >/dev/null 2>&1; then
            echo "Error: missing x86_64 Mach-O counterpart for $rel_path" >&2
            exit 1
        fi

        arm_archs="$(lipo -archs "$arm_file")"
        x86_archs="$(lipo -archs "$x86_file")"
        if [[ " $arm_archs " != *" arm64 "* ]]; then
            echo "Error: arm64 file does not contain arm64: $rel_path ($arm_archs)" >&2
            exit 1
        fi
        if [[ " $x86_archs " != *" x86_64 "* ]]; then
            echo "Error: x86_64 file does not contain x86_64: $rel_path ($x86_archs)" >&2
            exit 1
        fi

        arm_slice="$MERGE_TMP/$(printf '%s' "$rel_path" | sed 's#[/:]#_#g').arm64"
        x86_slice="$MERGE_TMP/$(printf '%s' "$rel_path" | sed 's#[/:]#_#g').x86_64"
        merged_slice="$MERGE_TMP/$(printf '%s' "$rel_path" | sed 's#[/:]#_#g').universal"
        copy_or_extract_arch "$arm_file" arm64 "$arm_slice"
        copy_or_extract_arch "$x86_file" x86_64 "$x86_slice"
        lipo -create "$arm_slice" "$x86_slice" -output "$merged_slice"
        chmod --reference="$arm_file" "$merged_slice" 2>/dev/null || chmod "$(stat -f '%Lp' "$arm_file")" "$merged_slice"
        mv "$merged_slice" "$arm_file"
        merged_count=$((merged_count + 1))
    done < <(find "$APP_BUNDLE" -type f -print0)

    if [ "$merged_count" -eq 0 ]; then
        echo "Error: no Mach-O files were merged; universal app would be invalid." >&2
        exit 1
    fi

    echo "Merged Mach-O files: $merged_count"
    xattr -cr "$APP_BUNDLE" 2>/dev/null || true

    BINARY_PATH="$APP_BUNDLE/Contents/MacOS/xlsOneQt"
    lipo -info "$BINARY_PATH"

    if [ "$SIGNED_BUILD" -eq 1 ]; then
        echo "==> Applying Developer ID signature to universal app ..."
        codesign --force --deep --sign "Developer ID Application: $TEAM_ID" \
            --options runtime \
            "$APP_BUNDLE"
        codesign --verify --deep --strict "$APP_BUNDLE"
    else
        echo "==> Applying ad-hoc code signature to universal app ..."
        codesign --force --deep --sign - "$APP_BUNDLE"
    fi

    if [ "$APP_ONLY" -eq 1 ]; then
        echo ""
        echo "Universal app bundle prepared: $APP_BUNDLE"
        exit 0
    fi

    echo ""
    echo "==> Creating DMG ..."
    rm -f "$OUTPUT_DMG"
    mkdir -p "$(dirname "$OUTPUT_DMG")"

    APP_NAME="xlsOneQt.app"
    VOLNAME="xlsOne $VERSION"

    CREATE_DMG_OK=0
    if [ "${XLSONE_USE_CREATE_DMG:-1}" != "0" ] && command -v create-dmg >/dev/null 2>&1; then
        echo "Using create-dmg ..."
        DMG_SOURCE_DIR="$BUILD_DIR/dmg-root"
        prepare_dmg_source_dir "$APP_BUNDLE" "$DMG_SOURCE_DIR" 0
        if create-dmg \
            --volname "$VOLNAME" \
            --window-size 800 400 \
            --icon-size 100 \
            --app-drop-link 600 185 \
            --icon "$APP_NAME" 200 185 \
            "$OUTPUT_DMG" \
            "$DMG_SOURCE_DIR"; then
            CREATE_DMG_OK=1
        else
            echo "create-dmg failed; using hdiutil fallback ..."
        fi
    fi

    if [ "$CREATE_DMG_OK" -eq 0 ]; then
        if [ "${XLSONE_USE_CREATE_DMG:-1}" = "0" ]; then
            echo "create-dmg disabled; using hdiutil fallback ..."
        elif ! command -v create-dmg >/dev/null 2>&1; then
            echo "create-dmg not found; using hdiutil fallback ..."
        fi

        TEMP_DMG="$BUILD_DIR/temp-xlsOne.dmg"
        DMG_SOURCE_DIR="$BUILD_DIR/dmg-root"
        prepare_dmg_source_dir "$APP_BUNDLE" "$DMG_SOURCE_DIR" 1
        APP_SIZE="$(du -sm "$DMG_SOURCE_DIR" | cut -f1)"
        DMG_SIZE=$((APP_SIZE + 20))

        rm -f "$TEMP_DMG"
        hdiutil create -size "${DMG_SIZE}m" -volname "$VOLNAME" \
            -srcfolder "$DMG_SOURCE_DIR" -fs HFS+ -format UDRW "$TEMP_DMG"
        hdiutil convert "$TEMP_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUTPUT_DMG"
        rm -f "$TEMP_DMG"
    fi

    if [ "$NOTARIZE" -eq 1 ]; then
        echo ""
        echo "==> Submitting DMG for notarization ..."
        xcrun notarytool submit "$OUTPUT_DMG" \
            --apple-id "$APPLE_ID" \
            --password "$APP_SPECIFIC_PASSWORD" \
            --team-id "$TEAM_ID" \
            --wait

        echo "==> Stapling notarization ticket ..."
        xcrun stapler staple "$OUTPUT_DMG"
    fi

    echo ""
    echo "========================================="
    echo "  DMG generated successfully"
    echo "========================================="
    echo "Output:      $OUTPUT_DMG"
    echo "Version:     $VERSION"
    echo "Build mode:  Release"
    echo "Architecture: universal"
    if [ "$OBFUSCATE" -eq 1 ]; then
        echo "Obfuscation: ON"
    else
        echo "Obfuscation: OFF"
    fi
    if [ "$DOMESTIC" -eq 1 ]; then
        echo "Endpoints:   Domestic (api.z-pulse.cn)"
    else
        echo "Endpoints:   International (api.xlsone.com)"
    fi
    if [ "$SIGNED_BUILD" -eq 1 ]; then
        echo "Signed:      YES (Team ID: $TEAM_ID)"
    else
        echo "Signed:      NO"
    fi
    if [ "$NOTARIZE" -eq 1 ]; then
        echo "Notarized:   YES"
    else
        echo "Notarized:   NO"
    fi
    echo ""
    echo "Install by dragging xlsOneQt.app to /Applications."
    exit 0
fi

# ---------------------------------------------------------------------------
# Detect Qt installation
# ---------------------------------------------------------------------------
# The project requires Qt6::ZlibPrivate when building against Qt6.  Homebrew's
# Qt6 package does not ship private headers/config, so on macOS we prefer Qt5
# unless a Qt6 installation with ZlibPrivate is explicitly available.

collect_qmake_candidates() {
    # Repo-local Qt installed by aqt/manual extraction for universal packaging.
    for path in \
        "$REPO_ROOT/.build/Qt-6.8.3-manual/bin/qmake" \
        "$REPO_ROOT/.build/Qt-6.8.3/6.8.3/macos/bin/qmake" \
        "$REPO_ROOT/.build/Qt-6.8.3/bin/qmake"; do
        [ -x "$path" ] && echo "$path"
    done

    if [ "$BUILD_ARCH" = "arm64" ] && [ -n "${XLSONE_QMAKE_ARM64:-}" ]; then
        [ -x "$XLSONE_QMAKE_ARM64" ] && echo "$XLSONE_QMAKE_ARM64"
    fi
    if [ "$BUILD_ARCH" = "x86_64" ] && [ -n "${XLSONE_QMAKE_X86_64:-}" ]; then
        [ -x "$XLSONE_QMAKE_X86_64" ] && echo "$XLSONE_QMAKE_X86_64"
    fi
    # PATH-resolved binaries
    for name in qmake qmake-qt5 qmake-qt6; do
        command -v "$name" 2>/dev/null && true
    done
    # Common Homebrew locations
    for path in \
        /opt/homebrew/opt/qt@5/bin/qmake \
        /opt/homebrew/opt/qt/bin/qmake \
        /usr/local/opt/qt@5/bin/qmake \
        /usr/local/opt/qt/bin/qmake \
        /Applications/Qt/*/macos/bin/qmake; do
        [ -x "$path" ] && echo "$path"
    done
}

qt_has_zlib_private() {
    local _qmake="$1"
    local _prefix
    _prefix="$_qmake"
    while [ "$(basename "$_prefix")" != "bin" ] && [ "$_prefix" != "/" ]; do
        _prefix="$(dirname "$_prefix")"
    done
    _prefix="$(dirname "$_prefix")"
    [ -f "$_prefix/lib/cmake/Qt6ZlibPrivate/Qt6ZlibPrivateConfig.cmake" ]
}

qt_core_binary_for_qmake() {
    local _qmake="$1"
    local _lib_dir
    _lib_dir="$($_qmake -query QT_INSTALL_LIBS 2>/dev/null || true)"
    if [ -f "$_lib_dir/QtCore.framework/QtCore" ]; then
        echo "$_lib_dir/QtCore.framework/QtCore"
    fi
}

qt_supports_arch() {
    local _qmake="$1"
    local _arch="$2"
    local _qt_core
    local _archs
    _qt_core="$(qt_core_binary_for_qmake "$_qmake")"
    if [ -z "$_qt_core" ]; then
        return 1
    fi
    _archs="$(lipo -archs "$_qt_core" 2>/dev/null || true)"
    [[ " $_archs " == *" $_arch "* ]]
}

QMAKE_CMD=""
CMAKE_PREFIX_PATH_ARG=""

for candidate in $(collect_qmake_candidates | sort -u); do
    candidate_version="$($candidate -query QT_VERSION 2>/dev/null || true)"
    candidate_prefix="$($candidate -query QT_INSTALL_PREFIX 2>/dev/null || true)"
    if [ -z "$candidate_version" ] || [ -z "$candidate_prefix" ]; then
        continue
    fi
    if ! qt_supports_arch "$candidate" "$BUILD_ARCH"; then
        qt_core="$(qt_core_binary_for_qmake "$candidate")"
        qt_archs="$(lipo -archs "$qt_core" 2>/dev/null || echo "unknown")"
        echo "   Found Qt at $candidate but it does not support $BUILD_ARCH ($qt_archs); skipping."
        continue
    fi

    if [ "${candidate_version%%.*}" = "6" ]; then
        if ! qt_has_zlib_private "$candidate"; then
            echo "   Found Qt6 at $candidate without Qt6ZlibPrivate; using system ZLIB fallback."
        fi
        QMAKE_CMD="$candidate"
        CMAKE_PREFIX_PATH_ARG="$candidate_prefix"
        break
    elif [ "${candidate_version%%.*}" = "5" ]; then
        # Use the first viable Qt5 we find.
        QMAKE_CMD="$candidate"
        CMAKE_PREFIX_PATH_ARG="$candidate_prefix"
        break
    fi
done

if [ -z "$QMAKE_CMD" ]; then
    echo "Error: could not find a usable Qt installation." >&2
    echo "On macOS, Homebrew Qt6 does not include private headers." >&2
    echo "Install Qt5 (e.g. brew install qt@5) or a full Qt6 build with private headers." >&2
    exit 1
fi

QT_VERSION="$($QMAKE_CMD -query QT_VERSION 2>/dev/null || true)"
QT_LIB_DIR="$($QMAKE_CMD -query QT_INSTALL_LIBS 2>/dev/null || true)"
QT_PLUGIN_DIR="$($QMAKE_CMD -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
MACDEPLOYQT_PATH="$(dirname "$QMAKE_CMD")/macdeployqt"
if [ ! -x "$MACDEPLOYQT_PATH" ]; then
    MACDEPLOYQT_PATH="$(dirname "$QMAKE_CMD")/macdeployqt-qt5"
fi
if [ ! -x "$MACDEPLOYQT_PATH" ]; then
    echo "Error: macdeployqt not found next to $QMAKE_CMD" >&2
    exit 1
fi

echo "Qt version:   $QT_VERSION"
echo "Qt prefix:    $CMAKE_PREFIX_PATH_ARG"
echo "Qt libraries: $QT_LIB_DIR"
echo "macdeployqt:  $MACDEPLOYQT_PATH"
echo ""

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
echo "==> Configuring build in $BUILD_DIR ..."

# Clean stale cache if the source path changed
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_SRC="$(grep '^CMAKE_HOME_DIRECTORY' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || true)"
    CACHED_PREFIX="$(grep '^CMAKE_PREFIX_PATH:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || true)"
    CACHED_ARCHS="$(grep '^CMAKE_OSX_ARCHITECTURES:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || true)"
    if [ -n "$CACHED_SRC" ] && [ "$CACHED_SRC" != "$PROJECT_ROOT" ]; then
        echo "⚠  Stale CMake cache detected (was configured from $CACHED_SRC)"
        echo "   → Cleaning build directory and reconfiguring..."
        rm -rf "$BUILD_DIR"
    elif [ -n "$CACHED_PREFIX" ] && [ "$CACHED_PREFIX" != "$CMAKE_PREFIX_PATH_ARG" ]; then
        echo "⚠  Build directory was configured with a different Qt prefix ($CACHED_PREFIX)"
        echo "   → Cleaning build directory and reconfiguring..."
        rm -rf "$BUILD_DIR"
    elif [ -n "$CACHED_ARCHS" ] && [ "$CACHED_ARCHS" != "$BUILD_ARCH" ]; then
        echo "⚠  Build directory was configured for a different architecture ($CACHED_ARCHS)"
        echo "   → Cleaning build directory and reconfiguring..."
        rm -rf "$BUILD_DIR"
    fi
fi

CMAKE_EXTRA_ARGS=""
if [ "$OBFUSCATE" -eq 1 ]; then
    CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DXLSONE_OBFUSCATE=ON -DXLSONE_LICENSE_PUBLIC_KEY=$XLSONE_LICENSE_PUBLIC_KEY"
fi
if [ "$DOMESTIC" -eq 1 ]; then
    CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DXLSONE_ACTIVATION_BASE_URL=https://api.z-pulse.cn"
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$BUILD_ARCH" \
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/install" \
    -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH_ARG" \
    $CMAKE_EXTRA_ARGS

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo ""
echo "==> Building project ..."
cmake --build "$BUILD_DIR" --parallel

# ---------------------------------------------------------------------------
# Locate the app bundle
# ---------------------------------------------------------------------------
APP_BUNDLE="$BUILD_DIR/app/xlsOneQt.app"
if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: app bundle not found at $APP_BUNDLE" >&2
    exit 1
fi

BINARY_PATH="$APP_BUNDLE/Contents/MacOS/xlsOneQt"
if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: bundle binary not found at $BINARY_PATH" >&2
    echo "       The CMake target may not have produced a macOS bundle." >&2
    exit 1
fi

echo ""
echo "==> Bundle found: $APP_BUNDLE"

# ---------------------------------------------------------------------------
# Copy translation files into the bundle
# ---------------------------------------------------------------------------
echo "==> Copying translation files ..."
mkdir -p "$APP_BUNDLE/Contents/Resources/i18n"
cp -f "$PROJECT_ROOT/i18n"/*.qm "$APP_BUNDLE/Contents/Resources/i18n/" 2>/dev/null || true

# ---------------------------------------------------------------------------
# Update Info.plist with version and bundle metadata
# ---------------------------------------------------------------------------
echo "==> Updating bundle Info.plist ..."
PLIST_BUDDY="/usr/libexec/PlistBuddy"
INFO_PLIST="$APP_BUNDLE/Contents/Info.plist"

if [ -x "$PLIST_BUDDY" ] && [ -f "$INFO_PLIST" ]; then
    # Generate a stable bundle identifier from the project
    "$PLIST_BUDDY" -c "Set :CFBundleIdentifier com.xlsone.xlsOneQt" "$INFO_PLIST" 2>/dev/null || \
        "$PLIST_BUDDY" -c "Add :CFBundleIdentifier string com.xlsone.xlsOneQt" "$INFO_PLIST"
    "$PLIST_BUDDY" -c "Set :CFBundleName xlsOne" "$INFO_PLIST" 2>/dev/null || \
        "$PLIST_BUDDY" -c "Add :CFBundleName string xlsOne" "$INFO_PLIST"
    "$PLIST_BUDDY" -c "Set :CFBundleDisplayName xlsOne" "$INFO_PLIST" 2>/dev/null || \
        "$PLIST_BUDDY" -c "Add :CFBundleDisplayName string xlsOne" "$INFO_PLIST"
    "$PLIST_BUDDY" -c "Set :CFBundleShortVersionString $VERSION" "$INFO_PLIST" 2>/dev/null || \
        "$PLIST_BUDDY" -c "Add :CFBundleShortVersionString string $VERSION" "$INFO_PLIST"
    "$PLIST_BUDDY" -c "Set :CFBundleVersion $VERSION" "$INFO_PLIST" 2>/dev/null || \
        "$PLIST_BUDDY" -c "Add :CFBundleVersion string $VERSION" "$INFO_PLIST"
    "$PLIST_BUDDY" -c "Set :NSHumanReadableCopyright \"Copyright © xlsOne. All rights reserved.\"" "$INFO_PLIST" 2>/dev/null || \
        "$PLIST_BUDDY" -c "Add :NSHumanReadableCopyright string \"Copyright © xlsOne. All rights reserved.\"" "$INFO_PLIST"
fi

# ---------------------------------------------------------------------------
# Bundle Qt frameworks with macdeployqt
# ---------------------------------------------------------------------------
echo "==> Running macdeployqt ..."

DEPLOYQT_ARGS=("$APP_BUNDLE")
# Always produce a deployment summary for debugging
DEPLOYQT_ARGS+=(-verbose=1)

if [ "$SIGNED_BUILD" -eq 1 ]; then
    # "Developer ID Application: <Team ID>" is the standard distribution identity.
    SIGN_IDENTITY="Developer ID Application: $TEAM_ID"
    DEPLOYQT_ARGS+=(-codesign="$SIGN_IDENTITY")
fi

"$MACDEPLOYQT_PATH" "${DEPLOYQT_ARGS[@]}"

# macdeployqt leaves the bundle with a mix of linker-signed and ad-hoc signed
# binaries.  On Apple Silicon an inconsistent signature causes launch to fail
# with "Code Signature Invalid".  Re-sign the whole bundle consistently.
if [ "$SIGNED_BUILD" -eq 1 ]; then
    echo "==> Verifying distribution code signature ..."
    codesign --verify --deep --strict "$APP_BUNDLE" || {
        echo "Warning: code signature verification failed." >&2
    }
else
    echo "==> Applying ad-hoc code signature ..."
    codesign --force --deep --sign - "$APP_BUNDLE"
fi

if [ "$APP_ONLY" -eq 1 ]; then
    echo ""
    lipo -info "$BINARY_PATH"
    echo "App bundle prepared: $APP_BUNDLE"
    exit 0
fi

# ---------------------------------------------------------------------------
# Create DMG
# ---------------------------------------------------------------------------
echo ""
echo "==> Creating DMG ..."

rm -f "$OUTPUT_DMG"
mkdir -p "$(dirname "$OUTPUT_DMG")"

APP_NAME="xlsOneQt.app"
VOLNAME="xlsOne $VERSION"

CREATE_DMG_OK=0
if [ "${XLSONE_USE_CREATE_DMG:-1}" != "0" ] && command -v create-dmg >/dev/null 2>&1; then
    echo "Using create-dmg ..."
    DMG_SOURCE_DIR="$BUILD_DIR/dmg-root"
    prepare_dmg_source_dir "$APP_BUNDLE" "$DMG_SOURCE_DIR" 0
    if create-dmg \
        --volname "$VOLNAME" \
        --window-size 800 400 \
        --icon-size 100 \
        --app-drop-link 600 185 \
        --icon "$APP_NAME" 200 185 \
        "$OUTPUT_DMG" \
        "$DMG_SOURCE_DIR"; then
        CREATE_DMG_OK=1
    else
        echo "create-dmg failed; using hdiutil fallback ..."
    fi
fi

if [ "$CREATE_DMG_OK" -eq 0 ]; then
    if [ "${XLSONE_USE_CREATE_DMG:-1}" = "0" ]; then
        echo "create-dmg disabled; using hdiutil fallback ..."
    elif ! command -v create-dmg >/dev/null 2>&1; then
        echo "create-dmg not found; using hdiutil fallback ..."
    fi

    TEMP_DMG="$BUILD_DIR/temp-xlsOne.dmg"

    DMG_SOURCE_DIR="$BUILD_DIR/dmg-root"
    prepare_dmg_source_dir "$APP_BUNDLE" "$DMG_SOURCE_DIR" 1

    # Estimate size (add 20 MB padding)
    APP_SIZE="$(du -sm "$DMG_SOURCE_DIR" | cut -f1)"
    DMG_SIZE=$((APP_SIZE + 20))

    rm -f "$TEMP_DMG"
    hdiutil create -size "${DMG_SIZE}m" -volname "$VOLNAME" \
        -srcfolder "$DMG_SOURCE_DIR" -fs HFS+ -format UDRW "$TEMP_DMG"
    hdiutil convert "$TEMP_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUTPUT_DMG"
    rm -f "$TEMP_DMG"
fi

# ---------------------------------------------------------------------------
# Notarize DMG
# ---------------------------------------------------------------------------
if [ "$NOTARIZE" -eq 1 ]; then
    echo ""
    echo "==> Submitting DMG for notarization ..."
    xcrun notarytool submit "$OUTPUT_DMG" \
        --apple-id "$APPLE_ID" \
        --password "$APP_SPECIFIC_PASSWORD" \
        --team-id "$TEAM_ID" \
        --wait

    echo "==> Stapling notarization ticket ..."
    xcrun stapler staple "$OUTPUT_DMG"
fi

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
echo ""
echo "========================================="
echo "  DMG generated successfully"
echo "========================================="
echo "Output:      $OUTPUT_DMG"
echo "Version:     $VERSION"
echo "Build mode:  Release"
if [ "$OBFUSCATE" -eq 1 ]; then
    echo "Obfuscation: ON"
else
    echo "Obfuscation: OFF"
fi
if [ "$DOMESTIC" -eq 1 ]; then
    echo "Endpoints:   Domestic (api.z-pulse.cn)"
else
    echo "Endpoints:   International (api.xlsone.com)"
fi
if [ "$SIGNED_BUILD" -eq 1 ]; then
    echo "Signed:      YES (Team ID: $TEAM_ID)"
else
    echo "Signed:      NO"
fi
if [ "$NOTARIZE" -eq 1 ]; then
    echo "Notarized:   YES"
else
    echo "Notarized:   NO"
fi
echo ""
echo "Install by dragging xlsOneQt.app to /Applications."
