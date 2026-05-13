#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Build the SwiftPM macOS executable, wrap it in a minimal .app bundle, and open it.

Usage:
  scripts/build_and_open_app.sh [options]

Options:
  -c, --configuration <debug|release>  Build configuration. Default: debug
      --product <name>                 SwiftPM executable product name. Default: xlsOne
      --app-name <name>                Generated app bundle name. Default: xlsOneWorkspace
      --bundle-id <id>                 Generated bundle identifier.
                                       Default: com.openai.codex.xlsone-workspace
      --no-open                        Build the .app bundle but do not launch it
  -h, --help                           Show this help message

Examples:
  scripts/build_and_open_app.sh
  scripts/build_and_open_app.sh --configuration release
  scripts/build_and_open_app.sh --no-open
EOF
}

CONFIGURATION="debug"
PRODUCT_NAME="xlsOne"
APP_NAME="xlsOneWorkspace"
BUNDLE_ID="com.openai.codex.xlsone-workspace"
OPEN_AFTER_BUILD=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--configuration)
            CONFIGURATION="${2:-}"
            shift 2
            ;;
        --product)
            PRODUCT_NAME="${2:-}"
            shift 2
            ;;
        --app-name)
            APP_NAME="${2:-}"
            shift 2
            ;;
        --bundle-id)
            BUNDLE_ID="${2:-}"
            shift 2
            ;;
        --no-open)
            OPEN_AFTER_BUILD=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ "$CONFIGURATION" != "debug" && "$CONFIGURATION" != "release" ]]; then
    echo "Invalid configuration: $CONFIGURATION" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$REPO_ROOT/.build/$CONFIGURATION"
APP_BUNDLE_DIR="$REPO_ROOT/.build/app-bundle"
APP_BUNDLE_PATH="$APP_BUNDLE_DIR/$APP_NAME.app"
APP_CONTENTS_PATH="$APP_BUNDLE_PATH/Contents"
APP_MACOS_PATH="$APP_CONTENTS_PATH/MacOS"
APP_EXEC_PATH="$APP_MACOS_PATH/$PRODUCT_NAME"
PLIST_PATH="$APP_CONTENTS_PATH/Info.plist"
SWIFT_MODULE_CACHE_DIR="$REPO_ROOT/.build/module-cache/swift"
CLANG_MODULE_CACHE_DIR="$REPO_ROOT/.build/module-cache/clang"

mkdir -p "$SWIFT_MODULE_CACHE_DIR" "$CLANG_MODULE_CACHE_DIR"

echo "Building product '$PRODUCT_NAME' with configuration '$CONFIGURATION'..."
(
    cd "$REPO_ROOT"
    export SWIFT_MODULECACHE_PATH="$SWIFT_MODULE_CACHE_DIR"
    export CLANG_MODULE_CACHE_PATH="$CLANG_MODULE_CACHE_DIR"
    swift build -c "$CONFIGURATION" --product "$PRODUCT_NAME"
)

if [[ ! -x "$BUILD_DIR/$PRODUCT_NAME" ]]; then
    echo "Build output not found: $BUILD_DIR/$PRODUCT_NAME" >&2
    exit 1
fi

mkdir -p "$APP_MACOS_PATH"

cat > "$PLIST_PATH" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>zh-Hans</string>
    <key>CFBundleLocalizations</key>
    <array>
        <string>zh-Hans</string>
        <string>en</string>
    </array>
    <key>CFBundleExecutable</key>
    <string>$PRODUCT_NAME</string>
    <key>CFBundleIconFile</key>
    <string>xlsOne.icns</string>
    <key>CFBundleIconName</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>$BUNDLE_ID</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>$PRODUCT_NAME</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
EOF

mkdir -p "$APP_CONTENTS_PATH/Resources"
if [[ -f "$REPO_ROOT/cpp/app/resources/xlsOne.icns" ]]; then
    cp "$REPO_ROOT/cpp/app/resources/xlsOne.icns" "$APP_CONTENTS_PATH/Resources/"
elif [[ -f "$REPO_ROOT/App/xlsOneMacApp/Assets.xcassets/AppIcon.appiconset/icon_256x256.png" ]]; then
    # Fallback: copy largest PNG as temp icon
    cp "$REPO_ROOT/App/xlsOneMacApp/Assets.xcassets/AppIcon.appiconset/icon_256x256.png" "$APP_CONTENTS_PATH/Resources/xlsOne.icns"
fi

cp "$BUILD_DIR/$PRODUCT_NAME" "$APP_EXEC_PATH"
chmod +x "$APP_EXEC_PATH"

# Copy any SPM resource bundles that were built
for bundle in "$BUILD_DIR"/*.bundle; do
    if [[ -d "$bundle" ]]; then
        cp -R "$bundle" "$APP_CONTENTS_PATH/Resources/"
    fi
done

plutil -lint "$PLIST_PATH" >/dev/null

echo "App bundle created:"
echo "  $APP_BUNDLE_PATH"

if [[ "$OPEN_AFTER_BUILD" -eq 0 ]]; then
    exit 0
fi

pkill -f "$APP_EXEC_PATH" >/dev/null 2>&1 || true

echo "Opening app..."
open "$APP_BUNDLE_PATH"
