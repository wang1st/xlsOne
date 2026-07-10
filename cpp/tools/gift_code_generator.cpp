#include "gift_code_generator.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace xlsone {

// ============================================================
// Construction & UI
// ============================================================

GiftCodeGenerator::GiftCodeGenerator(QWidget* parent)
    : QMainWindow(parent)
{
    net_ = new QNetworkAccessManager(this);
    setWindowTitle(QStringLiteral("表表归一 · 授权码生成器"));
    setMinimumSize(720, 600);
    resize(860, 720);

    buildUi();
    applyDarkTheme();
    loadSettings();
    loadHistory();
    renderHistory();
}

void GiftCodeGenerator::buildUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setSpacing(16);
    root->setContentsMargins(24, 24, 24, 24);

    // ---- Header ----
    auto* headerRow = new QHBoxLayout;
    auto* titleLabel = new QLabel(QStringLiteral("授权码生成器"), central);
    auto titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    auto* versionLabel = new QLabel(QStringLiteral("v1.0"), central);
    versionLabel->setStyleSheet(
        "QLabel { background:#161820; color:#9aa0ab; padding:2px 8px;"
        "border-radius:5px; border:1px solid #2a2e37; font-size:12px; }");

    connLabel_ = new QLabel(QStringLiteral("● 未连接"), central);
    connLabel_->setStyleSheet("QLabel { color:#9aa0ab; font-size:12px; }");

    headerRow->addWidget(titleLabel);
    headerRow->addWidget(versionLabel);
    headerRow->addStretch();
    headerRow->addWidget(connLabel_);
    root->addLayout(headerRow);

    // ---- Server config ----
    auto* serverGroup = new QGroupBox(QStringLiteral("  服务器配置  "), central);
    auto* serverLayout = new QHBoxLayout(serverGroup);

    apiUrlCombo_ = new QComboBox(serverGroup);
    apiUrlCombo_->addItem(QStringLiteral("国内 (api.z-pulse.cn)"),
                           QStringLiteral("https://api.z-pulse.cn"));
    apiUrlCombo_->addItem(QStringLiteral("国际 (api.xlsone.com)"),
                           QStringLiteral("https://api.xlsone.com"));
    apiUrlCombo_->addItem(QStringLiteral("自定义..."), QStringLiteral("custom"));

    customUrlEdit_ = new QLineEdit(serverGroup);
    customUrlEdit_->setPlaceholderText(QStringLiteral("https://your-server.com"));
    customUrlEdit_->setVisible(false);

    apiKeyEdit_ = new QLineEdit(serverGroup);
    apiKeyEdit_->setPlaceholderText(QStringLiteral("Admin API Key (Bearer Token)"));
    apiKeyEdit_->setEchoMode(QLineEdit::Password);

    testBtn_ = new QPushButton(QStringLiteral("测试连接"), serverGroup);

    serverLayout->addWidget(new QLabel(QStringLiteral("API 地址:"), serverGroup));
    serverLayout->addWidget(apiUrlCombo_, 1);
    serverLayout->addWidget(customUrlEdit_, 1);
    serverLayout->addWidget(new QLabel(QStringLiteral("Key:"), serverGroup));
    serverLayout->addWidget(apiKeyEdit_, 2);
    serverLayout->addWidget(testBtn_);
    root->addWidget(serverGroup);

    // ---- Generate ----
    auto* genGroup = new QGroupBox(QStringLiteral("  生成授权码  "), central);
    auto* genLayout = new QVBoxLayout(genGroup);

    platformTabs_ = new QTabWidget(genGroup);

    // -- Windows tab --
    auto* winTab = new QWidget;
    auto* winLayout = new QHBoxLayout(winTab);
    winCountSpin_ = new QSpinBox(winTab);
    winCountSpin_->setRange(1, 100);
    winCountSpin_->setValue(1);
    winPlanCombo_ = new QComboBox(winTab);
    winPlanCombo_->addItem(QStringLiteral("个人终身版"), QStringLiteral("personal_lifetime"));
    winPlanCombo_->addItem(QStringLiteral("个人年度版"), QStringLiteral("personal_yearly"));
    winPlanCombo_->addItem(QStringLiteral("团队终身版"), QStringLiteral("team_lifetime"));
    winPlanCombo_->addItem(QStringLiteral("试用版"), QStringLiteral("trial"));
    winLayout->addWidget(new QLabel(QStringLiteral("数量:"), winTab));
    winLayout->addWidget(winCountSpin_);
    winLayout->addWidget(new QLabel(QStringLiteral("套餐:"), winTab));
    winLayout->addWidget(winPlanCombo_, 1);
    winLayout->addStretch();
    platformTabs_->addTab(winTab, QStringLiteral("Windows (Ed25519)"));

    // -- macOS tab --
    auto* macTab = new QWidget;
    auto* macLayout = new QHBoxLayout(macTab);
    macCountSpin_ = new QSpinBox(macTab);
    macCountSpin_->setRange(1, 100);
    macCountSpin_->setValue(1);
    macPlanCombo_ = new QComboBox(macTab);
    macPlanCombo_->addItem(QStringLiteral("个人终身版"), QStringLiteral("personal_lifetime"));
    macPlanCombo_->addItem(QStringLiteral("个人年度版"), QStringLiteral("personal_yearly"));
    macPlanCombo_->addItem(QStringLiteral("团队终身版"), QStringLiteral("team_lifetime"));
    macPlanCombo_->addItem(QStringLiteral("试用版"), QStringLiteral("trial"));
    macMaxDevicesSpin_ = new QSpinBox(macTab);
    macMaxDevicesSpin_->setRange(1, 10);
    macMaxDevicesSpin_->setValue(1);
    macExpiryEdit_ = new QDateEdit(QDate::currentDate().addMonths(1), macTab);
    macExpiryEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    macExpiryEdit_->setCalendarPopup(true);
    macLayout->addWidget(new QLabel(QStringLiteral("数量:"), macTab));
    macLayout->addWidget(macCountSpin_);
    macLayout->addWidget(new QLabel(QStringLiteral("套餐:"), macTab));
    macLayout->addWidget(macPlanCombo_, 1);
    macLayout->addWidget(new QLabel(QStringLiteral("设备数:"), macTab));
    macLayout->addWidget(macMaxDevicesSpin_);
    macLayout->addWidget(new QLabel(QStringLiteral("过期:"), macTab));
    macLayout->addWidget(macExpiryEdit_);
    macLayout->addStretch();
    platformTabs_->addTab(macTab, QStringLiteral("macOS (HMAC/JWT)"));

    genLayout->addWidget(platformTabs_);

    auto* genActionRow = new QHBoxLayout;
    generateBtn_ = new QPushButton(QStringLiteral("  生成授权码  "), genGroup);
    generateBtn_->setCursor(Qt::PointingHandCursor);
    progressBar_ = new QProgressBar(genGroup);
    progressBar_->setVisible(false);
    progressBar_->setRange(0, 0);
    progressBar_->setTextVisible(false);
    progressBar_->setMaximumWidth(120);
    genActionRow->addWidget(generateBtn_);
    genActionRow->addWidget(progressBar_);
    genActionRow->addStretch();
    genLayout->addLayout(genActionRow);

    root->addWidget(genGroup);

    // ---- Results ----
    auto* resultGroup = new QGroupBox(QStringLiteral("  生成结果  "), central);
    auto* resultLayout = new QVBoxLayout(resultGroup);

    auto* resultActionRow = new QHBoxLayout;
    auto* copyAllBtn = new QPushButton(QStringLiteral("全部复制"), resultGroup);
    auto* exportBtn = new QPushButton(QStringLiteral("导出 TXT"), resultGroup);
    resultActionRow->addStretch();
    resultActionRow->addWidget(copyAllBtn);
    resultActionRow->addWidget(exportBtn);
    resultLayout->addLayout(resultActionRow);

    resultEdit_ = new QTextEdit(resultGroup);
    resultEdit_->setReadOnly(true);
    resultEdit_->setMinimumHeight(140);
    resultEdit_->setPlaceholderText(QStringLiteral("生成的授权码将显示在这里..."));
    resultLayout->addWidget(resultEdit_);

    root->addWidget(resultGroup);

    // ---- History ----
    auto* historyGroup = new QGroupBox(QStringLiteral("  生成历史  "), central);
    auto* historyLayout = new QVBoxLayout(historyGroup);

    auto* historyActionRow = new QHBoxLayout;
    clearHistoryBtn_ = new QPushButton(QStringLiteral("清空"), historyGroup);
    historyActionRow->addStretch();
    historyActionRow->addWidget(clearHistoryBtn_);
    historyLayout->addLayout(historyActionRow);

    historyList_ = new QListWidget(historyGroup);
    historyList_->setMinimumHeight(120);
    historyList_->setAlternatingRowColors(true);
    historyLayout->addWidget(historyList_);

    root->addWidget(historyGroup);

    // ---- Connections ----
    connect(apiUrlCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GiftCodeGenerator::onApiUrlChanged);
    connect(platformTabs_, &QTabWidget::currentChanged,
            this, &GiftCodeGenerator::onPlatformChanged);
    connect(testBtn_, &QPushButton::clicked,
            this, &GiftCodeGenerator::onTestConnection);
    connect(generateBtn_, &QPushButton::clicked,
            this, &GiftCodeGenerator::onGenerate);
    connect(copyAllBtn, &QPushButton::clicked,
            this, &GiftCodeGenerator::onCopyAll);
    connect(exportBtn, &QPushButton::clicked,
            this, &GiftCodeGenerator::onExportTxt);
    connect(clearHistoryBtn_, &QPushButton::clicked,
            this, &GiftCodeGenerator::onClearHistory);
    connect(historyList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                QString code = item->data(Qt::UserRole).toString();
                if (!code.isEmpty()) onCopyCode(code);
            });
}

