# ZENITH PULSE 产品网站实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 47.97.115.235 上部署 z-pulse.cn 静态产品网站，承载品牌展示、xlsOne 产品下载和自动更新 API。

**Architecture:** Nginx 纯静态站点，HTML/CSS 无框架。SSH 远程部署，Nginx 提供文件服务 + API + 下载目录。

**Tech Stack:** Nginx, HTML5, CSS3, Bash 脚本

---

### Task 1: SSH 连接服务器并安装 Nginx

**Files:**
- Create: `scripts/deploy/setup-server.sh`

- [ ] **Step 1: 创建服务器初始化脚本**

```bash
#!/usr/bin/env bash
# scripts/deploy/setup-server.sh
# 在服务器上安装 Nginx 并配置基础环境
set -euo pipefail

SERVER="47.97.115.235"
SSH_USER="root"
REMOTE_SITE="/var/www/z-pulse.cn"

ssh "${SSH_USER}@${SERVER}" bash -s << 'REMOTE_SCRIPT'
set -euo pipefail

# 检测系统并安装 Nginx
if command -v apt-get &>/dev/null; then
    apt-get update -y
    apt-get install -y nginx
elif command -v yum &>/dev/null; then
    yum install -y epel-release
    yum install -y nginx
fi

# 创建站点目录
mkdir -p /var/www/z-pulse.cn

# 确保 Nginx 开机启动
systemctl enable nginx || service nginx enable
systemctl start nginx || service nginx start

echo "Nginx installed and running"
REMOTE_SCRIPT
```

- [ ] **Step 2: 执行初始化脚本**

```bash
chmod +x scripts/deploy/setup-server.sh
bash scripts/deploy/setup-server.sh
```

Expected: Nginx 安装成功，服务运行中。

- [ ] **Step 3: 验证 Nginx 可通过 IP 访问**

```bash
curl -sI http://47.97.115.235 | head -1
```

Expected: `HTTP/1.1 200 OK`

- [ ] **Step 4: 提交**

```bash
git add scripts/deploy/setup-server.sh
git commit -m "feat: add server setup script for z-pulse.cn"
```

---

### Task 2: 创建全站 CSS 样式

**Files:**
- Create: `site/css/style.css`
- Remove: old `site/` files that are no longer needed

- [ ] **Step 1: 删除旧 site 内容（保留 api/ 目录）**

```bash
# 删除旧文件
rm -f site/index.html site/styles.css site/_headers site/_redirects site/robots.txt site/sitemap.xml
rm -rf site/changelog/
# 保留 site/api/version.json
```

- [ ] **Step 2: 创建 site/css/style.css**

