#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Build the macOS app and package it into a .dmg.

Usage:
  scripts/package_macos_swift_dmg.sh [options]

Options:
      --signed                  Enable code signing. Requires --team-id or
                                XLSONE_DEVELOPMENT_TEAM environment variable.
      --team-id <id>            Apple Developer Team ID.
      --version <version>       Version string used in the DMG file name.
                                Default: value from site/api/version.json or "0.0.0".
      --output <path>           Output DMG path. Default: ./.build/xlsOne-<version>-macos-arm64.dmg
      --notarize                Submit the DMG for Apple notarization (signed builds only).
                                Requires APPLE_ID and APP_SPECIFIC_PASSWORD env vars.
      --skip-archive            Skip archiving and use an existing .xcarchive at
                                .build/xcode-archives/xlsOne.xcarchive.
  -h, --help                    Show this help message.

Environment:
  XLSONE_DEVELOPMENT_TEAM       Default Team ID for signed builds.
  APPLE_ID                      Apple ID for notarization.
  APP_SPECIFIC_PASSWORD         App-specific password for notarization.

Examples:
  # Unsigned DMG for local testing
  scripts/package_macos_swift_dmg.sh

  # Signed DMG
  scripts/package_macos_swift_dmg.sh --signed --team-id ABCDE12345 --version 0.3.0

  # Signed + notarized DMG
  scripts/package_macos_swift_dmg.sh --signed --team-id ABCDE12345 --version 0.3.0 --notarize
EOF
}

SIGNED_BUILD=0
TEAM_ID="${XLSONE_DEVELOPMENT_TEAM:-}"
VERSION=""
OUTPUT_DMG=""
NOTARIZE=0
SKIP_ARCHIVE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --signed)
            SIGNED_BUILD=1
            shift
            ;;
        --team-id)
            TEAM_ID="${2:-}"
            shift 2
            ;;
        --version)
            VERSION="${2:-}"
            shift 2
            ;;
        --output)
            OUTPUT_DMG="${2:-}"
            shift 2
            ;;
        --notarize)
            NOTARIZE=1
            shift
            ;;
        --skip-archive)
            SKIP_ARCHIVE=1
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/.build"
ARCHIVE_PATH="$BUILD_DIR/xcode-archives/xlsOne.xcarchive"
APP_NAME="xlsOne.app"
EXPORT_DIR="$BUILD_DIR/exported-app"

# Resolve version
if [[ -z "$VERSION" ]]; then
    if [[ -f "$REPO_ROOT/site/api/version.json" ]]; then
        VERSION="$(python3 -c "import json,sys; print(json.load(open('$REPO_ROOT/site/api/version.json'))['latest_version'])" 2>/dev/null || echo "")"
    fi
    if [[ -z "$VERSION" ]]; then
        VERSION="0.0.0"
    fi
fi

if [[ -z "$OUTPUT_DMG" ]]; then
    OUTPUT_DMG="$BUILD_DIR/xlsOne-${VERSION}-macos-arm64.dmg"
fi

if [[ "$SIGNED_BUILD" -eq 1 && -z "$TEAM_ID" ]]; then
    echo "Signed build requires --team-id or XLSONE_DEVELOPMENT_TEAM." >&2
    exit 2
fi

if [[ "$NOTARIZE" -eq 1 && "$SIGNED_BUILD" -eq 0 ]]; then
    echo "--notarize requires --signed." >&2
    exit 2
fi

if [[ "$NOTARIZE" -eq 1 && ( -z "${APPLE_ID:-}" || -z "${APP_SPECIFIC_PASSWORD:-}" ) ]]; then
    echo "Notarization requires APPLE_ID and APP_SPECIFIC_PASSWORD environment variables." >&2
    exit 2
fi

mkdir -p "$(dirname "$OUTPUT_DMG")" "$EXPORT_DIR"

