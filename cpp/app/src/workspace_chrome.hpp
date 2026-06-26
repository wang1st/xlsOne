#pragma once

#include "xlsone/core/license_manager.hpp"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

class WorkspaceChrome final : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceChrome(xlsone::LicenseManager* licenseManager,
                             QWidget* parent = nullptr);

    void setWorkspaceState(bool hasWorkspace, bool canExport);

signals:
    void appendRequested();
    void reloadRequested();
    void clearRequested();
    void exportRequested();

private:
    void buildUi();
    void updateLicenseStatus();

    xlsone::LicenseManager* licenseManager_ = nullptr;
    QPushButton* appendButton_ = nullptr;
    QPushButton* reloadButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QWidget* buttonGroup_ = nullptr;
    QLabel* licenseBadge_ = nullptr;
    QWidget* licenseDot_ = nullptr;
};