```css
/* ZENITH PULSE - 巅峰脉动 */
/* Brand colors: deep teal #0d7377, dark bg #1a1a2e, accent #e94560 */

* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif;
    background: #0f0f1a;
    color: #e0e0e0;
    line-height: 1.6;
    min-height: 100vh;
}

a {
    color: #0d7377;
    text-decoration: none;
}
a:hover {
    color: #0fa3a8;
}

.container {
    max-width: 960px;
    margin: 0 auto;
    padding: 0 24px;
}

/* Header */
.site-header {
    padding: 24px 0;
    border-bottom: 1px solid #1e1e32;
}
.site-header .container {
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.brand {
    font-size: 1.6rem;
    font-weight: 700;
    color: #ffffff;
    letter-spacing: 0.05em;
}
.brand span {
    color: #0fa3a8;
}
.nav a {
    margin-left: 24px;
    font-size: 0.95rem;
}

/* Hero */
.hero {
    padding: 100px 0 60px;
    text-align: center;
}
.hero h1 {
    font-size: 3rem;
    font-weight: 800;
    color: #ffffff;
    margin-bottom: 16px;
    letter-spacing: 0.02em;
}
.hero h1 span {
    color: #0fa3a8;
}
.hero p {
    font-size: 1.15rem;
    color: #999;
    max-width: 560px;
    margin: 0 auto 32px;
}

.btn {
    display: inline-block;
    padding: 12px 32px;
    border-radius: 8px;
    font-weight: 600;
    font-size: 1rem;
    transition: all 0.2s;
}
.btn-primary {
    background: #0fa3a8;
    color: #fff;
}
.btn-primary:hover {
    background: #0d7377;
    color: #fff;
}
.btn-outline {
    border: 1px solid #0fa3a8;
    color: #0fa3a8;
}
.btn-outline:hover {
    background: #0fa3a8;
    color: #fff;
}

/* Product card */
.product-section {
    padding: 60px 0;
}
.product-card {
    background: #16162a;
    border: 1px solid #1e1e32;
    border-radius: 12px;
    padding: 40px;
    display: flex;
    gap: 32px;
    align-items: center;
}
.product-card img {
    width: 80px;
    height: 80px;
    border-radius: 16px;
    flex-shrink: 0;
}
.product-info h2 {
    font-size: 1.5rem;
    color: #fff;
    margin-bottom: 8px;
}
.product-info .tagline {
    color: #0fa3a8;
    font-size: 0.9rem;
    margin-bottom: 12px;
}
.product-info p {
    color: #999;
    font-size: 0.95rem;
    margin-bottom: 16px;
}
.platform-badges {
    display: flex;
    gap: 8px;
    margin-bottom: 16px;
}
.platform-badges span {
    padding: 4px 12px;
    border-radius: 6px;
    font-size: 0.8rem;
    background: #1e1e32;
    color: #888;
}

/* Features grid */
.features {
    padding: 60px 0;
}
.features h2 {
    text-align: center;
    font-size: 1.8rem;
    color: #fff;
    margin-bottom: 40px;
}
.feature-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 24px;
}
.feature-item {
    background: #16162a;
    border: 1px solid #1e1e32;
    border-radius: 10px;
    padding: 28px;
}
.feature-item h3 {
    color: #0fa3a8;
    font-size: 1.05rem;
    margin-bottom: 8px;
}
.feature-item p {
    color: #999;
    font-size: 0.9rem;
}

/* Download page */
.download-section {
    padding: 60px 0;
}
.download-section h2 {
    color: #fff;
    font-size: 1.4rem;
    margin-bottom: 24px;
}
.download-list {
    display: flex;
    flex-direction: column;
    gap: 12px;
}
.download-item {
    background: #16162a;
    border: 1px solid #1e1e32;
    border-radius: 10px;
    padding: 20px 24px;
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.download-item .platform-name {
    font-weight: 600;
    color: #fff;
}
.download-item .version {
    color: #888;
    font-size: 0.85rem;
    margin-left: 12px;
}

/* Content page (support, privacy) */
.content-page {
    padding: 60px 0;
    max-width: 720px;
    margin: 0 auto;
}
.content-page h1 {
    color: #fff;
    font-size: 1.8rem;
    margin-bottom: 24px;
}
.content-page h2 {
    color: #0fa3a8;
    font-size: 1.2rem;
    margin: 32px 0 12px;
}
.content-page p,
.content-page li {
    color: #999;
    font-size: 0.95rem;
    margin-bottom: 8px;
}
.content-page ul {
    padding-left: 20px;
}

/* Footer */
.site-footer {
    padding: 32px 0;
    border-top: 1px solid #1e1e32;
    text-align: center;
    color: #666;
    font-size: 0.85rem;
}
.site-footer a {
    color: #666;
    margin: 0 12px;
}
.site-footer a:hover {
    color: #0fa3a8;
}

/* Responsive */
@media (max-width: 768px) {
    .hero h1 { font-size: 2rem; }
    .product-card { flex-direction: column; text-align: center; }
    .platform-badges { justify-content: center; }
    .nav a { margin-left: 16px; font-size: 0.85rem; }
}
```

- [ ] **Step 3: 提交**

```bash
git add site/css/style.css
git commit -m "feat: add ZENITH PULSE stylesheet"
```

---

### Task 3: 创建首页 (index.html)

**Files:**
- Create: `site/index.html`

- [ ] **Step 1: 创建 site/index.html**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ZENITH PULSE · 巅峰脉动</title>
    <meta name="description" content="ZENITH PULSE 巅峰脉动 — 独立开发者打造的效率工具品牌">
    <link rel="stylesheet" href="/css/style.css">
    <link rel="icon" type="image/png" href="/favicon.png">
</head>
<body>

<header class="site-header">
    <div class="container">
        <div class="brand">ZENITH <span>PULSE</span></div>
        <nav class="nav">
            <a href="/">首页</a>
            <a href="/products/xlsone/">产品</a>
            <a href="/support/">支持</a>
        </nav>
    </div>
</header>