# 1. Archive
if [[ "$SKIP_ARCHIVE" -eq 0 ]]; then
    archive_args=()
    if [[ "$SIGNED_BUILD" -eq 1 ]]; then
        archive_args+=(--signed)
        if [[ -n "$TEAM_ID" ]]; then
            archive_args+=(--team-id "$TEAM_ID")
        fi
    fi
    "$SCRIPT_DIR/archive_xcode_app.sh" ${archive_args[@]+"${archive_args[@]}"}
else
    if [[ ! -d "$ARCHIVE_PATH" ]]; then
        echo "Archive not found: $ARCHIVE_PATH" >&2
        exit 1
    fi
    echo "Using existing archive: $ARCHIVE_PATH"
fi

# 2. Extract / export the .app
APP_SOURCE="$ARCHIVE_PATH/Products/Applications/$APP_NAME"
if [[ ! -d "$APP_SOURCE" ]]; then
    echo "App not found in archive: $APP_SOURCE" >&2
    exit 1
fi

rm -rf "$EXPORT_DIR/$APP_NAME"
cp -R "$APP_SOURCE" "$EXPORT_DIR/$APP_NAME"
APP_PATH="$EXPORT_DIR/$APP_NAME"

# 3. Sign the .app explicitly if requested (Xcode archive may already sign it)
if [[ "$SIGNED_BUILD" -eq 1 ]]; then
    echo "Codesigning $APP_PATH..."
    codesign --force --deep --sign "Developer ID Application: $TEAM_ID" \
        --options runtime \
        "$APP_PATH"
fi

# 4. Create DMG
rm -f "$OUTPUT_DMG"

if command -v create-dmg >/dev/null 2>&1; then
    echo "Creating DMG with create-dmg..."
    create-dmg \
        --volname "xlsOne $VERSION" \
        --window-size 800 400 \
        --icon-size 100 \
        --app-drop-link 600 185 \
        --icon "$APP_NAME" 200 185 \
        "$OUTPUT_DMG" \
        "$APP_PATH"
else
    echo "create-dmg not found; falling back to hdiutil..."

    TEMP_DMG="$BUILD_DIR/temp-xlsOne.dmg"
    MOUNT_POINT="/Volumes/xlsOne-$VERSION"

    # Estimate size
    APP_SIZE="$(du -sm "$APP_PATH" | cut -f1)"
    DMG_SIZE=$((APP_SIZE + 20))

    rm -f "$TEMP_DMG"
    hdiutil create -size "${DMG_SIZE}m" -volname "xlsOne $VERSION" \
        -srcfolder "$APP_PATH" -fs HFS+ -format UDRW "$TEMP_DMG"

    DEVICE="$(hdiutil attach -readwrite -noverify -noautoopen "$TEMP_DMG" | grep '^/dev/' | sed 's/^\(\/dev\/[^ ]*\).*/\1/')"

    # Create Applications symlink
    ln -sf /Applications "$MOUNT_POINT/Applications"

    hdiutil detach "$DEVICE"
    hdiutil convert "$TEMP_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUTPUT_DMG"
    rm -f "$TEMP_DMG"
fi

echo "DMG created: $OUTPUT_DMG"

# 5. Notarize DMG
if [[ "$NOTARIZE" -eq 1 ]]; then
    echo "Submitting DMG for notarization..."
    xcrun notarytool submit "$OUTPUT_DMG" \
        --apple-id "$APPLE_ID" \
        --password "$APP_SPECIFIC_PASSWORD" \
        --team-id "$TEAM_ID" \
        --wait

    echo "Stapling notarization ticket..."
    xcrun stapler staple "$OUTPUT_DMG"
fi

echo "Done: $OUTPUT_DMG"

# Collect into the repo-level .build/ dir so the deploy script can find it.
ARTIFACT_DIR="$REPO_ROOT/.build"
mkdir -p "$ARTIFACT_DIR"
cp -f "$OUTPUT_DMG" "$ARTIFACT_DIR/"
echo "Collected: $ARTIFACT_DIR/$(basename "$OUTPUT_DMG")"
