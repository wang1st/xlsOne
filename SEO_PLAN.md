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
- [x] ~~验证 Google Search Console 站点并提交 sitemap。~~
- [x] ~~验证百度搜索资源平台站点并提交链接/sitemap。~~
- [x] ~~验证 Bing Webmaster Tools 站点并提交 sitemap。~~
- [x] ~~部署生产环境并验证 HTTPS、HTTP/2、gzip、缓存、robots、sitemap、301、410、noindex、统计服务和页面可用性。~~

### 当前待账号操作

- 无。

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
- 2026-07-23：Google Search Console 已验证 `https://z-pulse.cn/` URL 前缀属性并成功提交 `/sitemap.xml`；平台识别类型为“站点地图”，状态为“成功”，发现网页 4 个、视频 0 个。
- 2026-07-23：Bing Webmaster Tools 已通过 `BingSiteAuth.xml` 验证 `https://z-pulse.cn/`，并提交 `https://z-pulse.cn/sitemap.xml`；平台显示已知 sitemap 1 个、错误 0、警告 0，首次抓取状态为 `Processing`。
- 2026-07-23：百度搜索资源平台已通过 `baidu_verify_codeva-Ehc3JtEqwd.html` 验证 `https://www.z-pulse.cn`；因 sitemap 提交额度为 0，改由用户通过链接提交通道提交 4 个无 `www` 规范 URL。
- 2026-07-23：为兼容百度验证和 sitemap 读取，仅在 `www` HTTPS 主机为百度验证文件及 `/sitemap.xml` 配置直接返回 200 的精确例外；其他 `www` URL 继续 301 到 `https://z-pulse.cn`。Nginx 配置测试通过，回滚备份为 `/root/xlsone-backups/baidu-www-verification-20260723T041100Z`、`/root/xlsone-backups/baidu-www-sitemap-20260723T041800Z`。
- 2026-07-23：生产联合验收脚本返回 `seo_phase1_server_verification=ok`。
- 生产回滚备份：`/root/xlsone-backups/seo-phase1-20260723T004935Z`、`/root/xlsone-backups/seo-delivery-20260723T010242Z`。

## 第二阶段：关键词与高意图落地页

- [x] ~~建立关键词地图，明确页面主意图、辅助意图、转化目标和防止关键词互相竞争的边界。~~
- [x] ~~发布“多张同格式 Excel 一键汇总”高意图落地页，并补齐首页导航与资源入口。~~
- [x] ~~为新增页面配置唯一标题、描述、canonical、H1、结构化数据、内链和 GoatCounter 转化事件。~~

## 第三阶段：教程、对比、场景与视频

- [x] ~~发布“多个 Excel 文件合并到一个表”完整教程。~~
- [x] ~~发布“Excel 合并计算与 Power Query 怎么选”对比页，并引用微软官方资料。~~
- [x] ~~发布月报汇总标准场景页；明确标注为标准场景，不虚构客户、案例或效果数据。~~
- [x] ~~发布真实产品界面视频演示页、13 秒 MP4、VideoObject 结构化数据和文字说明。~~
- [x] ~~将 5 个新页面加入 sitemap，使规范 URL 总数由 4 个增至 9 个。~~
- [x] ~~部署生产环境并验证所有新页面、视频、canonical、sitemap、Nginx 与原有 SEO 规则。~~
- [x] ~~通过 IndexNow 提交 5 个新增 URL；百度按用户要求不重复提交。~~

## 第四阶段：数据驱动优化机制

- [x] ~~检查 Google Search Console 与 GoatCounter 首轮数据，记录当前数据量不足以做关键词或转化结论。~~
- [x] ~~建立月度复盘模板，覆盖曝光、点击、CTR、排名、落地页访问、下载与授权转化。~~
- [x] ~~为新增内容页和首页资源入口配置可区分的 GoatCounter 点击事件。~~
- [x] ~~规定按“有曝光无点击、排名 5–20、访问无转化”三类信号安排后续优化，避免在样本不足时过拟合。~~

