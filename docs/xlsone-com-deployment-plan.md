# xlsone.com 部署方案

更新时间：2026-04-27

## 1. 目标

- 让 `xlsone.com` 成为 `xlsOne` 的正式品牌域名
- 满足 macOS App Store 提审所需的 `Marketing URL`、`Support URL`、`Privacy Policy URL`
- 尽量不引入后端、数据库和额外运维复杂度
- 保持后续可扩展：未来如果需要博客、更新日志、下载页或帮助中心，不必推倒重来

## 2. 当前状态

- 域名 `xlsone.com` 已注册在 Namecheap
- 当前截图显示域名仍使用 `Namecheap BasicDNS`
- 仓库里已新增 `site/` 静态站点，下一步是接入 Cloudflare Pages
- App 侧仍有几个发布前必须收口的问题：
  - `project.yml` 已切到 `com.xlsone.app`，并通过 `XLSONE_DEVELOPMENT_TEAM` 注入正式 Team
  - 用户入口与公开文案已收敛为 `.xlsx`，如要支持 `.xls` 仍需补解析能力
  - 发布脚本已提供 `--signed` 模式，仍需用真实 Team 和证书跑一次完整签名归档与 Xcode Organizer Validate App
  - cursor debug 日志已默认关闭，只有在调试场景显式开启时才写入

## 3. 推荐架构

### 核心建议

- 域名注册商继续保留在 Namecheap，不做转移
- 权威 DNS 切到 Cloudflare Free
- 官网使用 Cloudflare Pages 托管静态站点
- 邮件先用 Cloudflare Email Routing 做转发
- 网站先做成纯静态页面，不上后端
- 官网主域名使用 `https://xlsone.com`
- `https://www.xlsone.com` 统一 301 跳转到主域名

### 为什么这样选

- 你们现在最急的是“上架需要的网址与联系入口”，不是做复杂 Web 产品
- Cloudflare 把 DNS、HTTPS、CDN、静态托管和来信转发放在同一个地方，后续维护成本最低
- 静态站点足够承载首页、支持页、隐私页、更新日志和 FAQ
- Namecheap 继续只承担“持有域名”角色，职责清晰

## 4. 目标信息架构

建议把公开站点最少做成下面 4 个页面：

- `/`
  - 产品介绍
  - 主要功能
  - 支持的平台
  - App Store 按钮或“即将上线”提示
- `/support`
  - 联系邮箱
  - 常见问题
  - 支持的文件格式说明
  - 故障排查
- `/privacy`
  - 隐私政策
  - 数据处理说明
  - 联系方式
- `/changelog`
  - 版本更新记录
  - 重要修复与功能变化

## 5. App Store 对应关系

建议在 App Store Connect 中这样配置：

- `Marketing URL`: `https://xlsone.com`
- `Support URL`: `https://xlsone.com/support`
- `Privacy Policy URL`: `https://xlsone.com/privacy`

补充说明：

- `Support URL` 不能只是一个空白页，应该包含真实可联系信息
- `Privacy Policy URL` 对 macOS App 是必填
- 如果首页暂时还没写完，也不要拿“正在建设中”页面去顶 `support` 或 `privacy`

## 6. Bundle ID 与品牌建议

基于你们已经拥有 `xlsone.com`，建议把命名空间定为：

- 组织前缀：`com.xlsone`
- 当前 macOS App 的 `Bundle ID`：`com.xlsone.app`

这样做的好处是：

- 和域名所有权一致
- 以后如果扩展新应用，还能继续使用 `com.xlsone.*`
- 比 `com.example.xlsone` 更适合正式提审

## 7. DNS 与邮件方案

### 推荐路径

1. 在 Cloudflare 添加 `xlsone.com`
2. 让 Cloudflare 扫描现有记录
3. 如果 Namecheap 开启了 DNSSEC，先关闭
4. 把 Namecheap 的 nameserver 改成 Cloudflare 分配的 nameserver
5. 等待解析生效后，在 Cloudflare 里管理站点与邮件

### 建议的公开入口

- `xlsone.com`：官网主入口
- `www.xlsone.com`：跳转到 `xlsone.com`

### 建议的邮箱别名

- `support@xlsone.com`
- `privacy@xlsone.com`
- `hello@xlsone.com`

### 邮件策略

- 第一阶段只做“来信转发”就够了
- 把以上地址统一转发到你们现有常用邮箱
- 如果后面需要“从 `@xlsone.com` 主动发信”，再补一个真正的邮箱服务

## 8. 网站技术方案

### 第一阶段建议

- 使用当前仓库的 `site/` 目录
- 使用纯静态 HTML/CSS/JS 或极轻量静态构建方式
- 不引入数据库
- 不做登录
- 不做 SSR

### 这样做的原因

- 页面数量少
- 内容以说明文案为主
- 上架需要的是稳定、可访问、加载快、容易改
- 当前没有证据表明必须引入更重的 Web 框架

### 后续可升级方向

如果以后你们要加博客、文档或多语言，再把 `site/` 升级为 Astro 这类静态站点方案即可；托管层不需要变。

## 9. 页面内容要求

### 首页

- 一句话说明产品定位
- 3 到 5 个核心卖点
- 截图或界面预览
- 明确说明平台是 macOS
- 放置 App Store 按钮或占位入口

### 支持页

- 支持邮箱
- 响应时间说明
- 支持的文件格式
- 最低系统版本
- 常见问题

