#include "license_activation_dialog.hpp"
#include "dialog_utils.hpp"
#include "ui_theme.hpp"
#include "xlsone/core/license_manager.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QKeyEvent>
#include <QUrl>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLineEdit* makeKeyPartInput(QWidget* parent)
{
    auto* edit = new QLineEdit(parent);
    edit->setMaxLength(4);
    edit->setAlignment(Qt::AlignCenter);
    edit->setFixedSize(QSize(72, 38));
    edit->setAttribute(Qt::WA_MacShowFocusRect, false);
    return edit;
}

QString styleForKeyInput(const xlsone::ui::Theme& t, bool focused)
{
    return QStringLiteral(
        "QLineEdit {"
        " font-size: 15px; font-family: monospace;"
        " background: %1; color: %2;"
        " border: 1px solid %3; border-radius: 8px;"
        " padding: 0;"
        "}"
        "QLineEdit:focus { border: 1.5px solid %4; }"
    )
        .arg(t.surface.name())
        .arg(t.text.name())
        .arg(focused ? t.accent.name() : t.border.name())
        .arg(t.accent.name());
}

} // namespace

LicenseActivationDialog::LicenseActivationDialog(xlsone::LicenseManager* mgr,
                                                 QWidget* parent)
    : QDialog(parent)
    , licenseManager_(mgr)
{
    buildUi();

    connect(licenseManager_, &xlsone::LicenseManager::activationFinished,
            this, &LicenseActivationDialog::onActivationFinished);
}

bool LicenseActivationDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Paste)) {
            auto* edit = qobject_cast<QLineEdit*>(watched);
            if (edit) {
                const QString text = QApplication::clipboard()->text();
                if (text.length() > 4) {
                    // Likely a full key paste — distribute it
                    distributePastedKey(text);
                    return true;
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void LicenseActivationDialog::distributePastedKey(const QString& rawText)
{
    // Extract alphanumeric chars and split into 4-char parts
    QString cleaned;
    for (const QChar& c : rawText) {
        if (c.isLetterOrNumber()) {
            cleaned.append(c.toUpper());
        }
    }

    for (int i = 0; i < 4; ++i) {
        QString part = cleaned.mid(i * 4, 4).left(4);
        keyParts_[i]->setText(part);
    }

    // Focus the last non-empty field, or the first empty one
    for (int i = 3; i >= 0; --i) {
        if (keyParts_[i]->text().length() == 4) {
            if (i < 3) {
                keyParts_[i + 1]->setFocus();
                keyParts_[i + 1]->selectAll();
            } else {
                keyParts_[i]->setFocus();
                keyParts_[i]->selectAll();
            }
            break;
        }
    }

    updateActivateButton();
}

void LicenseActivationDialog::buildUi()
{
    setWindowTitle(tr("许可证"));
    setMinimumSize(680, 520);
    resize(720, 580);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const auto& t = xlsone::ui::theme();

    auto* root = new QHBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // Brand panel
    auto* brandPanel = new QWidget(this);
    brandPanel->setFixedWidth(280);
    buildBrandPanel(brandPanel);
    root->addWidget(brandPanel);

    // Separator
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet(QStringLiteral("color: %1").arg(t.border.name()));
    root->addWidget(separator);

    // Form panel
    auto* formPanel = new QWidget(this);
    buildFormPanel(formPanel);
    root->addWidget(formPanel, 1);

    setStyleSheet(QStringLiteral(
        "LicenseActivationDialog { background: %1; }"
    ).arg(t.bg0.name()));
}

void LicenseActivationDialog::buildBrandPanel(QWidget* container)
{
    const auto& t = xlsone::ui::theme();

    auto* layout = new QVBoxLayout(container);
    layout->setSpacing(16);
    layout->setContentsMargins(32, 32, 32, 32);
    container->setStyleSheet(QStringLiteral("background: %1").arg(t.accentSoft.name()));

    layout->addStretch();

    // Icon
    auto* iconLabel = new QLabel(container);
    iconLabel->setFixedSize(56, 56);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background: %1; border-radius: 14px;"
    ).arg(t.accent.name()));
    QPixmap logo(QStringLiteral(":/resources/xlsOne.png"));
    if (!logo.isNull()) {
        iconLabel->setPixmap(logo.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    layout->addWidget(iconLabel, 0, Qt::AlignCenter);

    // Title
    auto* titleLabel = new QLabel(tr("表表归一"), container);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = font();
    titleFont.setPointSize(18);
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color: %1").arg(t.text.name()));
    layout->addWidget(titleLabel);

    // Subtitle
    auto* subtitleLabel = new QLabel(tr("多张同格式 Excel 报表一键汇总"), container);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(t.text.name()));
    layout->addWidget(subtitleLabel);

    layout->addStretch();

    // Feature pills
    auto* pillLayout = new QHBoxLayout();
    pillLayout->setSpacing(8);
    auto makePill = [&](const QString& text) {
        auto* label = new QLabel(text, container);
        label->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border-radius: 12px; padding: 4px 10px; font-size: 11px;"
        ).arg(t.surface.name()).arg(t.textMuted.name()));
        return label;
    };
    pillLayout->addWidget(makePill(tr("快速")));
    pillLayout->addWidget(makePill(tr("安全")));
    pillLayout->addWidget(makePill(tr("原生")));
    pillLayout->addStretch();
    layout->addLayout(pillLayout);
}

