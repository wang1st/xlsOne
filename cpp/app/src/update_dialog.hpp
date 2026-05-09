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