注意：

- 站点和支持页应说明当前支持 `.xlsx` 与常见 BIFF8 `.xls`，导出统一为 `.xlsx`

### 隐私页

建议按“最少但真实”的原则写清楚：

- 是否上传用户文件到服务器
- 是否收集个人信息
- 是否使用第三方分析
- 是否保留崩溃或诊断信息
- 用户如何联系你们处理隐私请求

如果当前产品完全本地运行、无账号、无云同步、无第三方分析，就把这件事说清楚，这反而是优点。

## 10. 发布执行顺序

### Phase 0：先修 App 侧阻断项

1. 把 `project.yml` 中的 `Bundle ID` 改成正式值
2. 在 Apple Developer 后台注册同名 `Bundle ID`
3. 统一产品文案，只宣传当前真实支持的文件格式
4. 关闭 release 下的 debug 日志
5. 补一轮真实签名归档验证

### Phase 1：域名基础设施

1. 开通 Cloudflare
2. 切换 `xlsone.com` 的 nameserver
3. 配置 Pages 项目
4. 配置 `xlsone.com` 与 `www.xlsone.com`
5. 配置邮件转发

### Phase 2：官网最小上线版本

1. 首页
2. 支持页
3. 隐私页
4. 基础 SEO 信息
5. 站点图标、Open Graph 图、favicon

### Phase 3：提审联动

1. 在 App Store Connect 填入三个 URL
2. 准备截图、描述、关键词
3. 配置 TestFlight 测试信息与反馈邮箱
4. 上传真实签名构建
5. 先跑 `Validate App`，再提交审核

### Phase 4：上线后补强

1. 增加 `/changelog`
2. 增加 FAQ
3. 增加简单下载/更新说明
4. 如果邮件量上来，再升级为正式邮箱服务

## 11. 预计工期

如果不做复杂设计，按最小可发布范围估算：

- 域名与 DNS 切换：0.5 天
- 静态站点首版：1 天
- 支持页与隐私页文案：0.5 天
- App Store 元数据联动：0.5 天
- 真机签名与提审前验证：0.5 天

总计约 2.5 到 3 天，可进入“可提审状态”。

## 12. 风险与规避

### 风险 1：切 nameserver 时站点或邮件短时异常

- 规避：先在 Cloudflare 完整录入记录，再切换 nameserver
- 规避：低峰时段切换，切换前截图保存 Namecheap 当前记录

### 风险 2：隐私页写得太乐观，和产品真实行为不一致

- 规避：先按实际代码行为写
- 规避：后续只在功能变化时同步更新

### 风险 3：支持页宣传 `.xls`，但程序打不开

- 规避：在真正补上 `.xls` 解析前，全站统一写 `.xlsx`

### 风险 4：只有转发邮箱，没有品牌发件能力

- 规避：第一阶段先满足“收件可达”
- 规避：第二阶段再补发信能力，不影响提审

## 13. 推荐的下一步

最合理的执行顺序是：

1. 先把 `Bundle ID` 定成正式值
2. 再把 DNS 切到 Cloudflare
3. 然后上线 `xlsone.com` 的三页最小站点
4. 最后把这三个 URL 回填到 App Store Connect 并做提审验证

## 14. 参考文档

- Apple App Store Connect `App information`: [https://developer.apple.com/help/app-store-connect/reference/app-information/app-information](https://developer.apple.com/help/app-store-connect/reference/app-information/app-information)
- Apple `Platform version information`: [https://developer.apple.com/help/app-store-connect/reference/app-review-information](https://developer.apple.com/help/app-store-connect/reference/app-review-information)
- Apple `TestFlight Overview`: [https://developer.apple.com/help/app-store-connect/test-a-beta-version/testflight-overview/](https://developer.apple.com/help/app-store-connect/test-a-beta-version/testflight-overview/)
- Apple `Overview of export compliance`: [https://developer.apple.com/help/app-store-connect/manage-app-information/overview-of-export-compliance/](https://developer.apple.com/help/app-store-connect/manage-app-information/overview-of-export-compliance/)
- Cloudflare `Onboard a domain`: [https://developers.cloudflare.com/fundamentals/manage-domains/add-site/](https://developers.cloudflare.com/fundamentals/manage-domains/add-site/)
- Cloudflare `Set up a primary zone`: [https://developers.cloudflare.com/dns/zone-setups/full-setup/setup/](https://developers.cloudflare.com/dns/zone-setups/full-setup/setup/)
- Cloudflare Pages `Custom domains`: [https://developers.cloudflare.com/pages/configuration/custom-domains/](https://developers.cloudflare.com/pages/configuration/custom-domains/)
- Cloudflare Email Routing `Overview`: [https://developers.cloudflare.com/email-routing/](https://developers.cloudflare.com/email-routing/)
- Namecheap `How can I change the nameservers for my domain`: [https://www.namecheap.com/support/knowledgebase/article.aspx/767/10/how-can-i-change-the-nameservers-for-my-domain](https://www.namecheap.com/support/knowledgebase/article.aspx/767/10/how-can-i-change-the-nameservers-for-my-domain)
- Namecheap `How to set up Free Email Forwarding`: [https://www.namecheap.com/support/knowledgebase/article.aspx/308/77/how-to-setup-free-email-forwarding](https://www.namecheap.com/support/knowledgebase/article.aspx/308/77/how-to-setup-free-email-forwarding)
