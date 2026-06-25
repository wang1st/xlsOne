# xlsOne 国际化 (i18n) 实施计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 xlsOne（Swift/macOS + C++/Qt 双版本）添加完整的国际化支持，覆盖 UI 文本和算法关键词。

**Architecture:** Swift 端使用 Apple `Strings Catalog` (.xcstrings) 统一管理翻译，运行时通过 `String(localized:)` 取词。C++/Qt 端使用 Qt Linguist 工具链（`lupdate` -> `.ts` -> `lrelease` -> `.qm`），通过 `QTranslator` 在运行时加载。两端的算法关键词（如 "合计"、"sum"）抽离为独立的语言资源文件（JSON），由各自平台的 i18n 管理器加载。

**Tech Stack:** Swift 5.9+, String Catalog (.xcstrings), Qt 6, Qt Linguist (lupdate/lrelease), QTranslator, JSON

---

## 语言选择分析

### 推荐策略：先做英文，再扩展日韩

| 语言 | 新增市场 | 算法兼容性 | 开发工作量 | 优先级 |
|------|---------|-----------|-----------|-------|
| **英文 (en)** | 全球最大市场，非中文用户首次可用 | 算法已内置英文语义模式（sum/total/code等） | 中等 | P0 |
| 日文 (ja) | 日本财务软件市场规模大 | 需新增日文语义模式 | 高 | P2 |
| 韩文 (ko) | 韩国市场 | 需新增韩文语义模式 | 高 | P2 |
| 繁体中文 (zh-Hant) | 港澳台 | 与简体中文语义模式高度重叠 | 低 | P1 |

**结论：**
- **P0 英文** — 成本效益最高。算法层已有英文关键词（Models.swift:840-876, SimpleMerger.swift:263-287），仅需补全 UI 翻译。开发者工具完全支持。
- **P1 繁体中文** — 工作量最低。仅需 UI 翻译，算法关键词几乎可完全复用简体中文模式。
- **P2 日文/韩文** — 需要额外投入算法关键词研究和新增语义模式，建议在英文版本验证国际化架构后考虑。

### 实施步骤

1. 先搭建英文国际化基础设施（切换机制、资源文件格式、加载流程）
2. 抽离所有算法硬编码字符串到语言资源文件
3. 实现英文 UI 翻译
4. 验证后按需添加繁体中文

---

### Task 1: Swift 端国际化基础设施搭建

**Files:**
- Create: `Sources/xlsOneCore/Resources/Localizable.xcstrings` (String Catalog)
- Create: `Sources/xlsOneCore/I18n/LocaleManager.swift`
- Create: `Sources/xlsOneCore/I18n/AlgorithmI18n.swift`
- Modify: `Sources/xlsOne/XlsOneApp.swift` (注入语言设置)
- Modify: `Package.swift` (添加 Resources)

**Step 1: 创建语言管理器**

```swift
// Sources/xlsOneCore/I18n/LocaleManager.swift
import Foundation

public final class LocaleManager: ObservableObject {
    public static let shared = LocaleManager()

    @Published public var currentLanguage: AppLanguage {
        didSet { UserDefaults.standard.set(currentLanguage.rawValue, forKey: "appLanguage") }
    }

    public enum AppLanguage: String, CaseIterable, Identifiable {
        case system = "system"
        case english = "en"
        case chineseSimplified = "zh-Hans"
        case chineseTraditional = "zh-Hant"

        public var id: String { rawValue }

        public var displayName: String {
            switch self {
            case .system: return "跟随系统"
            case .english: return "English"
            case .chineseSimplified: return "简体中文"
            case .chineseTraditional: return "繁體中文"
            }
        }

        public var localeIdentifier: String? {
            switch self {
            case .system: return nil
            case .english: return "en"
            case .chineseSimplified: return "zh-Hans"
            case .chineseTraditional: return "zh-Hant"
            }
        }
    }

    private init() {
        let stored = UserDefaults.standard.string(forKey: "appLanguage") ?? ""
        currentLanguage = AppLanguage(rawValue: stored) ?? .system
    }

    func apply() {
        if let id = currentLanguage.localeIdentifier {
            UserDefaults.standard.set([id], forKey: "AppleLanguages")
        } else {
            UserDefaults.standard.removeObject(forKey: "AppleLanguages")
        }
        UserDefaults.standard.synchronize()
    }
}
```