// ============================================================
// Dark theme
// ============================================================

void GiftCodeGenerator::applyDarkTheme()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background-color: #0f1115; color: #e6e8ec;"
        "  font-family: 'PingFang SC','Microsoft YaHei',system-ui,sans-serif; }"

        "QGroupBox {"
        "  background-color: #1a1d24; border: 1px solid #2a2e37;"
        "  border-radius: 10px; margin-top: 12px; padding: 16px 12px 12px 12px;"
        "  font-size: 13px; font-weight: 600; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; subcontrol-position: top left;"
        "  padding: 0 8px; color: #e6e8ec; }"

        "QLineEdit, QComboBox, QSpinBox, QDateEdit {"
        "  background-color: #12141a; color: #e6e8ec;"
        "  border: 1px solid #2a2e37; border-radius: 8px;"
        "  padding: 8px 12px; font-size: 14px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateEdit:focus {"
        "  border-color: #4f8cff; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: none; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #161820; color: #e6e8ec;"
        "  border: 1px solid #2a2e37; selection-background-color: #2a5bb8; }"

        "QPushButton {"
        "  background-color: #161820; color: #9aa0ab;"
        "  border: 1px solid #2a2e37; border-radius: 8px;"
        "  padding: 8px 18px; font-size: 14px; }"
        "QPushButton:hover { color: #e6e8ec; border-color: #3a3e47; }"
        "QPushButton:pressed { background-color: #1a1d24; }"
        "QPushButton:disabled { color: #4a505a; }"

        // Generate button — primary style
        "QPushButton#generateBtn {"
        "  background-color: #4f8cff; color: #ffffff;"
        "  border: none; border-radius: 8px; font-weight: 500; }"
        "QPushButton#generateBtn:hover { background-color: #3a7aee; }"
        "QPushButton#generateBtn:disabled { background-color: #2a5bb8; color: #6a8acc; }"

        "QTextEdit {"
        "  background-color: #161820; color: #e6e8ec;"
        "  border: 1px solid #2a2e37; border-radius: 8px;"
        "  padding: 8px; font-family: 'Consolas','SF Mono','Monaco',monospace;"
        "  font-size: 14px; }"

        "QListWidget {"
        "  background-color: #161820; color: #e6e8ec;"
        "  border: 1px solid #2a2e37; border-radius: 8px;"
        "  padding: 4px; font-size: 13px; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 4px; }"
        "QListWidget::item:alternate { background-color: #12141a; }"
        "QListWidget::item:selected { background-color: #2a5bb8; }"

        "QTabWidget::pane {"
        "  border: 1px solid #2a2e37; border-radius: 8px;"
        "  background-color: #161820; }"
        "QTabBar::tab {"
        "  background-color: #12141a; color: #9aa0ab;"
        "  padding: 8px 16px; border: 1px solid #2a2e37;"
        "  border-bottom: none; border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px; font-size: 13px; }"
        "QTabBar::tab:selected {"
        "  background-color: #2a5bb8; color: #ffffff; }"

        "QProgressBar {"
        "  border: none; background-color: #1a1d24;"
        "  border-radius: 4px; max-height: 4px; }"
        "QProgressBar::chunk { background-color: #4f8cff; border-radius: 4px; }"

        "QLabel { background: transparent; }"
    ));

    generateBtn_->setObjectName("generateBtn");
}

