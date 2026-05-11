#include "main_window.hpp"

#include "dialog_utils.hpp"
#include "license_activation_dialog.hpp"
#include "merged_table_delegate.hpp"
#include "schema_manager_dialog.hpp"
#include "update_dialog.hpp"
#include "ui_theme.hpp"
#include "xlsone/core/exporter.hpp"
#include "xlsone/core/license_manager.hpp"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString fileStatusName(xlsone::FileValidationStatus status)
{
    switch (status) {
    case xlsone::FileValidationStatus::Included:
        return QStringLiteral("参与");
    case xlsone::FileValidationStatus::Warning:
        return QStringLiteral("警告");
    case xlsone::FileValidationStatus::Blocked:
        return QStringLiteral("阻断");
    }
    return QStringLiteral("未知");
}

QString readinessName(xlsone::MergeReadiness readiness)
{
    return readiness == xlsone::MergeReadiness::Ready ? QStringLiteral("可合并") : QStringLiteral("阻断");
}

QString severityName(xlsone::ValidationSeverity severity)
{
    return severity == xlsone::ValidationSeverity::Blocking ? QStringLiteral("阻断") : QStringLiteral("警告");
}

bool sameOverrideCell(const xlsone::SchemaCellOverride& lhs, const xlsone::SchemaCellOverride& rhs)
{
    return lhs.sheetName == rhs.sheetName
        && lhs.position.row == rhs.position.row
        && lhs.position.column == rhs.position.column;
}

bool containsOverrideCell(
    const std::vector<xlsone::SchemaCellOverride>& overrides,
    const xlsone::SchemaCellOverride& key
)
{
    return std::any_of(overrides.begin(), overrides.end(), [&](const auto& current) {
        return sameOverrideCell(current, key);
    });
}

void removeOverrideCell(
    std::vector<xlsone::SchemaCellOverride>& overrides,
    const xlsone::SchemaCellOverride& key
)
{
    overrides.erase(std::remove_if(overrides.begin(), overrides.end(), [&](const auto& current) {
        return sameOverrideCell(current, key);
    }), overrides.end());
}

std::vector<xlsone::SchemaCellOverride> mergedOverrides(
    std::vector<xlsone::SchemaCellOverride> existing,
    const std::vector<xlsone::SchemaCellOverride>& updates
)
{
    for (const auto& update : updates) {
        const auto iterator = std::find_if(existing.begin(), existing.end(), [&](const auto& current) {
            return sameOverrideCell(current, update);
        });
        if (iterator == existing.end()) {
            existing.push_back(update);
        } else {
            *iterator = update;
        }
    }
    return existing;
}

bool isNameDelimiter(QChar character)
{
    static const QString delimiters = QStringLiteral("-_()[]{}（）【】<>《》,.，。/\\| ");
    return character.isSpace() || delimiters.contains(character);
}

QString trimNameDelimiters(const QString& text)
{
    int start = 0;
    int end = text.size();
    while (start < end && isNameDelimiter(text.at(start))) {
        ++start;
    }
    while (end > start && isNameDelimiter(text.at(end - 1))) {
        --end;
    }
    return text.mid(start, end - start);
}

QString longestCommonPrefix(const QStringList& values)
{
    if (values.isEmpty()) {
        return {};
    }
    QString prefix = values.first();
    for (const auto& value : values.mid(1)) {
        while (!prefix.isEmpty() && !value.startsWith(prefix)) {
            prefix.chop(1);
        }
        if (prefix.isEmpty()) {
            return {};
        }
    }
    return prefix;
}

QStringList tokenizeName(const QString& text)
{
    QStringList tokens;
    QString current;
    std::optional<bool> lastWasDigit;
    for (const auto character : text) {
        if (isNameDelimiter(character)) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
            lastWasDigit.reset();
            continue;
        }

        const bool isDigit = character.isNumber();
        if (lastWasDigit.has_value() && *lastWasDigit != isDigit && !current.isEmpty()) {
            tokens.append(current);
            current.clear();
        }

        current.append(character);
        lastWasDigit = isDigit;
    }
    if (!current.isEmpty()) {
        tokens.append(current);
    }
    return tokens;
}

QString mostRepeatedTokenPhrase(const QStringList& values)
{
    if (values.isEmpty()) {
        return {};
    }

    QHash<QString, int> frequency;
    for (const auto& value : values) {
        QSet<QString> uniqueTokens;
        for (const auto& token : tokenizeName(value)) {
            if (token.size() >= 2) {
                uniqueTokens.insert(token);
            }
        }
        for (const auto& token : uniqueTokens) {
            ++frequency[token];
        }
    }

    QStringList repeatedTokens;
    for (auto iterator = frequency.cbegin(); iterator != frequency.cend(); ++iterator) {
        if (iterator.value() >= 2) {
            repeatedTokens.append(iterator.key());
        }
    }
    if (repeatedTokens.isEmpty()) {
        return {};
    }

    int maxFrequency = 0;
    for (const auto& token : repeatedTokens) {
        maxFrequency = std::max(maxFrequency, frequency.value(token));
    }

    QStringList topTokens;
    for (const auto& token : tokenizeName(values.first())) {
        if (frequency.value(token) == maxFrequency) {
            topTokens.append(token);
        }
    }
    if (!topTokens.isEmpty()) {
        return topTokens.join(QString());
    }

    std::sort(repeatedTokens.begin(), repeatedTokens.end(), [&](const QString& lhs, const QString& rhs) {
        const int lhsFrequency = frequency.value(lhs);
        const int rhsFrequency = frequency.value(rhs);
        if (lhsFrequency != rhsFrequency) {
            return lhsFrequency > rhsFrequency;
        }
        return lhs.size() > rhs.size();
    });
    return repeatedTokens.mid(0, 2).join(QString());
}

QString sanitizeSuggestedFileName(const QString& text)
{
    static const QString illegal = QStringLiteral(":*?\"<>|/");
    QString sanitized;
    sanitized.reserve(text.size());
    for (const auto character : text) {
        sanitized.append(illegal.contains(character) ? QChar(QLatin1Char('_')) : character);
    }
    return sanitized
        .replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "))
        .trimmed();
}

QString fileStemFromName(const QString& filename)
{
    return QFileInfo(filename).completeBaseName();
}