**Step 2: 验证 LocaleManager 编译通过**

Run: `swift build --target xlsOneCore`
Expected: Build SUCCESS

**Step 3: 在 App 入口集成语言设置**

修改 `Sources/xlsOne/XlsOneApp.swift`（或 `App/xlsOneMacApp/XlsOneMacApp.swift`）在 `init()` 或 `.onAppear` 中调用 `LocaleManager.shared.apply()`。

**Step 4: 创建 String Catalog**

在 Xcode 中，`File > New > File > String Catalog`，命名为 `Localizable.xcstrings`，保存到 `Sources/xlsOneCore/Resources/`。

或者手动创建最小骨架：

```json
{
  "sourceLanguage": "zh-Hans",
  "strings": {},
  "version": "1.0"
}
```

**Step 5: 注册资源到 Package.swift**

```swift
// 在 xlsOneCore target 中添加:
resources: [.process("Resources")]
```

**Step 6: 编译验证**

Run: `swift build`
Expected: Build SUCCESS

**Step 7: Commit**

```bash
git add Sources/xlsOneCore/I18n/LocaleManager.swift Sources/xlsOneCore/I18n/AlgorithmI18n.swift Sources/xlsOneCore/Resources/ Package.swift Sources/xlsOne/XlsOneApp.swift
git commit -m "feat(i18n): add Swift locale infrastructure with String Catalog support"
```

---

### Task 2: 抽离算法硬编码关键词到语言资源

**Files:**
- Create: `Sources/xlsOneCore/Resources/AlgorithmKeywords.json`
- Modify: `Sources/xlsOneCore/I18n/AlgorithmI18n.swift`
- Modify: `Sources/xlsOneCore/Models.swift:833-876` (关键词数组替换)
- Modify: `Sources/xlsOneCore/SimpleMerger.swift:263-287` (关键词数组替换)
- Modify: `Sources/xlsOneCore/ColumnTypeAnalyzer.swift:73-101` (关键词数组替换)

**Step 1: 创建算法关键词 JSON 资源文件**