void LicenseActivationDialog::buildFormPanel(QWidget* container)
{
    const auto& t = xlsone::ui::theme();

    auto* root = new QVBoxLayout(container);
    root->setSpacing(0);
    root->setContentsMargins(36, 32, 36, 24);
    container->setStyleSheet(QStringLiteral("background: %1").arg(t.bg0.name()));

    // Header
    auto* headerTitle = new QLabel(tr("许可证"), container);
    QFont headerFont = font();
    headerFont.setPointSize(17);
    headerFont.setWeight(QFont::DemiBold);
    headerTitle->setFont(headerFont);
    headerTitle->setStyleSheet(QStringLiteral("color: %1").arg(t.text.name()));
    root->addWidget(headerTitle);

    root->addSpacing(4);

    const bool isActivated = (licenseManager_->state() == xlsone::LicenseState::Activated);
    if (!isActivated) {
        // Tab bar
        auto* tabBar = new QWidget(container);
        auto* tabLayout = new QHBoxLayout(tabBar);
        tabLayout->setSpacing(0);
        tabLayout->setContentsMargins(0, 0, 0, 0);

        onlineTab_ = new QPushButton(tr("在线激活"), tabBar);
        offlineTab_ = new QPushButton(tr("离线激活"), tabBar);
        onlineTab_->setFlat(true);
        offlineTab_->setFlat(true);
        onlineTab_->setCursor(Qt::PointingHandCursor);
        offlineTab_->setCursor(Qt::PointingHandCursor);

        connect(onlineTab_, &QPushButton::clicked, this, [this]() { switchToPage(0); });
        connect(offlineTab_, &QPushButton::clicked, this, [this]() { switchToPage(1); });

        tabLayout->addWidget(onlineTab_);
        tabLayout->addWidget(offlineTab_);
        tabLayout->addStretch();
        root->addWidget(tabBar);

        root->addSpacing(4);
    }

    // Shared message area (visible for both pages)
    messageLabel_ = new QLabel(container);
    messageLabel_->setMinimumHeight(22);
    messageLabel_->setWordWrap(true);
    messageLabel_->setStyleSheet(QStringLiteral("font-size: 12px; padding-left: 2px;"));
    messageLabel_->hide();
    root->addWidget(messageLabel_);

    root->addSpacing(12);

    // Stacked pages
    pageStack_ = new QStackedWidget(container);
    pageStack_->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* onlinePage = new QWidget(pageStack_);
    buildOnlinePage(onlinePage);
    pageStack_->addWidget(onlinePage);

    auto* offlinePage = new QWidget(pageStack_);
    buildOfflinePage(offlinePage);
    pageStack_->addWidget(offlinePage);

    root->addWidget(pageStack_, 1);

    switchToPage(0);
    root->addStretch();
}

