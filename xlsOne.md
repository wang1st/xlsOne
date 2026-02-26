# xlsOne

<div align="center">

**原生 macOS Excel 报表合并工具**

[![Swift](https://img.shields.io/badge/Swift-5.9-orange.svg)](https://swift.org)
[![macOS](https://img.shields.io/badge/macOS-12.0+-blue.svg)](https://www.apple.com/macos)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Release](https://img.shields.io/badge/Download-.dmg-brightgreen.svg)](https://github.com/yourusername/xlsOne/releases)

极小包体 · 极致性能 · 原生体验

</div>

---

## 简介

xlsOne 是一款使用 **Swift + SwiftUI** 开发的纯原生 macOS Excel 报表合并工具。完全摒弃 WebView，提供真正的 macOS 原生体验。

### 核心特性

- **极致性能**：内存占用极低，支持大文件流式处理
- **原生体验**：100% macOS 风格，支持深色模式、Quick Look、拖拽
- **极小包体**：应用大小 < 10MB，秒级下载
- **Agents适配**：核心内核由CLI实现，方便Agents调用。

### 汇总原理

基于**位置对齐的单元格级独立汇总**，适用于卡片型表格（如发票、单据、报表卡片）：

- 不假设表格有表头
- 不假设行列之间有关联
- 每个单元格仅通过其在表格中的**位置（行，列）**来对齐

---

## 功能特性

### 智能数据识别

- **数值类型**：纯数字、货币（¥/$/€）、百分比（%）
- **编码类型**：订单001、ID-123、SKU-A456 等结构化编码
- **文本类型**：纯文本内容
- **混合内容**：自动识别并提取数值

### 灵活的汇总策略

- **数值汇总**：同一位置的数值自动相加
- **编码处理**：智能生成占位符（订单001 + 订单002 → 订单000）
- **文本冲突**：自动选择出现最多的文本
- **混合类型**：支持多种处理策略（仅汇总数值、保留首个、标记审核）

### 冲突管理

- **可视化标记**：单元格右上角显示状态标记
  - 🟢 绿色：可数值汇总
  - 🟡 黄色：有混合内容
  - 🔴 红色：有冲突需确认
  - ⚪ 灰色：全为空
- **穿透查看**：下方console面板实时显示各文件原始值
- **Agent适配**：提供CLI命令行工具方便Agent调用

---

## 安装

### 方式一：下载 DMG 安装（推荐）

1. 访问 [官网下载页](https://yourwebsite.com/xlsone)
2. 下载最新版本的 `.dmg` 镜像
3. 打开 DMG 文件，将 xlsOne 拖拽到 Applications 文件夹

### 方式二：Homebrew 安装

```bash
brew install --cask xlsone
```

### 方式三：从源码构建

```bash
# 克隆仓库
git clone https://github.com/yourusername/xlsOne.git
cd xlsOne

# 使用 Swift Package Manager 构建
swift build -c release

# 运行应用
.swift-build/release/xlsOne
```

详细构建说明请参考 [BUILD.md](BUILD.md)

---

## 快速开始

### UI基本使用
在Applications打开应用，窗口分为上下两个部分。上面部分显示合并后的excel表格，下面部分为console窗口。
1. **添加文件**
   - 直接将 Excel 文件拖拽到窗口上面部分面板
   - 支持批量导入文件和文件夹
   - 拖入文件后系统就开始汇总，在没有被重置前，后续拖入文件后会继续在原有基础上汇总

2. **预览数据**
   - 上面部分显示汇总后的文件的数据预览，可以在不同sheet间切换，鼠标可点击任一单元格
   - 点击任一单元格，下方console面板中画出ascii表格显示各文件的原始值

3. **结果处理**
   - 点击「导出」按钮，选择保存位置和格式（.xlsx / .csv）进行保存
   - 点击「重置」按钮，系统初始化为刚打开应用的状态
   - 点击「重载」按钮，重新读取所有文件，并重新汇总

### 键盘快捷键

| 快捷键 | 功能 |
|--------|------|
| `⌘ + O` | 打开文件选择器 |
| `⌘ + R` | 重载 |
| `⌘ + C` | 重置 |
| `⌘ + S` | 导出 |
| `⌘ + ,` | 打开偏好设置 |
| `⌘ + Q` | 退出应用 |
| `Tab` | 在上下面板间切换焦点 |
| `↑ ↓ ← →` | 移动选中单元格 |

---

## 汇总策略说明

### 数值格式

系统支持识别以下数值格式：

- **纯数字**：`123` → `123`
- **货币**：`¥100` → `100`、`$200` → `200`
- **百分比**：`50%` → `0.5`

### 冲突解决策略

在 **偏好设置** 中可配置以下策略：

1. **保留多数**：选择出现次数最多的值
2. **保留第一个**：保留第一个文件的值
3. **保留最后一个**：保留最后一个文件的值
4. **文本拼接**：将所有值用逗号连接
5. **每次询问**：逐个手动确认

### 空值处理

- **视为 0**：空单元格参与数值汇总时计为 0
- **忽略**：跳过空单元格，不参与汇总
- **保留空**：保留空值，不进行任何处理

### 混合类型策略

当同一位置既有数值又有文本/编码时：

- **仅汇总数值**：只汇总数值部分，编码/文本保留第一个
- **保留首个**：保留第一个文件的值
- **标记审核**：标记为待审核，输出空值

---

## 示例数据

### 示例 1：数值汇总

**文件 1:**
```
| A     | B   |
|-------|-----|
| 100   | 200 |
```

**文件 2:**
```
| A     | B   |
|-------|-----|
| 150   | 250 |
```

**汇总结果:**
```
| A     | B   |
|-------|-----|
| 250   | 450 |
```

### 示例 2：编码占位符

**文件 1:**
```
| 订单001 | ¥100 |
```

**文件 2:**
```
| 订单002 | ¥150 |
```

**汇总结果:**
```
| 订单000 | ¥250 |
```

### 示例 3：文本冲突处理

**文件 1-3:**
```
| 北京 | 北京 | 上海 |
```

**汇总结果:** `北京`（取多数）

---

## 系统要求

- **操作系统**：macOS 12.0 Monterey 或更高版本
- **架构**：Apple Silicon (arm64) 或 Intel (x86_64)
- **磁盘空间**：至少 20MB 可用空间
- **内存**：建议 4GB 以上

---

## 常见问题

### Q: 支持哪些 Excel 格式？

A: 支持 `.xlsx` 和 `.xls` 格式。推荐使用 `.xlsx` 格式以获得更好的性能。

### Q: 可以汇总多少个文件？

A: 默认最多 100 个文件，可在偏好设置中调整。系统会进行同构性检查，确保所有文件行列数一致。

### Q: 如何处理加密的 Excel 文件？

A: 请先在 Excel 中解除密码保护，然后再导入 xlsOne。

### Q: 汇总结果与预期不符怎么办？

A:
1. 查看单元格右上角的标记
2. 在console查看各文件原始值
3. 修改错误文件的内容并保存
4. 重载文件

### Q: 是否支持大型 Excel 文件？

A: 支持。系统使用流式处理技术，内存占用控制在 500MB 以内。

---

## 文档

- [用户使用指南](docs/user-guide.md) - 详细功能说明
- [汇总策略详解](docs/merge-strategies.md) - 深入了解汇总逻辑
- [快捷键列表](docs/shortcuts.md) - 全部快捷键参考
- [构建指南](BUILD.md) - 从源码构建应用

---

## 路线图

- [x] 基础汇总功能
- [x] 冲突检测和解决
- [x] 键盘导航
- [x] 大文件流式处理
- [x] 批量冲突解决
- [ ] CSV 文件导入
- [ ] 自定义汇总规则
- [ ] 插件系统

---

## 贡献

欢迎贡献代码、报告问题或提出建议！

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'feat: add amazing feature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

---

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

---

## 致谢

- [CoreXLSX](https://github.com/CoreXLSX/CoreXLSX) - Excel 文件解析库
- [SwiftUI](https://developer.apple.com/xcode/swiftui/) - 原生 UI 框架

---

<div align="center">

**Made with ❤️ for macOS**

[官网](https://yourwebsite.com/xlsone) · [文档](docs/) · [问题反馈](https://github.com/yourusername/xlsOne/issues) · [更新日志](CHANGELOG.md)

</div>
