#pragma once

#include "xlsone/core/license_manager.hpp"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

class LicenseActivationDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LicenseActivationDialog(xlsone::LicenseManager* mgr,
                                     QWidget* parent = nullptr);

private slots:
    void onActivateClicked();
    void onImportLicenseClicked();
    void onTrialClicked();
    void onActivationFinished(const xlsone::ActivationResult& result);
    void onKeyPartChanged(int index);
    void onKeyPartBackspace(int index);
    void toggleOfflineInfo();

private:
    void buildUi();
    void buildBrandPanel(QWidget* container);
    void buildFormPanel(QWidget* container);
    void updateActivateButton();
    void setMessage(const QString& message, bool error);
    void focusNextPart(int fromIndex);
    void focusPreviousPart(int fromIndex);

    xlsone::LicenseManager* licenseManager_ = nullptr;
    QVector<QLineEdit*> keyParts_;
    QPushButton* activateButton_ = nullptr;
    QPushButton* trialButton_ = nullptr;
    QPushButton* offlineToggle_ = nullptr;
    QPushButton* buyButton_ = nullptr;
    QWidget* offlineInfo_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    bool showOffline_ = false;
};
