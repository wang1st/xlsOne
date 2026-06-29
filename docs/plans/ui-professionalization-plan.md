# xlsOne UI 商品化专业化优化方案

> 版本：v1.0  
> 目标：将当前「可用的工具界面」升级为「可付费的工业级商品软件界面」  
> 范围：macOS SwiftUI 主界面、激活流程、截图材料、设计系统  
> 原则：先小改见效、再系统重构；不动核心算法与业务逻辑

---

## 一、目标与评判标准

### 1.1 什么是「工业化商品软件」的 UI 质感

| 维度 | 当前状态 | 目标状态 |
|------|---------|---------|
| 第一眼信任感 | 干净但偏个人工具 | 像 Apple 原生或精品付费 App |
| 信息层级 | 基本清晰，但靠颜色硬撑 | 靠间距、字重、容器自然分层 |
| 空状态/首屏 | 代码绘制的简单几何图形 | 高质量品牌插画 + 明确行动召唤 |
| 暗色模式 | 多处硬编码 `.white` | 完整适配 Light/Dark/HighContrast |
| 细节一致性 | 颜色/圆角/阴影分散在各处 | 统一 Design Tokens |
| 截图材料 | 背景是 IDE 聊天窗口 | 干净桌面背景、专业产品图 |

### 1.2 核心优化目标

1. **提升付费意愿**：首屏与激活流程是转化的关键，必须像专业软件。
2. **降低认知负荷**：财务用户多为非技术用户，界面要比 Excel 更简洁。
3. **增强可维护性**：抽出 Design Tokens，减少 `ContentView.swift` 的体积。
4. **通过软著/上架审核**：截图材料需重新制作。

---

## 二、现状诊断（基于代码与截图）

### 2.1 代码层面

- `Sources/xlsOneUI/ContentView.swift` 共 2150 行，集中了工具栏、空状态、标签栏、表格、检查面板、对话框等几乎所有主界面逻辑。
- 颜色、圆角、阴影、间距分散硬编码，无统一 Theme。
- 空状态使用 `EmptyWorkspaceArtwork` 代码绘制几何图形，缺乏品牌感。
- 激活窗口 `LicenseActivationView` 使用基础 VStack 堆叠，视觉节奏弱。
- 多处使用 `Color.white`、`.white.opacity(0.72)`，暗色模式会出问题。
- `LicenseStatusBadge` 已存在但未被引用到工具栏。

### 2.2 视觉层面（基于 `03-summary-grid.png`）

- 工具栏左侧按钮组与右侧「导出 XLSX」按钮视觉权重不平衡。
- Sheet 标签「sheet1/sheet2」选中态只有细下划线，辨识度一般。
- 右侧检查面板固定宽度，空白较多，信息密度偏低。
- 表格表头选中态（A1）颜色偏浅，选中感不强。
- 状态栏缺失：用户不知道当前导入了多少文件、汇总状态如何。
- 缺少应用 Logo/品牌名在工具栏的露出。

### 2.3 材料层面

- 软著截图 `01-empty-workspace.png` 未捕获到 xlsOne 窗口。
- 其他截图背景为 Codex IDE 聊天界面，极不正式。
- 无暗色模式截图、无高对比度截图、无辅助功能截图。

---

## 三、优化原则

1. **忠于 macOS HIG**：使用系统颜色、系统字体、系统控件行为，不发明新语言。
2. **少即是多**：财务用户不需要炫技，去掉不必要的装饰。
3. **Design Token 化**：所有颜色、间距、圆角、字体集中管理。
4. **首屏即品牌**：空状态、激活窗口、错误页面是用户第一印象，必须最高优先级优化。
5. **渐进实施**：先解决最影响付费与上架的问题，再逐步重构代码。

---

## 四、分模块优化建议

### 4.1 建立统一 Design System（最高优先级）

新建 `Sources/xlsOneUI/DesignSystem/` 目录，包含：

```
DesignSystem/
├── XTheme.swift          // 颜色、字体、间距、圆角、阴影
├── XColor.swift          // 语义化颜色（区分背景/表面/边框/文字/状态）
├── XFont.swift           // 字号规范
├── XSpacing.swift        // 间距规范
├── XButtonStyle.swift    // 主按钮/次按钮/工具栏按钮/链接按钮
├── XCard.swift           // 卡片容器
└── XIcon.swift           // 图标规范（SF Symbols 映射）
```

**关键 Token 示例**：

