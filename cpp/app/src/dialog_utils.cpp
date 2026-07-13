#include "dialog_utils.hpp"

#include "ui_theme.hpp"
#include "xlsone/core/obfuscation.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace xlsone::ui {

namespace {

QWidget* ownerWindow(QWidget* parent)
{
    if (parent != nullptr) {
        return parent->window();
    }
    return QApplication::activeWindow();
}

QPoint centerFor(QWidget* owner)
{
    if (owner != nullptr && owner->isVisible()) {
        return owner->frameGeometry().center();
    }

    QScreen* screen = QApplication::primaryScreen();
    if (screen == nullptr) {
        return {};
    }
    return screen->availableGeometry().center();
}

int execMessageBox(QMessageBox& box, QWidget* parent)
{
    box.setTextFormat(Qt::AutoText);
    return execDialogCentered(box, parent);
}

QPushButton* makeAboutButton(const QString& text, QWidget* parent, bool primary)
{
    auto* button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(34);
    if (primary) {
        stylePrimaryButton(button);
        return button;
    }

    const auto& t = theme();
    button->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; background: transparent; border: 1px solid %2;"
        " border-radius: 7px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background: %3; }"
    ).arg(t.accent.name()).arg(t.border.name()).arg(t.accentSoft.name()));
    return button;
}

QFrame* makeHomepageRow(QWidget* parent,
                        const QString& label,
                        const QString& url,
                        bool preferred)
{
    const auto& t = theme();

    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("aboutHomeRow"));
    row->setStyleSheet(QStringLiteral(
        "QFrame#aboutHomeRow { background: %1; border: 1px solid %2; border-radius: 8px; }"
    ).arg(preferred ? t.accentSoft.name() : t.surface.name()).arg(t.border.name()));

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    auto* labelWidget = new QLabel(label, row);
    labelWidget->setMinimumWidth(66);
    labelWidget->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 13px; font-weight: 600;"
    ).arg(preferred ? t.accent.name() : t.text.name()));
    layout->addWidget(labelWidget);

    auto* urlWidget = new QLineEdit(url, row);
    urlWidget->setReadOnly(true);
    urlWidget->setCursorPosition(0);
    urlWidget->setMinimumHeight(30);
    urlWidget->setStyleSheet(QStringLiteral(
        "QLineEdit { color: %1; background: %2; border: 1px solid %3;"
        " border-radius: 6px; padding: 4px 8px; font-size: 12px; }"
        "QLineEdit:focus { border-color: %4; }"
    ).arg(t.text.name()).arg(t.bg0.name()).arg(t.borderSoft.name()).arg(t.accent.name()));
    layout->addWidget(urlWidget, 1);

    auto* openButton = makeAboutButton(QObject::tr("打开"), row, preferred);
    QObject::connect(openButton, &QPushButton::clicked, row, [url] {
        QDesktopServices::openUrl(QUrl(url));
    });
    layout->addWidget(openButton);

    auto* copyButton = makeAboutButton(QObject::tr("复制"), row, false);
    QObject::connect(copyButton, &QPushButton::clicked, row, [url, copyButton] {
        QApplication::clipboard()->setText(url);
        copyButton->setText(QObject::tr("已复制"));
        QTimer::singleShot(1200, copyButton, [copyButton] {
            copyButton->setText(QObject::tr("复制"));
        });
    });
    layout->addWidget(copyButton);

    return row;
}

} // namespace

void centerDialogOnParent(QWidget* dialog, QWidget* parent)
{
    if (dialog == nullptr) {
        return;
    }

    dialog->adjustSize();
    QRect frame = dialog->frameGeometry();
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        frame.setSize(dialog->sizeHint());
    }
    frame.moveCenter(centerFor(ownerWindow(parent)));
    dialog->move(frame.topLeft());
}

int execDialogCentered(QDialog& dialog, QWidget* parent)
{
    if (parent != nullptr && dialog.parentWidget() == nullptr) {
        dialog.setParent(parent);
    }
    centerDialogOnParent(&dialog, parent);
    return dialog.exec();
}

void showDialogCentered(QDialog* dialog, QWidget* parent)
{
    if (dialog == nullptr) {
        return;
    }
    if (parent != nullptr && dialog->parentWidget() == nullptr) {
        dialog->setParent(parent);
    }
    QTimer::singleShot(0, dialog, [dialog, parent] {
        centerDialogOnParent(dialog, parent);
    });
    dialog->show();
}

