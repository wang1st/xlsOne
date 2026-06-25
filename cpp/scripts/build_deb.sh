#!/usr/bin/env bash
set -euo pipefail

# One-click .deb builder for xlsOne (Linux Qt build).
# Usage: ./cpp/scripts/build_deb.sh [build-dir]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_ROOT/build-linux-release}"

cd "$PROJECT_ROOT"

echo "========================================="
echo "  xlsOne DEB Packager"
echo "========================================="
echo ""

# Check dependencies
for cmd in cmake ninja cpack dpkg-deb; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Error: required command '$cmd' not found." >&2
        echo "Please install: cmake ninja-build cpack (cmake-cpack-helper) dpkg" >&2
        exit 1
    fi
done

# Configure if needed
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "==> Configuring build in $BUILD_DIR ..."
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
else
    echo "==> Build directory already configured: $BUILD_DIR"
fi

# Build
echo "==> Building project ..."
cmake --build "$BUILD_DIR" --parallel

# Package
echo "==> Generating .deb package ..."
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
echo "Install with:"
echo "  sudo dpkg -i $DEB_FILE"
echo ""
echo "Validate with:"
echo "  ./cpp/scripts/validate_deb.sh $DEB_FILE"
