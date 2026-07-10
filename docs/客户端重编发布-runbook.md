# 客户端重编发布 Runbook（v1.0.4）

> 适用：Ed25519 密钥已于 2026-07-09 轮换，国内/国际后端双站已验证闭环，
> 客户端内嵌新公钥 `acc209fbd4e9e198c94aef7fc3faaf44f80e278357045959e1d6a2afb1044ceb`，
> 版本号已统一到 **1.0.4**（见 `site/api/version.json`、`project.yml`、`cpp/CMakeLists.txt`）。
> 本文件把「重编 → 签名 → 上传 → 放开下载」串成一条可复制命令链。

## 0. 前置结论（本沙箱状态）

- ✅ 新公钥已焊进 Windows 客户端源码 `cpp/core/src/license_manager.cpp`（`kLicensePublicKey`），无需再改。
- ✅ Windows 打包脚本 `cpp/scripts/package_windows_full.ps1` 已支持 **Authenticode 代码签名**（`-Sign -CertFile/-CertSha1`），默认产出未签名包并给出 SmartScreen 警告。
- ⚠️ **代码签名需补**：本机已配齐 Qt 6.11.1（`C:\Qt\6.11.1\mingw_64`）+ 自带 MinGW 13.1（`C:\Qt\Tools\mingw1310_64`）+ CMake 3.30.5 + ninja + WiX 3.14，可直接跑 `package_windows_full.ps1`。唯独**缺 signtool**（需 Windows SDK / 代码签名证书），因此只能出未签名包，SmartScreen 会拦截最终用户。
- ⚠️ 代码签名需要一张 Windows 代码签名证书（沃通/TrustAsia 标准证书，或 EV 直过 SmartScreen）。无证书则只能出未签名包，SmartScreen 会拦截最终用户。

## 1. 顺序约束（务必遵守）

1. 后端已就绪：国内 `api.z-pulse.cn` 与国际 `api.xlsone.com` 都用**同一个新种子**（已验证）。
2. 再发布带新公钥的客户端（本 runbook）。顺序反了，旧客户端对新签发的授权会验签失败。
3. 三端（Win/macOS/Linux）版本号统一为 **1.0.4**，与 `version.json` 一致。

## 2. Windows 国内版（Domestic）

环境：Windows 11 + Qt 6.11.1（`C:\Qt\6.11.1\mingw_64`）+ 自带 MinGW 13.1（`C:\Qt\Tools\mingw1310_64`）+ WiX 3.14（`C:\Qt\Tools\wix314`）+ cmake 3.30.5 + ninja。Qt6 用内置 `Qt6::ZlibPrivate`，不再依赖外部 zlib/MSYS2。

```powershell
cd cpp
# 未签名（仅验证构建）：
.\scripts\package_windows_full.ps1 -Domestic

# 签名发布（需证书）：
#   PFX 方式：
.\scripts\package_windows_full.ps1 -Domestic -Sign -CertFile .\codesign.pfx -CertPassword $env:XLSONE_CODESIGN_PASSWORD
#   或证书存储 thumbprint 方式：
.\scripts\package_windows_full.ps1 -Domestic -Sign -CertSha1 <你的证书SHA1>
```

产出：`cpp/build-windows-cn-release/xlsone-1.0.4-windows-amd64.msi` 与同名 `.zip`。默认未签名；加 `-Sign` 并配证书后可签名（本机当前无 signtool，只能出未签名包）。

上传到国内下载目录（走 SSH，需 `ZP_PASS`）：

```bash
export ZP_PASS='<root密码>'
python3 activation/domestic-server/deploy/upload_installer.py \
  cpp/build-windows-cn-release/xlsone-1.0.4-windows-amd64.msi
# 上传后可通过 https://z-pulse.cn/downloads/xlsone-1.0.4-windows-amd64.msi 下载
```

## 3. macOS（Developer ID 直发 / App Store）

环境：macOS + Xcode + 有效 Apple Developer 账号（Developer ID Application 证书 + App Store 发布证书二选一）。

- macOS 客户端走 HMAC/JWT，**本地不验 Ed25519**（见 `Sources/xlsOneLicense/LicenseManager.swift`），因此不依赖客户端公钥；重编主要是版本号与渠道对齐。
- ⚠️ 已知缺口：`project.yml` 目前只有一套 scheme（默认国际 `api.xlsone.com`），**没有国内直发 scheme**（设 `XLSONEActivationBaseURL=https://api.z-pulse.cn`）。若要走国内 Developer ID 直发，需先加 scheme/plist（与 Windows `windows-cn-release` 对应），二者互斥，需先定渠道策略。下面命令对现有 scheme 重编：

```bash
# 签名 + 公证（国内直发必须公证，否则 Gatekeeper 拦截）：
XLSONE_DEVELOPMENT_TEAM=<TeamID> \
APPLE_ID=<你的AppleID> \
APP_SPECIFIC_PASSWORD=<app专用密码> \
./scripts/package_macos_dmg.sh --signed --team-id <TeamID> --version 1.0.4 --notarize

# 仅本地未签名 DMG（自测）：
./scripts/package_macos_dmg.sh --version 1.0.4
```

上传（同 Windows 的 `upload_installer.py`，把 `.dmg` 传国内；或走国际 CDN）。

## 4. Linux（amd64 / arm64）

`version.json` 的 `downloads.linux_amd64` 已指向 `xlsOne-1.0.4-linux-amd64.deb`，因此**需随本次重编 linux amd64 .deb 并上传**（arm64 同理）。步骤见 `docs/build-linux-deb.md`（示例版本已同步到 1.0.4），产出后用 `upload_installer.py` 或 `scp` 上传到 `z-pulse.cn/downloads/`。

## 5. 放开下载页与版本检查

- `site/api/version.json` 已写 `latest_version=1.0.4`，并把 `downloads.linux_amd64` 指向 1.0.4（需第 4 节产物落地后才有文件）。
- 放开 `site/products/xlsone/download.html` 中 Windows / macOS 的「即将推出」→ 真实链接（Windows 指向 `xlsone-1.0.4-windows-amd64.msi`）。
- 若站点走 CloudFlare R2，按 `docs/release-phase-1.md` 同步 `version.json` 到 R2。

## 6. 发版前收口清单

- [ ] 三端版本号 = 1.0.4（`version.json` / `project.yml` / `cpp/CMakeLists.txt` 已统一）
- [ ] Windows 用 `-Sign` 出**已签名** MSI/ZIP（无证书则只能未签名，SmartScreen 拦截）
- [ ] macOS **已签名 + 已公证**（Developer ID 直发场景）
- [ ] Linux amd64/arm64 .deb 已重编并上传（否则 `version.json` 指向的 1.0.4 文件不存在）
- [ ] 国内后端 `api.z-pulse.cn` 与 Worker 同新种子（已验证）
- [ ] 下载页放开真实链接；`version.json` 下载指针全部有效
- [ ] `cpp/build-windows-*` 等构建产物目录确认已 gitignore（避免误提交；本次未提交任何构建产物）
