#!/usr/bin/env bash
set -euo pipefail

# macOS QSettings stores preferences in one of these locations:
#   1. ~/Library/Preferences/com.xlsOne.xlsOne.plist  (CFPreferences-based)
#   2. ~/.config/xlsOne/xlsOne.conf                     (INI-based, if forced)
#
# License-related keys stored:
#   license/token, license/offline, license/lastSeenUtc

PLIST_FILE="${HOME}/Library/Preferences/com.xlsOne.xlsOne.plist"
INI_FILE="${HOME}/.config/xlsOne/xlsOne.conf"

echo "xlsOne macOS 本地授权清除工具"
echo "============================="
echo ""

FOUND=0
if [ -f "$PLIST_FILE" ]; then
    echo "找到: $PLIST_FILE"
    FOUND=1
fi
if [ -f "$INI_FILE" ]; then
    echo "找到: $INI_FILE"
    FOUND=1
fi

if [ "$FOUND" -eq 0 ]; then
    echo "未找到本地授权文件，无需清除。"
    exit 0
fi

echo ""
read -rp "确认清除所有本地授权状态? [y/N] " confirm
if [ "$confirm" != "y" ] && [ "$confirm" != "Y" ]; then
    echo "已取消。"
    exit 0
fi

if [ -f "$PLIST_FILE" ]; then
    rm -f "$PLIST_FILE"
    echo "已删除: $PLIST_FILE"
fi
if [ -f "$INI_FILE" ]; then
    rm -f "$INI_FILE"
    echo "已删除: $INI_FILE"
fi

echo ""
echo "授权状态已清除。下次启动将提示激活。"
