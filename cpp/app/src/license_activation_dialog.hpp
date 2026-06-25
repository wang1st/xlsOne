#pragma once

#include "xlsone/core/license_manager.hpp"

#include <QDialog>
#include <QString>

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

private:
    void buildUi();

    xlsone::LicenseManager* licenseManager_ = nullptr;
    QLineEdit* keyInput_ = nullptr;
    QPushButton* activateButton_ = nullptr;
    QPushButton* trialButton_ = nullptr;
    QPushButton* offlineToggle_ = nullptr;
    QWidget* offlineInfo_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    bool showOffline_ = false;
};
