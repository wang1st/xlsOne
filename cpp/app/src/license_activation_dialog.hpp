#pragma once

#include "xlsone/core/license_manager.hpp"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
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
    void switchToPage(int index);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void distributePastedKey(const QString& rawText);

private:
    void buildUi();
    void buildBrandPanel(QWidget* container);
    void buildFormPanel(QWidget* container);
    void buildOnlinePage(QWidget* container);
    void buildOfflinePage(QWidget* container);
    void updateActivateButton();
    void setMessage(const QString& message, bool error);
    void focusNextPart(int fromIndex);
    void focusPreviousPart(int fromIndex);

    xlsone::LicenseManager* licenseManager_ = nullptr;
    QVector<QLineEdit*> keyParts_;
    QPushButton* activateButton_ = nullptr;
    QPushButton* trialButton_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QStackedWidget* pageStack_ = nullptr;
    QPushButton* onlineTab_ = nullptr;
    QPushButton* offlineTab_ = nullptr;
    QLabel* expiredBanner_ = nullptr;
    QPushButton* purchaseButton_ = nullptr;
};
