# GitHub Actions 多平台自动打包

仓库使用 `.github/workflows/package.yml` 自动构建 Qt/C++ 客户端，当前覆盖：

| 平台 | Runner | 产物 |
|---|---|---|
| Windows amd64 | `windows-2022` | MSI、便携 ZIP |
| macOS Universal | `macos-15` | 同时包含 arm64、x86_64 的 DMG |
| Linux amd64 | `ubuntu-22.04` | 自包含 DEB |
| Linux arm64 | `ubuntu-22.04-arm` | 自包含 DEB |

## 触发方式

### 手工打包

进入 GitHub 仓库的 **Actions → Multi-platform packages → Run workflow**，选择版本口味：

- `domestic`：国内版，服务端点为 `api.z-pulse.cn`；
- `international`：国际版，服务端点为 `api.xlsone.com`。

四个平台构建完成后，可在该次运行的 **Artifacts** 区域下载。Artifacts 保留 14 天。

正式打包任务会在 Windows、macOS 和 Linux 上统一启用发布混淆，并把同一把 Ed25519 公钥编译进客户端。运行前必须在仓库的 **Settings → Secrets and variables → Actions → Secrets** 中创建：

```text
ED25519_PUBLIC_KEY=<64 位十六进制公钥>
```

本机的权威来源是当前用户主目录下的 `secrets.json`。只同步其中的 `ED25519_PUBLIC_KEY`；`ED25519_PRIVATE_KEY`、`ACTIVATION_SECRET` 和 `ADMIN_API_KEY` 都是服务端密钥，禁止上传到 GitHub Actions，也禁止编译进客户端。

### 标签打包

推送与项目版本一致的标签，例如：

```bash
git tag -a v1.0.6 -m "xlsOne 1.0.6"
git push github v1.0.6
```

标签必须与 `cpp/CMakeLists.txt` 和 `site/api/version.json` 中的版本一致，否则工作流会在打包前失败。标签构建成功后会创建一个 **Draft GitHub Release**，聚合所有平台的安装包及 `SHA256SUMS.txt`。确认产物后再手工发布 Draft Release。

标签构建默认使用国内版。若要让标签默认构建国际版，在 GitHub 仓库的 **Settings → Secrets and variables → Actions → Variables** 中添加：

```text
RELEASE_EDITION=international
```

## 当前安全边界

- 当前自动产物未做 Windows Authenticode 或 Apple Developer ID 正式签名，因此 Release 默认保持 Draft。
- macOS DMG 使用 ad-hoc 签名，只适合内部测试；面向用户分发前仍需 Developer ID 签名和 Apple 公证。
- Windows MSI/ZIP 未签名，测试机可能显示 SmartScreen 警告。
- 打包只需要授权公钥，也绝不能把 `ED25519_PRIVATE_KEY`、`ACTIVATION_SECRET`、`ADMIN_API_KEY`、证书密码或服务器密码写入仓库。
- Ubuntu 22.04 产物不能据此宣称兼容所有旧版 UOS/Kylin；面向特定国产系统发布前仍应在目标系统验证。

## GitHub 仓库设置

1. 使用私有仓库，避免业务报表、授权备份或历史材料意外公开。
2. 在 **Settings → Actions → General** 中允许本仓库使用 Actions。
3. 若组织策略限制第三方 Action，允许：
   - `jurplel/install-qt-action`；
   - `softprops/action-gh-release`；
   - GitHub 官方 `actions/*`。
4. Release job 仅申请 `contents: write`，其他构建 job 使用 `contents: read`。

## 本地检查

版本一致性检查：

```bash
python scripts/ci/release_version.py
python scripts/ci/release_version.py --tag v1.0.6
```

常规 C++ CI 位于 `.github/workflows/cpp-qt.yml`。它关闭发布混淆，仅执行跨平台编译、测试和格式检查；安装包构建与发布由 `package.yml` 独立完成。