// ============================================================
// Settings persistence (QSettings replaces localStorage)
// ============================================================

void GiftCodeGenerator::loadSettings()
{
    QSettings s;
    s.beginGroup("GiftCodeGenerator");
    int apiIdx = s.value("apiUrlIndex", 0).toInt();
    if (apiIdx >= 0 && apiIdx < apiUrlCombo_->count())
        apiUrlCombo_->setCurrentIndex(apiIdx);

    QString customUrl = s.value("customUrl").toString();
    if (!customUrl.isEmpty()) {
        customUrlEdit_->setText(customUrl);
        if (apiUrlCombo_->currentData().toString() == "custom")
            customUrlEdit_->setVisible(true);
    }

    QString key = s.value("apiKey").toString();
    if (!key.isEmpty()) apiKeyEdit_->setText(key);

    int platform = s.value("platform", 0).toInt();
    platformTabs_->setCurrentIndex(platform);
    s.endGroup();
}

void GiftCodeGenerator::saveSettings()
{
    QSettings s;
    s.beginGroup("GiftCodeGenerator");
    s.setValue("apiUrlIndex", apiUrlCombo_->currentIndex());
    s.setValue("customUrl", customUrlEdit_->text().trimmed());
    s.setValue("apiKey", apiKeyEdit_->text().trimmed());
    s.setValue("platform", platformTabs_->currentIndex());
    s.endGroup();
}

