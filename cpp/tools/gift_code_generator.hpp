#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QVector>

#include <QDateTime>
#include <QString>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextEdit;

namespace xlsone {

struct GeneratedKey {
    QString code;
    QString platform;   // "windows" | "macos"
    QString plan;       // "personal_lifetime" etc.
    qint64 timestamp;   // epoch ms
};

class GiftCodeGenerator final : public QMainWindow {
    Q_OBJECT

public:
    explicit GiftCodeGenerator(QWidget* parent = nullptr);
    ~GiftCodeGenerator() override = default;

private slots:
    void onApiUrlChanged(int index);
    void onTestConnection();
    void onPlatformChanged(int index);
    void onGenerate();
    void onCopyCode(const QString& code);
    void onCopyAll();
    void onExportTxt();
    void onClearHistory();

private:
    void buildUi();
    void applyDarkTheme();
    void loadSettings();
    void saveSettings();
    void loadHistory();
    void saveHistory();
    void addHistory(const QVector<GeneratedKey>& keys);
    void renderResults();
    void renderHistory();
    void setStatus(const QString& text, bool ok);
    QString apiBase() const;
    QString apiKey() const;
    static QString formatPlan(const QString& plan);

    // --- UI ---
    QComboBox* apiUrlCombo_ = nullptr;
    QLineEdit* customUrlEdit_ = nullptr;
    QLineEdit* apiKeyEdit_ = nullptr;
    QPushButton* testBtn_ = nullptr;
    QLabel* connLabel_ = nullptr;

    QTabWidget* platformTabs_ = nullptr;

    // Windows tab
    QSpinBox* winCountSpin_ = nullptr;
    QComboBox* winPlanCombo_ = nullptr;

    // macOS tab
    QSpinBox* macCountSpin_ = nullptr;
    QComboBox* macPlanCombo_ = nullptr;
    QSpinBox* macMaxDevicesSpin_ = nullptr;
    QDateEdit* macExpiryEdit_ = nullptr;

    QPushButton* generateBtn_ = nullptr;
    QProgressBar* progressBar_ = nullptr;

    QTextEdit* resultEdit_ = nullptr;
    QListWidget* historyList_ = nullptr;
    QPushButton* clearHistoryBtn_ = nullptr;

    // --- Data ---
    QNetworkAccessManager* net_ = nullptr;
    QVector<GeneratedKey> lastGenerated_;
    QVector<GeneratedKey> history_;
};

} // namespace xlsone