| Token | 当前硬编码 | 建议 Token |
|------|----------|-----------|
| 窗口背景 | `NSColor.windowBackgroundColor` | `XColor.background` |
| 卡片背景 | `NSColor.controlBackgroundColor` | `XColor.surface` |
| 主按钮背景 | `Color.accentColor` | `XColor.primary` |
| 成功色 | `Color.green` | `XColor.success` |
| 警告色 | `Color.orange` | `XColor.warning` |
| 错误色 | `Color.red` | `XColor.error` |
| 主圆角 | 8/10/12/14/16/28 混用 | `XRadius.small(6)` / `medium(8)` / `large(12)` / `xl(16)` |
| 阴影 | `black.opacity(0.04)` 等 | `XShadow.card` / `XShadow.dropZone` |

### 4.2 工具栏重构

**当前问题**：
- 左侧按钮组（追加/刷新/清空）与右侧导出按钮视觉不平衡。
- 缺少品牌 Logo、License 状态、全局搜索/视图切换。
- 按钮分组使用自定义背景+描边，与系统工具栏风格有割裂。

**优化建议**：

```
┌────────────────────────────────────────────────────────────────────┐
│  🟦 表表归一  │ 追加 ▾ │ 刷新 │ 清空 │  ─────  │  🔍 搜索  │  ⚙️  │  导出 XLSX │  ← 主工具栏
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
```

1. **左侧**：放置 App Icon（24pt）+ 应用名「表表归一」+ 操作按钮组。
2. **中间**：可加入全局搜索框（当文件多时需要）。
3. **右侧**：导出按钮使用系统 `.borderedProminent` 样式；旁边放置 `LicenseStatusBadge`。
4. **操作按钮组**：使用系统 `Divider()` 而非自定义 `Rectangle()` 做分隔。
5. **禁用态**：统一降低透明度 + 移除 hover 效果，符合 HIG。

### 4.3 Sheet 标签栏优化

**当前问题**：
- 选中态只有细下划线，不够明确。
- 跳过的 Sheet 用小橙点标识，不够直观。
- Sheet 多时没有滚动提示。

**优化建议**：

1. 选中标签使用**填充背景色**（`XColor.selectedSurface`）+ 加粗文字，而非仅下划线。
2. 未选中标签使用 `XColor.secondaryLabel`。
3. 跳过的 Sheet 在标签右侧显示橙色小徽章（文字「跳过」或图标），而非仅小点。
4. Sheet 数量超过可视区域时，左右显示渐变遮罩提示可滚动。
5. 支持右键菜单：重命名、隐藏、刷新单个 Sheet。

### 4.4 Excel 网格专业化

**当前问题**：
- 表头选中态颜色偏浅。
- 行号/列头字体偏小（11pt），对财务用户可能不够清晰。
- 网格线颜色在暗色模式下可能对比度不足。
- 可聚合数值使用蓝色文字+浅蓝背景，与系统风格略冲突。

**优化建议**：

1. **表头**：
   - 默认表头背景使用 `XColor.headerBackground`。
   - 选中列/行使用 `XColor.selectedHeaderBackground`（比当前更深）。
   - 字体提升到 12pt medium。
2. **单元格**：
   - 可聚合数值使用**单色下划线/图标**标识，而非整体改背景色，减少视觉噪音。
   - 修正过的单元格使用 subtle 的左侧色条（teal）+ 小圆点，而非大面积背景。
3. **网格线**：
   - 使用 `XColor.gridLine`，在 Dark Mode 下自动变亮。
4. **行号列**：
   - 当前行号高亮，帮助用户定位。
5. **选中态**：
   - 与 macOS Finder/Numbers 一致：选中行/列有轻微背景变化，选中单元格有系统蓝边框。

### 4.5 右侧检查面板重构

**当前问题**：
- 固定宽度 360pt，在大屏幕浪费空间，小屏幕过窄。
- 信息层级弱：A1、费用编码、说明文字、按钮、来源列表堆叠。
- 「4 个来源」展开按钮小，不易点击。
- 缺少文件图标、状态标签等视觉辅助。

**优化建议**：

1. **宽度可调**：使用 `NavigationSplitView` 或自定义 resizable sidebar，默认 320pt，最小 260pt，最大 480pt。
2. **信息分组**：
   - **单元格标识区**：坐标 A1 + 列名「费用编码」。
   - **智能判定区**：「首行按表头处理，强制视为标签列」等说明，使用 caption 样式 + 信息图标。
   - **操作区**：「标签」「求和」等按钮使用分段控件（Segmented Control）风格。
   - **来源穿透区**：每个来源显示文件名、单位名、原始值、偏差指示。