void LicenseActivationDialog::buildOnlinePage(QWidget* container)
{
    const auto& t = xlsone::ui::theme();
    auto* root = new QVBoxLayout(container);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);
    container->setStyleSheet(QStringLiteral("background: transparent;"));

    // Expired banner (shown when license is expired)
    auto* expiredBanner = new QLabel(container);
    expiredBanner->setWordWrap(true);
    expiredBanner->setMinimumHeight(36);
    expiredBanner->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px; font-weight: 500;"
        " background: %2; border-radius: 6px; padding: 8px 12px;"
    ).arg(t.error.name()).arg(t.errorSoft.name()));
    expiredBanner->hide();
    root->addWidget(expiredBanner);
    expiredBanner_ = expiredBanner;

    const xlsone::LicenseState state = licenseManager_->state();
    const bool isActivated = (state == xlsone::LicenseState::Activated);
    const bool isExpired = (state == xlsone::LicenseState::Expired);
    const bool isTrial = (state == xlsone::LicenseState::Trial);

    // Subtitle
    QString subtitleText;
    if (isExpired) {
        subtitleText = tr("许可证或试用期已过期，请输入激活码继续使用完整功能");
        expiredBanner->setText(tr("过期状态下每次最多处理 3 个文件，激活后解除限制。"));
        expiredBanner->show();
    } else if (isActivated) {
        const int graceDays = licenseManager_->graceRemainingDays();
        subtitleText = graceDays > 0
            ? tr("许可证宽限期剩余 %1 天，期间可继续使用全部功能").arg(graceDays)
            : tr("您的软件已激活，可正常使用全部功能");
    } else if (isTrial) {
        const int remaining = licenseManager_->checkTrial();
        const int graceDays = licenseManager_->graceRemainingDays();
        if (remaining > 0) {
            subtitleText = tr("试用期剩余 %1 天，期间所有功能开放").arg(remaining);
        } else if (graceDays > 0) {
            subtitleText = tr("试用宽限期剩余 %1 天，期间所有功能开放").arg(graceDays);
        } else {
            subtitleText = tr("您正在使用试用版，期间所有功能开放");
        }
    } else {
        subtitleText = tr("输入激活码，或先开始 14 天免费试用");
    }
    auto* headerSubtitle = new QLabel(subtitleText, container);
    headerSubtitle->setWordWrap(true);
    headerSubtitle->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px; margin-top: 4px;").arg(t.textMuted.name()));
    root->addWidget(headerSubtitle);

    root->addSpacing(24);

    if (isActivated) {
        // Show license info card
        auto* infoCard = new QWidget(container);
        auto* infoLayout = new QVBoxLayout(infoCard);
        infoLayout->setSpacing(12);
        infoLayout->setContentsMargins(14, 14, 14, 14);
        infoCard->setStyleSheet(QStringLiteral(
            "background: %1; border-radius: 8px; border: 1px solid %2;"
        ).arg(t.surface.name()).arg(t.border.name()));

        const auto info = licenseManager_->currentInfo();

        auto makeRow = [&](const QString& label, const QString& value, bool highlight = false) {
            auto* row = new QHBoxLayout();
            row->setSpacing(8);
            auto* labelWidget = new QLabel(label, infoCard);
            labelWidget->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(t.textMuted.name()));
            auto* valueWidget = new QLabel(value, infoCard);
            valueWidget->setStyleSheet(QStringLiteral(
                "color: %1; font-size: 13px; font-weight: %2;"
            ).arg(highlight ? t.success.name() : t.text.name()).arg(highlight ? QStringLiteral("600") : QStringLiteral("400")));
            valueWidget->setWordWrap(true);
            row->addWidget(labelWidget);
            row->addWidget(valueWidget, 1, Qt::AlignRight);
            return row;
        };

        QString planText;
        switch (info.plan) {
        case xlsone::LicensePlan::PersonalLifetime: planText = tr("个人终身版"); break;
        case xlsone::LicensePlan::PersonalYearly: planText = tr("个人年度版"); break;
        case xlsone::LicensePlan::Trial: planText = tr("试用版"); break;
        case xlsone::LicensePlan::Enterprise10: planText = tr("企业版"); break;
        default: planText = tr("未知套餐"); break;
        }

        infoLayout->addLayout(makeRow(tr("套餐类型"), planText, true));

        auto* keyRow = new QHBoxLayout();
        keyRow->setSpacing(8);
        auto* keyLabel = new QLabel(tr("激活码"), infoCard);
        keyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(t.textMuted.name()));
        keyRow->addWidget(keyLabel);

        auto* keyValue = new QLineEdit(info.keyId, infoCard);
        keyValue->setReadOnly(true);
        keyValue->setMinimumWidth(220);
        keyValue->setMinimumHeight(34);
        keyValue->setCursorPosition(0);
        keyValue->setAttribute(Qt::WA_MacShowFocusRect, false);
        keyValue->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            " color: %1; background: %2; border: 1px solid %3; border-radius: 6px;"
            " font-size: 13px; font-family: monospace; padding: 4px 8px;"
            " selection-background-color: %4; selection-color: white;"
            "}"
        ).arg(t.text.name()).arg(t.bg0.name()).arg(t.border.name()).arg(t.accent.name()));
        keyRow->addWidget(keyValue, 1);

        auto* copyKeyButton = new QPushButton(tr("复制"), infoCard);
        copyKeyButton->setCursor(Qt::PointingHandCursor);
        copyKeyButton->setMinimumHeight(30);
        copyKeyButton->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 13px; border: 1px solid %2; border-radius: 6px;"
            " padding: 4px 10px; background: transparent; }"
            "QPushButton:hover { background: %3; }"
        ).arg(t.accent.name()).arg(t.border.name()).arg(t.accentSoft.name()));
        connect(copyKeyButton, &QPushButton::clicked, this, [this, keyValue]() {
            QApplication::clipboard()->setText(keyValue->text());
            setMessage(tr("激活码已复制"), false);
        });
        keyRow->addWidget(copyKeyButton);
        infoLayout->addLayout(keyRow);

        if (info.expiresAt.isValid()) {
            infoLayout->addLayout(makeRow(tr("有效期至"), info.expiresAt.toString(QStringLiteral("yyyy-MM-dd"))));
            const int graceDays = licenseManager_->graceRemainingDays();
            if (graceDays > 0) {
                infoLayout->addLayout(makeRow(tr("宽限期"), tr("剩余 %1 天").arg(graceDays), true));
            }
        } else {
            infoLayout->addLayout(makeRow(tr("有效期"), tr("永久授权")));
        }

        root->addWidget(infoCard);

        root->addStretch();
        return;
    }

    // Key input
    auto* keyLabel = new QLabel(tr("激活码"), container);
    keyLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px; font-weight: 500;").arg(t.text.name()));
    root->addWidget(keyLabel);

    root->addSpacing(8);

    auto* keyRow = new QHBoxLayout();
    keyRow->setSpacing(8);
    keyRow->addStretch();
    for (int i = 0; i < 4; ++i) {
        auto* edit = makeKeyPartInput(container);
        edit->setStyleSheet(styleForKeyInput(t, false));
        connect(edit, &QLineEdit::textChanged, this, [this, i] { onKeyPartChanged(i); });
        connect(edit, &QLineEdit::textEdited, this, [this, i] { onKeyPartChanged(i); });
        // Handle paste: intercept full-key paste and distribute across parts
        edit->installEventFilter(this);
        keyParts_.append(edit);
        keyRow->addWidget(edit);
        if (i < 3) {
            auto* dash = new QLabel(QStringLiteral("-"), container);
            dash->setStyleSheet(QStringLiteral(
                "color: %1; font-size: 15px; font-family: monospace;").arg(t.textMuted.name()));
            keyRow->addWidget(dash);
        }
    }
    keyRow->addStretch();
    root->addLayout(keyRow);

    root->addSpacing(8);

    // Activate button
    activateButton_ = new QPushButton(tr("激活"), container);
    activateButton_->setEnabled(false);
    activateButton_->setMinimumHeight(40);
    xlsone::ui::stylePrimaryButton(activateButton_);
    connect(activateButton_, &QPushButton::clicked,
            this, &LicenseActivationDialog::onActivateClicked);
    root->addWidget(activateButton_);

    root->addSpacing(12);

    // Trial + Purchase
    auto* linkRow = new QHBoxLayout;
    linkRow->setSpacing(16);

    if (state == xlsone::LicenseState::Unactivated) {
        trialButton_ = new QPushButton(tr("开始免费试用 14 天"), container);
        trialButton_->setFlat(true);
        trialButton_->setCursor(Qt::PointingHandCursor);
        trialButton_->setStyleSheet(QStringLiteral(
            "QPushButton { color: %1; font-size: 13px; border: none; background: transparent; }"
            "QPushButton:hover { text-decoration: underline; }"
        ).arg(t.accent.name()));
        connect(trialButton_, &QPushButton::clicked,
                this, &LicenseActivationDialog::onTrialClicked);
        linkRow->addWidget(trialButton_);
    }

    auto* purchaseButton = new QPushButton(tr("获取激活码"), container);
    purchaseButton->setCursor(Qt::PointingHandCursor);
    purchaseButton->setMinimumHeight(34);
    purchaseButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 13px; border: 1px solid %2; border-radius: 8px;"
        " padding: 6px 14px; background: transparent; }"
        "QPushButton:hover { background: %3; }"
    ).arg(t.accent.name()).arg(t.border.name()).arg(t.accentSoft.name()));
    connect(purchaseButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://z-pulse.cn/xlsone/")));
    });
    linkRow->addWidget(purchaseButton);
    purchaseButton_ = purchaseButton;

    linkRow->addStretch();
    root->addLayout(linkRow);

    root->addStretch();
}

