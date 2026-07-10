# 国内 Windows 版「授权与分发」实施计划

> 目标：让 xlsOne（国内品牌名「表表归一」）的 Windows 版本能在中国大陆稳定地完成**激活/授权**与**下载/分发**。
> 适用范围：国内 Windows 用户（域名 `z-pulse.cn`）。

---

## 一、现状盘点

### 已完成（无需从零做）

| 模块 | 状态 | 证据 |
|------|------|------|
| Windows 客户端可编译 | ✅ 已产出 `xlsOneQt.exe`（Qt 6.11.1 / MinGW 13.1，构建目录 `build-windows-cn-release`） | `cpp/build-windows-cn-release/app/xlsOneQt.exe` |
| Windows 授权引擎 | ✅ Ed25519 签名校验、设备绑定、换机容差（2/3 组件匹配）、14 天试用、离线导入 | `cpp/core/src/license_manager.cpp` |
| 激活对话框 UI | ✅ 在线激活 + 试用按钮 + 离线激活（含设备码展示/复制/导入） | `cpp/app/src/license_activation_dialog.cpp` |
| 安装包（MSI/ZIP） | ✅ WiX + CPack 可产出 MSI 与便携 ZIP | `cpp/scripts/package_windows_full.ps1`、`cpp/packaging/windows/` |
| 服务端（激活 API） | ✅ Cloudflare Worker 已实现 `/api/activate/windows`、`/api/license/download`、爱发电 webhook、管理后台生成码 | `activation/worker/src/index.ts`、`schema.sql` |
| 更新检查 | ✅ 已指向 `z-pulse.cn`（`XLSONE_UPDATE_BASE_URL`） | `moc_predefs.h` 中 `#define XLSONE_UPDATE_BASE_URL "https://z-pulse.cn"` |
| EULA / 软著材料 | ✅ 中文 EULA 已存在；软著资料生成中 | `cpp/packaging/windows/licenses/EULA_zh_CN.txt`、`软件著作权申请资料/` |

### 关键缺口（针对「国内」）

1. **【阻断】激活/离线/支付链路全部跑在 Cloudflare（`api.xlsone.com`）** —— 大陆网络不可达或不稳定，`wrangler.toml` 注释已明确「国内中文版由国内节点承载，不挂在此 Worker 上」。
2. **客户端激活 URL 硬编码** `https://api.xlsone.com/api/activate/windows`（`license_manager.cpp:385`），无国内切换。
3. **无国内激活后端**：没有阿里云 FC / 函数计算部署，也没有国内数据库（D1 在 Cloudflare）。
4. **离线激活页 `z-pulse.cn/offline` 未搭建**：当前对话框只写「联系 831261@qq.com 获取离线授权页面地址」，依赖人工。
5. **无代码签名证书**：未签名的 `.exe`/`.msi` 会被 Windows SmartScreen 拦截，用户无法放心安装（这是「分发」的硬门槛）。
6. **无国内下载分发渠道**：`site/downloads/` 仅有 Linux `.deb`，没有 Windows 安装包托管。
7. **国内产品页 / 定价页缺失**：`z-pulse.cn.txt` 仅是占位（`root / Wang@703711!`），并非真实站点。
8. **爱发电 webhook 在 Cloudflare 上**：国内购买回调与访问不稳定。
9. **用户协议 & 隐私政策（中文正式版）待撰写**。
10. **试用到期功能封锁是否真正生效需验证**（代码有 `Trial` 状态，但需确认未激活/过期时是否限制功能）。

---

## 二、总体架构建议

**一套代码库，双构建口味（region flavor）：**

```
国际口味：域名 api.xlsone.com（Cloudflare）  → macOS + 海外 Windows
国内口味：域名 api.z-pulse.cn（阿里云 FC 等）→ 国内 Windows（后续可覆盖国内 macOS）
```

- 参照已有的 `XLSONE_UPDATE_BASE_URL`，新增编译期定义 `XLSONE_ACTIVATION_BASE_URL`：
  - 国际 = `https://api.xlsone.com`
  - 国内 = `https://api.z-pulse.cn`
  - 客户端据此拼出 `/api/activate/windows` 等路径；离线导入作为兜底始终保留。
- **授权密钥体系不变**：继续用 Windows 专用 `XLS1-XXXX-XXXX` 与 `windows_keys` 表；国内/国际可共用同一 Ed25519 密钥对（私钥安全迁移到国内密钥库，客户端公钥已固化，无需改客户端）。如要隔离风控再单独轮换。

---

## 三、分阶段实施计划

### 阶段 0 — 决策与准备（1–2 天）
- [ ] 确定国内激活后端技术栈：**阿里云函数计算 FC + 国内数据库**（RDS MySQL / PolarDB，或 FC 配套的 TableStore）。D1(SQLite) 语法需改写适配。
- [ ] 确定代码签名方案：**EV 证书**（Instant SmartScreen 信誉，贵）vs **普通代码签名证书**（便宜，需累计信誉）。建议 v1 先普通证书，后期升 EV；国内可选沃通/TrustAsia，国际可选 Sectigo/Comodo。
- [ ] 确定支付：先沿用**爱发电**（已接），必要时补微信/支付宝直连或「小鹅通」。v1 用爱发电 + 手动发码兜底。
- [ ] 锁定品牌与域名：**表表归一 / z-pulse.cn**，并**立即启动 ICP 备案**（周期长，前置条件）。
- [ ] 决策：国内与国际的密钥/数据是否共享（影响换机统计与风控）。