3. **来源列表**：
   - 每个来源左侧显示文件类型图标（Excel 图标或 SF Symbol `doc.text`）。
   - 显示单位名称而非仅文件名。
   - 异常值使用橙色/红色小标签标识。
4. **快捷键提示**：「按 1 或 2」改为更清晰的按钮 tooltip，而非面板内小字。

### 4.6 空工作台重设计（最高优先级）

**当前问题**：
- 使用代码绘制的简单矩形拼贴作为插画。
- 卡片背景硬编码 `.white.opacity(0.72)`，暗色模式失效。
- 缺少产品 Logo、功能亮点说明。

**优化建议**：

1. **使用高质量品牌插画**：
   - 设计一张 512×512 的 SVG/PNG 插画：多个 Excel 文件汇合成一张汇总表的视觉隐喻。
   - 风格：扁平、轻拟物、与 App Icon 一致的蓝色+橙色点缀。
   - 或使用 Lottie 微动画（文件飞入→合并→生成汇总表）。
2. **卡片容器**：
   - 使用 `XCard` 组件，背景跟随系统 `XColor.surface`。
   - 拖拽高亮时使用 `XColor.dropZoneBorder` + `XShadow.dropZone`。
3. **文案优化**：
   - 标题：「拖入 Excel 文件，一键汇总」
   - 副标题：「支持多单位同格式报表合并，自动识别表头与可汇总列」
   - 底部小字：「或点击工具栏 追加 按钮选择文件」
4. **品牌露出**：
   - 卡片顶部或标题旁放置 App Icon（48pt）。
5. **示例引导**：
   - 提供「查看示例」按钮，加载内置测试数据，降低首次使用门槛。

### 4.7 激活窗口重设计

**当前问题**：
- 基础 VStack 堆叠，视觉节奏弱。
- 输入框、按钮、链接混排，缺少步骤感。
- 离线激活说明是大段文字，难以阅读。
- 错误/成功信息挤在输入框下方，影响布局跳动。

**优化建议**：

1. **三栏或卡片式布局**：
   - 左侧：品牌区（Logo + 产品名 + 一句价值主张）。
   - 右侧：激活表单区。
2. **激活码输入框**：
   - 使用 4 个独立输入框（每框 4 字符），自动跳转，避免用户手动输入 `-`。
   - 输入框获得焦点时有蓝色边框。
3. **按钮**：
   - 「激活」使用 `.borderedProminent`。
   - 「免费试用 14 天」作为次级按钮，置于主按钮下方。
   - 「购买激活码」作为文本链接，不要过于突出。
4. **错误/成功提示**：
   - 使用 Snackbar 或输入框下方的固定高度提示区，避免布局跳动。
5. **离线激活**：
   - 使用折叠面板（Disclosure Group），步骤使用 1/2/3 编号 + 图标。
   - 提供「复制设备码」按钮。
6. **过期状态**：
   - 顶部显示红色横幅：「许可证已过期，请续费或开始试用」。

### 4.8 底部状态栏新增

**当前缺失**：
- 用户无法一眼看到当前工作区状态。

**优化建议**：

在表格下方新增状态栏：

```
┌────────────────────────────────────────────────────────────────────┐
│ 已导入 4 个文件  │ 当前 Sheet: sheet1  │ 可汇总列: 2  │ 已调整 1 处    [撤销] [清除] │
└────────────────────────────────────────────────────────────────────┘
```

1. 左侧显示文件数、Sheet 数、可汇总列数。
2. 中间显示当前操作提示。
3. 右侧放置「撤销」「清除」等操作按钮。
4. 长时间任务（如导入大文件）显示进度条。

### 4.9 对话框与弹窗统一

**当前问题**：
- `CenteredDialogWindow` 以 `.background` 形式挂在根视图上，尺寸固定。
- 缺少标准窗口行为（最小化/最大化/调整大小）。

**优化建议**：

1. 调整记忆弹窗使用系统 `.sheet` 或 `NSPanel`，支持调整大小。
2. 弹窗标题使用系统标题栏样式。
3. 统一按钮顺序：取消在左，确认在右（符合 macOS HIG）。

### 4.10 截图材料重制（立即执行）