void LicenseActivationDialog::buildOfflinePage(QWidget* container)
{
    const auto& t = xlsone::ui::theme();
    auto* root = new QVBoxLayout(container);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);
    container->setStyleSheet(QStringLiteral("background: transparent;"));

    // Steps card
    auto* stepsCard = new QWidget(container);
    auto* stepsLayout = new QVBoxLayout(stepsCard);
    stepsLayout->setSpacing(10);
    stepsLayout->setContentsMargins(14, 14, 14, 14);
    stepsCard->setStyleSheet(QStringLiteral(
        "background: %1; border-radius: 8px; border: 1px solid %2;"
    ).arg(t.surface.name()).arg(t.border.name()));

    const QString offlinePageUrl = xlsone::LicenseManager::activationBaseUrl()
        + QStringLiteral("/offline");

    auto addStep = [&](int number, const QString& text) {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        auto* badge = new QLabel(QString::number(number), stepsCard);
        badge->setFixedSize(22, 22);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QStringLiteral(
            "background: %1; color: white; border-radius: 11px; font-size: 12px; font-weight: 700;"
        ).arg(t.accent.name()));
        row->addWidget(badge);
        auto* label = new QLabel(text, stepsCard);
        label->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 13px; line-height: 1.5; padding: 2px 0;"
        ).arg(t.text.name()));
        label->setWordWrap(true);
        label->setMinimumHeight(32);
        row->addWidget(label, 1);
        stepsLayout->addLayout(row);
    };

    addStep(1, tr("点击下方「打开离线激活页面」，在网页里粘贴设备码并提交"));
    addStep(2, tr("在网页中下载生成的授权文件（.license）"));
    addStep(3, tr("回到此处，点击「导入授权文件」选择该 .license 完成激活"));

    // Device fingerprint
    auto* fpLabel = new QLabel(tr("本机设备码"), stepsCard);
    fpLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 500;").arg(t.text.name()));
    stepsLayout->addWidget(fpLabel);

    auto* fpRow = new QHBoxLayout();
    fpRow->setSpacing(8);
    auto* fpValue = new QLabel(xlsone::LicenseManager::deviceFingerprint(), stepsCard);
    fpValue->setWordWrap(true);
    fpValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fpValue->setMinimumHeight(28);
    fpValue->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px; font-family: monospace; background: %2; padding: 4px 6px; border-radius: 4px;"
    ).arg(t.text.name()).arg(t.bg0.name()));
    fpRow->addWidget(fpValue, 1);

    auto* copyBtn = new QPushButton(tr("复制"), stepsCard);
    copyBtn->setFlat(true);
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 13px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }"
    ).arg(t.accent.name()));
    connect(copyBtn, &QPushButton::clicked, this, [fpValue]() {
        QApplication::clipboard()->setText(fpValue->text());
    });
    fpRow->addWidget(copyBtn);
    stepsLayout->addLayout(fpRow);

    // Open page button
    auto* openPageBtn = new QPushButton(tr("打开离线激活页面"), stepsCard);
    openPageBtn->setMinimumHeight(36);
    xlsone::ui::stylePrimaryButton(openPageBtn);
    connect(openPageBtn, &QPushButton::clicked, this, [offlinePageUrl]() {
        QDesktopServices::openUrl(QUrl(offlinePageUrl));
    });
    stepsLayout->addWidget(openPageBtn);

    // Import button
    auto* importBtn = new QPushButton(tr("导入授权文件..."), stepsCard);
    importBtn->setMinimumHeight(36);
    xlsone::ui::stylePrimaryButton(importBtn);
    connect(importBtn, &QPushButton::clicked,
            this, &LicenseActivationDialog::onImportLicenseClicked);
    stepsLayout->addWidget(importBtn);

    // Help link
    auto* purchaseLink = new QPushButton(tr("还没有激活码？获取激活码"), stepsCard);
    purchaseLink->setFlat(true);
    purchaseLink->setCursor(Qt::PointingHandCursor);
    purchaseLink->setMinimumHeight(24);
    purchaseLink->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 12px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }"
    ).arg(t.accent.name()));
    connect(purchaseLink, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://z-pulse.cn/xlsone/")));
    });
    stepsLayout->addWidget(purchaseLink, 0, Qt::AlignLeft);

    root->addWidget(stepsCard);

    root->addStretch();
}

