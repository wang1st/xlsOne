# xlsone.com 站点说明

这个目录是 `xlsone.com` 的静态站点源码，面向 Cloudflare Pages 这类静态托管环境。

## 目录用途

- `index.html`: 首页，对应 App Store 的 `Marketing URL`
- `support/index.html`: 支持页，对应 `Support URL`
- `privacy/index.html`: 隐私政策页，对应 `Privacy Policy URL`
- `changelog/index.html`: 更新记录页
- `styles.css`: 全站共享样式
- `_headers`: 基础安全响应头
- `_redirects`: Cloudflare Pages 跳转规则文件
- `robots.txt`: 搜索引擎抓取规则
- `sitemap.xml`: 站点地图

## 推荐部署方式

1. 在 Cloudflare Pages 新建项目
2. 连接当前仓库
3. 构建命令留空
4. 输出目录设置为 `site`
5. 绑定自定义域名：
   - `xlsone.com`
   - `www.xlsone.com`
6. 确认 `xlsone.com` 与 `www.xlsone.com` 均可访问；如后续需要 SEO 规范化，再在 Cloudflare 中追加 `www` 到根域的 Redirect Rule

## 上线前核对

- `support@xlsone.com` 与 `privacy@xlsone.com` 已配置为可收件
- `https://www.xlsone.com` 可访问，页面 canonical 指向 `https://xlsone.com/`
- `https://xlsone.com/robots.txt` 和 `https://xlsone.com/sitemap.xml` 可访问
- 页面内容仍与产品真实行为一致
- 站点链接已经填入 App Store Connect