QString suggestedWorkbookName(const QStringList& filenames)
{
    QStringList stems;
    for (const auto& filename : filenames) {
        const auto stem = fileStemFromName(filename)
            .replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "))
            .trimmed();
        if (!stem.isEmpty()) {
            stems.append(stem);
        }
    }

    if (stems.isEmpty()) {
        return QStringLiteral("汇总结果");
    }

    QString baseName;
    if (stems.size() == 1) {
        baseName = stems.first();
    } else {
        const auto prefix = trimNameDelimiters(longestCommonPrefix(stems));
        const auto tokenPhrase = trimNameDelimiters(mostRepeatedTokenPhrase(stems));
        if (prefix.size() >= 4) {
            baseName = prefix;
        } else if (tokenPhrase.size() >= 2) {
            baseName = tokenPhrase;
        } else if (prefix.size() >= 2) {
            baseName = prefix;
        } else {
            baseName = QStringLiteral("汇总结果");
        }
    }

    const auto sanitized = sanitizeSuggestedFileName(baseName);
    if (sanitized.isEmpty()) {
        return QStringLiteral("汇总结果");
    }
    if (sanitized.endsWith(QStringLiteral("汇总"))) {
        return sanitized;
    }
    return sanitized + QStringLiteral("_汇总");
}

QStringList exportNamingFilenames(
    const xlsone::WorkbookValidationReport& report,
    const QStringList& selectedPaths
)
{
    QStringList filenames;
    for (const auto& file : report.files) {
        if (!file.filename.isEmpty()) {
            filenames.append(file.filename);
        }
    }
    if (!filenames.isEmpty()) {
        return filenames;
    }
    for (const auto& path : selectedPaths) {
        filenames.append(QFileInfo(path).fileName());
    }
    return filenames;
}

QString suggestedWorkbookFileName(
    const xlsone::WorkbookValidationReport& report,
    const QStringList& selectedPaths
)
{
    auto filename = suggestedWorkbookName(exportNamingFilenames(report, selectedPaths));
    if (!filename.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) {
        filename += QStringLiteral(".xlsx");
    }
    return filename;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    buildUi();
    setAcceptDrops(true);
}

