#include "dialog_utils.hpp"

#include <QApplication>
#include <QDialog>
#include <QScreen>
#include <QTimer>
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
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    execMessageBox(box, parent);
}

void showWarning(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    execMessageBox(box, parent);
}

void showCritical(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Critical, title, text, QMessageBox::Ok, parent);
    execMessageBox(box, parent);
}

void showAbout(QWidget* parent, const QString& title, const QString& html)
{
    QMessageBox box(QMessageBox::Information, title, html, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::RichText);
    execDialogCentered(box, parent);
}

QMessageBox::StandardButton askQuestion(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
    box.setDefaultButton(QMessageBox::No);
    return static_cast<QMessageBox::StandardButton>(execMessageBox(box, parent));
}

} // namespace xlsone::ui