<main>
    <section class="hero">
        <h1>ZENITH <span>PULSE</span></h1>
        <p>巅峰脉动 · 独立开发者打造的效率工具</p>
    </section>

    <section class="product-section">
        <div class="container">
            <div class="product-card">
                <img src="/products/xlsone/icon.png" alt="表表归一" onerror="this.style.display='none'">
                <div class="product-info">
                    <h2>表表归一</h2>
                    <div class="tagline">xlsOne — 多张同格式 Excel 报表一键汇总</div>
                    <p>将多个结构相同的 Excel 表格智能合并为一张：金额自动求和、标签原样保留、混合类型智能标注。</p>
                    <div class="platform-badges">
                        <span>macOS</span>
                        <span>Windows</span>
                        <span>Linux</span>
                    </div>
                    <a href="/products/xlsone/" class="btn btn-primary">了解详情</a>
                    <a href="/products/xlsone/download.html" class="btn btn-outline" style="margin-left:8px">免费下载</a>
                </div>
            </div>
        </div>
    </section>
</main>

<footer class="site-footer">
    <div class="container">
        <p>
            <a href="/support/">支持</a>
            <a href="/privacy/">隐私政策</a>
        </p>
        <p style="margin-top:12px">&copy; 2026 ZENITH PULSE</p>
    </div>
</footer>

</body>
</html>
```

- [ ] **Step 2: 提交**

```bash
git add site/index.html
git commit -m "feat: add ZENITH PULSE homepage"
```

---

### Task 4: 创建 xlsOne 产品页

**Files:**
- Create: `site/products/xlsone/index.html`
- Create: `site/products/xlsone/download.html`

- [ ] **Step 1: 创建产品介绍页 site/products/xlsone/index.html**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>表表归一 xlsOne — ZENITH PULSE</title>
    <meta name="description" content="表表归一 xlsOne — 多张同格式 Excel 报表一键汇总工具。支持 macOS / Windows / Linux。">
    <link rel="stylesheet" href="/css/style.css">
</head>
<body>

<header class="site-header">
    <div class="container">
        <div class="brand"><a href="/" style="color:inherit">ZENITH <span>PULSE</span></a></div>
        <nav class="nav">
            <a href="/">首页</a>
            <a href="/products/xlsone/">产品</a>
            <a href="/support/">支持</a>
        </nav>
    </div>
</header>

<main class="container content-page">
    <h1>表表归一 <span style="color:#0fa3a8;font-size:1rem;font-weight:400">xlsOne</span></h1>
    <p>多张同格式 Excel 财务报表一键汇总工具。将多个结构相同的 Excel 表格智能合并为一张汇总表。</p>

    <section class="features">
        <h2>核心功能</h2>
        <div class="feature-grid">
            <div class="feature-item">
                <h3>智能聚合</h3>
                <p>金额列自动求和、标签列原样保留、混合类型显示 X 条。无需手动比对。</p>
            </div>
            <div class="feature-item">
                <h3>穿透查阅</h3>
                <p>点击任意单元格，查看该位置在所有源文件中的原始值，每个文件的来源一目了然。</p>
            </div>
            <div class="feature-item">
                <h3>多格式支持</h3>
                <p>支持 .xlsx 和 BIFF8 .xls 文件，拖拽导入、批量追加，一次处理 20+ 个文件。</p>
            </div>
            <div class="feature-item">
                <h3>导出结果</h3>
                <p>汇总结果一键导出为 .xlsx 工作簿，保留原有表头结构和行列布局。</p>
            </div>
            <div class="feature-item">
                <h3>手动修正</h3>
                <p>对自动判断有疑问？手动指定任意单元格为标签或求和类型，支持批量操作和撤销。</p>
            </div>
            <div class="feature-item">
                <h3>跨平台</h3>
                <p>macOS / Windows / Linux 全平台支持。原生体验，离线使用，数据不上传。</p>
            </div>
        </div>
    </section>

    <div style="text-align:center;margin-top:40px">
        <a href="/products/xlsone/download.html" class="btn btn-primary">免费下载</a>
    </div>
</main>

<footer class="site-footer">
    <div class="container">
        <p><a href="/support/">支持</a><a href="/privacy/">隐私政策</a></p>
        <p style="margin-top:12px">&copy; 2026 ZENITH PULSE</p>
    </div>
</footer>

</body>
</html>
```