void MainWindow::buildUi()
{
    xlsone::ui::applyAppStyle(this);

    licenseManager_ = new xlsone::LicenseManager(this);

    auto* openAction = new QAction(tr("导入文件..."), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFiles);

    auto* appendAction = new QAction(tr("追加文件..."), this);
    appendAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(appendAction, &QAction::triggered, this, &MainWindow::appendFiles);

    auto* reloadAction = new QAction(tr("刷新"), this);
    reloadAction->setShortcut(QKeySequence::Refresh);
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadFiles);

    auto* clearAction = new QAction(tr("清空工作区"), this);
    clearAction->setShortcut(QKeySequence::New);
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearWorkspace);

    auto* exportAction = new QAction(tr("导出 XLSX..."), this);
    exportAction->setShortcut(QKeySequence::Save);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportResult);

    auto* undoOverrideAction = new QAction(tr("撤销修正"), this);
    undoOverrideAction->setShortcut(QKeySequence::Undo);
    connect(undoOverrideAction, &QAction::triggered, this, &MainWindow::undoLastOverride);

    auto* clearOverridesAction = new QAction(tr("清除所有修正"), this);
    connect(clearOverridesAction, &QAction::triggered, this, &MainWindow::clearOverrides);

    auto* manageSchemasAction = new QAction(tr("查看规则"), this);
    manageSchemasAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(manageSchemasAction, &QAction::triggered, this, &MainWindow::manageSchemas);

    auto* saveSchemaAction = new QAction(tr("保存规则"), this);
    connect(saveSchemaAction, &QAction::triggered, this, &MainWindow::saveCurrentSchema);

    auto* licenseActivateAction = new QAction(tr("激活/导入许可证..."), this);
    connect(licenseActivateAction, &QAction::triggered, this, &MainWindow::showLicenseActivation);

    // ---- 文件 ----
    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(appendAction);
    fileMenu->addAction(reloadAction);
    fileMenu->addAction(clearAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exportAction);
    fileMenu->addSeparator();
    auto* quitAction = new QAction(tr("退出"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    // ---- 编辑 ----
    auto* editMenu = menuBar()->addMenu(tr("编辑"));
    editMenu->addAction(undoOverrideAction);
    editMenu->addAction(clearOverridesAction);

    // ---- 查看 ----
    auto* viewMenu = menuBar()->addMenu(tr("查看"));
    viewMenu->addAction(manageSchemasAction);
    viewMenu->addAction(saveSchemaAction);

    // ---- 许可 ----
    auto* licenseMenu = menuBar()->addMenu(tr("许可"));
    licenseMenu->addAction(licenseActivateAction);

    auto* helpAction = new QAction(tr("使用帮助"), this);
    connect(helpAction, &QAction::triggered, this, [this] {
        xlsone::ui::showInformation(this, tr("使用帮助"),
            tr("表表归一  ·  多张同格式 Excel 报表一键汇总\n\n"
               "1. 导入文件\n"
               "   拖拽 .xlsx 或 .xls 文件到窗口，或点击 [文件] → [导入文件]\n\n"
               "2. 切换工作表\n"
               "   点击顶部的 Sheet 标签可切换要查看的报表页\n\n"
               "3. 查看汇总\n"
               "   - 金额、数量等能相加的数自动合计\n"
               "   - 名称、编号等信息保留最常见的共同前缀\n"
               "   - 结构不一致的工作表会跳过，并提示原因\n\n"
               "4. 穿透查阅\n"
               "   点击单元格可查看各文件原始值\n\n"
               "5. 导出结果\n"
               "   点击 [导出 XLSX] 保存汇总结果\n\n"
               "6. 单元格修正\n"
               "   在右侧面板可将单元格手动指定为标签或求和\n\n"
               "快捷键:\n"
               "   Ctrl+O  导入文件\n"
               "   Ctrl+Shift+O  追加文件\n"
               "   Ctrl+S  导出\n"
               "   Ctrl+R  刷新\n"
               "   Ctrl+N  清空\n"
               "   Ctrl+Z  撤销修正"));
    });

    auto* aboutAction = new QAction(tr("关于 表表归一"), this);
    connect(aboutAction, &QAction::triggered, this, [this] {
        const QString ver = QStringLiteral("%1.%2.%3")
            .arg(XLSONE_VERSION_MAJOR)
            .arg(XLSONE_VERSION_MINOR)
            .arg(XLSONE_VERSION_PATCH);
        xlsone::ui::showAbout(this, tr("关于 表表归一"),
            tr("<h3>表表归一  V%1</h3>"
               "<p>多张同格式 Excel 报表一键汇总</p>"
               "<p>把多张格式一致的 Excel 表合成一份汇总表。"
               "金额、数量等能相加的数会自动合计；"
               "名称、编号等不该相加的信息，会保留各文件里最常见的共同前缀。</p>"
               "<p><b>作者：</b>王臻</p>"
               "<p><b>技术：</b>C++  /  Qt</p>"
               "<p><b>邮箱：</b>831261@qq.com</p>"
                "<p>&copy; 2026 王臻. 保留所有权利.</p>").arg(ver));
    });

    auto* checkUpdateAction = new QAction(tr("检查更新"), this);
    connect(checkUpdateAction, &QAction::triggered, this, &MainWindow::checkForUpdates);

    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(checkUpdateAction);
    helpMenu->addSeparator();
    helpMenu->addAction(helpAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);

    auto* root = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    chrome_ = new WorkspaceChrome(root);
    rootLayout->addWidget(chrome_);
    connect(chrome_, &WorkspaceChrome::appendRequested, this, &MainWindow::appendFiles);
    connect(chrome_, &WorkspaceChrome::reloadRequested, this, &MainWindow::reloadFiles);
    connect(chrome_, &WorkspaceChrome::clearRequested, this, &MainWindow::clearWorkspace);
    connect(chrome_, &WorkspaceChrome::exportRequested, this, &MainWindow::exportResult);

    contentStack_ = new QStackedWidget(root);
    rootLayout->addWidget(contentStack_, 1);

    emptyView_ = new EmptyWorkspaceView(contentStack_);
    connect(emptyView_, &EmptyWorkspaceView::openRequested, this, &MainWindow::openFiles);
    contentStack_->addWidget(emptyView_);

    workspaceView_ = new QWidget(contentStack_);
    auto* workspaceLayout = new QVBoxLayout(workspaceView_);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    sheetStrip_ = new SheetStrip(workspaceView_);
    workspaceLayout->addWidget(sheetStrip_);
    connect(sheetStrip_, &SheetStrip::sheetSelected, this, &MainWindow::selectSheet);

    auto* splitter = new QSplitter(Qt::Horizontal, workspaceView_);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    workspaceStack_ = new QStackedWidget(splitter);
    tableModel_ = new MergedTableModel(this);
    table_ = new QTableView(this);
    table_->setModel(tableModel_);
    table_->setItemDelegate(new MergedTableDelegate(table_));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(28);
    table_->horizontalHeader()->setDefaultSectionSize(112);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->setWordWrap(false);
    workspaceStack_->addWidget(table_);

    diagnostics_ = new DiagnosticsView(workspaceStack_);
    workspaceStack_->addWidget(diagnostics_);
    splitter->addWidget(workspaceStack_);

    inspector_ = new InspectorPanel(splitter);
    inspector_->setMinimumWidth(320);
    inspector_->setMaximumWidth(420);
    inspector_->resize(360, inspector_->height());
    splitter->addWidget(inspector_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 360});
    workspaceLayout->addWidget(splitter, 1);

    correctionBar_ = new QWidget(workspaceView_);
    correctionBar_->setObjectName(QStringLiteral("correctionBar"));
    auto* correctionLayout = new QHBoxLayout(correctionBar_);
    correctionLayout->setContentsMargins(14, 8, 14, 8);
    correctionLabel_ = new QLabel(correctionBar_);
    undoButton_ = new QPushButton(tr("撤销上一步"), correctionBar_);
    clearOverridesButton_ = new QPushButton(tr("清除调整"), correctionBar_);
    correctionLayout->addWidget(correctionLabel_);
    correctionLayout->addStretch(1);
    correctionLayout->addWidget(undoButton_);
    correctionLayout->addWidget(clearOverridesButton_);
    correctionBar_->setStyleSheet(QStringLiteral(
        "QWidget#correctionBar { background: %1; border-top: 1px solid %2; }"
        "QLabel { color: %3; }"
    )
        .arg(xlsone::ui::theme().bg1.name(),
             xlsone::ui::theme().border.name(),
             xlsone::ui::theme().textMuted.name()));
    workspaceLayout->addWidget(correctionBar_);
    correctionBar_->hide();
    connect(undoButton_, &QPushButton::clicked, this, &MainWindow::undoLastOverride);
    connect(clearOverridesButton_, &QPushButton::clicked, this, &MainWindow::clearOverrides);

    contentStack_->addWidget(workspaceView_);
    setCentralWidget(root);

    connect(table_, &QTableView::clicked, this, &MainWindow::inspectCell);
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        inspectCell(current);
        table_->viewport()->update();
    });
    connect(inspector_, &InspectorPanel::markLabelRequested, this, &MainWindow::markSelectedAsLabel);
    connect(inspector_, &InspectorPanel::markSumRequested, this, &MainWindow::markSelectedAsSum);
    connect(inspector_, &InspectorPanel::restoreAutomaticRequested, this, &MainWindow::restoreAutomaticDecisionForSelection);

    statusLabel_ = new QLabel(tr("拖入 Excel 文件，或点击打开开始。"), this);
    statusBar()->addPermanentWidget(statusLabel_, 1);

    updateChecker_ = new xlsone::UpdateChecker(this);
    connect(updateChecker_, &xlsone::UpdateChecker::updateAvailable, this,
        [this](const xlsone::UpdateInfo& info) {
            auto* dialog = new UpdateDialog(info.latestVersion,
                                            info.changelog,
                                            info.downloadUrl,
                                            this);
            connect(dialog, &QDialog::accepted, this, [this] {
                QDesktopServices::openUrl(QUrl(
                    QStringLiteral(XLSONE_UPDATE_BASE_URL "/products/xlsone/download.html")));
            });
            xlsone::ui::showDialogCentered(dialog, this);
        });

    updateChromeState();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (firstShow_) {
        firstShow_ = false;
        checkForUpdates();
    }
}

