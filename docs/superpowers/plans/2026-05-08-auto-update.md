# xlsOne 自动更新检测实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 xlsOne Qt 客户端添加启动时版本检查与更新通知功能。

**Architecture:** UpdateChecker（core 库）通过 QNetworkAccessManager 异步请求版本 API JSON，解析并发射信号。UpdateDialog（app 层）接收信号后弹出对话框，引导用户下载。MainWindow 在 `buildUi()` 中连接信号，并在 `showEvent()` 首次显示时触发自动检查。

**Tech Stack:** Qt5 Core/Network/Gui/Widgets/Test

---

### Task 1: CMake 变更 — 添加 Qt5::Network 依赖 + 版本宏

**Files:**
- Modify: `cpp/core/CMakeLists.txt:15-30`

- [ ] **Step 1: 在 core CMakeLists.txt 中添加 Qt5::Network + 版本宏**

将 `cpp/core/CMakeLists.txt` 完整替换为：

```cmake
find_package(ZLIB REQUIRED)

add_library(xlsone_core STATIC
    src/biff8_xls_parser.cpp
    src/compound_file_reader.cpp
    src/excel_parser.cpp
    src/exporter.cpp
    src/merger.cpp
    src/models.cpp
    src/schema_repository.cpp
    src/validator.cpp
    src/zip_archive.cpp
)

target_include_directories(xlsone_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(xlsone_core
    PUBLIC
        Qt5::Core
        Qt5::Network
    PRIVATE
        ZLIB::ZLIB
)

target_compile_definitions(xlsone_core
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        XLSONE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        XLSONE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        XLSONE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
```

- [ ] **Step 2: 构建验证 CMake 配置**

```bash
cmake -S cpp -B cpp/build -G Ninja
```

Expected: 配置成功。

- [ ] **Step 3: 编译验证**