### 阶段 1 — 国内激活后端（核心，约 1 周）
- [ ] 将 `activation/worker` 逻辑移植到 FC + 国内 DB，保留全部 Windows 路由：`/api/activate/windows`、`/api/license/download`、`/api/webhook/afdian`、`/api/admin/generate-windows-keys`。
- [ ] 改写 `schema.sql` 与 SQL：D1 的 `datetime('now')` / `ON CONFLICT` 等语法适配 MySQL。
- [ ] **Ed25519 私钥安全迁移**到国内密钥库（绝不明文入库/提交）；客户端公钥 `kLicensePublicKey` 保持不变。
- [ ] 部署 `api.z-pulse.cn` 并配置 HTTPS（国内备案域名）。

### 阶段 2 — 客户端改造（约 2–3 天）
- [ ] `license_manager.cpp`：激活 URL 改为由 `XLSONE_ACTIVATION_BASE_URL` 决定（国内口味指向 `api.z-pulse.cn`），保留离线导入兜底。
- [ ] 对话框中「离线激活」步骤改为指向 `z-pulse.cn/offline`（去掉「联系邮箱」人工步骤）。
- [ ] 验证：国内口味构建在大陆网络下可完成**在线激活、离线下载、换机迁移**。
- [ ] 验证**试用到期封锁逻辑**生效 + 14 天试用按钮完整流程。
- [x] 锁定 Windows 发货目标为 **Qt 6.11.1**（打包脚本 `package_windows_full.ps1` 面向 Qt6，Qt5 工具链已弃用）。

### 阶段 3 — 离线激活页 & 产品页（约 1 周）
- [ ] 搭建 `z-pulse.cn/offline`：用户输入设备码 → 调国内 `/api/license/download` → 下载 `.license`（与客户端 `importOfflineLicenseFile` 兼容）。
- [ ] 搭建 `z-pulse.cn` 国内产品首页 + 定价页（Windows 买断 `personal_lifetime`，可选 `enterprise_10`）。
- [ ] 撰写**用户协议 & 隐私政策**（中文正式版）页面。
- [ ] 打通购买链路：爱发电下单 → 国内 webhook → 发码（私信/邮件）全链路联调。

### 阶段 4 — 代码签名 + 安装包 + 分发（约 3–5 天）
- [ ] 采购代码签名证书，对 `xlsOneQt.exe` 与最终 `MSI` 签名（建议二者都签）。
- [ ] 用 `package_windows_full.ps1` 产出**已签名** MSI + ZIP。
- [ ] 部署到国内下载渠道：**阿里云 OSS + CDN**（或 `z-pulse.cn` 自托管 + 已有 `nginx.conf`），更新下载页与 `version.json`（更新检查已指向 `z-pulse.cn`）。
- [ ] 确认 Windows 端自动更新的下载链接走国内地址。

### 阶段 5 — 合规与上线（持续）
- [ ] 软件著作权登记提交（资料已生成于 `软件著作权申请资料/正式资料/`）。
- [ ] ICP 备案完成；如有要求做等保。
- [ ] 反盗版（代码混淆）列入后期。

---

## 四、最小可上线 MVP（先跑通这 5 步）

1. 国内激活后端（FC + 国内 DB）部署 `api.z-pulse.cn`。
2. 客户端改为**可配置激活域名**（国内口味 = `api.z-pulse.cn`）。
3. 离线激活页 `z-pulse.cn/offline`（绕开 Cloudflare 依赖，兜底必备）。
4. 代码签名 + 国内下载托管（OSS/CDN）。
5. 产品页 + 爱发电购买发码闭环。

> 这 5 步完成即可在国内「能买、能激活、能下载、能安装」，其余为体验与合规增强。

---

## 五、风险与注意事项

- **Cloudflare 在大陆不可达是硬约束**：必须先把激活后端搬回国内，否则「国内分发」无从谈起。
- **Ed25519 密钥对**：客户端公钥已固化，国内后端**必须用同一私钥**；迁移时务必保密。如需轮换，要同步更新客户端并提醒已激活用户重新激活。
- **域名备案是前置条件且周期长**，应阶段 0 立即启动，否则 `z-pulse.cn` 大陆无法访问。
- **SmartScreen 信誉**：新证书初期仍可能报「未知发布者」，建议引导「仍要运行」并尽快累计下载量；条件允许直接上 EV。
- **国内/国际数据是否共享**需尽早决策，影响换机次数统计与风控策略。
- Windows 构建目标已统一为 **Qt 6.11.1 + MinGW 13.1**（`build-windows-cn-release`），旧的 `build-windows-qt5-release` 为过期缓存，可删除。

---

## 六、与现有 TODO.md 的对应关系

| 本计划 | TODO.md 条目 |
|--------|--------------|
| 阶段 1 国内后端 | 阿里云函数计算 FC 国内激活端点部署 |
| 阶段 2 客户端改造 | Windows 端 License 校验集成（基本已完成，补国内域名+试用封锁） |
| 阶段 3 离线页/产品页 | z-pulse.cn/offline 离线授权文件生成页面；z-pulse.cn 国内产品首页；定价页发布 |
| 阶段 3 支付 | 爱发电 webhook 实际对接测试；激活码购买后自动邮件/私信发送 |
| 阶段 4 签名+安装包 | Windows 安装包制作；Windows 端 License 校验集成 |
| 阶段 4 分发 | （新增）国内下载托管 |
| 阶段 5 合规 | 用户协议 & 隐私政策撰写；软件著作权登记 |
| 阶段 2 验证 | 14 天免费试用模式实现；激活窗口 UI 完善 |