void MainWindow::checkForUpdates()
{
    if (updateChecker_ == nullptr) {
        return;
    }
    const QString apiUrl = QStringLiteral(
        XLSONE_UPDATE_BASE_URL "/api/version");
    updateChecker_->checkForUpdates(apiUrl);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        if (emptyView_ != nullptr && contentStack_ != nullptr && contentStack_->currentWidget() == emptyView_) {
            emptyView_->setDropTargeted(true);
        }
        event->acceptProposedAction();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (emptyView_ != nullptr) {
        emptyView_->setDropTargeted(false);
    }
    QMainWindow::dragLeaveEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (emptyView_ != nullptr) {
        emptyView_->setDropTargeted(false);
    }
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    loadFiles(paths, !selectedPaths_.isEmpty());
    event->acceptProposedAction();
}

QStringList MainWindow::chooseInputFiles() const
{
    return xlsone::ui::getOpenFileNamesCentered(
        const_cast<MainWindow*>(this),
        tr("选择工作簿"),
        {},
        tr("Excel Files (*.xlsx *.xls);;All Files (*)")
    );
}

void MainWindow::openFiles()
{
    loadFiles(chooseInputFiles(), false);
}

void MainWindow::appendFiles()
{
    loadFiles(chooseInputFiles(), true);
}

void MainWindow::reloadFiles()
{
    loadFiles(selectedPaths_, false);
}

void MainWindow::clearWorkspace()
{
    selectedPaths_.clear();
    parsedFiles_.clear();
    parseFailures_.clear();
    validation_ = {};
    baseResults_.clear();
    results_.clear();
    currentOverrides_.clear();
    forgottenOverrides_.clear();
    overrideHistory_.clear();
    workspaceBaseOverrides_.clear();
    workspaceBaseSchemaId_.reset();
    workspaceActiveSchemaId_.reset();
    selectedSheetName_.clear();
    selectedSheetMergeable_ = true;
    tableModel_->setResult({});
    diagnostics_->showEmpty();
    inspector_->showPlaceholder(tr("选择单元格后查看结果与来源。"));
    updateSheetStrip();
    updateCorrectionBar();
    updateChromeState();
    statusLabel_->setText(tr("工作区已清空。"));
}

void MainWindow::loadFiles(const QStringList& paths, bool append)
{
    if (paths.isEmpty()) {
        return;
    }

    selectedPaths_ = append ? selectedPaths_ + paths : paths;
    currentOverrides_.clear();
    forgottenOverrides_.clear();
    overrideHistory_.clear();
    workspaceBaseOverrides_.clear();
    workspaceBaseSchemaId_.reset();
    workspaceActiveSchemaId_.reset();
    const auto parsed = parser_.parseFiles(selectedPaths_);
    parsedFiles_ = parsed.files;
    parseFailures_ = parsed.failures;

    recomputeWorkspace();
}

void MainWindow::recomputeWorkspace()
{
    validation_ = validator_.validate(parsedFiles_, parseFailures_);
    showValidationSummary();
    updateDiagnostics();

    baseResults_.clear();
    results_.clear();
    if (validation_.report.readiness != xlsone::MergeReadiness::Ready) {
        tableModel_->setResult({});
        selectedSheetName_.clear();
        selectedSheetMergeable_ = true;
        workspaceStack_->setCurrentWidget(diagnostics_);
        inspector_->showPlaceholder(tr("没有可参与汇总的同构工作表。"));
        updateSheetStrip();
        updateCorrectionBar();
        updateChromeState();
        return;
    }

    for (const auto& sheetName : validation_.report.commonSheetNames) {
        baseResults_.push_back(merger_.merge(validation_.mergeableFiles, sheetName));
    }

    const auto match = xlsone::SchemaMatcher::match(
        xlsone::fingerprintFor(validation_.mergeableFiles, validation_.report.commonSheetNames),
        schemaRepository_.loadAll()
    );
    const auto exactSchema = match.exactSchema();
    syncWorkspaceSchemaBase(exactSchema);
    if (exactSchema.has_value()) {
        statusLabel_->setText(statusLabel_->text() + tr(" 已应用规则：%1。").arg(exactSchema->name));
    } else if (match.kind == xlsone::SchemaMatchKind::Ambiguous) {
        statusLabel_->setText(statusLabel_->text() + tr(" 检测到多套高置信规则，未自动应用。"));
    } else if (match.kind == xlsone::SchemaMatchKind::Similar) {
        statusLabel_->setText(statusLabel_->text() + tr(" 发现相近规则，未自动应用。"));
    }

    rebuildResultsWithCurrentOverrides();

    QString nextSheet = selectedSheetName_;
    const auto resultIterator = std::find_if(results_.begin(), results_.end(), [&](const auto& result) {
        return result.sheetName == nextSheet;
    });
    if (resultIterator == results_.end()) {
        nextSheet = results_.empty() ? QString() : results_.front().sheetName;
    }
    selectedSheetName_ = nextSheet;
    selectedSheetMergeable_ = true;
    updateSheetStrip();
    updateCorrectionBar();
    updateChromeState();
    if (!selectedSheetName_.isEmpty()) {
        selectSheet(selectedSheetName_, true);
    } else if (!validation_.report.skippedSheetNames.isEmpty()) {
        selectSheet(validation_.report.skippedSheetNames.front(), false);
    }
}

void MainWindow::showValidationSummary()
{
    statusLabel_->setText(tr("已选 %1 个文件，可合并 %2 张工作表，解析失败 %3 个。")
        .arg(selectedPaths_.size())
        .arg(validation_.report.commonSheetNames.size())
        .arg(parseFailures_.size()));
}

void MainWindow::updateDiagnostics()
{
    if (validation_.report.files.empty()) {
        diagnostics_->showEmpty();
        return;
    }
    diagnostics_->setReport(validation_.report);
}

void MainWindow::updateChromeState()
{
    const bool hasWorkspace = !selectedPaths_.isEmpty();
    const bool canExport = !results_.empty();
    if (chrome_ != nullptr) {
        chrome_->setWorkspaceState(hasWorkspace, canExport);
    }
    if (contentStack_ != nullptr) {
        contentStack_->setCurrentWidget(hasWorkspace ? workspaceView_ : emptyView_);
    }
}

