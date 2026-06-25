# xlsone.com Cloudflare 部署清单

更新时间：2026-04-28

## 目标

把当前仓库的 `site/` 目录发布到 `https://xlsone.com`，并配置 App Store 提审需要的支持和隐私入口。

## 前置条件

- 已拥有 `xlsone.com`
- 可以登录 Namecheap
- 可以登录或创建 Cloudflare 账号
- 已安装并登录 Cloudflare Wrangler，或可在 Cloudflare Pages 控制台上传 `site/`
- `support@xlsone.com` 和 `privacy@xlsone.com` 的转发目标邮箱已确定

## 1. Cloudflare 接管 DNS

1. 在 Cloudflare 添加 `xlsone.com`
2. 选择 Free 计划即可
3. 让 Cloudflare 扫描现有 DNS 记录
4. 在 Namecheap 确认 DNSSEC 未启用；如果已启用，先关闭
5. 在 Namecheap 的 `Nameservers` 区域选择 `Custom DNS`
6. 填入 Cloudflare 分配的两个 nameserver
7. 等待 Cloudflare 显示域名状态为 Active

验证命令：

```bash
dig ns xlsone.com @1.1.1.1
dig ns xlsone.com @8.8.8.8
```

## 2. 部署 Cloudflare Pages

1. 进入 Cloudflare `Workers & Pages`
2. 创建 Pages 项目；本次项目名为 `xlsone`
3. 使用 Wrangler 上传 `site/` 目录，或在控制台使用 Direct Upload
4. 打开 Cloudflare 分配的 `*.pages.dev` 地址检查页面

本次实际部署命令：

```bash
npx --yes wrangler pages deploy site --project-name xlsone --branch main
```

上线前必须检查：

- 首页可访问
- `/support/` 可访问
- `/privacy/` 可访问
- `/changelog/` 可访问
- 样式文件 `/styles.css` 正常加载
- `/robots.txt` 可访问
- `/sitemap.xml` 可访问

## 3. 绑定正式域名

1. 在 Pages 项目的 `Custom domains` 中添加 `xlsone.com`
2. 再添加 `www.xlsone.com`
3. 确认 Cloudflare Pages 自定义域名状态为 `active`
4. 当前 `xlsone.com` 与 `www.xlsone.com` 均可直接访问同一站点；如后续需要 SEO 规范化，再在 Cloudflare 中追加 `www` 到根域的 Redirect Rule

验证命令：

```bash
curl -I https://xlsone.com
curl -I https://www.xlsone.com
curl -I https://xlsone.com/support/
curl -I https://xlsone.com/privacy/
curl -I https://xlsone.com/robots.txt
curl -I https://xlsone.com/sitemap.xml
```

## 4. 配置邮件转发

1. 进入 Cloudflare `Email Routing`
2. 启用 `xlsone.com`
3. 添加目标邮箱 `831261@qq.com` 并完成验证
4. 创建以下转发地址：
   - `support@xlsone.com`
   - `privacy@xlsone.com`
5. 从外部邮箱分别发测试邮件，确认能收到

注意：Cloudflare Email Routing 只负责收信转发，不提供从 `@xlsone.com` 发信的 SMTP 能力。

本次已完成：

- `support@xlsone.com` -> `831261@qq.com`
- `privacy@xlsone.com` -> `831261@qq.com`
- Email Routing 状态：`ready`
- 目标邮箱状态：verified

## 5. 回填 App Store Connect

- `Marketing URL`: `https://xlsone.com`
- `Support URL`: `https://xlsone.com/support/`
- `Privacy Policy URL`: `https://xlsone.com/privacy/`

## 参考

- Cloudflare DNS full setup: [https://developers.cloudflare.com/dns/zone-setups/full-setup/setup/](https://developers.cloudflare.com/dns/zone-setups/full-setup/setup/)
- Cloudflare Pages custom domains: [https://developers.cloudflare.com/pages/configuration/custom-domains/](https://developers.cloudflare.com/pages/configuration/custom-domains/)
- Cloudflare Email Routing: [https://developers.cloudflare.com/email-routing/get-started/](https://developers.cloudflare.com/email-routing/get-started/)
- Namecheap nameserver setup: [https://www.namecheap.com/support/knowledgebase/article.aspx/767/10/how-can-i-change-the-nameservers-for-my-domain](https://www.namecheap.com/support/knowledgebase/article.aspx/767/10/how-can-i-change-the-nameservers-for-my-domain)