void GiftCodeGenerator::loadHistory()
{
    QSettings s;
    s.beginGroup("GiftCodeGenerator");
    int count = s.beginReadArray("history");
    history_.clear();
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        GeneratedKey k;
        k.code = s.value("code").toString();
        k.platform = s.value("platform").toString();
        k.plan = s.value("plan").toString();
        k.timestamp = s.value("timestamp").toLongLong();
        if (!k.code.isEmpty()) history_.append(k);
    }
    s.endArray();
    s.endGroup();
}

void GiftCodeGenerator::saveHistory()
{
    QSettings s;
    s.beginGroup("GiftCodeGenerator");
    // Keep last 200
    while (history_.size() > 200)
        history_.removeLast();

    s.beginWriteArray("history");
    for (int i = 0; i < history_.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("code", history_[i].code);
        s.setValue("platform", history_[i].platform);
        s.setValue("plan", history_[i].plan);
        s.setValue("timestamp", history_[i].timestamp);
    }
    s.endArray();
    s.endGroup();
}

void GiftCodeGenerator::addHistory(const QVector<GeneratedKey>& keys)
{
    // Prepend new keys
    for (int i = keys.size() - 1; i >= 0; --i)
        history_.prepend(keys[i]);
    while (history_.size() > 200)
        history_.removeLast();
    saveHistory();
}

// ============================================================
// Slots
// ============================================================

void GiftCodeGenerator::onApiUrlChanged(int /*index*/)
{
    bool isCustom = (apiUrlCombo_->currentData().toString() == "custom");
    customUrlEdit_->setVisible(isCustom);
    saveSettings();
}

void GiftCodeGenerator::onPlatformChanged(int /*index*/)
{
    saveSettings();
}

void GiftCodeGenerator::setStatus(const QString& text, bool ok)
{
    QString color = ok ? "#36c275" : (text.contains("连") ? "#f0a040" : "#ff6b6b");
    connLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(color));
    connLabel_->setText(text);
}

QString GiftCodeGenerator::apiBase() const
{
    QString sel = apiUrlCombo_->currentData().toString();
    if (sel == "custom")
        return customUrlEdit_->text().trimmed();
    return sel;
}

QString GiftCodeGenerator::apiKey() const
{
    return apiKeyEdit_->text().trimmed();
}

void GiftCodeGenerator::onTestConnection()
{
    QString base = apiBase();
    QString key = apiKey();
    if (base.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请填写 API 地址"));
        return;
    }
    if (key.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请填写 Admin API Key"));
        return;
    }

    setStatus(QStringLiteral("● 连接中..."), false);
    testBtn_->setEnabled(false);

    QNetworkRequest req((QUrl(base + "/api/health")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        testBtn_->setEnabled(true);
        if (reply->error() == QNetworkReply::NoError) {
            setStatus(QStringLiteral("● 服务器在线"), true);
        } else {
            int code = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code > 0)
                setStatus(QStringLiteral("● HTTP %1").arg(code), false);
            else
                setStatus(QStringLiteral("● 连接失败"), false);
        }
    });
}

