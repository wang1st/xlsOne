#!/usr/bin/env bash
set -euo pipefail

# One-click .deb builder for xlsOne (Linux Qt build).
#
# By default, bundles Qt libraries into a self-contained .deb (KylinOS mode).
# Use --system-qt to build a package that uses the system Qt5 installation
# instead (recommended when building on the same OS as the target, e.g. UOS).
#
# Usage:
#   ./cpp/scripts/build_deb.sh [--system-qt] [build-dir]

ORIG_DIR="$(pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"      # cpp/ directory

# ---- Parse arguments ----
BUNDLE_MODE="auto"     # auto | bundle | system
BUILD_DIR=""

while [ $# -gt 0 ]; do
    case "$1" in
        --system-qt|--no-bundle)
            BUNDLE_MODE="system"
            shift
            ;;
        --bundle|--self-contained)
            BUNDLE_MODE="bundle"
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--system-qt|--no-bundle] [--bundle] [build-dir]"
            echo ""
            echo "  --system-qt, --no-bundle   Use system Qt5 packages (for UOS/Debian native builds)"
            echo "  --bundle, --self-contained Bundle Qt5 libraries (for KylinOS self-contained builds)"
            echo ""
            echo "  If neither flag is given, auto-detects based on build host."
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *)
            BUILD_DIR="$1"
            shift
            ;;
    esac
done

# Resolve build directory: if relative, make it relative to original cwd
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="$PROJECT_ROOT/build-linux-release"
elif [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$ORIG_DIR/$BUILD_DIR"
fi

cd "$PROJECT_ROOT"

echo "========================================="
echo "  xlsOne DEB Packager"
echo "========================================="
echo ""

# ---------------------------------------------------------------------------
# Check build dependencies
# ---------------------------------------------------------------------------
echo "==> Checking build dependencies ..."
MISSING_PKGS=""
for cmd in cmake ninja cpack dpkg-deb g++ ldd; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "   ✗ $cmd not found"
        case "$cmd" in
            cmake)   MISSING_PKGS="$MISSING_PKGS cmake" ;;
            ninja)   MISSING_PKGS="$MISSING_PKGS ninja-build" ;;
            cpack)   MISSING_PKGS="$MISSING_PKGS cmake-cpack-helper" ;;
            dpkg-deb) MISSING_PKGS="$MISSING_PKGS dpkg-dev" ;;
            g++)     MISSING_PKGS="$MISSING_PKGS g++" ;;
        esac
    fi
done

# Check for Qt5 development headers
if ! dpkg -l qtbase5-dev >/dev/null 2>&1; then
    echo "   ✗ qtbase5-dev not installed"
    MISSING_PKGS="$MISSING_PKGS qtbase5-dev"
fi
if ! dpkg -l zlib1g-dev >/dev/null 2>&1; then
    echo "   ✗ zlib1g-dev not installed"
    MISSING_PKGS="$MISSING_PKGS zlib1g-dev"
fi

if [ -n "$MISSING_PKGS" ]; then
    echo ""
    echo "Install missing packages with:"
    echo "  sudo apt install $MISSING_PKGS"
    exit 1
fi
echo "   ✓ All build dependencies found"
echo ""

# ---------------------------------------------------------------------------
# Find a working qmake (handle UOS qtchooser quirk)
# ---------------------------------------------------------------------------
find_qmake() {
    # Try qmake with QT_SELECT=5 first (fixes UOS qtchooser)
    if QT_SELECT=5 qmake -query QT_INSTALL_LIBS >/dev/null 2>&1; then
        echo "QT_SELECT=5 qmake"
        return
    fi
    if QT_SELECT=qt5 qmake -query QT_INSTALL_LIBS >/dev/null 2>&1; then
        echo "QT_SELECT=qt5 qmake"
        return
    fi
    # Try bare qmake
    if qmake -query QT_INSTALL_LIBS >/dev/null 2>&1; then
        echo "qmake"
        return
    fi
    # Try qmake-qt5 (Debian alternative)
    if command -v qmake-qt5 >/dev/null 2>&1; then
        if qmake-qt5 -query QT_INSTALL_LIBS >/dev/null 2>&1; then
            echo "qmake-qt5"
            return
        fi
    fi
    echo ""
}