**当前问题**：
- 截图背景为 IDE 聊天界面，极不正式。
- `01-empty-workspace.png` 未拍到应用窗口。

**重制清单**：

| 编号 | 场景 | 要求 |
|------|------|------|
| 01 | 空工作台 | 干净 macOS 桌面背景，窗口居中，展示空状态插画 |
| 02 | 批量导入 | 使用真实测试文件或系统 OpenPanel，背景干净 |
| 03 | 汇总表格 | 展示工具栏、Sheet 标签、表格、检查面板 |
| 04 | 来源穿透 | 展示右侧「4 个来源」展开状态 |
| 05 | 类型修正 | 展示单元格修正为「标签」后的状态 |
| 06 | Sheet 切换 | 展示 sheet2 的不同表头 |
| 07 | 导出保存 | 系统 SavePanel，背景干净 |
| 08 | 暗色模式 | 新增：Dark Mode 下的主界面 |
| 09 | 激活窗口 | 新增：激活窗口截图 |
| 10 | 关于窗口 | 新增：关于/版权信息窗口 |

**拍摄规范**：
- 使用纯色或渐变桌面壁纸，关闭所有无关窗口。
- 窗口尺寸统一为 1440×900 或 1280×800。
- 使用 `screencapture -w` 按窗口捕获，避免多余背景。
- 截图命名统一为 `xlsone-screenshot-01-empty.png` 等英文命名，便于国际化材料使用。

---

## 五、视觉规范建议

### 5.1 颜色系统

```swift
enum XColor {
    // 背景
    static var background: Color { Color(NSColor.windowBackgroundColor) }
    static var surface: Color { Color(NSColor.controlBackgroundColor) }
    static var elevatedSurface: Color { Color(NSColor.controlBackgroundColor).opacity(0.8) }
    
    // 文字
    static var primaryLabel: Color { Color.primary }
    static var secondaryLabel: Color { Color.secondary }
    static var tertiaryLabel: Color { Color(NSColor.tertiaryLabelColor) }
    
    // 边框与分隔
    static var divider: Color { Color(NSColor.separatorColor) }
    static var gridLine: Color { Color.gray.opacity(0.25) } // 需适配 Dark Mode
    
    // 强调色
    static var primary: Color { Color.accentColor }
    static var primaryHover: Color { Color.accentColor.opacity(0.9) }
    
    // 状态色
    static var success: Color { Color(NSColor.systemGreen) }
    static var warning: Color { Color(NSColor.systemOrange) }
    static var error: Color { Color(NSColor.systemRed) }
    static var info: Color { Color(NSColor.systemBlue) }
    
    // 业务语义
    static var aggregableValue: Color { Color.accentColor } // 可汇总数值
    static var correctedValue: Color { Color(red: 0.16, green: 0.62, blue: 0.56) }
    static var selectedHeaderBackground: Color { Color.accentColor.opacity(0.12) }
}
```

### 5.2 字体系统

| 用途 | 字号 | 字重 | 说明 |
|------|------|------|------|
| 窗口大标题 | 28pt | Semibold | 空状态标题 |
| 面板标题 | 18pt | Semibold | 检查面板列名 |
| 工具栏按钮 | 13pt | Medium | 操作按钮 |
| 表格数据 | 12pt | Regular | 默认单元格 |
| 表头/行号 | 12pt | Medium | 列头、行号 |
| 说明文字 | 11pt | Regular | 来源文件名、状态说明 |
| 辅助/快捷键 | 10pt | Regular | 次要提示 |

### 5.3 间距与圆角

| Token | 值 | 用途 |
|------|-----|------|
| XSpacing.xs | 4 | 图标与文字间距 |
| XSpacing.sm | 8 | 按钮内部、相邻控件 |
| XSpacing.md | 12 | 卡片内边距 |
| XSpacing.lg | 16 | 面板间距 |
| XSpacing.xl | 24 | 大模块间距 |
| XRadius.sm | 6 | 小按钮、输入框 |
| XRadius.md | 8 | 卡片、工具栏按钮 |
| XRadius.lg | 12 | 大卡片、弹窗 |
| XRadius.xl | 16 | 空工作台卡片 |

---

## 六、代码重构建议

### 6.1 拆分 `ContentView.swift`

将 2150 行的 `ContentView.swift` 拆分为以下独立文件：

