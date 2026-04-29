#pragma once

#include <QPushButton>
#include <QWidget>

class WorkspaceChrome final : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceChrome(QWidget* parent = nullptr);

    void setWorkspaceState(bool hasWorkspace, bool canExport);

signals:
    void appendRequested();
    void reloadRequested();
    void clearRequested();
    void exportRequested();

private:
    QPushButton* appendButton_ = nullptr;
    QPushButton* reloadButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
};