QMAKE_CMD="$(find_qmake)"
if [ -z "$QMAKE_CMD" ]; then
    echo "Error: could not find a working qmake for Qt5." >&2
    echo "Please install qt5-qmake and qtbase5-dev:" >&2
    echo "  sudo apt install qt5-qmake qtbase5-dev" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Detect Qt installation
# ---------------------------------------------------------------------------
QT_LIB_DIR="$(eval "$QMAKE_CMD" -query QT_INSTALL_LIBS)"
QT_PLUGIN_DIR="$(eval "$QMAKE_CMD" -query QT_INSTALL_PLUGINS)"
QT_VERSION="$(eval "$QMAKE_CMD" -query QT_VERSION)"

if [ ! -d "$QT_LIB_DIR" ] || [ ! -f "$QT_LIB_DIR/libQt5Core.so" ]; then
    echo "Error: Qt libraries not found under $QT_LIB_DIR" >&2
    exit 1
fi

echo "Build host Qt:    $QT_VERSION"
echo "Qt libraries:     $QT_LIB_DIR"
echo "Qt plugins:       $QT_PLUGIN_DIR"

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
BUILD_GLIBC_VER=$(ldd --version 2>&1 | sed -n '1s/.*\([0-9]\+\.[0-9]\+\).*/\1/p')
echo "Host glibc:       ${BUILD_GLIBC_VER:-unknown}"

# Detect OS
HOST_OS="unknown"
if [ -f /etc/os-release ]; then
    HOST_OS=$(grep '^ID=' /etc/os-release | cut -d= -f2 | tr -d '"')
elif [ -f /etc/deepin-version ]; then
    HOST_OS="deepin"
elif [ -f /etc/kylin-release ]; then
    HOST_OS="kylin"
fi
echo "Host OS:          $HOST_OS"
echo ""

# ---------------------------------------------------------------------------
# Decide bundle mode
# ---------------------------------------------------------------------------
if [ "$BUNDLE_MODE" = "auto" ]; then
    # Auto-detect: on UOS/Deepin/Debian, prefer system Qt to avoid
    # glibc compatibility issues with bundled libs.
    case "$HOST_OS" in
        uos|deepin|debian|ubuntu)
            # On these systems the target probably matches the build host.
            # Using system Qt5 is safer (no glibc mismatch).
            BUNDLE_MODE="system"
            echo "→ Auto-detected $HOST_OS build — using system Qt5 (no bundling)."
            echo "  Use --bundle to force self-contained mode."
            ;;
        *)
            # KylinOS or unknown — default to self-contained bundle
            BUNDLE_MODE="bundle"
            echo "→ Auto-detected $HOST_OS build — bundling Qt5 (self-contained)."
            echo "  Use --system-qt to force system Qt5 mode."
            ;;
    esac
    echo ""
fi

# ---------------------------------------------------------------------------
# Auto-download low-glibc Qt5 libraries for self-contained bundle
# ---------------------------------------------------------------------------
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
    x86_64)
        DEEPIN_QT_ARCH="amd64"
        DEEPIN_QT_FILENAME="qt5.15-gles_231205_amd64.tar.xz"
        ;;
    aarch64|arm64)
        DEEPIN_QT_ARCH="aarch64"
        DEEPIN_QT_FILENAME="qt5.15-gles_231207_aarch64.tar.xz"
        ;;
    *)
        echo "Error: unsupported host architecture for Qt5 bundle: $HOST_ARCH" >&2
        echo "  Supported: x86_64, aarch64" >&2
        exit 1
        ;;
esac

DEEPIN_QT_DIR="$PROJECT_ROOT/deepin-qt5.15/usr/local/qt5.15-gles"
DEEPIN_QT_DIR_ALT="$PROJECT_ROOT/deepin-qt5.15/qt5.15-gles"
DEEPIN_QT_URL="https://github.com/deepin-community/sig-deepin-shared-libs/releases/download/Qt5.15.10-OpenGLES%3D%3D5.15.10%2Bszbt2/$DEEPIN_QT_FILENAME"

