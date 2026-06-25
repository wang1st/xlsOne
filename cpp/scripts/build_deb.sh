#!/usr/bin/env bash
set -euo pipefail

# One-click self-contained .deb builder for xlsOne (Linux Qt build).
# All Qt libraries, plugins and ICU dependencies are bundled into the package,
# so the end user does not need to install Qt packages separately.
#
# Usage: ./cpp/scripts/build_deb.sh [build-dir]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_ROOT/build-linux-release}"

cd "$PROJECT_ROOT"

echo "========================================="
echo "  xlsOne Self-Contained DEB Packager"
echo "========================================="
echo ""

# Check dependencies
for cmd in cmake ninja cpack dpkg-deb qmake ldd; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Error: required command '$cmd' not found." >&2
        echo "Please install: cmake ninja-build cmake-cpack-helper dpkg-dev qt5-qmake-bin" >&2
        exit 1
    fi
done

# Detect system Qt installation
QT_LIB_DIR="$(qmake -query QT_INSTALL_LIBS)"
QT_PLUGIN_DIR="$(qmake -query QT_INSTALL_PLUGINS)"

if [ ! -d "$QT_LIB_DIR" ] || [ ! -f "$QT_LIB_DIR/libQt5Core.so" ]; then
    echo "Error: Qt libraries not found under $QT_LIB_DIR" >&2
    exit 1
fi

echo "Detected Qt libraries: $QT_LIB_DIR"
echo "Detected Qt plugins:   $QT_PLUGIN_DIR"
echo ""

# Configure if needed (force re-configure so bundle settings are applied)
echo "==> Configuring build in $BUILD_DIR ..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DXLSONE_QT_BUNDLE_DIR="$QT_LIB_DIR" \
    -DXLSONE_QT_PLUGIN_DIR="$QT_PLUGIN_DIR"

# Build
echo "==> Building project ..."
cmake --build "$BUILD_DIR" --parallel

# Package
echo "==> Generating self-contained .deb package ..."
cd "$BUILD_DIR"
cpack -G DEB

# Report
DEB_FILE="$(ls -t "$BUILD_DIR"/*.deb | head -1)"
echo ""
echo "========================================="
echo "  DEB generated successfully"
echo "========================================="
echo "$DEB_FILE"
echo ""
echo "Package info:"
dpkg-deb -f "$DEB_FILE" Package Version Architecture Depends Maintainer Description Section Priority
echo ""
echo "Install with:"
echo "  sudo dpkg -i $DEB_FILE"
echo ""
echo "Validate with:"
echo "  ./cpp/scripts/validate_deb.sh $DEB_FILE"