- [ ] **Step 2: 创建下载页 site/products/xlsone/download.html**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>下载 表表归一 xlsOne</title>
    <link rel="stylesheet" href="/css/style.css">
</head>
<body>

<header class="site-header">
    <div class="container">
        <div class="brand"><a href="/" style="color:inherit">ZENITH <span>PULSE</span></a></div>
        <nav class="nav">
            <a href="/">首页</a>
            <a href="/products/xlsone/">产品</a>
            <a href="/support/">支持</a>
        </nav>
    </div>
</header>

<main class="container download-section">
    <h2>下载 表表归一 <span style="color:#0fa3a8;font-size:0.9rem;font-weight:400">v0.2.0</span></h2>

    <div class="download-list">
        <div class="download-item">
            <div>
                <span class="platform-name">macOS</span>
                <span class="version">v0.2.0</span>
            </div>
            <a href="/downloads/xlsOne-0.2.0-macos.dmg" class="btn btn-primary">下载 DMG</a>
        </div>
        <div class="download-item">
            <div>
                <span class="platform-name">Windows</span>
                <span class="version">v0.2.0</span>
            </div>
            <a href="/downloads/xlsOne-0.2.0-win64.exe" class="btn btn-primary">下载 EXE</a>
        </div>
        <div class="download-item">
            <div>
                <span class="platform-name">Linux</span>
                <span class="version">v0.2.0 AppImage</span>
            </div>
            <a href="/downloads/xlsOne-0.2.0-linux-x86_64.AppImage" class="btn btn-primary">下载 AppImage</a>
        </div>
    </div>

    <p style="color:#666;margin-top:32px;font-size:0.9rem">
        macOS 用户也可通过 <a href="macappstore://apps.apple.com/app/id0000000000">Mac App Store</a> 安装。
    </p>
</main>

<footer class="site-footer">
    <div class="container">
        <p><a href="/support/">支持</a><a href="/privacy/">隐私政策</a></p>
        <p style="margin-top:12px">&copy; 2026 ZENITH PULSE</p>
    </div>
</footer>

</body>
</html>
```

- [ ] **Step 3: 提交**

```bash
git add site/products/
git commit -m "feat: add xlsOne product and download pages"
```

---

### Task 5: 创建支持页和隐私页

**Files:**
- Create: `site/support/index.html`
- Create: `site/privacy/index.html`
- Create: `site/robots.txt`

- [ ] **Step 1: 创建 site/support/index.html**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>支持 — ZENITH PULSE</title>
    <link rel="stylesheet" href="/css/style.css">
</head>
<body>

<header class="site-header">
    <div class="container">
        <div class="brand"><a href="/" style="color:inherit">ZENITH <span>PULSE</span></a></div>
        <nav class="nav">
            <a href="/">首页</a>
            <a href="/products/xlsone/">产品</a>
            <a href="/support/">支持</a>
        </nav>
    </div>
</header>

<main class="container content-page">
    <h1>支持</h1>

    <h2>联系方式</h2>
    <p>邮箱：<a href="mailto:831261@qq.com">831261@qq.com</a></p>
    <p>通常 24 小时内回复。</p>

    <h2>系统要求</h2>
    <ul>
        <li><strong>macOS</strong> 12.0 或更高版本（Apple Silicon / Intel）</li>
        <li><strong>Windows</strong> 10 / 11 64 位</li>
        <li><strong>Linux</strong> x86_64，glibc 2.28+（Ubuntu 20.04+ / Debian 11+ / UOS）</li>
    </ul>

    <h2>支持的文件格式</h2>
    <ul>
        <li>.xlsx（Office Open XML）</li>
        <li>.xls（BIFF8 格式，常见旧版 Excel 文件）</li>
    </ul>

    <h2>常见问题</h2>
    <p><strong>Q: 我的文件无法导入？</strong><br>
    A: 请确认文件格式为 .xlsx 或 .xls，且未被加密或损坏。</p>

    <p><strong>Q: 数据会被上传到服务器吗？</strong><br>
    A: 不会。所有数据处理均在本地完成，软件不收集或上传任何文件内容。</p>

    <p><strong>Q: 如何更新到最新版本？</strong><br>
    A: 打开软件后会自动检测新版本。也可在菜单 帮助 → 检查更新 手动触发。</p>
</main>

<footer class="site-footer">
    <div class="container">
        <p><a href="/support/">支持</a><a href="/privacy/">隐私政策</a></p>
        <p style="margin-top:12px">&copy; 2026 ZENITH PULSE</p>
    </div>
</footer>

</body>
</html>
```

