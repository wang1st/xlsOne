# xlsOne 脚本入口

脚本入口按职责分区管理，完整规则见 `../docs/脚本管理指南.md`。

## 常用入口

| 任务 | 入口 |
|------|------|
| 本地构建并打开 Swift/macOS App | `scripts/build_and_open_app.sh` |
| 生成 Xcode 工程 | `scripts/generate_xcode_project.sh` |
| 校验 Xcode App 构建 | `scripts/validate_xcode_app.sh` |
| Swift/Xcode 版 macOS DMG | `scripts/package_macos_swift_dmg.sh` |
| Windows MSI/ZIP 打包包装器 | `scripts/package_windows.sh` |
| 国内站点与安装包一键部署 | `scripts/deploy/deploy.sh` |

## 平台专用入口

| 平台/模块 | 入口 |
|-----------|------|
| Qt/C++ 构建与测试 | `cpp/scripts/build.sh` |
| Linux `.deb` 打包 | `cpp/scripts/package_linux_deb.sh` |
| Qt macOS universal DMG | `cpp/scripts/package_macos_qt_dmg.sh` |
| Windows MSI/ZIP 真实打包 | `cpp/scripts/package_windows_msi_zip.ps1` |
| 生成图标资源 | `scripts/generate_app_icon.swift` |
| Windows 测试 VM 下载 | `scripts/download-test-vm.sh` |
| DDE 登录修复工具 | `scripts/fix-dde-login.sh` |
| Qt 混淆源码生成 | `cpp/scripts/generate_obfuscation.py` |
| Golden JSON 对比器 | `cpp/scripts/compare_snapshots.py` |
| 激活服务部署 | `activation/domestic-server/deploy/install.sh` |
| 单安装包上传 | `activation/domestic-server/deploy/upload_installer.py` |

## 约定

- 根目录不再新增脚本；需要入口时放到 `scripts/` 或对应模块的 `*/scripts/`。
- 旧脚本、临时测试脚本、乱码副本不要保留在仓库里。
- 新增脚本必须在本文件和 `docs/脚本管理指南.md` 登记用途、运行位置和替代关系。