```json
{
  "zh-Hans": {
    "amountPatterns": ["合计", "总计", "小计", "金额", "数额", "额度", "数量", "单价", "总价", "价格", "数值", "预算", "收入", "支出", "成本", "费用", "利润", "执行", "决算", "款", "税金", "人数", "人口", "户数", "家数", "个数", "人员", "编制", "职工"],
    "weakAmountPatterns": ["数", "额", "值", "量", "价"],
    "codePatterns": ["代码", "编码", "编号", "序号", "号码", "证号", "区划", "邮编", "邮政编码", "身份证", "电话", "传真", "期间", "年月", "年份", "日期", "时间", "学号", "工号", "账号", "户号", "卡号", "单号", "订单号", "票号", "发票号", "批号", "书号", "卷号", "册号", "期号", "版号", "件号", "条码", "档案号", "准考证号", "资格证号", "许可证号", "机号", "箱号", "包号", "袋号"],
    "labelPatterns": ["名称", "名字", "描述", "说明", "备注", "标题", "内容", "详情", "类型", "性质", "状态"],
    "sumForcingPatterns": ["合计", "总计", "小计"],
    "currencySymbols": ["¥", "\\¥", "[$¥]"],
    "dollarSymbols": ["$", "[$-"],
    "dashMarkers": ["—", "-", "/", "NA", "N/A", "无", "null", "NULL", "~"],
    "strongAmountKeywords": ["金额", "总额", "合计", "总计", "小计", "预算", "执行", "决算", "收入", "支出", "费用", "成本", "资金", "付款", "收款"],
    "mediumAmountKeywords": ["数", "额", "值", "量", "价"],
    "strongLabelKeywords": ["名称", "名字", "描述", "说明", "备注", "标题", "内容", "详情", "注释"],
    "strongCodeKeywords": ["代码", "编码", "编号", "序号", "id", "code", "no", "区划", "行政区划", "科目代码", "项目代码", "邮编", "电话", "传真", "社会信用代码", "统一代码"],
    "strongDateKeywords": ["日期", "时间", "年度", "年份", "月份", "年月", "填报日期", "报送日期", "截止日期", "创建时间"],
    "amountKeywordsEn": ["sum", "total", "subtotal", "amount", "quantity", "qty", "price", "unit price", "total price", "value", "budget", "revenue", "income", "expense", "cost", "fee", "profit", "tax", "fund", "population", "headcount", "staff"],
    "codeKeywordsEn": ["code", "number", "no.", "no ", " id", "index", "serial", "zip", "zipcode", "postal code", "phone", "tel", "fax", "period", "date", "time", "year", "month"],
    "labelKeywordsEn": ["name", "desc", "description", "title", "remark", "note", "type", "kind", "status", "content", "detail"]
  },
  "en": {
    "amountPatterns": ["sum", "total", "subtotal", "amount", "quantity", "qty", "price", "unit price", "total price", "value", "budget", "revenue", "income", "expense", "cost", "fee", "profit", "tax", "fund", "population", "headcount", "staff"],
    "weakAmountPatterns": ["count", "val", "qty", "amt"],
    "codePatterns": ["code", "number", "no.", "no ", "id", "index", "serial", "zip", "zipcode", "postal code", "phone", "tel", "fax", "period", "date", "time", "year", "month"],
    "labelPatterns": ["name", "desc", "description", "title", "remark", "note", "type", "kind", "status", "content", "detail"],
    "sumForcingPatterns": ["sum", "total", "subtotal", "grand total"],
    "currencySymbols": ["$", "[$-"],
    "dollarSymbols": ["$", "[$-"],
    "dashMarkers": ["—", "-", "/", "NA", "N/A", "n/a", "null", "NULL", "~", "none"],
    "strongAmountKeywords": ["amount", "total", "sum", "subtotal", "budget", "revenue", "income", "expense", "cost", "fee", "fund", "payment", "tax"],
    "mediumAmountKeywords": ["count", "val", "qty", "amt", "rate"],
    "strongLabelKeywords": ["name", "description", "remark", "note", "title", "comment", "detail"],
    "strongCodeKeywords": ["code", "id", "number", "no", "index", "serial", "zip", "phone", "fax"],
    "strongDateKeywords": ["date", "time", "year", "month", "period", "created", "updated"],
    "amountKeywordsEn": ["sum", "total", "subtotal", "amount", "quantity", "qty", "price", "unit price", "total price", "value", "budget", "revenue", "income", "expense", "cost", "fee", "profit", "tax", "fund", "population", "headcount", "staff"],
    "codeKeywordsEn": ["code", "number", "no.", "no ", " id", "index", "serial", "zip", "zipcode", "postal code", "phone", "tel", "fax", "period", "date", "time", "year", "month"],
    "labelKeywordsEn": ["name", "desc", "description", "title", "remark", "note", "type", "kind", "status", "content", "detail"]
  }
}
```

**Step 2: 创建算法 i18n 访问器**

```swift
// Sources/xlsOneCore/I18n/AlgorithmI18n.swift
import Foundation

public final class AlgorithmI18n {
    public static let shared = AlgorithmI18n()

    private var keywords: AlgorithmKeywords

    public struct AlgorithmKeywords {
        public let amountPatterns: [String]
        public let weakAmountPatterns: [String]
        public let codePatterns: [String]
        public let labelPatterns: [String]
        public let sumForcingPatterns: [String]
        public let currencySymbols: [String]
        public let dollarSymbols: [String]
        public let dashMarkers: [String]
        public let strongAmountKeywords: [String]
        public let mediumAmountKeywords: [String]
        public let strongLabelKeywords: [String]
        public let strongCodeKeywords: [String]
        public let strongDateKeywords: [String]
        public let amountKeywordsEn: [String]
        public let codeKeywordsEn: [String]
        public let labelKeywordsEn: [String]
    }

    private init() {
        let lang = LocaleManager.shared.currentLanguage.localeIdentifier ?? "zh-Hans"
        keywords = AlgorithmI18n.load(for: lang)
    }

    public func reload() {
        let lang = LocaleManager.shared.currentLanguage.localeIdentifier ?? "zh-Hans"
        keywords = AlgorithmI18n.load(for: lang)
    }

    private static func load(for language: String) -> AlgorithmKeywords {
        guard let url = Bundle.module.url(forResource: "AlgorithmKeywords", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let langData = json[language] as? [String: Any]
        else {
            // Fallback to zh-Hans
            return loadBuiltinChinese()
        }
        return parse(from: langData)
    }

    // ... parser methods

    public var current: AlgorithmKeywords { keywords }
}
```

