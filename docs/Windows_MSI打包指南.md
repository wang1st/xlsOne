# xlsOne Windows MSI/ZIP 打包指南

## 概述

本文档说明如何使用 `package_windows_msi_zip.ps1` 脚本构建 xlsOne Windows 安装包（MSI + 便携 ZIP）。

生成的安装包包含：
- 开始菜单快捷方式
- 桌面快捷方式（可选安装）
- **安装完成后自动启动选项**（可选，默认不勾选）
- Qt 运行时自动打包
- 支持国内版/国际版切换

---

## 前置要求

### 必需工具

| 工具 | 版本 | 用途 | 安装方式 |
|------|------|------|----------|
| CMake | >= 3.22 | 构建系统 | `pip install cmake` |
| Ninja | 最新 | 构建工具 | `pip install ninja` |
| Qt (MinGW) | 5.15+ 或 6.x | GUI 框架 | [Qt 在线安装器](https://www.qt.io/download-qt-installer) |
| MinGW-w64 | 匹配 Qt 版本 | 编译器 | Qt 安装器自带 |
| WiX Toolset | 3.11+ | MSI 生成 | [下载 wix314-binaries.zip](https://github.com/wixtoolset/wix3/releases) |

### 推荐目录结构

```
C:\Qt\6.11.1\mingw_64          ← Qt 根目录
C:\Qt\Tools\mingw1310_64       ← MinGW 编译器
C:\Qt\Tools\wix314             ← WiX 工具集
C:\msys64\mingw64              ← zlib（仅 Qt5 需要）
```

### 环境变量（可选）

```powershell
# 代码签名密码（避免在命令行暴露）
$env:XLSONE_CODESIGN_PASSWORD = "your-password"
```

---

## 快速开始

### 1. 打开 PowerShell

```powershell
# 以管理员身份运行（推荐）
# 进入项目目录
cd D:\xlsone\cpp\scripts
```

### 2. 基础打包

```powershell
# 国际版（默认，激活服务器: api.xlsone.com）
.\package_windows_msi_zip.ps1

# 国内版（激活服务器: api.z-pulse.cn）
.\package_windows_msi_zip.ps1 -Domestic
```

### 3. 输出文件

打包完成后，MSI 文件位于：

```
# 国际版
cpp\build-windows-release\xlsone-1.0.4-windows-amd64.msi

# 国内版
cpp\build-windows-cn-release\xlsone-1.0.4-windows-amd64.msi
```

---

## 参数说明

### 路径参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-QtRoot` | 自动检测 | Qt MinGW 安装根目录 |
| `-MingwRoot` | `C:\Qt\Tools\mingw1310_64` | MinGW 编译器目录 |
| `-WiXRoot` | `C:\Qt\Tools\wix314` | WiX 工具集目录 |
| `-ZlibRoot` | `C:\msys64\mingw64` | zlib 目录（仅 Qt5） |
| `-BuildDir` | 自动 | CMake 构建目录 |

### 构建选项

| 参数 | 说明 |
|------|------|
| `-Domestic` | 国内版（激活服务器指向 `api.z-pulse.cn`） |
| `-Clean` | 清理构建目录后重新构建 |
| `-Preset` | CMake 构建类型，默认 `Release` |

### 代码签名参数

| 参数 | 说明 |
|------|------|
| `-Sign` | 启用代码签名 |
| `-CertFile` | PFX/P12 证书文件路径 |
| `-CertPassword` | 证书密码（建议用环境变量） |
| `-CertSha1` | 证书存储中的 SHA1 指纹 |
| `-SignTool` | signtool.exe 路径（自动检测） |
| `-TimestampServer` | 时间戳服务器，默认 `timestamp.digicert.com` |

---

## 使用示例

### 基础示例

```powershell
# 默认构建（国际版）
.\package_windows_msi_zip.ps1

# 国内版
.\package_windows_msi_zip.ps1 -Domestic

# 完全重新构建（清理 + 构建）
.\package_windows_msi_zip.ps1 -Clean

# 国内版 + 完全重新构建
.\package_windows_msi_zip.ps1 -Domestic -Clean
```

### 自定义路径

```powershell
# Qt 安装在非默认位置
.\package_windows_msi_zip.ps1 `
    -QtRoot "D:\Qt\6.11.1\mingw_64" `
    -MingwRoot "D:\Qt\Tools\mingw1310_64" `
    -WiXRoot "D:\Tools\wix314"
```

### 代码签名

```powershell
# 使用 PFX 证书
.\package_windows_msi_zip.ps1 -Sign -CertFile ".\codesign.pfx" -CertPassword "密码"

# 使用环境变量存储密码（更安全）
$env:XLSONE_CODESIGN_PASSWORD = "密码"
.\package_windows_msi_zip.ps1 -Sign -CertFile ".\codesign.pfx"

# 使用证书存储指纹
.\package_windows_msi_zip.ps1 -Sign -CertSha1 "A1B2C3D4E5F6789012345678901234567890ABCD"

# 国内版 + 签名
.\package_windows_msi_zip.ps1 -Domestic -Sign -CertFile ".\codesign.pfx"
```

### 完整示例（生产环境）

```powershell
# 生产构建：国内版 + 清理 + 签名
$env:XLSONE_CODESIGN_PASSWORD = (Read-Host "输入证书密码" -AsSecureString | ConvertFrom-SecureString)
.\package_windows_msi_zip.ps1 `
    -Domestic `
    -Clean `
    -Sign `
    -CertFile "C:\Certs\xlsone_codesign.pfx" `
    -TimestampServer "http://timestamp.digicert.com"
```

---

## 安装包特性

### 安装界面

安装完成后，用户会看到：

```
┌─────────────────────────────────┐
│  xlsOne 安装完成                │
│                                 │
│  [✓] 启动 xlsOne                │  ← 可选复选框（默认未勾选）
│                                 │
│           [完成]                │
└─────────────────────────────────┘
```

勾选后点击「完成」，自动启动 xlsOne。

### 多语言支持

安装包语言由 `XLSONE_INSTALLER_LANGUAGE` 环境变量控制：

| 语言 | 环境变量值 | 启动复选框文本 |
|------|-----------|--------------|
| 简体中文 | `zh_CN` | 启动 xlsOne |
| 繁体中文 | `zh_TW` | 啟動 xlsOne |
| 日语 | `ja` | xlsOne を起動する |
| 英语（默认）| `en` | Launch xlsOne |

设置方式：

```powershell
# 在 CMakeLists.txt 中配置，或构建时指定
$env:XLSONE_INSTALLER_LANGUAGE = "zh_CN"
.\package_windows_msi_zip.ps1
```

### 快捷方式

| 类型 | 默认 | 说明 |
|------|------|------|
| 开始菜单 | 安装 | 安装到 `开始菜单 > xlsOne` |
| 桌面 | 可选 | 用户在安装时可选择是否创建 |

---

## 常见问题

### Q: 脚本提示 "未找到 Qt 安装"

确保 Qt MinGW 版本已安装（不是 MSVC 版本）。检查路径：

```powershell
# 手动指定 Qt 路径
.\package_windows_msi_zip.ps1 -QtRoot "C:\Qt\6.11.1\mingw_64"
```

### Q: 提示 "缺少 WiX 工具"

下载 WiX 3.x 并解压到 `C:\Qt\Tools\wix314`：

```powershell
# 或使用自定义路径
.\package_windows_msi_zip.ps1 -WiXRoot "D:\wix314"
```

### Q: 代码签名失败

检查 signtool 是否可用：

```powershell
# 自动检测
Get-Command signtool

# 或手动指定
.\package_windows_msi_zip.ps1 -Sign -SignTool "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
```

### Q: 构建失败（Qt5 需要 zlib）

Qt5 构建需要外部 zlib：

```powershell
# MSYS2 安装 zlib
pacman -S mingw-w64-x86_64-zlib

# 然后指定路径
.\package_windows_msi_zip.ps1 -ZlibRoot "C:\msys64\mingw64"
```

### Q: 如何验证生成的 MSI

```powershell
# 查看 MSI 信息
msiexec /i xlsone-1.0.4-windows-amd64.msi /l*v install.log

# 静默安装
msiexec /i xlsone-1.0.4-windows-amd64.msi /qn

# 卸载
msiexec /x xlsone-1.0.4-windows-amd64.msi /qn
```

发布流水线还会执行 `cpp/scripts/verify_windows_msi_upgrade.ps1`：先真实安装带 Qt 6.11.1 的旧 MSI，再升级到候选 MSI，确认 `bin` 中 Qt DLL、`platforms\qwindows.dll`、开始菜单快捷方式和实际启动均正常。这个脚本会修改 Windows Installer 的机器级状态，因此默认只允许在 GitHub Actions 的一次性 runner 上运行；本机调试必须显式传入 `-AllowLocalMachineMutation`，并且检测到已有 xlsOne 时会拒绝继续。

---

## 故障排查

### 查看详细日志

```powershell
# 构建时保留详细输出
.\package_windows_msi_zip.ps1 -Clean 2>&1 | Tee-Object build.log
```

### 手动清理构建目录

```powershell
Remove-Item -Recurse -Force .\build-windows-release
Remove-Item -Recurse -Force .\build-windows-cn-release
```

### 检查 CMake 配置

```powershell
cmake -S . -B build-test -G Ninja -DCMAKE_BUILD_TYPE=Release
```

---

## 相关文件

| 文件 | 说明 |
|------|------|
| `cpp/scripts/package_windows_msi_zip.ps1` | Windows 唯一打包入口，生成 MSI + ZIP |
| `scripts/package_windows.sh` | macOS/Linux/WSL 上调用 Windows 打包机的包装入口 |
| `cpp/packaging/windows/shortcuts.wxs.in` | WiX 快捷方式模板（含启动选项） |
| `cpp/packaging/windows/shortcuts.wxs.patch.in` | WiX 功能补丁模板 |
| `cpp/CMakeLists.txt` | CMake 主配置（版本号、打包配置） |

---

## 版本历史

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-07-10 | 1.0 | 初始版本，支持 MSI 打包 + 启动选项 |