void LicenseActivationDialog::switchToPage(int index)
{
    const auto& t = xlsone::ui::theme();
    if (!pageStack_) return;
    pageStack_->setCurrentIndex(index);
    if (!onlineTab_ || !offlineTab_) return;

    QString active = QStringLiteral(
        "QPushButton { color: %1; font-size: 14px; font-weight: 600; "
        "border: none; background: transparent; padding: 6px 12px; "
        "border-bottom: 2px solid %2; }"
    ).arg(t.text.name()).arg(t.accent.name());

    QString inactive = QStringLiteral(
        "QPushButton { color: %1; font-size: 14px; font-weight: 400; "
        "border: none; background: transparent; padding: 6px 12px; "
        "border-bottom: 2px solid transparent; }"
        "QPushButton:hover { border-bottom: 2px solid %2; }"
    ).arg(t.textMuted.name()).arg(t.accent.name());

    if (index == 0) {
        onlineTab_->setStyleSheet(active);
        offlineTab_->setStyleSheet(inactive);
    } else {
        offlineTab_->setStyleSheet(active);
        onlineTab_->setStyleSheet(inactive);
    }
}

void LicenseActivationDialog::onKeyPartChanged(int index)
{
    setMessage(QString(), false);

    QLineEdit* edit = keyParts_[index];
    QString text = edit->text().toUpper();
    QString cleaned;
    for (const QChar& c : text) {
        if (c.isLetterOrNumber()) {
            cleaned.append(c);
        }
    }

    if (cleaned.length() > 4) {
        QString overflow = cleaned.mid(4);
        cleaned = cleaned.left(4);

        // Distribute overflow to next fields
        int current = index + 1;
        while (!overflow.isEmpty() && current < 4) {
            QString part = overflow.left(4 - keyParts_[current]->text().length());
            keyParts_[current]->setText(keyParts_[current]->text() + part);
            overflow.remove(0, part.length());
            ++current;
        }
    }

    if (cleaned != edit->text()) {
        edit->setText(cleaned);
    }

    updateActivateButton();

    if (cleaned.length() == 4 && index < 3) {
        focusNextPart(index);
    }
}