if [ "$BUNDLE_MODE" = "bundle" ]; then
    if [ -f "$DEEPIN_QT_DIR_ALT/lib/libQt5Core.so" ] && [ -d "$DEEPIN_QT_DIR_ALT/plugins/platforms" ]; then
        DEEPIN_QT_DIR="$DEEPIN_QT_DIR_ALT"
    fi
    if [ ! -f "$DEEPIN_QT_DIR/lib/libQt5Core.so" ] || [ ! -d "$DEEPIN_QT_DIR/plugins/platforms" ]; then
        echo "==> Downloading low-glibc Qt5.15 bundle for $DEEPIN_QT_ARCH ..."
        echo "    Source: $DEEPIN_QT_URL"
        echo "    Target: $DEEPIN_QT_DIR"
        mkdir -p "$PROJECT_ROOT/deepin-qt5.15"
        _qt_tar="$PROJECT_ROOT/deepin-qt5.15/$DEEPIN_QT_FILENAME"

        if ! command -v wget >/dev/null 2>&1 && ! command -v curl >/dev/null 2>&1; then
            echo "Error: neither wget nor curl is available for downloading Qt5 bundle." >&2
            exit 1
        fi

        if command -v wget >/dev/null 2>&1; then
            wget -q --show-progress -O "$_qt_tar" "$DEEPIN_QT_URL" || {
                echo "Error: failed to download Qt5 bundle from $DEEPIN_QT_URL" >&2
                exit 1
            }
        else
            curl -L --progress-bar -o "$_qt_tar" "$DEEPIN_QT_URL" || {
                echo "Error: failed to download Qt5 bundle from $DEEPIN_QT_URL" >&2
                exit 1
            }
        fi

        echo "==> Extracting Qt5 bundle ..."
        tar -xf "$_qt_tar" -C "$PROJECT_ROOT/deepin-qt5.15" || {
            echo "Error: failed to extract Qt5 bundle" >&2
            exit 1
        }

        if [ -f "$DEEPIN_QT_DIR_ALT/lib/libQt5Core.so" ] && [ -d "$DEEPIN_QT_DIR_ALT/plugins/platforms" ]; then
            DEEPIN_QT_DIR="$DEEPIN_QT_DIR_ALT"
        fi

        if [ ! -f "$DEEPIN_QT_DIR/lib/libQt5Core.so" ]; then
            echo "Error: Qt5 bundle extracted but libQt5Core.so not found in $DEEPIN_QT_DIR/lib" >&2
            exit 1
        fi

        echo "   ✓ Qt5 bundle ready"
        echo ""
    else
        echo "==> Using existing low-glibc Qt5 bundle: $DEEPIN_QT_DIR"
        echo ""
    fi

    # Override Qt paths so CMake bundles the downloaded libraries instead of system Qt
    QT_LIB_DIR="$DEEPIN_QT_DIR/lib"
    QT_PLUGIN_DIR="$DEEPIN_QT_DIR/plugins"
fi

# ---------------------------------------------------------------------------
# GLIBC compatibility check (only relevant when bundling)
# ---------------------------------------------------------------------------
if [ "$BUNDLE_MODE" = "bundle" ] && [ -n "$BUILD_GLIBC_VER" ]; then
    # UOS 20 / Debian 10 = glibc 2.28
    MIN_TARGET_GLIBC="2.28"
    build_major=$(echo "$BUILD_GLIBC_VER" | cut -d. -f1)
    build_minor=$(echo "$BUILD_GLIBC_VER" | cut -d. -f2)
    min_major=$(echo "$MIN_TARGET_GLIBC" | cut -d. -f1)
    min_minor=$(echo "$MIN_TARGET_GLIBC" | cut -d. -f2)

    if [ "$build_major" -gt "$min_major" ] || \
       ([ "$build_major" -eq "$min_major" ] && [ "$build_minor" -gt "$min_minor" ]); then
        echo "⚠  WARNING: Build glibc ($BUILD_GLIBC_VER) > common target minimum ($MIN_TARGET_GLIBC)"
        echo "   Bundled libraries may not work on older targets (UOS 20, Debian 10)."
        echo "   → Using deepin-shared-libs Qt5.15 (built for UOS V20) should improve compatibility."
        echo "   → Or build with --system-qt to avoid bundling entirely."
        echo ""
    fi
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
echo "==> Configuring build in $BUILD_DIR (mode: $BUNDLE_MODE) ..."