**Step 3: 替换 Models.swift 中的硬编码数组**

将 `Models.swift:833-876` 中的直接数组字面量替换为 `AlgorithmI18n.shared.current.amountPatterns` 等引用。

Run: `swift build`
Expected: Build SUCCESS

**Step 4: 替换 SimpleMerger.swift 中的硬编码数组**

将 `SimpleMerger.swift:263-287` 中的 `metricAnchorPatterns` 和 `codeAnchorPatterns` 替换为算法 i18n 引用。

Run: `swift build`
Expected: Build SUCCESS

**Step 5: 替换 ColumnTypeAnalyzer.swift 中的硬编码数组**

将 `ColumnTypeAnalyzer.swift:73-101` 中的关键词数组替换为算法 i18n 引用。

Run: `swift build`
Expected: Build SUCCESS

**Step 6: 运行测试验证算法行为不变**

Run: `swift test --filter SimpleMergerTests`
Expected: All tests PASS

Run: `swift test --filter WorkspaceRuleTests`
Expected: All tests PASS

**Step 7: Commit**

```bash
git add Sources/xlsOneCore/Resources/AlgorithmKeywords.json Sources/xlsOneCore/I18n/AlgorithmI18n.swift Sources/xlsOneCore/Models.swift Sources/xlsOneCore/SimpleMerger.swift Sources/xlsOneCore/ColumnTypeAnalyzer.swift
git commit -m "feat(i18n): extract algorithm keywords to language resource JSON"
```

---

### Task 3: Swift UI 国际化 — 第一批（启动/导入/导出相关）

需要国际化的文件约 500+ 个字符串，分批进行。第一批覆盖最常用的用户交互。

**Files:**
- Modify: `Sources/xlsOneCore/Resources/Localizable.xcstrings`
- Modify: `Sources/xlsOneUI/ContentView.swift`
- Modify: `Sources/xlsOneUI/WorkspaceScene.swift`
- Modify: `Sources/xlsOneUI/WorkspaceModels.swift`

**Step 1: 将第一批 UI 字符串添加到 String Catalog**

在 `Localizable.xcstrings` 中添加以下条目（zh-Hans 为源语言，en 为目标）：

| Key | zh-Hans | en |
|-----|---------|-----|
| `追加` | 追加 | Add |
| `刷新` | 刷新 | Refresh |
| `清空` | 清空 | Clear |
| `导出 XLSX` | 导出 XLSX | Export XLSX |
| `松手即可导入` | 松手即可导入 | Drop to Import |
| `拖入 Excel 文件` | 拖入 Excel 文件 | Drop Excel Files |
| `支持多个 .xlsx / .xls` | 支持多个 .xlsx / .xls | Supports .xlsx / .xls |
| `选择文件` | 选择文件 | Select Files |
| `向当前批次追加文件` | 向当前批次追加文件 | Add files to current batch |
| `重新读取当前文件并刷新汇总结果` | 重新读取当前文件并刷新汇总结果 | Reload files and refresh results |
| `清空当前工作区，不影响原始 Excel 文件` | 清空当前工作区，不影响原始 Excel 文件 | Clear workspace (original files unaffected) |
| `导出同构汇总 Excel` | 导出同构汇总 Excel | Export merged Excel workbook |
| `正在校验工作簿结构并准备汇总工作台…` | 正在校验工作簿结构并准备汇总工作台… | Validating workbook structures… |
| `没有可参与汇总的同构工作表` | 没有可参与汇总的同构工作表 | No compatible sheets for merging |
| `参与文件` | 参与文件 | Included Files |
| `阻断文件` | 阻断文件 | Blocked Files |
| `警告文件` | 警告文件 | Warning Files |
| `跳过工作表` | 跳过工作表 | Skipped Sheets |
| `参与合并` | 参与合并 | Included |
| `已跳过` | 已跳过 | Skipped |
| `阻断` | 阻断 | Blocked |
| `当前没有可显示的汇总结果` | 当前没有可显示的汇总结果 | No merge results to display |
| `撤销上一步` | 撤销上一步 | Undo |
| `清除本批次调整` | 清除本批次调整 | Clear All Corrections |
| `修正为` | 修正为 | Correct As |
| `标签` | 标签 | Label |
| `求和` | 求和 | Sum |
| `恢复自动判断` | 恢复自动判断 | Restore Auto |