- [ ] **Step 2: 创建 site/privacy/index.html**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>隐私政策 — ZENITH PULSE</title>
    <link rel="stylesheet" href="/css/style.css">
</head>
<body>

<header class="site-header">
    <div class="container">
        <div class="brand"><a href="/" style="color:inherit">ZENITH <span>PULSE</span></a></div>
        <nav class="nav">
            <a href="/">首页</a>
            <a href="/products/xlsone/">产品</a>
            <a href="/support/">支持</a>
        </nav>
    </div>
</header>

<main class="container content-page">
    <h1>隐私政策</h1>
    <p>最后更新：2026 年 5 月</p>

    <h2>数据收集</h2>
    <p>我们的桌面应用 <strong>表表归一（xlsOne）</strong> 完全在本地运行，<strong>不收集、不上传任何用户文件内容或个人数据</strong>。</p>
    <p>软件在启动时会检查是否有新版本可用（通过请求 <code>/api/version.json</code>），此请求仅包含基本 HTTP 信息，<strong>不包含任何个人标识信息或文件内容</strong>。</p>

    <h2>第三方服务</h2>
    <p>本软件不使用任何第三方分析、广告或追踪服务。</p>

    <h2>数据存储</h2>
    <p>所有导入的 Excel 文件和处理结果均保存在用户本地设备上。软件不会将数据同步到云端。</p>

    <h2>联系方式</h2>
    <p>如有隐私相关问题，请发送邮件至 <a href="mailto:831261@qq.com">831261@qq.com</a>。</p>
</main>

<footer class="site-footer">
    <div class="container">
        <p><a href="/support/">支持</a><a href="/privacy/">隐私政策</a></p>
        <p style="margin-top:12px">&copy; 2026 ZENITH PULSE</p>
    </div>
</footer>

</body>
</html>
```

- [ ] **Step 3: 创建 site/robots.txt**

```
User-agent: *
Allow: /
Sitemap: https://z-pulse.cn/sitemap.xml
```

- [ ] **Step 4: 提交**

```bash
git add site/support/ site/privacy/ site/robots.txt
git commit -m "feat: add support, privacy pages and robots.txt"
```

---

### Task 6: 创建 Nginx 配置文件

**Files:**
- Create: `site/nginx.conf`

- [ ] **Step 1: 创建 site/nginx.conf**

```nginx
server {
    listen 80;
    server_name z-pulse.cn www.z-pulse.cn 47.97.115.235;

    root /var/www/z-pulse.cn;
    index index.html;

    # 日志
    access_log /var/log/nginx/z-pulse.cn-access.log;
    error_log /var/log/nginx/z-pulse.cn-error.log;

    # 安全头
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-Frame-Options "SAMEORIGIN" always;

    # API — 确保返回 JSON Content-Type
    location = /api/version {
        alias /var/www/z-pulse.cn/api/version.json;
        default_type application/json;
        add_header Cache-Control "no-cache";
    }

    # 下载目录 — 允许直接访问文件
    location /downloads/ {
        alias /var/www/z-pulse.cn/downloads/;
        autoindex off;
        types {
            application/octet-stream dmg;
            application/octet-stream exe;
            application/octet-stream AppImage;
            application/vnd.debian.binary-package deb;
        }
    }

    # 静态文件
    location / {
        try_files $uri $uri/ =404;
    }
}
```

- [ ] **Step 2: 提交**

```bash
git add site/nginx.conf
git commit -m "feat: add Nginx config for z-pulse.cn"
```

---

### Task 7: 创建部署脚本并上传到服务器

**Files:**
- Create: `scripts/deploy/upload-site.sh`

- [ ] **Step 1: 创建上传部署脚本**

```bash
#!/usr/bin/env bash
# scripts/deploy/upload-site.sh
# 将 site/ 目录上传到服务器并重载 Nginx
set -euo pipefail

SERVER="47.97.115.235"
SSH_USER="root"
REMOTE_SITE="/var/www/z-pulse.cn"
LOCAL_SITE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/site"

echo "Uploading site files to ${SERVER}..."