QString getOpenFileNameCentered(QWidget* parent, const QString& title, const QString& dir, const QString& filter)
{
    QFileDialog dialog(parent, title, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (execDialogCentered(dialog, parent) != QDialog::Accepted) {
        return {};
    }
    const auto files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

QStringList getOpenFileNamesCentered(QWidget* parent, const QString& title, const QString& dir, const QString& filter)
{
    QFileDialog dialog(parent, title, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (execDialogCentered(dialog, parent) != QDialog::Accepted) {
        return {};
    }
    return dialog.selectedFiles();
}

QString getSaveFileNameCentered(QWidget* parent, const QString& title, const QString& suggestedName, const QString& filter)
{
    QFileDialog dialog(parent, title, QString(), filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (!suggestedName.isEmpty()) {
        dialog.selectFile(suggestedName);
    }
    if (execDialogCentered(dialog, parent) != QDialog::Accepted) {
        return {};
    }
    const auto files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : files.first();
}

void showInformation(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::NoButton, parent);
    auto* okButton = box.addButton(QObject::tr("确定"), QMessageBox::AcceptRole);
    box.setDefaultButton(okButton);
    execMessageBox(box, parent);
}

void showWarning(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::NoButton, parent);
    auto* okButton = box.addButton(QObject::tr("确定"), QMessageBox::AcceptRole);
    box.setDefaultButton(okButton);
    execMessageBox(box, parent);
}

void showCritical(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::NoButton, parent);
    auto* okButton = box.addButton(QObject::tr("确定"), QMessageBox::AcceptRole);
    box.setDefaultButton(okButton);
    execMessageBox(box, parent);
}

void showAbout(QWidget* parent, const QString& title, const QString& html)
{
    QMessageBox box(QMessageBox::NoIcon, title, html, QMessageBox::NoButton, parent);
    box.setTextFormat(Qt::RichText);
    auto* okButton = box.addButton(QObject::tr("确定"), QMessageBox::AcceptRole);
    box.setDefaultButton(okButton);
    execDialogCentered(box, parent);
}

void showProductAbout(QWidget* parent, const QString& title, const QString& version, bool domesticBuild)
{
    const auto& t = theme();
    const QString domesticUrl = XLSONE_OBF_STRING("https://z-pulse.cn/xlsone/");
    const QString internationalUrl = XLSONE_OBF_STRING("https://xlsone.com/");

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(560);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background: %1; }"
    ).arg(t.bg0.name()));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(24, 24, 24, 20);
    root->setSpacing(16);

    auto* header = new QHBoxLayout();
    header->setSpacing(14);

    auto* icon = new QLabel(&dialog);
    icon->setFixedSize(64, 64);
    const QPixmap iconPixmap(QStringLiteral(":/resources/xlsOne.png"));
    icon->setPixmap(iconPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    header->addWidget(icon, 0, Qt::AlignTop);

    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(5);

    auto* nameRow = new QHBoxLayout();
    nameRow->setSpacing(10);
    auto* name = new QLabel(QObject::tr("表表归一"), &dialog);
    QFont nameFont = name->font();
    nameFont.setPointSize(20);
    nameFont.setWeight(QFont::DemiBold);
    name->setFont(nameFont);
    name->setStyleSheet(QStringLiteral("color: %1;").arg(t.text.name()));
    nameRow->addWidget(name);

    nameRow->addStretch();
    titleBlock->addLayout(nameRow);

    auto* subtitle = new QLabel(QObject::tr("多张同格式 Excel 报表一键汇总"), &dialog);
    subtitle->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(t.textMuted.name()));
    titleBlock->addWidget(subtitle);

    auto* versionLabel = new QLabel(QObject::tr("版本 %1").arg(version), &dialog);
    versionLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(t.textDisabled.name()));
    titleBlock->addWidget(versionLabel);
    header->addLayout(titleBlock, 1);
    root->addLayout(header);

    auto* summary = new QLabel(
        QObject::tr("把格式一致的 Excel 表合成一份汇总表。金额、数量等可相加字段会自动合计，名称、编号等文本信息会保留共同特征，适合财务、统计和运营报表整理。"),
        &dialog);
    summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral(
        "color: %1; background: %2; border: 1px solid %3; border-radius: 8px;"
        " padding: 12px 14px; line-height: 1.45; font-size: 13px;"
    ).arg(t.text.name()).arg(t.surface.name()).arg(t.border.name()));
    root->addWidget(summary);

#if !defined(Q_OS_LINUX)
    auto* homeTitle = new QLabel(QObject::tr("首页地址"), &dialog);
    homeTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 700;").arg(t.text.name()));
    root->addWidget(homeTitle);

    const QString homepageUrl = domesticBuild ? domesticUrl : internationalUrl;
    root->addWidget(makeHomepageRow(&dialog, QObject::tr("官方网站"), homepageUrl, true));
#endif

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* closeButton = makeAboutButton(QObject::tr("关闭"), &dialog, true);
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    execDialogCentered(dialog, parent);
}

QMessageBox::StandardButton askQuestion(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::NoIcon, title, text, QMessageBox::NoButton, parent);
    box.addButton(QObject::tr("是"), QMessageBox::YesRole);
    auto* noButton = box.addButton(QObject::tr("否"), QMessageBox::NoRole);
    box.setDefaultButton(noButton);
    box.exec();
    if (box.buttonRole(box.clickedButton()) == QMessageBox::YesRole) {
        return QMessageBox::Yes;
    }
    return QMessageBox::No;
}

} // namespace xlsone::ui