**Step 2: 替换 ContentView.swift 中的硬编码字符串**

将硬编码中文字符串替换为 `String(localized: "追加")`、`String(localized: "刷新")` 等。

示例：
```swift
// Before
Button("追加") { ... }
    .help("向当前批次追加文件")

// After
Button(String(localized: "追加")) { ... }
    .help(String(localized: "向当前批次追加文件"))
```

**Step 3: 编译验证**

Run: `swift build`
Expected: Build SUCCESS

**Step 4: Commit**

```bash
git add Sources/xlsOneCore/Resources/Localizable.xcstrings Sources/xlsOneUI/ContentView.swift Sources/xlsOneUI/WorkspaceScene.swift Sources/xlsOneUI/WorkspaceModels.swift
git commit -m "feat(i18n): internationalize ContentView and WorkspaceScene batch 1"
```

---

### Task 4: Swift UI 国际化 — 第二批（菜单/弹窗/错误消息）

**Files:**
- Modify: `Sources/xlsOneCore/Resources/Localizable.xcstrings`
- Modify: `Sources/xlsOneUI/WorkspaceScene.swift` (菜单项、关于/帮助弹窗)
- Modify: `Sources/xlsOneUI/SchemaManagerView.swift`
- Modify: `Sources/xlsOneLicense/LicenseActivationView.swift`
- Modify: `Sources/xlsOneLicense/LicenseManager.swift` (错误描述)

**Step 1: 添加第二批 String Catalog 条目**

添加菜单、弹窗、许可证相关 UI 字符串的翻译。

**Step 2: 替换 WorkspaceScene.swift 中的菜单文字**

注意：`WorkspaceMenuLocalizer` (第446-617行) 已有一套手动本地化逻辑。保留其对 macOS 标准菜单项的本地化（File/Edit/View/Window/Help），但将应用自定义菜单项改为使用 `String(localized:)`。

**Step 3: 替换弹窗文本**

将关于弹窗（第376-389行）、帮助弹窗（第398-429行）中的硬编码中文替换为 String Catalog 引用。

**Step 4: 替换许可证 UI 文本**

将 `LicenseActivationView.swift` 中的激活界面文字国际化。

**Step 5: 编译验证**

Run: `swift build`
Expected: Build SUCCESS

**Step 6: Commit**

```bash
git add Sources/xlsOneCore/Resources/Localizable.xcstrings Sources/xlsOneUI/WorkspaceScene.swift Sources/xlsOneUI/SchemaManagerView.swift Sources/xlsOneLicense/
git commit -m "feat(i18n): internationalize menus, dialogs, and license views"
```

---

### Task 5: Swift 端决定原因 (Decision Reason) 国际化

`Models.swift` 中包含约 40 条中文决策原因字符串（如 "所有来源单元格均为空或缺失"），这些在检查侧边栏中显示给用户。

**Files:**
- Modify: `Sources/xlsOneCore/Resources/Localizable.xcstrings`
- Modify: `Sources/xlsOneCore/Models.swift:297-810`

**Step 1: 在 String Catalog 中添加决策原因条目**

为所有决策原因字符串添加 key/en 翻译。

**Step 2: 替换 Models.swift 中的决策原因字符串**

将直接中文字符串替换为 `String(localized:)`。

**Step 3: 编译验证并运行测试**

Run: `swift build && swift test`
Expected: Build + Tests PASS

**Step 4: Commit**

