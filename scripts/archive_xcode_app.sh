#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Archive the macOS app target.

Usage:
  scripts/archive_xcode_app.sh [--signed] [--team-id <id>]

Options:
      --signed          Archive with automatic signing enabled instead of forcing
                        an unsigned archive.
      --team-id <id>    Override DEVELOPMENT_TEAM for signed archives. You can also
                        provide this with the XLSONE_DEVELOPMENT_TEAM environment variable.
  -h, --help            Show this help message.
EOF
}

SIGNED_BUILD=0
TEAM_ID="${XLSONE_DEVELOPMENT_TEAM:-}"

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
PROJECT_PATH="$REPO_ROOT/xlsOne.xcodeproj"
SCHEME="xlsOneMacApp"
ARCHIVE_PATH="$REPO_ROOT/.build/xcode-archives/xlsOne.xcarchive"
DERIVED_DATA="$REPO_ROOT/.build/xcode-derived-data/Archive"
PACKAGE_CACHE="$REPO_ROOT/.build/xcode-source-packages"
XCODE_HOME="$REPO_ROOT/.build/xcode-home"
SWIFT_CACHE="$REPO_ROOT/.build/module-cache/swift"
CLANG_CACHE="$REPO_ROOT/.build/module-cache/clang"

mkdir -p \
    "$(dirname "$ARCHIVE_PATH")" \
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

if [[ "$SIGNED_BUILD" -eq 1 && -z "$TEAM_ID" ]]; then
    echo "Signed archive requires --team-id or XLSONE_DEVELOPMENT_TEAM." >&2
    exit 2
fi

rm -rf "$ARCHIVE_PATH"

xcodebuild_args=(
    xcodebuild
    -project "$PROJECT_PATH"
    -scheme "$SCHEME"
    -configuration Release
    -destination "generic/platform=macOS"
    -archivePath "$ARCHIVE_PATH"
    -derivedDataPath "$DERIVED_DATA"
    -clonedSourcePackagesDirPath "$PACKAGE_CACHE"
    archive
)

if [[ "$SIGNED_BUILD" -eq 1 ]]; then
    echo "Archiving $SCHEME with automatic signing..."
    xcodebuild_args+=(-allowProvisioningUpdates CODE_SIGN_STYLE=Automatic)
    xcodebuild_args+=("XLSONE_DEVELOPMENT_TEAM=$TEAM_ID")
else
    echo "Archiving $SCHEME without code signing..."
    xcodebuild_args+=(CODE_SIGNING_ALLOWED=NO)
fi

"${xcodebuild_args[@]}"
