# xlsOne SEO 执行计划

最后更新：2026-07-23

说明：完成的任务使用 `[x]` 并加删除线；未完成或需要账号配合的任务保持 `[ ]`，不提前标记完成。

## 第一阶段：技术基础与收录

- [x] ~~建立 SEO 计划跟踪文件。~~
- [x] ~~完成站点基线审计：页面、标题、canonical、robots、sitemap、Nginx、HTTPS 和现有统计。~~
- [x] ~~清理 sitemap，只保留 4 个规范页面，并填写准确的 `lastmod`。~~
- [x] ~~将历史重复 URL 从 HTML `meta refresh` 改为服务器端 HTTP 301。~~
- [x] ~~将 `www` 与 HTTP 请求统一 301 到 `https://z-pulse.cn` 规范主机。~~
- [x] ~~将内部推广文案移出公开站点，并让线上旧地址返回 410。~~
- [x] ~~为统计后台、接口和安装包目录配置抓取限制及 `X-Robots-Tag`。~~
- [x] ~~配置 IndexNow key，提交首批 4 个规范 URL；接口返回 202 Accepted。~~
- [x] ~~检查并优化移动端性能：Lighthouse Performance 84 → 100，LCP 2.9s → 1.3s，TBT 350ms → 0ms，CLS 保持 0。~~
- [x] ~~校验首页结构化数据：Google 检测到 1 项有效的 SoftwareApplication，无严重错误；未伪造用户评分来消除非严重提示。~~
- [ ] 验证 Google Search Console 站点并提交 sitemap。
- [ ] 验证百度搜索资源平台站点并提交链接/sitemap。
- [ ] 验证 Bing Webmaster Tools 站点并提交 sitemap。
- [x] ~~部署生产环境并验证 HTTPS、HTTP/2、gzip、缓存、robots、sitemap、301、410、noindex、统计服务和页面可用性。~~

### 当前待账号操作

- Google Search Console：首页验证标记和官方验证文件 `googlede3d88d210fda816.html` 已部署并确认线上返回 200；Chrome 扩展打开页面持续超时，待在 Search Console 点击“验证”并提交 `sitemap.xml`。
- 百度搜索资源平台：需要登录百度账号，可能触发实名、短信或验证码。
- Bing Webmaster Tools：需要登录 Microsoft 账号；IndexNow 已先独立接入完成。

## 第一阶段验收标准

- sitemap 中不存在跳转 URL、重复 canonical 或内部页面。
- 旧地址返回 301，内部推广文案旧地址返回 410。
- `/analytics/`、`/api/`、`/downloads/` 不进入搜索索引。
- 首页、下载、授权和隐私页面均返回 200，canonical 指向自身。
- Google、百度、Bing 至少完成站点验证与 sitemap 提交；如遇登录、实名或验证码，明确记录待用户完成的步骤。
- IndexNow 成功接收首批 URL。
- 移动端性能和结构化数据有可复查的检测结果。

## 第一阶段执行记录

- 2026-07-23：IndexNow 批量提交返回 `202 Accepted`。
- 2026-07-23：Lighthouse 移动端复测为 Performance 100、SEO 100、FCP 1.3s、LCP 1.3s、TBT 0ms、CLS 0、Speed Index 1.5s。
- 2026-07-23：Google 富媒体搜索结果测试检测到 1 项有效的 SoftwareApplication，无严重错误。
- 2026-07-23：部署 Google Search Console 首页验证标记及官方 HTML 验证文件，线上验证地址返回 200；回滚备份为 `/root/xlsone-backups/google-verification-20260723T020124Z`。
- 2026-07-23：生产联合验收脚本返回 `seo_phase1_server_verification=ok`。
- 生产回滚备份：`/root/xlsone-backups/seo-phase1-20260723T004935Z`、`/root/xlsone-backups/seo-delivery-20260723T010242Z`。

## 后续阶段（第一阶段完成后启动）

- [ ] 第二阶段：建立关键词地图和第一批高意图落地页。
- [ ] 第三阶段：发布场景教程、对比、案例和视频搜索页面。
- [ ] 第四阶段：按站长平台搜索词与 GoatCounter 转化数据持续优化。