```bash
git add Sources/xlsOneCore/Resources/Localizable.xcstrings Sources/xlsOneCore/Models.swift
git commit -m "feat(i18n): internationalize cell type decision reasons"
```

---

### Task 6: Swift 端 FormatFingerprint 描述国际化

**Files:**
- Modify: `Sources/xlsOneCore/Resources/Localizable.xcstrings`
- Modify: `Sources/xlsOneCore/Models.swift:1304-1320`

**Step 1: 添加格式指纹描述翻译**

| Key | zh-Hans | en |
|-----|---------|-----|
| 强数值 | 强数值 | Strong Numeric |
| 宽整数 | 宽整数 | Wide Integer |
| 整数编码 | 整数编码 | Integer Code |
| 中文文本 | 中文文本 | Chinese Text |
| 英文文本 | 英文文本 | English Text |
| 日期 | 日期 | Date |
| 占位符 | 占位符 | Placeholder |
| 空值 | 空值 | Empty |
| 混合格式 | 混合格式 | Mixed Format |

**Step 2: 替换为 String(localized:) 并验证**

**Step 3: Commit**

---

### Task 7: C++/Qt 端国际化基础设施搭建

**Files:**
- Modify: `cpp/app/src/main.cpp`
- Create: `cpp/i18n/xlsone_zh_CN.ts` (源翻译文件)
- Create: `cpp/i18n/xlsone_en.ts` (英文翻译文件)
- Modify: `cpp/CMakeLists.txt` (添加 lupdate/lrelease 目标)

**Step 1: 使用 lupdate 从 tr() 调用生成 .ts 文件**

```bash
cd /Users/ethan/xlsOne/cpp
mkdir -p i18n
lupdate app/src/ core/src/ -ts i18n/xlsone_zh_CN.ts
```

**Step 2: 复制 zh_CN.ts 生成英文模板**

```bash
cp i18n/xlsone_zh_CN.ts i18n/xlsone_en.ts
```

**Step 3: 编辑 xlsone_en.ts 填入英文翻译**

手动或用 Qt Linguist 工具编辑 `.ts` 文件，将 `<translation type="unfinished"></translation>` 替换为对应英文翻译。

涉及约 155 条 tr() 字符串 + 约 25 条遗漏的 UI 字符串（需先改为 tr()）。

**Step 4: 添加 CMake 翻译编译目标**

```cmake
# 在 CMakeLists.txt 中添加
set(TS_FILES
    i18n/xlsone_zh_CN.ts
    i18n/xlsone_en.ts
)

qt_add_translations(xlsOneQt
    TS_FILES ${TS_FILES}
)
```

**Step 5: 在 main.cpp 中安装 QTranslator**

```cpp
// cpp/app/src/main.cpp
#include <QTranslator>
#include <QLocale>
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "xlsone_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }

    // ...
}
```

**Step 6: 编译验证 Qt 端**

```bash
cd cpp && mkdir -p build && cd build && cmake .. && make
```
Expected: Build SUCCESS with .qm files generated

**Step 7: Commit**

```bash
git add cpp/i18n/ cpp/app/src/main.cpp cpp/CMakeLists.txt
git commit -m "feat(i18n): add Qt translation infrastructure with en/zh_CN support"
```

---

### Task 8: C++/Qt 端遗漏的 UI 字符串 tr() 包装

**Files:**
- Modify: `cpp/app/src/license_activation_dialog.cpp`
- Modify: `cpp/core/src/license_manager.cpp`
- Modify: `cpp/core/src/validator.cpp`
- Modify: `cpp/core/src/models.cpp` (决策原因字符串)

**Step 1: license_activation_dialog.cpp — 将 QStringLiteral() 改为 tr()**

约 25 处 UI 字符串当前使用 `QStringLiteral()`。改为 `tr()` 调用以纳入翻译系统。

**Step 2: license_manager.cpp — 将 Unicode 转义字符串改为 tr()**

将 `"\u6fc0\u6d3b\u7801\u4e0d\u5b58\u5728"` 等改为 `tr("激活码不存在")`。

**Step 3: validator.cpp — 将硬编码消息改为 tr()**

将诊断消息字符串（如 "解析失败: %1"、"缺少工作表 %1"）包装为 `tr()`。