void GiftCodeGenerator::onGenerate()
{
    QString base = apiBase();
    QString key = apiKey();
    if (base.isEmpty() || key.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先填写 API 地址和 Admin API Key"));
        return;
    }

    bool isWindows = (platformTabs_->currentIndex() == 0);
    int count = isWindows ? winCountSpin_->value() : macCountSpin_->value();
    QString plan = isWindows
                       ? winPlanCombo_->currentData().toString()
                       : macPlanCombo_->currentData().toString();

    QString endpoint;
    QJsonObject body;
    body["count"] = count;
    body["plan"] = plan;

    if (isWindows) {
        endpoint = "/api/admin/generate-windows-keys";
    } else {
        endpoint = "/api/admin/generate";
        body["max_devices"] = macMaxDevicesSpin_->value();
        QDate expiry = macExpiryEdit_->date();
        if (expiry.isValid())
            body["expires_at"] = QDateTime(expiry, QTime(0, 0)).toUTC()
                                     .toString(Qt::ISODate);
    }

    // UI: loading state
    generateBtn_->setEnabled(false);
    generateBtn_->setText(QStringLiteral("  生成中...  "));
    progressBar_->setVisible(true);
    setStatus(QStringLiteral("● 请求中..."), false);

    QNetworkRequest req((QUrl(base + endpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());

    QByteArray postData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = net_->post(req, postData);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, isWindows, plan, count]() {
                reply->deleteLater();
                generateBtn_->setEnabled(true);
                generateBtn_->setText(QStringLiteral("  生成授权码  "));
                progressBar_->setVisible(false);

                if (reply->error() != QNetworkReply::NoError) {
                    int httpCode = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (httpCode == 401) {
                        setStatus(QStringLiteral("● API Key 无效 (401)"), false);
                        QMessageBox::critical(this,
                            QStringLiteral("认证失败"),
                            QStringLiteral("API Key 无效，请检查 Admin API Key"));
                    } else if (httpCode > 0) {
                        setStatus(QStringLiteral("● HTTP %1").arg(httpCode), false);
                        QMessageBox::critical(this,
                            QStringLiteral("请求失败"),
                            QStringLiteral("服务器返回 HTTP %1").arg(httpCode));
                    } else {
                        setStatus(QStringLiteral("● 网络错误"), false);
                        QMessageBox::critical(this,
                            QStringLiteral("网络错误"),
                            reply->errorString());
                    }
                    return;
                }

                // Parse response
                QByteArray data = reply->readAll();
                QJsonParseError parseErr;
                QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
                if (parseErr.error != QJsonParseError::NoError) {
                    setStatus(QStringLiteral("● 响应解析失败"), false);
                    QMessageBox::critical(this,
                        QStringLiteral("解析错误"),
                        QStringLiteral("无法解析服务器响应: %1")
                            .arg(parseErr.errorString()));
                    return;
                }

                QJsonArray keysArray = doc.object().value("keys").toArray();
                if (keysArray.isEmpty()) {
                    setStatus(QStringLiteral("● 无授权码返回"), false);
                    QMessageBox::warning(this,
                        QStringLiteral("空结果"),
                        QStringLiteral("服务器未返回任何授权码"));
                    return;
                }

                qint64 now = QDateTime::currentMSecsSinceEpoch();
                lastGenerated_.clear();
                for (const QJsonValue& v : keysArray) {
                    GeneratedKey k;
                    k.code = v.toString();
                    k.platform = isWindows ? "windows" : "macos";
                    k.plan = plan;
                    k.timestamp = now;
                    lastGenerated_.append(k);
                }

                renderResults();
                addHistory(lastGenerated_);
                renderHistory();
                setStatus(QStringLiteral("● 成功生成 %1 个")
                              .arg(lastGenerated_.size()), true);

                // Save platform setting
                saveSettings();
            });
}

void GiftCodeGenerator::onCopyCode(const QString& code)
{
    QApplication::clipboard()->setText(code);
    statusBar()->showMessage(
        QStringLiteral("已复制: %1").arg(code), 2000);
}

void GiftCodeGenerator::onCopyAll()
{
    if (lastGenerated_.isEmpty()) return;
    QStringList codes;
    for (const auto& k : lastGenerated_)
        codes << k.code;
    QApplication::clipboard()->setText(codes.join('\n'));
    statusBar()->showMessage(
        QStringLiteral("已复制全部 %1 个授权码").arg(lastGenerated_.size()), 3000);
}

