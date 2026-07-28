# GitHub Actions：纯 C 多平台打包与 z-pulse.cn 自动部署

仓库通过 `.github/workflows/package.yml` 构建和部署纯 C11 桌面客户端。
默认发布流程不编译、不打包也不链接 Qt。

| 平台 | 构建基线 | 发布文件 |
| --- | --- | --- |
| Windows amd64 | `windows-2022`、MSVC、静态 SDL 2 | MSI、便携 ZIP |
| macOS Universal | `macos-15`、arm64 + x86_64、静态 SDL 2 | DMG |
| Linux amd64 | Debian 10、glibc 2.28 | DEB |
| Linux arm64 | Debian 10、glibc 2.28 | DEB |

规范文件名固定为：

```text
xlsone-<version>-windows-amd64.msi
xlsone-<version>-windows-amd64.zip
xlsOne-<version>-macos-universal.dmg
xlsOne-<version>-linux-amd64.deb
xlsOne-<version>-linux-arm64.deb
```

## 正式发布

版本唯一来源是根目录 `CMakeLists.txt`。正式标签必须指向已经推送到 GitHub
`main` 的提交：

```sh
python3 scripts/ci/release_version.py
git push github main
git tag -a v1.1.1 -m "xlsOne 1.1.1"
git push github v1.1.1
```

稳定标签会依次完成：

1. 校验标签版本与 CMake 版本一致，并确认标签提交属于远端 `main`。
2. 并行构建五个安装文件，运行核心回归测试、真实样本界面冒烟测试和 Qt
   依赖审计。
3. 创建 Draft GitHub Release。
4. 从本次产物生成 `version.json` 与 `checksums.txt`。
5. 上传到服务器 staging 目录，由服务器重新校验文件名、版本和 SHA-256。
6. 备份当前版本并原子切换 `z-pulse.cn` 生产文件。
7. 通过 HTTPS 回读线上清单、校验文件和五个下载地址。

预发布标签（如 `v1.1.2-rc.1`）只创建预发布草稿，不部署生产站点。

## 手工运行

只打包：

```sh
gh workflow run package.yml \
  --repo wang1st/xlsOne \
  --ref main \
  -f deploy_to_zpulse=false
```

从 `main` 手工打包并部署：

```sh
gh workflow run package.yml \
  --repo wang1st/xlsOne \
  --ref main \
  -f deploy_to_zpulse=true \
  -f changelog="本次更新说明"
```

## GitHub 生产环境

Environment 名称为 `zpulse-production`，需要以下配置：

| 类型 | 名称 | 说明 |
| --- | --- | --- |
| Secret | `ZPULSE_DEPLOY_SSH_PRIVATE_KEY` | 专用最小权限部署私钥 |
| Secret | `ZPULSE_DEPLOY_KNOWN_HOSTS` | 经可信渠道核验的 SSH 主机公钥 |
| Variable | `ZPULSE_HOST` | 默认 `z-pulse.cn` |
| Variable | `ZPULSE_PORT` | 默认 `22` |
| Variable | `ZPULSE_USER` | 默认 `xlsone-deploy` |

部署密钥必须与产品授权密钥分开。私钥只进入受保护的 GitHub Environment，
不写入仓库或构建产物；Actions 使用固定 `known_hosts`，不在运行时信任
`ssh-keyscan` 的即时结果。

## 服务器发布与回滚

服务器由以下脚本管理：

- `scripts/deploy/setup-zpulse-deploy-user.sh`：创建最小权限部署账户、目录和
  `sudoers` 规则。
- `scripts/deploy/zpulse-promote.sh`：校验 staging 载荷、备份当前线上文件并
  原子发布。

线上目录是 `/var/www/z-pulse.cn`，上传暂存目录是
`/srv/xlsone-upload/incoming/<release-id>`。发布脚本只接受规范版本号和五个
规范文件名，并在切换前逐项验证 `checksums.txt`。

需要回滚时，从服务器备份目录选择完整版本，先核验其中的
`api/version.json`、`downloads/checksums.txt` 和五个安装文件，再通过同一
发布脚本恢复，避免直接覆盖部分线上文件。

## 发布验收

工作流成功后仍应检查：

- `https://z-pulse.cn/api/version` 的 `latest` 为目标版本；
- `https://z-pulse.cn/downloads/checksums.txt` 包含五个文件；
- 五个下载地址均可访问且 SHA-256 与清单一致；
- GitHub Release 资产与线上文件一致；
- Windows/macOS 如未配置可信签名证书，会分别出现 SmartScreen 或
  Gatekeeper 提示。签名和 Apple 公证应作为后续发行基础设施补齐。