**Step 4: models.cpp — 将决策原因改为 tr()**

约 25 条决策原因（如 "所有有效来源均为数值，按求和处理"）当前硬编码，改为 `tr()`。

**Step 5: 重新运行 lupdate 更新 .ts 文件**

```bash
lupdate app/src/ core/src/ -ts i18n/xlsone_zh_CN.ts i18n/xlsone_en.ts
```

**Step 6: 编译验证并运行测试**

```bash
cd cpp/build && cmake .. && make && ctest
```
Expected: Build + Tests PASS

**Step 7: Commit**

```bash
git add cpp/app/src/license_activation_dialog.cpp cpp/core/src/license_manager.cpp cpp/core/src/validator.cpp cpp/core/src/models.cpp cpp/i18n/
git commit -m "fix(i18n): wrap remaining hardcoded UI strings with tr() in Qt"
```

---

### Task 9: C++/Qt 端算法关键词国际化

与 Swift 端类似，将 `models.cpp` 和 `merger.cpp` 中的语义关键词数组抽离为 JSON。

**Files:**
- Create: `cpp/i18n/algorithm_keywords.json`
- Create: `cpp/core/src/algorithm_i18n.hpp`
- Create: `cpp/core/src/algorithm_i18n.cpp`
- Modify: `cpp/core/src/models.cpp` (替换关键词数组引用)
- Modify: `cpp/core/src/merger.cpp` (替换关键词数组引用)

**Step 1: 创建算法关键词 JSON 文件**

与 Swift 端相同的 JSON 结构，复制到 `cpp/i18n/algorithm_keywords.json`。

**Step 2: 创建 C++ i18n 加载器**

```cpp
// cpp/core/src/algorithm_i18n.hpp
#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>

class AlgorithmKeywords {
public:
    static AlgorithmKeywords& instance();
    void setLanguage(const QString& lang);

    QVector<QString> amountPatterns;
    QVector<QString> weakAmountPatterns;
    QVector<QString> codePatterns;
    // ... all other keyword arrays

private:
    AlgorithmKeywords();
    void load(const QString& lang);
};
```

**Step 3: 替换 models.cpp 中的硬编码数组**

将 `models.cpp:207-270` 中的静态 QVector 初始化替换为 `AlgorithmKeywords::instance().amountPatterns` 等。

**Step 4: 替换 merger.cpp 中的硬编码数组**

将 `merger.cpp:153-181` 中的关键词数组替换为算法 i18n 引用。

**Step 5: 编译并运行测试**

```bash
cd cpp/build && cmake .. && make && ctest
```
Expected: All tests PASS

**Step 6: Commit**

```bash
git add cpp/i18n/algorithm_keywords.json cpp/core/src/algorithm_i18n.* cpp/core/src/models.cpp cpp/core/src/merger.cpp
git commit -m "feat(i18n): extract C++ algorithm keywords to language resource"
```

---

### Task 10: 英文翻译补全与整体验证

**Files:**
- Modify: `Sources/xlsOneCore/Resources/Localizable.xcstrings` (补全所有英文翻译)
- Modify: `cpp/i18n/xlsone_en.ts` (补全所有英文翻译)
- Modify: `cpp/i18n/algorithm_keywords.json` (确认英文关键词完整)

**Step 1: 逐文件审核 Swift String Catalog 翻译完整性**

确保 Localizable.xcstrings 中每个条目都有英文翻译（约 300+ 条目）。

**Step 2: 逐文件审核 Qt .ts 翻译完整性**

使用 `lupdate` 重新扫描确认无遗漏，并确保 `xlsone_en.ts` 中 `<translation type="unfinished">` 已全部替换。

**Step 3: 端到端测试（Swift 端）**

运行应用，在设置中将语言切换为英文，验证：
- 菜单项显示正确
- 工具栏按钮文字正确
- 弹窗/消息框文字正确
- 汇总结果中的类型标签（标签/求和/混合）显示正确
- 决策原因文字正确

**Step 4: 端到端测试（Qt 端）**

启动 Qt 应用并设置 `LANGUAGE=en`，验证所有 UI 显示正确。

**Step 5: 运行全部测试确保无回归**