void GiftCodeGenerator::onExportTxt()
{
    if (lastGenerated_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("没有可导出的授权码"));
        return;
    }

    QString defaultName = QStringLiteral("xlsone-codes-%1.txt")
        .arg(QDate::currentDate().toString("yyyy-MM-dd"));
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出授权码"), defaultName,
        QStringLiteral("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"),
            QStringLiteral("无法写入文件: %1").arg(file.errorString()));
        return;
    }

    QTextStream ts(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5 QTextStream defaults to the locale codec; force UTF-8 output.
    ts.setCodec("UTF-8");
#else
    // Qt6 QTextStream is UTF-8 by default and setCodec() was removed.
#endif
    ts << "# 表表归一 授权码\n";
    ts << "# 生成时间: " << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "\n";
    bool isWin = (platformTabs_->currentIndex() == 0);
    ts << "# 平台: " << (isWin ? "Windows (Ed25519)" : "macOS (HMAC/JWT)") << "\n";
    QString plan = isWin ? winPlanCombo_->currentData().toString()
                         : macPlanCombo_->currentData().toString();
    ts << "# 套餐: " << formatPlan(plan) << "\n";
    ts << "# 数量: " << lastGenerated_.size() << "\n\n";
    for (int i = 0; i < lastGenerated_.size(); ++i) {
        ts << (i + 1) << ". " << lastGenerated_[i].code << "\n";
    }
    ts << "\n# 激活方式:\n";
    ts << "# Windows: 打开软件 -> 激活 -> 输入授权码\n";
    ts << "# macOS:   打开软件 -> 激活 -> 输入授权码\n";
    file.close();

    statusBar()->showMessage(QStringLiteral("已导出到: %1").arg(path), 3000);
}

void GiftCodeGenerator::onClearHistory()
{
    if (history_.isEmpty()) return;
    auto ret = QMessageBox::question(this,
        QStringLiteral("确认"),
        QStringLiteral("确定清空全部生成历史吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    history_.clear();
    saveHistory();
    renderHistory();
    statusBar()->showMessage(QStringLiteral("历史已清空"), 2000);
}

// ============================================================
// Rendering
// ============================================================

void GiftCodeGenerator::renderResults()
{
    if (lastGenerated_.isEmpty()) {
        resultEdit_->clear();
        return;
    }

    QString html;
    for (const auto& k : lastGenerated_) {
        QString badgeColor = (k.platform == "windows") ? "#1a3a5c" : "#2a1a3c";
        QString badgeText = (k.platform == "windows") ? "Win" : "Mac";
        QString badgeFg = (k.platform == "windows") ? "#5b9cf5" : "#b388e0";
        QString timeStr = QDateTime::fromMSecsSinceEpoch(k.timestamp)
                              .toString("MM-dd HH:mm");

        html += QStringLiteral(
            "<div style='margin-bottom:8px; padding:8px 12px;"
            " background:#161820; border:1px solid #2a2e37; border-radius:6px;'>"
            "<span style='background:%1; color:%2; padding:1px 6px;"
            " border-radius:3px; font-size:11px; font-weight:600;'>%3</span>"
            " <span style='font-family:Consolas,monospace; font-size:15px;"
            " font-weight:600; color:#e6e8ec; letter-spacing:1px;'>%4</span>"
            "<br><span style='color:#9aa0ab; font-size:11px;'>%5 · %6</span>"
            "</div>")
            .arg(badgeColor, badgeFg, badgeText, k.code, formatPlan(k.plan), timeStr);
    }
    resultEdit_->setHtml(html);
}

void GiftCodeGenerator::renderHistory()
{
    historyList_->clear();
    if (history_.isEmpty()) return;

    for (const auto& k : history_) {
        QString badge = (k.platform == "windows") ? "[Win]" : "[Mac]";
        QString timeStr = QDateTime::fromMSecsSinceEpoch(k.timestamp)
                              .toString("MM-dd HH:mm");
        QString display = QStringLiteral("%1  %2  ·  %3  ·  %4")
            .arg(badge, k.code, formatPlan(k.plan), timeStr);

        auto* item = new QListWidgetItem(display, historyList_);
        item->setData(Qt::UserRole, k.code);
        item->setToolTip(QStringLiteral("双击复制: %1").arg(k.code));
    }
}

QString GiftCodeGenerator::formatPlan(const QString& plan)
{
    static const QHash<QString, QString> map = {
        {"personal_lifetime", QStringLiteral("个人终身版")},
        {"personal_yearly",   QStringLiteral("个人年度版")},
        {"team_lifetime",     QStringLiteral("团队终身版")},
        {"trial",             QStringLiteral("试用版")},
    };
    return map.value(plan, plan);
}

} // namespace xlsone
