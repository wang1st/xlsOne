# 巅峰脉动 (ZENITH PULSE) 产品网站设计

## 背景

- 品牌：巅峰脉动 / ZENITH PULSE（个人品牌，非公司）
- 域名：z-pulse.cn（审核中）
- 服务器：47.97.115.235（空系统）
- 首款产品：xlsOne（Excel 财务报表汇总工具）
- 目标：品牌展示 + 产品下载 + 自动更新 API

## 架构

```
47.97.115.235 (Nginx)
├── z-pulse.cn
│   ├── /                    首页 · ZENITH PULSE 品牌
│   ├── /products/xlsone/    xlsOne 产品介绍
│   ├── /downloads/          xlsOne 下载页 + 软件包文件
│   ├── /api/version.json    自动更新检测 API（供 UpdateChecker 消费）
│   ├── /support/            支持页面
│   └── /privacy/            隐私政策
```

## 技术方案

- Nginx 纯静态站点
- HTML/CSS，无框架，无后端
- 域名审批通过后配 Let's Encrypt HTTPS

## 文件结构（重构 site/）

```
site/
├── index.html               公司首页
├── products/xlsone/
│   ├── index.html           产品介绍页
│   └── download.html        下载页（多平台入口）
├── api/
│   └── version.json         更新 API JSON（每版更新）
├── downloads/               .gitignore 排除，存放软件包
├── support/
│   └── index.html           支持与联系
├── privacy/
│   └── index.html           隐私政策
├── css/
│   └── style.css            全站样式
├── robots.txt
└── nginx.conf               服务器 Nginx 配置
```

## 页面内容规划

### 首页 (/)
- ZENITH PULSE 品牌标题
- 一句话介绍
- xlsOne 产品卡片（图标+简介+入口链接）
- 多平台支持标识

### 产品页 (/products/xlsone/)
- 产品名称、定位
- 核心功能列表
- 平台支持（macOS/Windows/Linux）
- 跳转下载链接

### 下载页 (/products/xlsone/download.html)
- 版本选择
- macOS / Windows / Linux 下载链接（指向 /downloads/ 目录下的文件）
- 安装说明

### 更新 API (/api/version.json)
```json
{
  "latest_version": "0.2.0",
  "changelog": "...",
  "downloads": {
    "macos": "https://z-pulse.cn/downloads/v0.2.0/xlsOne.dmg",
    "windows": "https://z-pulse.cn/downloads/v0.2.0/xlsOne.exe",
    "linux": "https://z-pulse.cn/downloads/v0.2.0/xlsOne.AppImage"
  }
}
```

### 支持页 (/support/)
- 联系方式
- 支持的文件格式
- 系统要求
- FAQ

### 隐私页 (/privacy/)
- 本地运行、不上传数据
- 无用户追踪
- 联系邮箱

## 实施阶段

1. SSH 登录服务器，安装 Nginx
2. 创建站点目录，上传 site/ 文件
3. 配置 Nginx 虚拟主机
4. 绑定域名 + HTTPS
5. 部署 version.json + UpdateChecker API URL 更新到域名
6. CI/CD 脚本集成：每次发版自动更新 site/api/version.json