```bash
# Swift
swift test

# C++/Qt
cd cpp/build && ctest
```
Expected: All tests PASS

**Step 6: Commit**

```bash
git add -A
git commit -m "feat(i18n): complete English translations for Swift and Qt"
```

---

### Task 11: 语言切换 UI 组件 (Swift 端)

**Files:**
- Create: `Sources/xlsOneUI/LanguagePickerView.swift`
- Modify: `Sources/xlsOneUI/ContentView.swift` (添加入口)

**Step 1: 创建语言选择器视图**

一个简单的 Picker 组件，列出 `LocaleManager.AppLanguage.allCases`，切换时调用 `LocaleManager.shared.apply()`。

**Step 2: 在菜单栏或设置面板中集成**

在 `WorkspaceScene.swift` 的 "帮助" 或 "文件" 菜单中添加语言切换入口。

**Step 3: 编译验证**

Run: `swift build`
Expected: Build SUCCESS

**Step 4: Commit**

```bash
git add Sources/xlsOneUI/LanguagePickerView.swift Sources/xlsOneUI/ContentView.swift
git commit -m "feat(i18n): add language switcher UI for Swift/macOS"
```

---

## 文件变更汇总

| 操作 | 文件 | 说明 |
|------|------|------|
| **新建** | `Sources/xlsOneCore/I18n/LocaleManager.swift` | Swift 语言管理器 |
| **新建** | `Sources/xlsOneCore/I18n/AlgorithmI18n.swift` | Swift 算法关键词加载器 |
| **新建** | `Sources/xlsOneCore/Resources/Localizable.xcstrings` | Swift String Catalog |
| **新建** | `Sources/xlsOneCore/Resources/AlgorithmKeywords.json` | 算法关键词 JSON |
| **新建** | `Sources/xlsOneUI/LanguagePickerView.swift` | 语言切换 UI |
| **修改** | `Sources/xlsOneCore/Models.swift` | 替换硬编码关键词和决策原因 |
| **修改** | `Sources/xlsOneCore/SimpleMerger.swift` | 替换硬编码关键词 |
| **修改** | `Sources/xlsOneCore/ColumnTypeAnalyzer.swift` | 替换硬编码关键词 |
| **修改** | `Sources/xlsOneUI/ContentView.swift` | 国际化 UI 字符串 |
| **修改** | `Sources/xlsOneUI/WorkspaceScene.swift` | 国际化菜单/弹窗 |
| **修改** | `Sources/xlsOneUI/WorkspaceModels.swift` | 国际化状态文本 |
| **修改** | `Sources/xlsOneUI/SchemaManagerView.swift` | 国际化 schema 管理 |
| **修改** | `Sources/xlsOneLicense/LicenseActivationView.swift` | 国际化许可证 UI |
| **修改** | `Sources/xlsOneLicense/LicenseManager.swift` | 国际化错误描述 |
| **修改** | `Package.swift` | 添加资源声明 |
| **新建** | `cpp/i18n/xlsone_zh_CN.ts` | Qt 中文翻译源文件 |
| **新建** | `cpp/i18n/xlsone_en.ts` | Qt 英文翻译源文件 |
| **新建** | `cpp/i18n/algorithm_keywords.json` | C++ 算法关键词 JSON |
| **新建** | `cpp/core/src/algorithm_i18n.hpp` | C++ 算法 i18n 头文件 |
| **新建** | `cpp/core/src/algorithm_i18n.cpp` | C++ 算法 i18n 实现 |
| **修改** | `cpp/CMakeLists.txt` | 添加翻译编译目标 |
| **修改** | `cpp/app/src/main.cpp` | 安装 QTranslator |
| **修改** | `cpp/app/src/license_activation_dialog.cpp` | QStringLiteral -> tr() |
| **修改** | `cpp/core/src/license_manager.cpp` | Unicode 转义 -> tr() |
| **修改** | `cpp/core/src/validator.cpp` | 诊断消息 -> tr() |
| **修改** | `cpp/core/src/models.cpp` | 决策原因 -> tr() + 算法关键词替换 |
| **修改** | `cpp/core/src/merger.cpp` | 算法关键词替换 |
