# xlsOne 自动更新检测设计

> 基于方案 A：启动时版本检查 + 弹窗引导下载

## 目标

为 xlsOne Qt 跨平台版本（macOS/Windows/Linux）提供版本更新通知能力：用户打开应用时自动检测新版本，有新版则弹窗提示并提供下载入口。Swift/macOS 版依赖 Mac App Store 自带更新机制，无需额外开发。

## 用户场景

1. 用户启动 xlsOne Qt 客户端
2. 客户端异步请求 `updates.xlsone.com/api/version`（5秒超时）
3. 如网络不可达，静默跳过，不打扰用户
4. 如服务端版本 > 本地版本，弹出更新提示对话框，显示版本号和更新日志
5. 用户点击"立即下载"→ 打开系统默认浏览器跳转下载页
6. 用户可随时通过菜单 **帮助 → 检查更新** 主动触发

## 架构

```
┌──────────────────────────────────────────────────┐
│                   服务端 (CloudFlare R2)           │
│  GET /api/version → 返回 JSON                     │
│  {                                                │
│    "latest_version": "1.3.0",                     │
│    "changelog": "...",                            │
│    "downloads": {                                 │
│      "macos": "https://...",                      │
│      "windows": "https://...",                    │
│      "linux": "https://...",                      │
│      "uos-x86_64": "https://...",                 │
│      "uos-arm64": "https://...",                  │
│      "uos-loongarch64": "https://..."             │
│    }                                              │
│  }                                                │
└────────────────────┬─────────────────────────────┘
                     │  HTTP GET (QNetworkAccessManager)
  ┌──────────────────┼──────────────────┐
  ▼                  ▼                  ▼
┌────────┐     ┌──────────┐     ┌──────────┐
│macOS Qt│     │ Windows  │     │  Linux   │
└────────┘     └──────────┘     └──────────┘
  检测到新版 → 弹 UpdateDialog → 用户点下载 → 浏览器打开 URL
```

## 文件结构

```
cpp/core/
├── include/xlsone/core/
│   └── update_checker.hpp     ← 新增：UpdateChecker 声明
├── src/
│   └── update_checker.cpp     ← 新增：UpdateChecker 实现

cpp/app/src/
├── update_dialog.cpp          ← 新增：更新提示弹窗
├── update_dialog.hpp          ← 新增：弹窗声明
├── main_window.cpp            ← 修改：集成 UpdateChecker + 菜单项
├── main_window.hpp            ← 修改：添加成员/槽函数

服务端（不发版时一次性部署）:
└── api/version.json           ← 静态 JSON，放 R2
```

## 组件设计

### UpdateChecker（cpp/core/include/xlsone/core/update_checker.hpp）

```
class UpdateChecker : public QObject
    - checkForUpdates(const QString& apiUrl)
    - 信号: updateAvailable(version, changelog, downloadUrl)
    - 信号: noUpdateAvailable()
    - 信号: checkError(message)
    - 内部: QNetworkAccessManager 异步 GET，5秒超时
    - 版本比较: 语义版本分割 (major.minor.patch)
```

### UpdateDialog（cpp/app/src/update_dialog.hpp）

```
class UpdateDialog : public QDialog
    - 构造函数接收: version, changelog, downloadUrl, parent
    - UI: QLabel 标题 + QTextEdit(只读，changelog) + "立即下载"按钮 + "稍后提醒"按钮
    - "立即下载": QDesktopServices::openUrl(downloadUrl)
```

### MainWindow 集成点

- 菜单栏新增: **帮助 → 检查更新**
- `buildUi()` 中初始化 UpdateChecker
- `showEvent()` 首次显示时触发自动检查
- 连接 UpdateChecker 信号到弹出 UpdateDialog

### 版本号来源

CMakeLists.txt 中 `project(xlsOneQt VERSION 0.1.0)` → 通过编译定义传递给 C++:

```cmake
target_compile_definitions(xlsone_app
    PRIVATE
        XLSONE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        XLSONE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        XLSONE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
```

UpdateChecker 读取 `XLSONE_VERSION` 宏作为当前版本。

### 平台检测

UpdateChecker 根据编译时宏选择 download url key:
- `Q_OS_MACOS` → "macos"
- `Q_OS_WIN` → "windows"
- `Q_OS_LINUX` → "linux" (默认)
- UOS 各架构通过运行时检测区分（后续可选）

## 错误处理

- 网络不可达: 静默跳过，不弹错误
- HTTP 非200: 静默跳过
- JSON 解析失败: 静默跳过
- 超时: 5秒后中止请求，静默跳过
- 原则: 更新检查失败不影响正常使用

## Swift/macOS 版

App Store 自动处理更新。可选在菜单 **帮助** 中添加一项 "在 App Store 中查看..."，打开 App Store 页面。

## 后续扩展

- 可在 UpdateDialog 中增加 "不再提示此版本" 复选框，写入 QSettings
- 可增加 Linux 系统包管理器检测，提示用户用 apt/pacman 更新