## 第二至第四阶段执行记录

- 2026-07-23：建立关键词地图 `docs/seo-keyword-map.md`，覆盖现有页面与 5 个新增页面的搜索意图、内容边界和转化目标。
- 2026-07-23：发布高意图落地页、完整教程、方法对比、标准场景和视频演示共 5 个页面；首页增加导航与资源内链。
- 2026-07-23：生成并发布 1280×720、H.264、13 秒真实界面演示视频；线上返回 `video/mp4` 并支持 byte ranges。
- 2026-07-23：新增页面的标题、描述、canonical、唯一 H1、JSON-LD 和内部资源路径检查通过；sitemap XML 校验通过，共 9 个规范 URL。
- 2026-07-23：生产环境 5 个新页面、首页与视频均返回 200；Nginx 配置和联合验收脚本通过。
- 2026-07-23：IndexNow 接收 5 个新增 URL，返回 HTTP 200；未向百度重复提交。
- 2026-07-23：Search Console 暂处于数据处理中，GoatCounter 仅有少量基线访问，当前不足以做有效搜索词或转化归因；建立 `docs/seo-monthly-review-template.md` 供数据积累后的月度复盘。
- 生产回滚备份：`/root/xlsone-backups/seo-content-20260723T130000CST`。

## 第五阶段：外链管理

- [x] ~~建立 90 天外链管理计划，明确域名分流、目标页面、合规边界、渠道优先级、评分标准和复盘节奏。~~
- [x] ~~建立外链机会与存量链接跟踪表。~~
- [ ] 导出 Google、Bing、百度与 GoatCounter 的首轮外链/推荐访问基线。
- [ ] 统一受控的 Gitee、GitHub 和官网品牌、版本与链接信息。
- [x] ~~发布第一份可引用资产：4 份虚构双工作表同模板月报、公式驱动的预期汇总结果、使用说明与对比图。~~
- [ ] 建立并评分首批 40 个候选来源。
- [ ] 完成第一轮 8–10 次个性化外联。
- [ ] 30 天后完成首次外链复盘。

执行文件：

- `docs/seo-backlink-management-plan.md`
- `docs/seo-backlink-tracker.csv`

执行记录：

- 2026-07-23：将样例包升级至 v1.1；4 份部门月报均包含“经营指标”和“项目进度”两个同名同结构工作表，预期结果按两张汇总表完成 36 个数值校验点，全部通过。
- 2026-07-23：发布免费资源页 `/resources/monthly-report-sample/`、ZIP 下载包、使用说明和汇总前后对比图，并接入首页、教程、落地页、案例页与 sitemap。
- 2026-07-23：资源页、图片和 ZIP 生产环境均返回 200；线上下载包 SHA-256 与本地一致，移动端无横向溢出，生产联合验收脚本通过。
- 2026-07-23：通过 IndexNow 提交新增资源页，返回 HTTP 200；未向百度重复提交。
- 2026-07-23：修复全站响应式图片缺少 `height: auto` 导致的纵向拉伸；首页 3 张产品截图和资源页对比图在桌面、手机端的渲染宽高比复测均通过。
- 2026-07-23：v1.1 双工作表样例包上线；资源页、ZIP 和新版 1600×1000 对比图均返回 200，线上 ZIP SHA-256 为 `3df1a13490ca7c689bb9256108f815dab1858f4b0d14844b2160108286725a3b`，桌面与移动端验收通过。
- 2026-07-23：通过 IndexNow 通知 v1.1 资源页更新，返回 HTTP 200；未向百度提交。
- 生产回滚备份：`/root/xlsone-backups/monthly-report-sample-20260723T174500CST`。
- 图片比例修复回滚备份：`/root/xlsone-backups/image-ratio-fix-20260723T175500CST`。
- v1.1 生产回滚备份：`/root/xlsone-backups/monthly-report-sample-v1.1-20260723T181500CST`。
- v1.1 图片缓存刷新页备份：`/root/xlsone-backups/monthly-report-sample-v1.1-cache-bust-20260723T182000CST`。
