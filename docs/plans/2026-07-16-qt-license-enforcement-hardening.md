# Qt 版授权功能限制加固计划

## 1. 体检结论（当前状态）

### 1.1 授权状态机
- `cpp/core/include/xlsone/core/license_manager.hpp` 已定义 `LicenseState`：
  - `Unactivated`（未激活）
  - `Activated`（已激活）
  - `Expired`（已过期）
  - `Trial`（试用中）
- `LicenseManager` 已具备：在线激活、14 天试用申请、离线授权导入、设备指纹、签名验证、宽限期计算。

### 1.2 已发现的缺陷
| 缺陷 | 位置 | 影响 |
|------|------|------|
| **无功能限制入口** | `main_window.cpp` | `loadFiles`、`exportResult` 未读取 license state，未激活/过期均可全功能使用 |
| **导出水印未启用** | `main_window.cpp` → `exporter.cpp` | `exportWorkbook` 的 `addExpiredWatermark` 参数永远为 `false` |
| **过期状态丢失** | `license_manager.cpp` | 授权过期后 `loadPersistedState()` 会清除 license 并回退到 `Unactivated`，用户看不到「已过期」提示 |
| **顶部授权按钮未连接** | `main_window.cpp` | `WorkspaceChrome::licenseRequested` 信号未连接（已在本会话临时修复） |
| **ARM64 菜单被删除** | `main_window.cpp` | 「许可」菜单在 Apple Silicon 上被条件编译删除（已在本会话临时修复） |
| **客户端无能力查询 API** | `license_manager.hpp` | 缺少 `canExport()`、`maxImportFiles()` 等统一判断接口，各 UI 点容易遗漏 |

### 1.3 与 SwiftUI / Windows 版对比
- SwiftUI 版同样只有状态展示，**没有真正实施功能限制**。
- Windows Qt 版与国内文档已明确记录：「试用到期封锁逻辑待验证」。
- 收费策略报告写明：Linux / UOS 免费；macOS / Windows 付费；14 天全功能试用；导出水印「暂不实施」。

---

## 2. 加固目标

让 Qt 版（macOS/Windows）在 **未激活** 和 **已过期** 状态下进入「受限模式」：

1. **导入限制**：单次最多处理 3 个文件（与现有 UI 文案一致）。
2. **导出限制**：
   - 未激活：导出 XLSX 时添加半透明水印 / 页脚声明。
   - 过期：同未激活，或完全禁用导出（需产品决策）。
3. **试用状态**：14 天内全功能，过期后回退到受限模式。
4. **已激活 / 宽限期内**：全功能，无水印。
5. **用户体验**：在受限时给出明确引导（弹窗或状态栏提示），而非静默失败。

---

## 3. 建议的权限模型

新增 `LicenseCapability` 查询层，集中在 `LicenseManager`，避免各 UI 点重复判断。

```cpp
namespace xlsone {

enum class LicenseFeature {
    ImportFiles,      // 导入/追加文件
    BatchExport,      // 导出汇总结果
    NoExportWatermark // 导出无水印
};

class LicenseManager {
public:
    // 当前是否处于“全功能”状态
    bool isFullyLicensed() const;

    // 是否在宽限期内（过期但仍有完整功能）
    bool isInGracePeriod() const;

    // 统一权限判断
    bool canUse(LicenseFeature feature) const;

    // 单次最多可导入文件数；0 表示禁止导入
    int maxImportFiles() const;

    // 导出时应添加的水印文本；空字符串表示无水印
    QString exportWatermarkText() const;

    // 受限状态下的用户提示文案
    QString restrictionMessage() const;
};

} // namespace xlsone
```

### 状态 → 能力映射

| 状态 | `isFullyLicensed` | `maxImportFiles` | `canUse(BatchExport)` | 水印 |
|------|-------------------|------------------|-----------------------|------|
| Activated | ✅ | 无限制 | ✅ | 无 |
| Trial（剩余 > 0） | ✅ | 无限制 | ✅ | 无 |
| GracePeriod / Expired 宽限 | ✅ | 无限制 | ✅ | 无 |
| Expired（宽限结束） | ❌ | 3 | ✅ | 「授权已过期 — xlsOne」 |
| Unactivated | ❌ | 3 | ✅ | 「未激活试用版 — xlsOne」 |

> 注：若产品决策要求“过期后完全禁止导出”，可将 `canUse(BatchExport)` 在 Expired 时设为 ❌。

---

## 4. 代码改造清单

### 4.1 核心层：`LicenseManager` 增加能力 API

**文件**：
- `cpp/core/include/xlsone/core/license_manager.hpp`
- `cpp/core/src/license_manager.cpp`

**改动**：
1. 新增 `LicenseFeature` 枚举与 `canUse()`、`maxImportFiles()`、`exportWatermarkText()`、`restrictionMessage()`。
2. 修复 `loadPersistedState()`：
   - 签名/设备有效但已过期时，保持 `Expired` 状态并保留 `currentInfo_`（当前代码已在 `loadPersistedState()` 后半段实现，但前面的失败分支会提前 return，需整理顺序）。
   - 添加「宽限期」逻辑：过期 7 天内视为 `Expired` 但 `isInGracePeriod() == true`。
