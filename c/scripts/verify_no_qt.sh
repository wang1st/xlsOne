#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
binary_path=${1:-"$repository_root/c/build/app/xlsOne.app/Contents/MacOS/xlsOne"}

if command -v rg >/dev/null 2>&1; then
    if rg -n \
        '#[[:space:]]*include[[:space:]]*[<"][Qq][A-Za-z]|find_package\([[:space:]]*Qt|Qt[0-9]*::|QT_[A-Z_]+|AUTOMOC|AUTOUIC|AUTORCC' \
        "$repository_root/c" \
        --glob '!build/**' \
        --glob '!**/verify_no_qt.sh' \
        --glob '!README.md' \
        --glob '!THIRD_PARTY_NOTICES.md'; then
        echo "发现 Qt 源码或构建依赖。" >&2
        exit 1
    fi
fi

if [ -f "$binary_path" ]; then
    linked_libraries=
    if command -v otool >/dev/null 2>&1; then
        linked_libraries=$(otool -L "$binary_path")
    elif command -v ldd >/dev/null 2>&1; then
        linked_libraries=$(ldd "$binary_path")
    fi
    if [ -n "$linked_libraries" ]; then
        if printf '%s\n' "$linked_libraries" \
            | grep -Eiq '(^|[/[:space:]])Qt[0-9A-Za-z]'; then
            echo "生成的应用仍链接 Qt。" >&2
            exit 1
        fi
    fi
fi

echo "通过：纯 C 源码与目标中未发现 Qt 依赖。"