void MainWindow::updateSheetStrip()
{
    QList<SheetStripItem> items;
    for (const auto& result : results_) {
        int columnCount = 0;
        for (const auto& row : result.rows) {
            columnCount = std::max(columnCount, static_cast<int>(row.size()));
        }
        SheetStripItem item;
        item.sheetName = result.sheetName;
        item.mergeable = true;
        item.subtitle = tr("%1 行 / %2 列").arg(static_cast<int>(result.rows.size())).arg(columnCount);
        item.tooltip = item.subtitle;
        items.push_back(item);
    }

    for (const auto& sheetName : validation_.report.skippedSheetNames) {
        QStringList reasons;
        for (const auto& issue : validation_.report.skippedSheetIssues) {
            if (issue.sheetName == sheetName) {
                reasons << tr("[%1] %2: %3").arg(severityName(issue.severity), issue.fileName, issue.message);
            }
        }
        SheetStripItem item;
        item.sheetName = sheetName;
        item.mergeable = false;
        item.subtitle = reasons.isEmpty() ? tr("已跳过") : reasons.join(QLatin1Char('\n'));
        item.tooltip = item.subtitle;
        items.push_back(item);
    }

    if (sheetStrip_ != nullptr) {
        sheetStrip_->setVisible(!items.isEmpty());
        sheetStrip_->setItems(items);
        if (!selectedSheetName_.isEmpty()) {
            sheetStrip_->setCurrentSheet(selectedSheetName_, selectedSheetMergeable_);
        }
    }
}

void MainWindow::updateCorrectionBar()
{
    const int count = static_cast<int>(currentOverrides_.size() + forgottenOverrides_.size());
    if (correctionLabel_ != nullptr) {
        correctionLabel_->setText(tr("已调整 %1 处").arg(count));
    }
    if (undoButton_ != nullptr) {
        undoButton_->setEnabled(!overrideHistory_.empty());
    }
    if (clearOverridesButton_ != nullptr) {
        clearOverridesButton_->setEnabled(count > 0);
    }
    if (correctionBar_ != nullptr) {
        correctionBar_->setVisible(count > 0);
    }
}

void MainWindow::selectSheet(const QString& sheetName, bool mergeable)
{
    if (sheetName.isEmpty()) {
        return;
    }
    selectedSheetName_ = sheetName;
    selectedSheetMergeable_ = mergeable;
    sheetStrip_->setCurrentSheet(sheetName, mergeable);
    if (!mergeable) {
        showSkippedSheet(sheetName);
        return;
    }

    const auto iterator = std::find_if(results_.begin(), results_.end(), [&](const auto& result) {
        return result.sheetName == sheetName;
    });
    if (iterator == results_.end()) {
        return;
    }
    showResult(*iterator);
}

void MainWindow::showResult(const xlsone::MergedResult& result)
{
    workspaceStack_->setCurrentWidget(table_);
    tableModel_->setResult(result);
    selectedSheetName_ = result.sheetName;
    selectedSheetMergeable_ = true;
    inspector_->showPlaceholder(tr("选择一个单元格查看来源。"));
    statusLabel_->setText(tr("正在查看“%1”，共 %2 行。").arg(result.sheetName).arg(static_cast<int>(result.rows.size())));
}

void MainWindow::showSkippedSheet(const QString& sheetName)
{
    tableModel_->setResult({});
    selectedSheetName_ = sheetName;
    selectedSheetMergeable_ = false;
    workspaceStack_->setCurrentWidget(diagnostics_);
    diagnostics_->showSkippedSheet(validation_.report, sheetName);
    inspector_->showPlaceholder(tr("该工作表未参与合并，请查看左侧诊断原因。"));
    statusLabel_->setText(tr("“%1”已跳过。").arg(sheetName));
}

void MainWindow::inspectCell(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    const auto* cell = tableModel_->cellAt(index.row(), index.column());
    if (cell == nullptr) {
        return;
    }

    inspector_->showCell(
        xlsone::cellReference(index.row(), index.column()),
        *cell,
        hasRestorableOverride(index.row(), index.column(), selectedSheetName_)
    );
}

int MainWindow::currentResultIndex() const
{
    if (results_.empty()) {
        return -1;
    }
    if (!selectedSheetName_.isEmpty()) {
        const auto iterator = std::find_if(results_.begin(), results_.end(), [&](const auto& result) {
            return result.sheetName == selectedSheetName_;
        });
        if (iterator != results_.end()) {
            return static_cast<int>(std::distance(results_.begin(), iterator));
        }
    }
    return 0;
}

bool MainWindow::hasRestorableOverride(int row, int column, const QString& sheetName) const
{
    if (sheetName.isEmpty()) {
        return false;
    }
    const xlsone::SchemaCellOverride key{
        {row, column},
        xlsone::SchemaCellOverrideType::Label,
        sheetName,
    };
    return containsOverrideCell(currentOverrides_, key)
        || (containsOverrideCell(workspaceBaseOverrides_, key) && !containsOverrideCell(forgottenOverrides_, key));
}

void MainWindow::rebuildResultsWithCurrentOverrides()
{
    results_ = baseResults_;
    xlsone::MergeSchema transientSchema;
    transientSchema.name = tr("当前手动规则");
    transientSchema.overrides = effectiveWorkspaceOverrides();
    for (auto& result : results_) {
        result = xlsone::applySchema(transientSchema, result);
    }
}

std::vector<xlsone::SchemaCellOverride> MainWindow::effectiveWorkspaceOverrides() const
{
    std::vector<xlsone::SchemaCellOverride> remembered;
    for (const auto& override : workspaceBaseOverrides_) {
        if (!containsOverrideCell(forgottenOverrides_, override)) {
            remembered.push_back(override);
        }
    }
    return mergedOverrides(remembered, currentOverrides_);
}

void MainWindow::syncWorkspaceSchemaBase(const std::optional<xlsone::MergeSchema>& schema)
{
    if (!currentOverrides_.empty() || !forgottenOverrides_.empty()) {
        return;
    }
    workspaceBaseOverrides_ = schema.has_value() ? schema->overrides : std::vector<xlsone::SchemaCellOverride>{};
    workspaceBaseSchemaId_ = schema.has_value() ? std::optional<QUuid>{schema->id} : std::nullopt;
    workspaceActiveSchemaId_ = workspaceBaseSchemaId_;
}

