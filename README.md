# xlsOne

**macOS 原生 Excel 报表合并工具**。

xlsOne 专注于“同构报表”的合并：以**单元格位置对齐**为核心原则，对多个 Excel 的同一位置进行汇总，并提供**穿透查看**（选中单元格后查看各文件原始值）。

## 特性
- **同构合并**：按行列位置对齐，适合卡片型/固定版式报表
- **多工作表支持**：顶部标签切换不同 sheet
- **穿透查看**：右侧检视栏展示选中单元格的各文件值
- **可调列宽**：拖拽列头调整宽度（带参考线）
- **CLI 工具**：批量合并、导出 JSON / XLSX（见 CLI 指南）

## 系统要求
- macOS 12+
- Xcode 15 / Swift 5.9

## 快速开始（App）
1. 生成工程：
   ```bash
   xcodegen
   ```
2. 打开 `xlsOne.xcodeproj` 并运行
3. 拖拽 `.xlsx` 文件到窗口或点击「打开」
4. 点击工作表标签切换 sheet
5. 点击单元格在右侧检视栏查看穿透值

> 说明：App 内“导出”功能目前仍在开发中。

## CLI（可选）
参见 `CLI_README.md`，示例：
```bash
# 安装依赖
pip3 install openpyxl --user

# 安装核心库
cd Sources/xlsOneCore
pip3 install -e . --user

# 使用 CLI
cd ../..
./xlsone-cli "仙居县/*.xlsx"
```

## 汇总策略（默认）
- **数值**：相加
- **文本**：取多数
- **编码**：生成占位符

## 开发说明
- 工程由 `project.yml` 生成（XcodeGen）
- Swift 核心在 `Sources/xlsOneCore/`

## 数据说明
示例数据不会提交到仓库（已加入 `.gitignore`）。

---

如需更进一步的功能（导出、规则自定义、冲突高亮等），欢迎继续提需求。
