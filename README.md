# xlsOne - Excel 财务报表汇总工具

根据 PRD.md 开发的 macOS Excel 财务报表汇总工具。

## 功能特性

- **文件导入**: 支持拖拽导入多个 .xlsx 或 .xls 文件
- **工作表管理**: 自动识别所有文件中的工作表名称，支持切换不同工作表
- **智能单元格聚合**:
  - 所有文件值相同 → 显示标签（如"201"）
  - 值不同且都是数字 → 显示求和结果（如"4500"）
  - 值不同且包含非数字 → 显示混合数量（如"3条"）
- **穿透查阅**: 点击单元格可查看该位置在所有文件中的原始值
- **导出功能**: 将汇总结果导出为 HTML 格式

## 技术架构

```
xlsOneCore/
├── Models.swift           # 数据模型（CellData, MergedCell, SheetData, ExcelFile, MergedResult）
├── ExcelParser.swift      # Excel 解析（基于 CoreXLSX）
├── SimpleMerger.swift     # 合并引擎（核心逻辑）
├── ExcelExporter.swift    # 导出功能（CSV, HTML）
└── CLI.swift              # 命令行测试工具

xlsOne/
├── XlsOneApp.swift        # 应用入口（SwiftUI）
└── ContentView.swift      # 主界面
```

## 构建与运行

### 构建
```bash
swift build -c release
```

### 运行GUI应用
```bash
.build/release/xlsOne
```

### 运行CLI测试（验证仙居县文件）
```bash
.build/release/xlsOneCLI
```

### 运行测试
```bash
swift test
```

## 验证结果

使用仙居县13个乡镇的财务报表进行验证：

- ✅ 成功解析 13 个 Excel 文件
- ✅ 识别 11 个工作表
- ✅ 金额列正确求和（如 ∑15467.63）
- ✅ 标签列正确显示
- ✅ 混合类型正确标记（如"2条"）

## 数字格式支持

- 纯数字（如"1000"）
- 千分位格式（如"1,000.50"）
- 欧式格式（如"1.234,56"）
- 小数（如"3950.0"）

## 待实现功能

根据 PRD.md：
- [ ] 导出为真正的 Excel 格式（当前仅支持 HTML/CSV）
- [ ] LLM 类型识别（可选增强）
- [ ] 括号表示负数的支持
- [ ] 单元格格式保留（字体、颜色等）