void LicenseActivationDialog::onKeyPartBackspace(int index)
{
    if (keyParts_[index]->text().isEmpty() && index > 0) {
        focusPreviousPart(index);
    }
}

void LicenseActivationDialog::focusNextPart(int fromIndex)
{
    if (fromIndex + 1 < keyParts_.size()) {
        keyParts_[fromIndex + 1]->setFocus();
        keyParts_[fromIndex + 1]->selectAll();
    }
}

void LicenseActivationDialog::focusPreviousPart(int fromIndex)
{
    if (fromIndex > 0) {
        keyParts_[fromIndex - 1]->setFocus();
        keyParts_[fromIndex - 1]->setCursorPosition(keyParts_[fromIndex - 1]->text().length());
    }
}

void LicenseActivationDialog::updateActivateButton()
{
    bool complete = true;
    for (const auto* edit : keyParts_) {
        if (edit->text().length() != 4) {
            complete = false;
            break;
        }
    }
    activateButton_->setEnabled(complete);
}

void LicenseActivationDialog::setMessage(const QString& message, bool error)
{
    const auto& t = xlsone::ui::theme();
    if (message.isEmpty()) {
        messageLabel_->hide();
        messageLabel_->clear();
        return;
    }
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(QStringLiteral(
        "font-size: 12px; padding-left: 2px; color: %1;"
    ).arg(error ? t.error.name() : t.success.name()));
    messageLabel_->show();
}

