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
