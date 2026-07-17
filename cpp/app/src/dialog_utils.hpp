#pragma once

#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>

class QDialog;
class QWidget;

namespace xlsone::ui {

void centerDialogOnParent(QWidget* dialog, QWidget* parent = nullptr);
int execDialogCentered(QDialog& dialog, QWidget* parent = nullptr);
void showDialogCentered(QDialog* dialog, QWidget* parent = nullptr);

QString getOpenFileNameCentered(
    QWidget* parent,
    const QString& title,
    const QString& dir = QString(),
    const QString& filter = QString()
);

QStringList getOpenFileNamesCentered(
    QWidget* parent,
    const QString& title,
    const QString& dir = QString(),
    const QString& filter = QString()
);

QString getSaveFileNameCentered(
    QWidget* parent,
    const QString& title,
    const QString& suggestedName = QString(),
    const QString& filter = QString()
);

void showInformation(QWidget* parent, const QString& title, const QString& text);
void showWarning(QWidget* parent, const QString& title, const QString& text);
void showCritical(QWidget* parent, const QString& title, const QString& text);
void showToast(QWidget* parent, const QString& message);
void showAbout(QWidget* parent, const QString& title, const QString& html);
void showProductAbout(QWidget* parent, const QString& title, const QString& version, bool domesticBuild);
QMessageBox::StandardButton askQuestion(QWidget* parent, const QString& title, const QString& text);

} // namespace xlsone::ui