```
Sources/xlsOneUI/
├── Workspace/
│   ├── WorkspaceView.swift         // 原 ContentView 的容器
│   ├── WorkspaceToolbar.swift      // 工具栏
│   ├── WorkspaceStatusBar.swift    // 底部状态栏
│   └── WorkspaceEmptyState.swift   // 空工作台
├── Workbook/
│   ├── WorkbookView.swift          // Sheet 标签 + 表格容器
│   ├── SheetTabBar.swift           // Sheet 标签栏
│   └── ExcelGridView.swift         // 表格（从 ContentView 提取）
├── Inspector/
│   ├── InspectorPanel.swift        // 右侧检查面板
│   ├── InspectorCellHeader.swift   // 单元格标识区
│   ├── InspectorActionPanel.swift  // 标签/求和操作
│   └── InspectorSourceList.swift   // 来源穿透列表
├── Dialogs/
│   └── SchemaManagerSheet.swift    // 调整记忆弹窗
└── DesignSystem/                   // 前文提到的设计系统
```

### 6.2 统一按钮样式

将 `WorkspaceChromePrimaryButtonStyle`、`WorkspaceChromeUtilityButtonStyle` 等迁移到 `DesignSystem/XButtonStyle.swift`，并确保：
- 主按钮使用系统 `.borderedProminent` 或语义上等价的自定义样式。
- 次按钮使用 `.bordered`。
- 工具栏按钮使用 `.plain` + hover 背景。
- 链接按钮使用 `.link`。

### 6.3 暗色模式适配检查清单

- [ ] 所有 `.white`、`.white.opacity()` 替换为 `XColor.surface`。
- [ ] 所有硬编码 `Color.gray` 替换为 `XColor.divider` 或 `XColor.gridLine`。
- [ ] 检查 `EmptyWorkspaceBackdrop` 在 Dark Mode 下的渐变。
- [ ] 检查表格选中态、表头背景在 Dark Mode 下的对比度。
- [ ] 检查警告/错误/成功色在 Dark Mode 下的可读性。
- [ ] 运行 VoiceOver 检查标签、按钮、表格的可访问性。

---

## 七、实施路线图

### 阶段一：立即见效（1-2 周）

1. **重制软著截图**：按 4.10 规范拍摄。
2. **空工作台重设计**：替换插画、优化文案、适配 Dark Mode。
3. **激活窗口重设计**：改为左右分栏 + 4 框输入 + 步骤化离线激活。
4. **工具栏微调**：加入 License 状态徽章、平衡左右按钮权重。

### 阶段二：系统化（2-4 周）

1. 建立 `DesignSystem` 模块，抽出颜色/字体/间距/圆角。
2. 拆分 `ContentView.swift` 为多个独立视图文件。
3. 重构 Sheet 标签栏、检查面板、底部状态栏。
4. 完整适配 Light/Dark/HighContrast 模式。

### 阶段三：精细化（4-6 周）

1. 增加动画与微交互（拖拽高亮、导入进度、导出成功提示）。
2. 优化表格细节（选中态、可汇总标识、修正标识）。
3. 增加 onboarding 引导（首次使用提示）。
4. 制作品牌插画库、应用图标高清版本。

---

## 八、预期效果

| 指标 | 当前 | 预期 |
|------|------|------|
| 首屏专业感 | 6/10 | 9/10 |
| 暗色模式完成度 | 4/10 | 9/10 |
| 代码可维护性 | 5/10 | 8/10 |
| 软著材料质量 | 3/10 | 9/10 |
| 激活转化率（估算） | 基准 | +20%~30% |
| 用户学习成本 | 中等 | 低 |

---

## 九、暂不做的优化（避免过度设计）

1. **不更换技术栈**：继续 SwiftUI + AppKit，不引入 Qt/WinUI 的 macOS 版。
2. **不做复杂自定义主题**：优先适配系统 Light/Dark，不做用户自选主题。
3. **不做 3D/复杂动效**：保持办公软件的克制感。
4. **不动核心算法 UI 行为**：只改视觉与交互，不改汇总逻辑。

---

## 十、下一步建议

1. 确认本方案范围与优先级。
2. 选择「先重制截图 + 空状态 + 激活窗口」作为第一冲刺。
3. 并行设计品牌插画（可交给设计师，或使用 Figma/Sketch 快速出图）。
4. 开始建立 `DesignSystem` 基础 Token，为后续重构铺路。

---

*本方案为只读分析与规划，未修改任何代码。确认后可选定模块进入实施阶段。*
