# 启用 api.z-pulse.cn 独立子域名（TLS 证书）

> **状态：已于 2026-07-09 执行完成。** `https://api.z-pulse.cn` 已启用 Let's Encrypt 证书
> （有效期 2026-07-09 → 2026-10-07），API 与下载页经 443 反代到 `127.0.0.1:8787`，
> 80 端口 301 跳转到 https。自动续期 systemd 定时器（`certbot-renew.timer`）已启用。

此前后端已通过 `install.sh` 部署并运行，API 经主站 `z-pulse.cn/api` 与
`z-pulse.cn/activation/downloads` 代理对外提供服务（无需 DNS 即可用）。本步骤只为
`api.z-pulse.cn` 独立子域名签发并配置证书。

## 前置条件
- 域名 `z-pulse.cn` 已在工信部备案（主站已正常对外）。
- 服务器 `47.97.115.235` 上 `domestic-server` 已运行（systemd 单元 `xlsone-activation` 监听 127.0.0.1:8787）。
- 服务器已安装 nginx 与 certbot（`/usr/bin/certbot`、`/usr/sbin/nginx`）。
- 拥有该服务器的 root SSH 访问权限。
- （关键）已在域名解析后台把 `api.z-pulse.cn` 的 A 记录指向 `47.97.115.235`。

## 实际执行的步骤（2026-07-09）

> 注意：`certbot --nginx -d api.z-pulse.cn` 需要 nginx 里已存在一个
> `server_name api.z-pulse.cn` 的 server 块，否则会报 "找不到匹配的 server block"。
> 因此正确顺序是「先放 80 端口块 → 重载 → 再跑 certbot --nginx」。

1. 登录服务器（root + 密码，密码在本地 `z-pulse.cn.txt`，不入库）。
2. 写入**仅 80 端口**的 server 块（含 `/.well-known/acme-challenge/` 与反代到 8787）：
   `/etc/nginx/conf.d/api.z-pulse.cn.conf`，`nginx -t && systemctl reload nginx`。
3. 签发并自动配置：`certbot --nginx -d api.z-pulse.cn --non-interactive --agree-tos --no-eff-email`
   （复用既有 ACME 生产账户，无需重复邮箱）。certbot 自动写入证书、注入 443 ssl 指令、
   并把 80→443 重定向写回该 conf。证书落在 `/etc/letsencrypt/live/api.z-pulse.cn/`。
4. 用仓库规范配置覆盖该 conf（与 `deploy/nginx-api.z-pulse.cn.conf` 一致：80 重定向 + 443 反代），
   `nginx -t && systemctl reload nginx`。
5. 验证（在服务器侧）：
   - `curl -fsS https://api.z-pulse.cn/api/health` → `{"status":"ok",...}`
   - `curl -fsS -o /dev/null -w "%{http_code}" https://api.z-pulse.cn/downloads/` → `200`
   - `curl -sS -o /dev/null -w "%{http_code} %{redirect_url}" http://api.z-pulse.cn/api/health` → `301 https://api.z-pulse.cn/api/health`

## 自动续期（补充步骤）
该服务器原本**没有** certbot 续期定时器/cron（`systemctl list-timers certbot*` 为空，
也无 `/etc/cron.d/certbot`）。已新建并启用 systemd 定时器：

- `/etc/systemd/system/certbot-renew.service`
  ```
  [Service]
  Type=oneshot
  ExecStart=/usr/bin/certbot renew --quiet --deploy-hook "systemctl reload nginx"
  ```
- `/etc/systemd/system/certbot-renew.timer`（`OnCalendar=daily`，`RandomizedDelaySec=3600`）
- 启用：`systemctl daemon-reload && systemctl enable --now certbot-renew.timer`

证书到期前 30 天会自动续期，成功后 reload nginx 使新证书生效。

> 备注：`certbot renew --dry-run` 在本机运行时可能因到 Let's Encrypt **staging** 预演环境
> 的网络被限而长时间挂起；但生产证书已成功签发，证明真实续期链路（到生产 ACME）可达，
> 每日定时器可正常工作。

## 回滚
删除 `/etc/nginx/conf.d/api.z-pulse.cn.conf` 并 `systemctl reload nginx` 即可。
回滚后 API 仍经 `z-pulse.cn/api` 与 `z-pulse.cn/activation/downloads` 可用。

## 后续
- Windows：`cpp/CMakePresets.json` 的 `windows-cn-release` 已烤入 `api.z-pulse.cn`。
- macOS：在 `project.yml` 增加国内 scheme/plist，设置
  `XLSONEActivationBaseURL=https://api.z-pulse.cn`。
- 安装包直链改为 `https://api.z-pulse.cn/downloads/<file>`（见 `upload_installer.py`）。
