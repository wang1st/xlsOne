#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_PATH="$REPO_ROOT/xlsOne.xcodeproj"
SCHEME="xlsOneMacApp"
DERIVED_DATA="$REPO_ROOT/.build/xcode-derived-data"
PACKAGE_CACHE="$REPO_ROOT/.build/xcode-source-packages"
XCODE_HOME="$REPO_ROOT/.build/xcode-home"
SWIFT_CACHE="$REPO_ROOT/.build/module-cache/swift"
CLANG_CACHE="$REPO_ROOT/.build/module-cache/clang"

mkdir -p \
    "$DERIVED_DATA" \
    "$PACKAGE_CACHE" \
    "$XCODE_HOME/Library/Caches" \
    "$XCODE_HOME/Library/Developer" \
    "$SWIFT_CACHE" \
    "$CLANG_CACHE"

export HOME="$XCODE_HOME"
export XDG_CACHE_HOME="$XCODE_HOME/.cache"
export SWIFT_MODULECACHE_PATH="$SWIFT_CACHE"
export CLANG_MODULE_CACHE_PATH="$CLANG_CACHE"

"$SCRIPT_DIR/generate_xcode_project.sh"

for configuration in Debug Release; do
    echo "Building $SCHEME ($configuration)..."
    xcodebuild \
        -project "$PROJECT_PATH" \
        -scheme "$SCHEME" \
        -configuration "$configuration" \
        -destination "platform=macOS" \
        -derivedDataPath "$DERIVED_DATA/$configuration" \
        -clonedSourcePackagesDirPath "$PACKAGE_CACHE" \
        CODE_SIGNING_ALLOWED=NO \
        build
done
