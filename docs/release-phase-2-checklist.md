# App Store 第二阶段待补项

更新时间：2026-04-28

## 当前结论

工程侧的第一批阻断项已经基本收口：`Bundle ID` 已切到 `com.xlsone.app`，公开文案已从 `.xlsx` only 更新为 `.xlsx` / 常见 BIFF8 `.xls`，release 下的 cursor debug 日志默认关闭，签名验证脚本已支持真实 Team 注入。

第二阶段的目标是把 App Store Connect 需要的素材、页面、联系人和合规答案整理到可提交状态。

## 仍需你提供

- Apple Developer Team ID
- App Store Connect 登录与 App 创建权限
- 是否用个人开发者名义还是组织名义发布
- 最终品牌图标或确认继续使用当前临时图标

已完成：

- `https://xlsone.com/` 已上线并绑定 Cloudflare Pages
- `https://xlsone.com/support/` 可作为 App Store `Support URL`
- `https://xlsone.com/privacy/` 可作为 App Store `Privacy Policy URL`
- `support@xlsone.com` 与 `privacy@xlsone.com` 已转发到 `831261@qq.com` 并验证可收信

## App Store Connect 字段草稿

- App 名称：`xlsOne`
- Subtitle：`Excel 汇总与复核工作台`
- Bundle ID：`com.xlsone.app`
- SKU：`xlsone-macos`
- Primary Category：`Productivity`
- Secondary Category：`Business`
- Marketing URL：`https://xlsone.com`
- Support URL：`https://xlsone.com/support/`
- Privacy Policy URL：`https://xlsone.com/privacy/`

## 描述草稿

### 简短描述

xlsOne 是一款面向 macOS 的本地优先 Excel 汇总工具，帮助你把多个结构一致的 `.xlsx` 或常见 `.xls` 工作簿合并、复核并导出为新的 `.xlsx` 汇总文件。

### 完整描述

xlsOne 为需要反复整理结构一致 Excel 报表的团队准备。你可以一次导入多个 `.xlsx` 或常见 BIFF8 `.xls` 工作簿，按工作表结构进行对齐，查看汇总结果，并在异常单元格上追溯每个来源文件中的原始值。

主要能力：

- 多个 `.xlsx` / `.xls` 工作簿导入与追加
- 按工作表结构合并可对齐文件
- 数值汇总、差异提示与来源追溯
- 导出新的 `.xlsx` 汇总工作簿
- 本地优先处理，默认不上传用户工作簿到远端服务器

当前 `.xls` 导入为原生 BIFF8 解析，不依赖外部转换工具；公式单元格读取工作簿内的缓存结果，不在应用内重新计算公式。

## 关键词草稿

`Excel, XLSX, spreadsheet, merge, workbook, finance, report, macOS, 汇总, 表格`

## 截图清单

至少准备以下 macOS 截图，尺寸按 App Store Connect 当前要求导出：

- 空工作区和导入入口
- 多文件导入后的文件列表
- 汇总表格视图
- 单元格来源追溯面板
- 导出成功或导出入口

截图前确认：

- 不出现真实敏感数据
- 可展示 `.xlsx` 与常见 `.xls`；如展示公式单元格，应确认缓存值与预期一致
- 关闭调试日志、调试菜单和终端窗口
- 菜单栏、窗口标题和按钮文案保持正式发布语气

## App Review 信息

- 登录账号：不需要账号
- 审核说明：App 在本地处理用户主动选择的 `.xlsx` 或常见 `.xls` 工作簿，不需要登录、不依赖服务器，不内置第三方广告或分析 SDK。
- 测试建议：审核人员可准备两个结构一致的 `.xlsx` 或 `.xls` 文件，导入后查看汇总表格，再点击单元格查看来源值，最后导出新的 `.xlsx` 工作簿。

## 隐私与合规

- 数据收集：当前版本不要求创建账号，不内置第三方广告或行为分析 SDK。
- 用户文件：导入的工作簿默认在本机处理，不因正常使用流程上传到 xlsOne 服务器。
- 支持邮件：如果用户主动发送支持邮件，会收到用户在邮件里提供的联系信息和问题描述。
- Export Compliance：`ITSAppUsesNonExemptEncryption` 已设置为 `false`，最终仍需在 App Store Connect 按实际加密使用情况确认。
- 年龄分级：预计为最低风险档，但需要在 App Store Connect 中按问卷逐项确认。

## 发布前验证

本地无签名验证：

```bash
swift test
./scripts/validate_xcode_app.sh
./scripts/archive_xcode_app.sh
```

拿到 Team ID 后的签名验证：

```bash
./scripts/validate_xcode_app.sh --signed --team-id YOURTEAMID
./scripts/archive_xcode_app.sh --signed --team-id YOURTEAMID
```

也可以使用环境变量：

```bash
XLSONE_DEVELOPMENT_TEAM=YOURTEAMID ./scripts/validate_xcode_app.sh --signed
XLSONE_DEVELOPMENT_TEAM=YOURTEAMID ./scripts/archive_xcode_app.sh --signed
```

签名归档完成后，还需要在 Xcode Organizer 里执行 `Validate App`，再上传到 App Store Connect。