void LicenseActivationDialog::onActivateClicked()
{
    QString key;
    for (int i = 0; i < keyParts_.size(); ++i) {
        if (i > 0) key += QLatin1Char('-');
        key += keyParts_[i]->text();
    }
    if (key.length() != 19) return;

    activateButton_->setEnabled(false);
    activateButton_->setText(tr("验证中..."));
    setMessage(QString(), false);

    const QString deviceId = xlsone::LicenseManager::deviceFingerprint();
    licenseManager_->activate(key, deviceId);
}

void LicenseActivationDialog::onImportLicenseClicked()
{
    const QString path = xlsone::ui::getOpenFileNameCentered(
        this,
        tr("导入授权文件"),
        QString(),
        tr("授权文件 (*.license *.txt);;所有文件 (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    QString errorMessage;
    const QString deviceId = xlsone::LicenseManager::deviceFingerprint();
    if (!licenseManager_->importOfflineLicenseFile(path, deviceId, &errorMessage)) {
        setMessage(errorMessage, true);
        return;
    }

    setMessage(tr("授权文件已导入"), false);
    accept();
}

void LicenseActivationDialog::onTrialClicked()
{
    if (trialButton_ != nullptr) {
        trialButton_->setEnabled(false);
        trialButton_->setText(tr("正在申请试用..."));
    }
    setMessage(QString(), false);

    const QString deviceId = xlsone::LicenseManager::deviceFingerprint();
    licenseManager_->requestTrial(deviceId);
}

void LicenseActivationDialog::onActivationFinished(const xlsone::ActivationResult& result)
{
    updateActivateButton();
    activateButton_->setText(tr("激活"));
    if (trialButton_ != nullptr) {
        trialButton_->setEnabled(true);
        trialButton_->setText(tr("开始免费试用 14 天"));
    }

    if (result.success) {
        const auto info = licenseManager_->currentInfo();
        if (result.trial) {
            const int remaining = licenseManager_->checkTrial();
            setMessage(remaining > 0
                ? tr("试用已启用，剩余 %1 天").arg(remaining)
                : tr("试用已启用"), false);
            QTimer::singleShot(1000, this, &LicenseActivationDialog::accept);
        } else if (info.expiresAt.isValid()) {
            const QString dateStr = info.expiresAt.toString(QStringLiteral("yyyy-MM-dd"));
            setMessage(tr("激活成功！有效期至 %1").arg(dateStr), false);
            // 短暂显示成功消息后关闭对话框
            QTimer::singleShot(1500, this, &LicenseActivationDialog::accept);
        } else {
            setMessage(tr("激活成功！永久授权"), false);
            QTimer::singleShot(1000, this, &LicenseActivationDialog::accept);
        }
    } else {
        setMessage(result.errorMessage, true);
    }
}
