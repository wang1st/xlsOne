#pragma once

#include <QDialog>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;

/// Industrial-grade Quick Reference Guide dialog.
/// Left sidebar (QTreeWidget) provides categorized topic navigation;
/// right pane (QTextBrowser) renders the HTML help content from resources.
class HelpDialog final : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget* parent = nullptr);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onSearchTextChanged(const QString& text);

private:
    void buildUi();
    void addTopic(const QString& title, const QString& anchor, QTreeWidgetItem* parent = nullptr);
    void collectTopicItems(QTreeWidgetItem* root, std::vector<QTreeWidgetItem*>& out) const;
    bool itemMatches(QTreeWidgetItem* item, const QString& text) const;

    QLineEdit* searchEdit_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTextBrowser* browser_ = nullptr;
};
