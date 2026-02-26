# xlsOne CLI 使用指南

## 安装

```bash
# 1. 安装依赖
pip3 install openpyxl --break-system-packages --user

# 2. 安装核心库
cd Sources/xlsOneCore
pip3 install -e . --break-system-packages --user

# 3. 使用 CLI
cd ../..
./xlsone-cli "仙居县/*.xlsx"
```

## 使用方法

```bash
# 合并所有 xlsx 文件
./xlsone-cli *.xlsx

# 合并指定文件
./xlsone-cli file1.xlsx file2.xlsx

# 只显示统计
./xlsone-cli *.xlsx --stats-only

# 导出为 Excel (默认)
./xlsone-cli *.xlsx -o result.xlsx

# 导出为 JSON
./xlsone-cli *.xlsx -f json -o result.json

# 解析所有 sheet 并按名汇总
./xlsone-cli *.xlsx --all-sheets -o all_sheets.xlsx

# 自定义汇总策略
./xlsone-cli *.xlsx -n sum -t majority -c placeholder
```

## 命令参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `files` | Excel 文件路径（支持通配符） | 必填 |
| `-o, --output` | 输出文件路径 | merged_result.xlsx |
| `-f, --format` | 输出格式 (json/xlsx) | xlsx |
| `--all-sheets` | 解析所有 sheet 并按名汇总 | false |
| `-n, --numeric-strategy` | 数值策略 (sum/avg/max/min/first/last) | sum |
| `-t, --text-strategy` | 文本策略 (majority/first/last/concat) | majority |
| `-c, --code-strategy` | 编码策略 (placeholder/first/last) | placeholder |
| `-s, --stats-only` | 只显示统计 | false |
| `-v, --verbose` | 详细输出 | false |

## 输出示例

```
🔷 xlsOne - Excel 报表合并工具 v0.1.0
==================================================
📁 发现 13 个文件:
   1. 仙居县官路镇人民政府2025乡镇报表主体信息表.xlsx
   ...

📊 解析 Excel 文件...
✓ 解析完成 (13 个文件)

🔄 执行汇总...
✓ 汇总完成

📈 汇总统计:
------------------------------
  文件数量:    13
  总单元格:    110
  数值单元格:  7
  ...

📋 汇总结果预览 (前5行):
------------------------------------------------------------
  (空)              (空)              ...
------------------------------------------------------------

💾 已导出 JSON: merged_result.json
```

## 输出 JSON 格式

```json
{
  "statistics": {
    "totalFiles": 13,
    "totalCells": 110,
    "numericCells": 7,
    "textCells": 27,
    "codeCells": 3,
    "emptyCells": 73
  },
  "cells": [
    {
      "position": {
        "row": 0,
        "column": 0,
        "a1Notation": "A1"
      },
      "resultType": "numericSum",
      "resultValue": "250",
      "numericValue": 250.0,
      "sourceValues": ["100", "150"],
      "conflictMarkers": {
        "hasNumeric": true,
        "hasText": false,
        "hasCode": false,
        "hasEmpty": false
      }
    }
  ]
}
```