QString MainWindow::suggestedAdjustmentMemoryName() const
{
    auto exportName = suggestedWorkbookName(exportNamingFilenames(validation_.report, selectedPaths_));
    if (exportName.endsWith(QStringLiteral("_汇总"))) {
        exportName.chop(QStringLiteral("_汇总").size());
        return exportName + QStringLiteral("_调整记忆");
    }
    if (exportName.endsWith(QStringLiteral("汇总"))) {
        exportName.chop(QStringLiteral("汇总").size());
        return exportName + QStringLiteral("调整记忆");
    }
    return exportName + QStringLiteral("_调整记忆");
}

void MainWindow::persistAdjustmentMemory()
{
    if (validation_.report.readiness != xlsone::MergeReadiness::Ready
        || validation_.mergeableFiles.empty()
        || validation_.report.commonSheetNames.isEmpty()) {
        return;
    }

    const auto fingerprint = xlsone::fingerprintFor(validation_.mergeableFiles, validation_.report.commonSheetNames);
    const auto effectiveOverrides = effectiveWorkspaceOverrides();
    const auto now = QDateTime::currentDateTimeUtc();
    const auto schemaId = workspaceActiveSchemaId_.has_value() ? workspaceActiveSchemaId_ : workspaceBaseSchemaId_;

    if (effectiveOverrides.empty()) {
        if (workspaceActiveSchemaId_.has_value() && !workspaceBaseSchemaId_.has_value()) {
            schemaRepository_.remove(*workspaceActiveSchemaId_);
            workspaceActiveSchemaId_.reset();
        } else if (schemaId.has_value()) {
            auto schema = schemaRepository_.find(*schemaId);
            if (schema.has_value()) {
                schema->fingerprint = fingerprint;
                schema->overrides.clear();
                schema->updatedAt = now;
                schemaRepository_.save(*schema);
                workspaceActiveSchemaId_ = schema->id;
            }
        }
        return;
    }

    if (schemaId.has_value()) {
        auto schema = schemaRepository_.find(*schemaId);
        if (schema.has_value()) {
            schema->fingerprint = fingerprint;
            schema->overrides = effectiveOverrides;
            schema->updatedAt = now;
            schemaRepository_.save(*schema);
            workspaceActiveSchemaId_ = schema->id;
            return;
        }
    }

    xlsone::MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = suggestedAdjustmentMemoryName();
    schema.version = 2;
    schema.fingerprint = fingerprint;
    schema.overrides = effectiveOverrides;
    schema.createdAt = now;
    schema.updatedAt = now;
    schemaRepository_.save(schema);
    workspaceActiveSchemaId_ = schema.id;
}

QModelIndexList MainWindow::selectedCellIndexes() const
{
    QModelIndexList indexes;
    if (table_ != nullptr && table_->selectionModel() != nullptr) {
        indexes = table_->selectionModel()->selectedIndexes();
    }
    if (indexes.isEmpty() && table_ != nullptr && table_->currentIndex().isValid()) {
        indexes.append(table_->currentIndex());
    }
    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex& lhs, const QModelIndex& rhs) {
        if (lhs.row() != rhs.row()) {
            return lhs.row() < rhs.row();
        }
        return lhs.column() < rhs.column();
    });
    indexes.erase(std::unique(indexes.begin(), indexes.end(), [](const QModelIndex& lhs, const QModelIndex& rhs) {
        return lhs.row() == rhs.row() && lhs.column() == rhs.column();
    }), indexes.end());
    return indexes;
}