# If a stale CMakeCache.txt exists from a different source path (e.g. the repo
# was moved or mounted at a different location), clean it to avoid cmake errors.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_SRC=$(grep '^CMAKE_HOME_DIRECTORY' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)
    if [ -n "$CACHED_SRC" ] && [ "$CACHED_SRC" != "$PROJECT_ROOT" ]; then
        echo "⚠  Stale CMake cache detected (was configured from $CACHED_SRC)"
        echo "   → Cleaning build directory and reconfiguring..."
        rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
    fi
fi

CMAKE_EXTRA_ARGS=""
if [ "$BUNDLE_MODE" = "system" ]; then
    CMAKE_EXTRA_ARGS="-DXLSONE_BUNDLE_QT=OFF"
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DXLSONE_QT_BUNDLE_DIR="$QT_LIB_DIR" \
    -DXLSONE_QT_PLUGIN_DIR="$QT_PLUGIN_DIR" \
    $CMAKE_EXTRA_ARGS

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo "==> Building project ..."
cmake --build "$BUILD_DIR" --parallel

# ---------------------------------------------------------------------------
# Post-build: check for missing transitive dependencies (bundle mode only)
# ---------------------------------------------------------------------------
if [ "$BUNDLE_MODE" = "bundle" ]; then
    echo ""
    echo "==> Checking transitive library dependencies ..."

    BINARY="$(find "$BUILD_DIR" -name xlsone_app -type f -executable 2>/dev/null | head -1)"
    if [ -z "$BINARY" ]; then
        BINARY="$(find "$BUILD_DIR" -name xlsOneQt -type f -executable 2>/dev/null | head -1)"
    fi

    if [ -n "$BINARY" ]; then
        MISSING_LIBS=$(ldd "$BINARY" 2>&1 | grep "not found" || true)

        if [ -n "$MISSING_LIBS" ]; then
            echo "⚠  WARNING: Missing shared libraries detected:"
            echo "$MISSING_LIBS" | while read -r line; do
                echo "   $line"
            done
            echo "   → Add them to CMakeLists.txt in the XLSONE_BUNDLE_QT section."
            echo ""
        else
            echo "   ✓ No missing shared libraries detected."
        fi

        GLIBC_ISSUES=$(ldd "$BINARY" 2>&1 | grep "version \`GLIBC" || true)
        if [ -n "$GLIBC_ISSUES" ]; then
            echo ""
            echo "⚠  Bundled libraries require newer glibc symbols:"
            echo "$GLIBC_ISSUES" | while read -r line; do
                echo "   $line"
            done
            echo "   → These symbols may not exist on older target systems."
            echo "   → The postinst script will attempt to fall back to system Qt5."
            echo ""
        fi
    else
        echo "⚠  Could not locate built binary for dependency check."
    fi
    echo ""
fi

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
echo "==> Generating .deb package ..."
cd "$BUILD_DIR"
cpack -G DEB

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
DEB_FILE="$(ls -t "$BUILD_DIR"/*.deb 2>/dev/null | head -1)"
echo ""
echo "========================================="
echo "  DEB generated successfully"
echo "========================================="
if [ -n "$DEB_FILE" ]; then
    echo "Package:     $DEB_FILE"
    echo ""
    echo "Package info:"
    dpkg-deb -f "$DEB_FILE" Package Version Architecture Depends Maintainer Description Section Priority 2>/dev/null
    echo ""
    echo "Install with:"
    echo "  sudo dpkg -i $DEB_FILE"
    echo ""
    echo "Validate with:"
    echo "  ./cpp/scripts/validate_deb.sh $DEB_FILE"
    echo ""
    echo "Build mode:  $BUNDLE_MODE"
    if [ "$BUNDLE_MODE" = "system" ]; then
        echo "  (Uses system Qt5 — target must have Qt5 packages installed)"
    else
        echo "  (Self-contained — bundles Qt5 libraries)"
    fi
else
    echo "Error: No .deb file found in $BUILD_DIR" >&2
    exit 1
fi