```bash
cmake --build cpp/build --target xlsone_core
```

Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add cpp/core/CMakeLists.txt
git commit -m "build: add Qt5::Network dependency and version macros to xlsone_core"
```

---

### Task 2: UpdateChecker 头文件

**Files:**
- Create: `cpp/core/include/xlsone/core/update_checker.hpp`

- [ ] **Step 1: 创建 UpdateChecker 头文件**

```cpp
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace xlsone {

struct UpdateInfo {
    QString latestVersion;
    QString changelog;
    QString downloadUrl;
};

class UpdateChecker final : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const;
    void checkForUpdates(const QString& apiUrl);

    static int compareVersions(const QString& lhs, const QString& rhs);
    static UpdateInfo parseUpdateInfo(const QByteArray& json);
    static QString platformKey();

signals:
    void updateAvailable(const UpdateInfo& info);
    void noUpdateAvailable();
    void checkError(const QString& message);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* networkManager_ = nullptr;
    int pendingRequests_ = 0;
};

} // namespace xlsone
```

- [ ] **Step 2: 提交**

```bash
git add cpp/core/include/xlsone/core/update_checker.hpp
git commit -m "feat: add UpdateChecker header"
```

---

### Task 3: UpdateChecker 实现

**Files:**
- Create: `cpp/core/src/update_checker.cpp`
- Modify: `cpp/core/CMakeLists.txt:3-12` (添加源文件)

- [ ] **Step 1: 注册源文件到 CMake**

在 `cpp/core/CMakeLists.txt` 的 `add_library(xlsone_core STATIC` 块中添加一行：

```cmake
add_library(xlsone_core STATIC
    src/biff8_xls_parser.cpp
    src/compound_file_reader.cpp
    src/excel_parser.cpp
    src/exporter.cpp
    src/merger.cpp
    src/models.cpp
    src/schema_repository.cpp
    src/update_checker.cpp
    src/validator.cpp
    src/zip_archive.cpp
)
```

- [ ] **Step 2: 实现 UpdateChecker**

创建 `cpp/core/src/update_checker.cpp`：

```cpp
#include "xlsone/core/update_checker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVersionNumber>

namespace xlsone {

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
{
    connect(networkManager_, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral("%1.%2.%3")
        .arg(XLSONE_VERSION_MAJOR)
        .arg(XLSONE_VERSION_MINOR)
        .arg(XLSONE_VERSION_PATCH);
}

void UpdateChecker::checkForUpdates(const QString& apiUrl)
{
    QNetworkRequest request(QUrl(apiUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    auto* reply = networkManager_->get(request);

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(5000);

    ++pendingRequests_;
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    --pendingRequests_;

    if (reply->error() != QNetworkReply::NoError) {
        return; // Silently ignore network errors
    }

    const int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode != 200) {
        return;
    }

    const QByteArray body = reply->readAll();
    const UpdateInfo info = parseUpdateInfo(body);

    if (info.latestVersion.isEmpty() || info.downloadUrl.isEmpty()) {
        return;
    }

    if (compareVersions(info.latestVersion, currentVersion()) > 0) {
        emit updateAvailable(info);
    } else {
        emit noUpdateAvailable();
    }
}

int UpdateChecker::compareVersions(const QString& lhs, const QString& rhs)
{
    return QVersionNumber::compare(
        QVersionNumber::fromString(lhs),
        QVersionNumber::fromString(rhs));
}

UpdateInfo UpdateChecker::parseUpdateInfo(const QByteArray& json)
{
    UpdateInfo info;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return info;
    }

    const QJsonObject root = doc.object();
    info.latestVersion = root.value(QStringLiteral("latest_version")).toString();
    info.changelog = root.value(QStringLiteral("changelog")).toString();

    const QJsonObject downloads = root.value(QStringLiteral("downloads")).toObject();
    const QString key = platformKey();
    info.downloadUrl = downloads.value(key).toString();

    return info;
}

QString UpdateChecker::platformKey()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace xlsone
```

- [ ] **Step 3: 验证编译**

```bash
cmake --build cpp/build --target xlsone_core
```

Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add cpp/core/src/update_checker.cpp cpp/core/CMakeLists.txt
git commit -m "feat: implement UpdateChecker"
```

---

### Task 4: UpdateChecker 测试

**Files:**
- Modify: `cpp/tests/core_tests.cpp` (添加 include 和测试用例)
- Modify: `cpp/tests/CMakeLists.txt` (添加 Qt5::Network 链接)

- [ ] **Step 1: 添加 Qt5::Network 到测试链接**

在 `cpp/tests/CMakeLists.txt` 的 `target_link_libraries` 中添加 `Qt5::Network`：

```cmake
target_link_libraries(xlsone_core_tests
    PRIVATE
        xlsone_core
        Qt5::Test
        Qt5::Network
)
```

- [ ] **Step 2: 在 core_tests.cpp 头部添加 include**

找到 `#include "xlsone/core/validator.hpp"` 之后，添加：

```cpp
#include "xlsone/core/update_checker.hpp"
```

- [ ] **Step 3: 在 CoreTests 类的 private slots 中添加测试声明**

找到 `private slots:` 区域的末尾行 `void parsesLocalXianjuXlsWhenPresent();` 之后添加：

```cpp
    void updateCheckerCurrentVersionIsValid();
    void updateCheckerCompareVersions();
    void updateCheckerParseUpdateInfoJson();
    void updateCheckerParseInvalidJson();
    void updateCheckerPlatformKeyIsNotEmpty();
```

- [ ] **Step 4: 在文件末尾 `#include "core_tests.moc"` 之前添加测试实现**

```cpp
void CoreTests::updateCheckerCurrentVersionIsValid()
{
    UpdateChecker checker;
    const QString ver = checker.currentVersion();
    QVERIFY(!ver.isEmpty());
    const auto parts = ver.split(QLatin1Char('.'));
    QCOMPARE(parts.size(), 3);
    for (const auto& p : parts) {
        bool ok = false;
        p.toInt(&ok);
        QVERIFY(ok);
    }
}

void CoreTests::updateCheckerCompareVersions()
{
    // equal
    QCOMPARE(UpdateChecker::compareVersions(
        QStringLiteral("1.0.0"), QStringLiteral("1.0.0")), 0);
    // patch bump
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.0.1"), QStringLiteral("1.0.0")) > 0);
    // minor bump
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.1.0"), QStringLiteral("1.0.9")) > 0);
    // major bump
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("2.0.0"), QStringLiteral("1.9.9")) > 0);
    // older
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("0.9.0"), QStringLiteral("1.0.0")) < 0);
    // two-digit versions
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.10.0"), QStringLiteral("1.9.0")) > 0);
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.2.0"), QStringLiteral("1.10.0")) < 0);
}

void CoreTests::updateCheckerParseUpdateInfoJson()
{
    const QByteArray json = R"({
        "latest_version": "2.0.0",
        "changelog": "test changelog",
        "downloads": {
            "linux": "https://example.com/linux.deb",
            "windows": "https://example.com/win.exe",
            "macos": "https://example.com/mac.dmg"
        }
    })";

    const auto info = UpdateChecker::parseUpdateInfo(json);

    QCOMPARE(info.latestVersion, QStringLiteral("2.0.0"));
    QCOMPARE(info.changelog, QStringLiteral("test changelog"));
    QVERIFY(!info.downloadUrl.isEmpty());
}

void CoreTests::updateCheckerParseInvalidJson()
{
    const QByteArray bad = "not valid json";
    const auto info = UpdateChecker::parseUpdateInfo(bad);
    QVERIFY(info.latestVersion.isEmpty());
    QVERIFY(info.downloadUrl.isEmpty());
}

void CoreTests::updateCheckerPlatformKeyIsNotEmpty()
{
    const QString key = UpdateChecker::platformKey();
    QVERIFY(!key.isEmpty());
}
```

- [ ] **Step 5: 构建并运行测试**

```bash
cmake --build cpp/build && ctest --test-dir cpp/build --output-on-failure -R updateChecker
```

Expected: 5 个新增测试全部 PASS。

- [ ] **Step 6: 提交**

```bash
git add cpp/tests/core_tests.cpp cpp/tests/CMakeLists.txt
git commit -m "test: add UpdateChecker unit tests"
```

---

### Task 5: UpdateDialog 头文件

**Files:**
- Create: `cpp/app/src/update_dialog.hpp`

- [ ] **Step 1: 创建 UpdateDialog 头文件**

```cpp
#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTextEdit;

class UpdateDialog final : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(const QString& version,
                          const QString& changelog,
                          const QString& downloadUrl,
                          QWidget* parent = nullptr);

private:
    QString downloadUrl_;
};
```

- [ ] **Step 2: 提交**

```bash
git add cpp/app/src/update_dialog.hpp
git commit -m "feat: add UpdateDialog header"
```

---

### Task 6: UpdateDialog 实现

**Files:**
- Create: `cpp/app/src/update_dialog.cpp`
- Modify: `cpp/app/CMakeLists.txt:1-23` (添加源文件)

- [ ] **Step 1: 注册源文件到 CMake**

在 `cpp/app/CMakeLists.txt` 的 `add_executable(xlsone_app` 块中添加：

```cmake
add_executable(xlsone_app
    src/diagnostics_view.cpp
    src/diagnostics_view.hpp
    src/empty_workspace_view.cpp
    src/empty_workspace_view.hpp
    src/inspector_panel.cpp
    src/inspector_panel.hpp
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/merged_table_delegate.cpp
    src/merged_table_delegate.hpp
    src/merged_table_model.cpp
    src/merged_table_model.hpp
    src/schema_manager_dialog.cpp
    src/schema_manager_dialog.hpp
    src/sheet_strip.cpp
    src/sheet_strip.hpp
    src/ui_theme.cpp
    src/ui_theme.hpp
    src/update_dialog.cpp
    src/update_dialog.hpp
    src/workspace_chrome.cpp
    src/workspace_chrome.hpp
)
```

- [ ] **Step 2: 实现 UpdateDialog**

创建 `cpp/app/src/update_dialog.cpp`：

```cpp
#include "update_dialog.hpp"

#include <QDesktopServices>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(const QString& version,
                           const QString& changelog,
                           const QString& downloadUrl,
                           QWidget* parent)
    : QDialog(parent)
    , downloadUrl_(downloadUrl)
{
    setWindowTitle(tr("发现新版本"));
    setMinimumWidth(420);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(
        tr("新版本 %1 已发布").arg(version), this);
    auto font = titleLabel->font();
    font.setPointSize(14);
    font.setBold(true);
    titleLabel->setFont(font);
    layout->addWidget(titleLabel);

    auto* changelogEdit = new QTextEdit(this);
    changelogEdit->setReadOnly(true);
    changelogEdit->setPlainText(changelog);
    changelogEdit->setMinimumHeight(160);
    layout->addWidget(changelogEdit);

    layout->addStretch();

    auto* downloadButton = new QPushButton(tr("立即下载"), this);
    downloadButton->setDefault(true);
    connect(downloadButton, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(downloadUrl_));
        accept();
    });
    layout->addWidget(downloadButton);

    auto* laterButton = new QPushButton(tr("稍后提醒"), this);
    connect(laterButton, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(laterButton);
}
```

- [ ] **Step 3: 验证编译**

```bash
cmake --build cpp/build --target xlsone_app
```

Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add cpp/app/src/update_dialog.cpp cpp/app/src/update_dialog.hpp cpp/app/CMakeLists.txt
git commit -m "feat: implement UpdateDialog"
```

---

### Task 7: MainWindow 集成 UpdateChecker

**Files:**
- Modify: `cpp/app/src/main_window.hpp:23-119` (添加 forward declaration, include, 成员, 槽函数, showEvent)
- Modify: `cpp/app/src/main_window.cpp:1-551` (添加 include, buildUi 集成, showEvent, checkForUpdates)

- [ ] **Step 1: 在 main_window.hpp 添加 QShowEvent forward declaration**

在 `class QDragLeaveEvent;` 之后添加一行：

```cpp
class QShowEvent;
```

- [ ] **Step 2: 在 main_window.hpp 添加 UpdateChecker include 和成员**

在最后一个 `#include` 行（`#include <optional>`）之后添加：

```cpp
#include "xlsone/core/update_checker.hpp"
```

在 `private slots:` 区域末尾（`void inspectCell` 之后）添加：

```cpp
    void checkForUpdates();
```

在 `protected:` 区域（`void dropEvent(QDropEvent* event) override;` 之后）添加：

```cpp
    void showEvent(QShowEvent* event) override;
```

在 `private:` 区域末尾（`QLabel* statusLabel_ = nullptr;` 之后）添加：

```cpp
    xlsone::UpdateChecker* updateChecker_ = nullptr;
    bool firstShow_ = true;
```

- [ ] **Step 3: 在 main_window.cpp 顶部添加 update_dialog.hpp include**

找到 `#include "schema_manager_dialog.hpp"` 附近，在它之后添加：

```cpp
#include "update_dialog.hpp"
```

- [ ] **Step 4: 在 main_window.cpp 的 buildUi() 中添加"检查更新"菜单项**

找到帮助菜单代码块，当前代码为：

```cpp
    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(helpAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);
```

替换为：

```cpp
    auto* checkUpdateAction = new QAction(tr("检查更新"), this);
    connect(checkUpdateAction, &QAction::triggered, this, &MainWindow::checkForUpdates);

    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(checkUpdateAction);
    helpMenu->addSeparator();
    helpMenu->addAction(helpAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);
```

- [ ] **Step 5: 在 main_window.cpp 的 buildUi() 尾部初始化 UpdateChecker**

找到 `buildUi()` 函数的最后语句 `updateChromeState();`（约第550行），在它之前插入：

```cpp
    updateChecker_ = new xlsone::UpdateChecker(this);
    connect(updateChecker_, &xlsone::UpdateChecker::updateAvailable, this,
        [this](const xlsone::UpdateInfo& info) {
            auto* dialog = new UpdateDialog(info.latestVersion,
                                            info.changelog,
                                            info.downloadUrl,
                                            this);
            dialog->show();
        });
```

- [ ] **Step 6: 在 main_window.cpp 中添加 showEvent 和 checkForUpdates 实现**

在 `buildUi()` 函数结束的 `}` 之后、`dragEnterEvent()` 开始之前插入：

```cpp
void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (firstShow_) {
        firstShow_ = false;
        checkForUpdates();
    }
}

void MainWindow::checkForUpdates()
{
    if (updateChecker_ == nullptr) {
        return;
    }
    const QString apiUrl = QStringLiteral(
        "https://updates.xlsone.com/api/version");
    updateChecker_->checkForUpdates(apiUrl);
}
```

- [ ] **Step 7: 编译验证**

```bash
cmake --build cpp/build
```

Expected: 编译成功，无错误。

- [ ] **Step 8: 提交**

```bash
git add cpp/app/src/main_window.hpp cpp/app/src/main_window.cpp
git commit -m "feat: integrate UpdateChecker into MainWindow with menu and showEvent"
```

---

### Task 8: Swift macOS 版菜单补充

**Files:**
- Modify: `Sources/xlsOne/XlsOneApp.swift` 或 `App/xlsOneMacApp/XlsOneMacApp.swift`

- [ ] **Step 1: 添加"检查更新"菜单项**

找到 App 文件中创建菜单的位置（可能是 `.commands {}` 或 `MenuBarExtra`），在帮助菜单区域添加一个打开 App Store 的命令：

```swift
// In the Commands block:
CommandGroup(replacing: .appInfo) {
    Button("在 App Store 中查看...") {
        if let url = URL(string: "macappstore://apps.apple.com/app/idXXXXXXXXXX") {
            NSWorkspace.shared.open(url)
        }
    }
}
```

> 注：`idXXXXXXXXXX` 需替换为实际的 App Store App ID。初次可留占位，待上架后填入。

- [ ] **Step 2: 提交**

```bash
git add App/xlsOneMacApp/XlsOneMacApp.swift
git commit -m "feat: add App Store link to Help menu"
```

---

### Task 9: 服务端静态 JSON 部署

**Files:**
- Create: `site/api/version.json`

- [ ] **Step 1: 创建版本 JSON 模板**

```json
{
  "latest_version": "0.1.0",
  "changelog": "初始版本发布",
  "downloads": {
    "macos": "https://downloads.xlsone.com/v0.1.0/xlsOne-0.1.0-macos.dmg",
    "windows": "https://downloads.xlsone.com/v0.1.0/xlsOne-0.1.0-win64.exe",
    "linux": "https://downloads.xlsone.com/v0.1.0/xlsOne-0.1.0-linux-x86_64.AppImage"
  }
}
```

- [ ] **Step 2: 添加到部署脚本说明**

在 `docs/release-phase-1.md` 末尾添加发版步骤：

```markdown
## 版本更新发布

每次发版后在 `site/api/version.json` 中更新 `latest_version` 和 `changelog`，并同步到 CloudFlare R2。
```

- [ ] **Step 3: 提交**

```bash
git add site/api/version.json docs/release-phase-1.md
git commit -m "chore: add version API JSON template and release docs"
```

---

### Task 10: 全量构建 + 测试验证

- [ ] **Step 1: 清理构建并全量编译**

```bash
rm -rf cpp/build
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
```

Expected: 0 错误。

- [ ] **Step 2: 运行全部测试**

```bash
ctest --test-dir cpp/build --output-on-failure
```

Expected: 所有测试通过。

- [ ] **Step 3: 最终提交（如有遗漏）**

```bash
git status
```
