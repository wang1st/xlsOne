#!/usr/bin/env bash
set -euo pipefail

QS_DIR="${HOME}/.config/xlsOne"
QS_FILE="${QS_DIR}/xlsOne.conf"

echo "xlsOne 本地授权清除工具"
echo "========================"
echo ""

if [ ! -f "$QS_FILE" ] && [ ! -d "$QS_DIR" ]; then
    echo "未找到本地授权文件: $QS_FILE"
    echo "无需清除。"
    exit 0
fi

echo "当前授权状态:"
if [ -f "$QS_FILE" ]; then
    grep -i "license/" "$QS_FILE" 2>/dev/null || echo "  (无授权数据)"
else
    echo "  (无配置文件)"
fi

echo ""
read -rp "确认清除所有本地授权状态? [y/N] " confirm
if [ "$confirm" != "y" ] && [ "$confirm" != "Y" ]; then
    echo "已取消。"
    exit 0
fi

if [ -f "$QS_FILE" ]; then
    rm -f "$QS_FILE"
    echo "已删除: $QS_FILE"
fi
if [ -d "$QS_DIR" ]; then
    rm -rf "$QS_DIR"
    echo "已删除: $QS_DIR"
fi

echo ""
echo "授权状态已清除。下次启动将提示激活。"