3. 确保 `checkTrial()` 返回 `0` 时不会错误地让 `Trial` 状态继续被视为全功能。

### 4.2 UI 层：主窗口限制入口

**文件**：`cpp/app/src/main_window.cpp`

**改动**：
1. `loadFiles()` / `appendFiles()`：
   - 计算 `selectedPaths_.size() + paths.size()` 是否超过 `licenseManager_->maxImportFiles()`。
   - 超过时弹窗提示：`restrictionMessage()` + 「激活/试用」按钮。
2. `exportResult()`：
   - 若 `!licenseManager_->canUse(BatchExport)`，弹窗引导购买。
   - 否则调用 `TemplateWorkbookExporter().exportWorkbook(..., addExpiredWatermark = !licenseManager_->canUse(NoExportWatermark))`。
3. 首次启动时：
   - 若 `Unactivated` 且非 App Store 分发，自动弹出 `LicenseActivationDialog` 引导试用/激活（与 SwiftUI 行为对齐）。

### 4.3 UI 层：工具栏状态同步

**文件**：`cpp/app/src/workspace_chrome.cpp`

**改动**：
1. `updateLicenseStatus()` 增加受限模式样式（红色 / 橙色提示）。
2. 点击受限状态下的「开始试用」按钮时，若已受限，打开授权窗口。

### 4.4 导出器：水印实现

**文件**：`cpp/core/src/exporter.cpp`

**改动**：
1. 当前 `addExpiredWatermark` 只在第一行后插入水印行，需改为：
   - 在工作表顶部增加一行醒目的水印文本；
   - 或在页眉/页脚中插入水印声明（更轻量，不影响数据行列）。
2. 建议同时支持「水印行」和「CSV 模式下的文件名后缀/注释」两种导出路径。

### 4.5 激活对话框：状态感知

**文件**：`cpp/app/src/license_activation_dialog.cpp`

**改动**：
1. `buildOnlinePage()` 中，若状态为 `Expired` 或 `Unactivated`，顶部 banner 显示 `restrictionMessage()`。
2. 试用成功后刷新主窗口状态。

### 4.6 服务端兼容（无需改动，但需确认）

- 国内/国际服务器均已实现 `/api/trial/windows`、`/api/activate/windows`。
- 签名公钥已硬编码在 `license_manager.cpp`，需确保与国内 Worker 私钥匹配。
- 客户端已支持 `--domestic` 构建参数（本会话新增）。

---

## 5. 测试验收标准

### 5.1 单元测试
在 `cpp/tests/core_tests.cpp` 或新增 `license_manager_tests.cpp` 中覆盖：

| 用例 | 期望 |
|------|------|
| `Activated` 状态 | `isFullyLicensed()==true`，`maxImportFiles()==INT_MAX`，无水印 |
| `Trial` 剩余 5 天 | 全功能，无水印 |
| `Expired` 宽限期内 | 全功能，无水印 |
| `Expired` 宽限结束 | `maxImportFiles()==3`，有水印 |
| `Unactivated` | `maxImportFiles()==3`，有水印 |
| 签名有效但时钟回拨 | 按现有逻辑返回 `Expired` 或错误提示 |

### 5.2 手动测试
1. 全新安装（Unactivated）：
   - 导入 3 个文件，正常汇总导出，检查水印。
   - 导入第 4 个文件，提示受限。
2. 申请 14 天试用：
   - 试用期间全功能、无水印。
   - 修改系统时间到 15 天后，重启应用，进入受限模式。
3. 激活永久授权：
   - 输入激活码后全功能、无水印。
4. 离线授权导入：
   - 导入 `.license` 后状态变为 Activated。

---

## 6. 实施优先级

| 优先级 | 任务 | 估计工时 |
|--------|------|----------|
| P0 | 修复 `LicenseManager` 过期状态保留 + 新增能力 API | 半天 |
| P0 | `main_window.cpp` 导入 3 文件限制 + 导出限制 | 半天 |
| P1 | 导出器水印增强（更美观、不破坏数据） | 半天 |
| P1 | 首次启动自动弹授权窗 | 2 小时 |
| P2 | 单元测试覆盖 | 半天 |
| P2 | 工具栏状态样式优化 | 2 小时 |

---

## 7. 风险与待决策

1. **导出策略**：过期后是「允许导出但带水印」还是「完全禁止导出」？
   - 建议 P0 采用「允许导出但带水印」，降低用户流失。
2. **Linux / UOS 策略**：当前 `loadOrCreateLinuxDefaultLicense()` 已让 Linux 默认 Activated，应保持不变。
3. **App Store 分发**：SwiftUI 版有 `isAppStoreDistribution` 判断；Qt 版暂无此概念。若未来 Qt 版也上 App Store，需要类似豁免。
4. **防绕过**：客户端限制可被技术手段绕过；本计划属于「轻度保护」，不追求绝对安全，重点在引导转化。

---

## 8. 下一步行动

待你确认方案后，按以下顺序实施：
1. 实现 `LicenseManager` 能力 API 并修复过期状态。
2. 在 `MainWindow` 加入导入/导出限制。
3. 增强导出器水印。
4. 补充单元测试与手动测试。