void MainWindow::applyOverrideForSelection(xlsone::SchemaCellOverrideType type)
{
    const int resultIndex = currentResultIndex();
    const auto indexes = selectedCellIndexes();
    if (resultIndex < 0 || indexes.isEmpty()) {
        xlsone::ui::showInformation(this, tr("规则"), tr("请先选择一个或多个汇总单元格。"));
        return;
    }

    const QString sheetName = results_[static_cast<size_t>(resultIndex)].sheetName;
    overrideHistory_.push_back({currentOverrides_, forgottenOverrides_});
    for (const auto& index : indexes) {
        xlsone::SchemaCellOverride override{
            {index.row(), index.column()},
            type,
            sheetName,
        };
        removeOverrideCell(forgottenOverrides_, override);
        const auto existing = std::find_if(currentOverrides_.begin(), currentOverrides_.end(), [&](const auto& current) {
            return sameOverrideCell(current, override);
        });
        if (existing == currentOverrides_.end()) {
            currentOverrides_.push_back(override);
        } else {
            *existing = override;
        }
    }

    rebuildResultsWithCurrentOverrides();
    showResult(results_[static_cast<size_t>(resultIndex)]);
    updateSheetStrip();
    updateCorrectionBar();
    const auto firstIndex = indexes.front();
    const QModelIndex refreshed = tableModel_->index(firstIndex.row(), firstIndex.column());
    table_->setCurrentIndex(refreshed);
    inspectCell(refreshed);
    const auto kind = type == xlsone::SchemaCellOverrideType::Label
        ? xlsone::CellKind::Label
        : type == xlsone::SchemaCellOverrideType::Sum ? xlsone::CellKind::Sum : xlsone::CellKind::Mixed;
    statusLabel_->setText(tr("已将 %1 个单元格标记为 %2，可点击“保存规则”复用。")
        .arg(indexes.size())
        .arg(xlsone::cellKindName(kind)));
    try {
        persistAdjustmentMemory();
        statusLabel_->setText(statusLabel_->text() + tr(" 已记住调整。"));
    } catch (const std::exception& error) {
        xlsone::ui::showWarning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::markSelectedAsLabel()
{
    applyOverrideForSelection(xlsone::SchemaCellOverrideType::Label);
}

void MainWindow::markSelectedAsSum()
{
    applyOverrideForSelection(xlsone::SchemaCellOverrideType::Sum);
}

void MainWindow::restoreAutomaticDecisionForSelection()
{
    const int resultIndex = currentResultIndex();
    const QModelIndex index = table_ == nullptr ? QModelIndex() : table_->currentIndex();
    if (resultIndex < 0 || !index.isValid()) {
        xlsone::ui::showInformation(this, tr("恢复自动"), tr("请先选择一个汇总单元格。"));
        return;
    }

    const QString sheetName = results_[static_cast<size_t>(resultIndex)].sheetName;
    xlsone::SchemaCellOverride key{
        {index.row(), index.column()},
        xlsone::SchemaCellOverrideType::Label,
        sheetName,
    };

    const bool hasManualOverride = containsOverrideCell(currentOverrides_, key);
    const bool hasRememberedOverride = containsOverrideCell(workspaceBaseOverrides_, key)
        && !containsOverrideCell(forgottenOverrides_, key);
    if (!hasManualOverride && !hasRememberedOverride) {
        xlsone::ui::showInformation(this, tr("恢复自动"), tr("当前单元格没有手动或已记住的修正。"));
        return;
    }

    overrideHistory_.push_back({currentOverrides_, forgottenOverrides_});
    removeOverrideCell(currentOverrides_, key);
    if (containsOverrideCell(workspaceBaseOverrides_, key) && !containsOverrideCell(forgottenOverrides_, key)) {
        forgottenOverrides_.push_back(key);
    }

    rebuildResultsWithCurrentOverrides();
    showResult(results_[static_cast<size_t>(resultIndex)]);
    updateSheetStrip();
    updateCorrectionBar();
    const QModelIndex refreshed = tableModel_->index(index.row(), index.column());
    table_->setCurrentIndex(refreshed);
    inspectCell(refreshed);
    statusLabel_->setText(tr("已将 %1 恢复为自动判定。").arg(xlsone::cellReference(index.row(), index.column())));
    try {
        persistAdjustmentMemory();
        statusLabel_->setText(statusLabel_->text() + tr(" 已记住调整。"));
    } catch (const std::exception& error) {
        xlsone::ui::showWarning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::undoLastOverride()
{
    if (overrideHistory_.empty()) {
        xlsone::ui::showInformation(this, tr("撤销修正"), tr("当前没有可撤销的修正。"));
        return;
    }

    const int resultIndex = currentResultIndex();
    currentOverrides_ = overrideHistory_.back().currentOverrides;
    forgottenOverrides_ = overrideHistory_.back().forgottenOverrides;
    overrideHistory_.pop_back();
    rebuildResultsWithCurrentOverrides();
    if (resultIndex >= 0 && resultIndex < static_cast<int>(results_.size())) {
        showResult(results_[static_cast<size_t>(resultIndex)]);
    }
    updateSheetStrip();
    updateCorrectionBar();
    statusLabel_->setText(tr("已撤销上一步修正。"));
    try {
        persistAdjustmentMemory();
    } catch (const std::exception& error) {
        xlsone::ui::showWarning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::clearOverrides()
{
    if (currentOverrides_.empty() && forgottenOverrides_.empty()) {
        xlsone::ui::showInformation(this, tr("清除修正"), tr("当前没有手动修正。"));
        return;
    }

    overrideHistory_.push_back({currentOverrides_, forgottenOverrides_});
    currentOverrides_.clear();
    forgottenOverrides_.clear();
    const int resultIndex = currentResultIndex();
    rebuildResultsWithCurrentOverrides();
    if (resultIndex >= 0 && resultIndex < static_cast<int>(results_.size())) {
        showResult(results_[static_cast<size_t>(resultIndex)]);
    }
    updateSheetStrip();
    updateCorrectionBar();
    statusLabel_->setText(tr("已清除当前工作区的手动修正。"));
    try {
        persistAdjustmentMemory();
    } catch (const std::exception& error) {
        xlsone::ui::showWarning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::jumpAcrossAnomalies(int step)
{
    const int resultIndex = currentResultIndex();
    if (resultIndex < 0) {
        xlsone::ui::showInformation(this, tr("异常导航"), tr("当前没有可检查的汇总结果。"));
        return;
    }

    const auto& result = results_[static_cast<size_t>(resultIndex)];
    const QModelIndex current = table_ == nullptr ? QModelIndex() : table_->currentIndex();
    const int startRow = current.isValid() ? current.row() : 0;
    const int startColumn = current.isValid() ? current.column() : -1;

    std::vector<xlsone::CellPosition> positions;
    for (int row = 0; row < static_cast<int>(result.rows.size()); ++row) {
        for (int column = 0; column < static_cast<int>(result.rows[static_cast<size_t>(row)].size()); ++column) {
            const auto& cell = result.rows[static_cast<size_t>(row)][static_cast<size_t>(column)];
            if (cell.type.kind == xlsone::CellKind::Mixed || cell.decision.isSuspicious) {
                positions.push_back({row, column});
            }
        }
    }

    if (positions.empty()) {
        xlsone::ui::showInformation(this, tr("异常导航"), tr("当前工作表没有混合或可疑单元格。"));
        return;
    }

    auto next = step >= 0 ? positions.front() : positions.back();
    if (step >= 0) {
        const auto iterator = std::find_if(positions.begin(), positions.end(), [&](const xlsone::CellPosition& position) {
            return position.row > startRow || (position.row == startRow && position.column > startColumn);
        });
        if (iterator != positions.end()) {
            next = *iterator;
        }
    } else {
        const auto iterator = std::find_if(positions.rbegin(), positions.rend(), [&](const xlsone::CellPosition& position) {
            return position.row < startRow || (position.row == startRow && position.column < startColumn);
        });
        if (iterator != positions.rend()) {
            next = *iterator;
        }
    }

    const QModelIndex target = tableModel_->index(next.row, next.column);
    table_->setCurrentIndex(target);
    table_->scrollTo(target, QAbstractItemView::PositionAtCenter);
    inspectCell(target);
    statusLabel_->setText(tr("已跳转到 %1。").arg(xlsone::cellReference(next.row, next.column)));
}

void MainWindow::saveCurrentSchema()
{
    if (currentOverrides_.empty()) {
        xlsone::ui::showInformation(this, tr("保存规则"), tr("当前还没有手动修正的单元格。"));
        return;
    }
    if (validation_.mergeableFiles.empty() || validation_.report.commonSheetNames.isEmpty()) {
        xlsone::ui::showInformation(this, tr("保存规则"), tr("当前没有可用于生成规则指纹的同构工作簿。"));
        return;
    }

    const auto fingerprint = xlsone::fingerprintFor(validation_.mergeableFiles, validation_.report.commonSheetNames);
    const auto now = QDateTime::currentDateTimeUtc();
    const auto effectiveOverrides = effectiveWorkspaceOverrides();

    // 1. 如果已有活跃规则，直接更新
    const auto schemaId = workspaceActiveSchemaId_.has_value()
        ? workspaceActiveSchemaId_ : workspaceBaseSchemaId_;
    if (schemaId.has_value()) {
        auto schema = schemaRepository_.find(*schemaId);
        if (schema.has_value()) {
            schema->fingerprint = fingerprint;
            schema->overrides = effectiveOverrides;
            schema->updatedAt = now;
            schemaRepository_.save(*schema);
            workspaceActiveSchemaId_ = schema->id;
            workspaceBaseOverrides_ = schema->overrides;
            workspaceBaseSchemaId_ = schema->id;
            currentOverrides_.clear();
            forgottenOverrides_.clear();
            overrideHistory_.clear();
            rebuildResultsWithCurrentOverrides();
            const int resultIndex = currentResultIndex();
            if (resultIndex >= 0) {
                showResult(results_[static_cast<size_t>(resultIndex)]);
            }
            updateSheetStrip();
            updateCorrectionBar();
            statusLabel_->setText(tr("已更新规则“%1”。").arg(schema->name));
            return;
        }
    }

    // 2. 按指纹查找已有规则
    auto allSchemas = schemaRepository_.loadAll();
    const auto match = xlsone::SchemaMatcher::match(fingerprint, allSchemas);
    if (match.kind == xlsone::SchemaMatchKind::Exact) {
        auto schema = match.candidates[0].schema;
        schema.overrides = effectiveOverrides;
        schema.updatedAt = now;
        schemaRepository_.save(schema);
        workspaceBaseOverrides_ = schema.overrides;
        workspaceBaseSchemaId_ = schema.id;
        workspaceActiveSchemaId_ = schema.id;
        currentOverrides_.clear();
        forgottenOverrides_.clear();
        overrideHistory_.clear();
        rebuildResultsWithCurrentOverrides();
        const int resultIndex = currentResultIndex();
        if (resultIndex >= 0) {
            showResult(results_[static_cast<size_t>(resultIndex)]);
        }
        updateSheetStrip();
        updateCorrectionBar();
        statusLabel_->setText(tr("已更新规则“%1”。").arg(schema.name));
        return;
    }

    // 3. 自动生成名称，新建规则
    const QString name = suggestedAdjustmentMemoryName();
    xlsone::MergeSchema newSchema;
    newSchema.id = QUuid::createUuid();
    newSchema.name = name;
    newSchema.version = 2;
    newSchema.fingerprint = fingerprint;
    newSchema.overrides = effectiveOverrides;
    newSchema.createdAt = now;
    newSchema.updatedAt = now;
    schemaRepository_.save(newSchema);

    workspaceBaseOverrides_ = newSchema.overrides;
    workspaceBaseSchemaId_ = newSchema.id;
    workspaceActiveSchemaId_ = newSchema.id;
    currentOverrides_.clear();
    forgottenOverrides_.clear();
    overrideHistory_.clear();
    rebuildResultsWithCurrentOverrides();
    const int resultIndex = currentResultIndex();
    if (resultIndex >= 0) {
        showResult(results_[static_cast<size_t>(resultIndex)]);
    }
    updateSheetStrip();
    updateCorrectionBar();
    statusLabel_->setText(tr("已保存规则“%1”，包含 %2 个修正。").arg(newSchema.name).arg(static_cast<int>(newSchema.overrides.size())));
}

void MainWindow::manageSchemas()
{
    std::optional<xlsone::MergeSchema> currentSchema;
    const auto schemaId = workspaceActiveSchemaId_.has_value()
        ? workspaceActiveSchemaId_ : workspaceBaseSchemaId_;
    if (schemaId.has_value()) {
        currentSchema = schemaRepository_.find(*schemaId);
    }

    SchemaManagerDialog dialog(schemaRepository_, currentSchema, this);
    if (xlsone::ui::execDialogCentered(dialog, this) != QDialog::Accepted) {
        return;
    }

    if (auto imported = dialog.importedSchema(); imported.has_value()) {
        schemaRepository_.save(*imported);
        syncWorkspaceSchemaBase(imported);
        rebuildResultsWithCurrentOverrides();
        const int resultIndex = currentResultIndex();
        if (resultIndex >= 0) {
            showResult(results_[static_cast<size_t>(resultIndex)]);
        }
        updateSheetStrip();
        updateCorrectionBar();
        statusLabel_->setText(tr("已导入规则“%1”。").arg(imported->name));
        return;
    }

    if (dialog.clearRequested() && schemaId.has_value()) {
        schemaRepository_.remove(*schemaId);
        workspaceActiveSchemaId_.reset();
        workspaceBaseSchemaId_.reset();
        workspaceBaseOverrides_.clear();
        currentOverrides_.clear();
        forgottenOverrides_.clear();
        overrideHistory_.clear();
        rebuildResultsWithCurrentOverrides();
        const int resultIndex = currentResultIndex();
        if (resultIndex >= 0) {
            showResult(results_[static_cast<size_t>(resultIndex)]);
        }
        updateSheetStrip();
        updateCorrectionBar();
        statusLabel_->setText(tr("已清除规则。"));
    }
}

void MainWindow::exportResult()
{
    if (results_.empty()) {
        xlsone::ui::showInformation(this, tr("导出"), tr("当前没有可导出的汇总结果。"));
        return;
    }

    const auto path = xlsone::ui::getSaveFileNameCentered(
        this,
        tr("导出汇总"),
        suggestedWorkbookFileName(validation_.report, selectedPaths_),
        tr("Excel Workbook (*.xlsx);;All Files (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    try {
        QString outputPath = path;
        if (!outputPath.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive)) {
            outputPath += QStringLiteral(".xlsx");
        }
        const QString templatePath = selectedPaths_.isEmpty() ? QString() : selectedPaths_.front();
        xlsone::TemplateWorkbookExporter().exportWorkbook(templatePath, results_, outputPath);
    } catch (const std::exception& error) {
        xlsone::ui::showCritical(this, tr("导出失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::showLicenseActivation()
{
    LicenseActivationDialog dialog(licenseManager_, this);
    xlsone::ui::execDialogCentered(dialog, this);
}