# 同步文件（排除 downloads 目录中的大文件、.gitkeep 等）
rsync -avz --delete \
    --exclude='downloads/*' \
    --exclude='*.log' \
    --exclude='.DS_Store' \
    "${LOCAL_SITE}/" \
    "${SSH_USER}@${SERVER}:${REMOTE_SITE}/"

# 部署 Nginx 配置
echo "Deploying Nginx config..."
scp "${LOCAL_SITE}/nginx.conf" "${SSH_USER}@${SERVER}:/etc/nginx/sites-available/z-pulse.cn"

ssh "${SSH_USER}@${SERVER}" bash -s << 'REMOTE'
set -euo pipefail

# 如果 sites-enabled 中有默认站点，先移除
rm -f /etc/nginx/sites-enabled/default

# 创建软链接（如果不存在）
if [ ! -L /etc/nginx/sites-enabled/z-pulse.cn ]; then
    ln -sf /etc/nginx/sites-available/z-pulse.cn /etc/nginx/sites-enabled/z-pulse.cn
fi

# 测试配置并重载
nginx -t && systemctl reload nginx

echo "Nginx reloaded successfully"
REMOTE

echo "Deployment complete: http://${SERVER}"
```

- [ ] **Step 2: 执行部署**

```bash
chmod +x scripts/deploy/upload-site.sh
bash scripts/deploy/upload-site.sh
```

Expected: rsync 成功，Nginx reload 成功。

- [ ] **Step 3: 验证部署**

```bash
curl -sI http://47.97.115.235 | head -3
curl -s http://47.97.115.235/api/version
```

Expected: 首页返回 200，API 返回 JSON。

- [ ] **Step 4: 提交**

```bash
git add scripts/deploy/upload-site.sh
git commit -m "feat: add site upload and deploy script"
```

---

### Task 8: 更新 UpdateChecker API URL 指向 z-pulse.cn

**Files:**
- Modify: `cpp/app/src/main_window.cpp` (更新 checkForUpdates 中的 API URL)

- [ ] **Step 1: 修改 API URL**

找到 `checkForUpdates()` 函数中的 URL，将：
```cpp
    const QString apiUrl = QStringLiteral(
        "https://updates.xlsone.com/api/version");
```
改为：
```cpp
    const QString apiUrl = QStringLiteral(
        "https://z-pulse.cn/api/version");
```

- [ ] **Step 2: 编译验证**

```bash
cmake --build cpp/build --target xlsone_app
```

Expected: 编译通过。

- [ ] **Step 3: 提交**

```bash
git add cpp/app/src/main_window.cpp
git commit -m "fix: update API URL to z-pulse.cn"
```

---

### Task 9: 更新 site/api/version.json 内容

**Files:**
- Modify: `site/api/version.json`

- [ ] **Step 1: 更新 version.json 中的下载链接指向真实域名**

```json
{
  "latest_version": "0.2.0",
  "changelog": "v0.2.0 更新内容：\n- 新增自动更新检测功能\n- 优化 BIFF8 .xls 解析兼容性\n- 修复 Qt 跨平台 UI 一致性问题",
  "downloads": {
    "macos": "https://z-pulse.cn/downloads/xlsOne-0.2.0-macos.dmg",
    "windows": "https://z-pulse.cn/downloads/xlsOne-0.2.0-win64.exe",
    "linux": "https://z-pulse.cn/downloads/xlsOne-0.2.0-linux-x86_64.AppImage"
  }
}
```

- [ ] **Step 2: 提交**

```bash
git add site/api/version.json
git commit -m "feat: update version.json with real download URLs"
```

---

### Task 10: 全量验证

- [ ] **Step 1: 重新部署到服务器**

```bash
bash scripts/deploy/upload-site.sh
```

- [ ] **Step 2: 验证所有页面可访问**

```bash
for path in "/" "/products/xlsone/" "/products/xlsone/download.html" "/support/" "/privacy/" "/api/version"; do
    echo -n "$path: "
    curl -s -o /dev/null -w "%{http_code}" "http://47.97.115.235${path}"
    echo
done
```

Expected: 全部返回 200。

- [ ] **Step 3: 验证 C++ 端编译 + 测试通过**

```bash
cmake --build cpp/build && ctest --test-dir cpp/build --output-on-failure
```

Expected: 编译通过，测试全部通过。

- [ ] **Step 4: 推送**

```bash
git push origin main --tags
```
