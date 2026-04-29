#include "main_window.hpp"

#include "schema_manager_dialog.hpp"
#include "xlsone/core/exporter.hpp"

#include <QAction>
#include <QDateTime>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

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

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    buildUi();
    setAcceptDrops(true);
}

void MainWindow::buildUi()
{
    auto* toolbar = addToolBar(tr("Workspace"));
    toolbar->setMovable(false);

    auto* openAction = toolbar->addAction(tr("打开"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFiles);

    auto* appendAction = toolbar->addAction(tr("追加"));
    appendAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(appendAction, &QAction::triggered, this, &MainWindow::appendFiles);

    auto* reloadAction = toolbar->addAction(tr("刷新"));
    reloadAction->setShortcut(QKeySequence::Refresh);
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadFiles);

    auto* clearAction = toolbar->addAction(tr("清空"));
    clearAction->setShortcut(QKeySequence::New);
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearWorkspace);

    auto* exportAction = toolbar->addAction(tr("导出"));
    exportAction->setShortcut(QKeySequence::Save);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportResult);

    auto* markLabelAction = toolbar->addAction(tr("标签"));
    connect(markLabelAction, &QAction::triggered, this, &MainWindow::markSelectedAsLabel);

    auto* markSumAction = toolbar->addAction(tr("求和"));
    connect(markSumAction, &QAction::triggered, this, &MainWindow::markSelectedAsSum);

    auto* markMixedAction = toolbar->addAction(tr("混合"));
    connect(markMixedAction, &QAction::triggered, this, &MainWindow::markSelectedAsMixed);

    auto* restoreAutoAction = toolbar->addAction(tr("恢复自动"));
    connect(restoreAutoAction, &QAction::triggered, this, &MainWindow::restoreAutomaticDecisionForSelection);

    auto* saveSchemaAction = toolbar->addAction(tr("保存规则"));
    connect(saveSchemaAction, &QAction::triggered, this, &MainWindow::saveCurrentSchema);

    auto* manageSchemasAction = toolbar->addAction(tr("管理规则"));
    connect(manageSchemasAction, &QAction::triggered, this, &MainWindow::manageSchemas);

    auto* undoOverrideAction = toolbar->addAction(tr("撤销修正"));
    undoOverrideAction->setShortcut(QKeySequence::Undo);
    connect(undoOverrideAction, &QAction::triggered, this, &MainWindow::undoLastOverride);

    auto* clearOverridesAction = toolbar->addAction(tr("清除修正"));
    connect(clearOverridesAction, &QAction::triggered, this, &MainWindow::clearOverrides);

    auto* previousAnomalyAction = toolbar->addAction(tr("上一异常"));
    connect(previousAnomalyAction, &QAction::triggered, this, &MainWindow::jumpToPreviousAnomaly);

    auto* nextAnomalyAction = toolbar->addAction(tr("下一异常"));
    connect(nextAnomalyAction, &QAction::triggered, this, &MainWindow::jumpToNextAnomaly);

    toolbar->addSeparator();
    sheetCombo_ = new QComboBox(this);
    sheetCombo_->setMinimumWidth(220);
    toolbar->addWidget(sheetCombo_);
    connect(sheetCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::selectSheet);

    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(appendAction);
    fileMenu->addAction(reloadAction);
    fileMenu->addAction(clearAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exportAction);

    auto* rulesMenu = menuBar()->addMenu(tr("规则"));
    rulesMenu->addAction(markLabelAction);
    rulesMenu->addAction(markSumAction);
    rulesMenu->addAction(markMixedAction);
    rulesMenu->addAction(restoreAutoAction);
    rulesMenu->addSeparator();
    rulesMenu->addAction(saveSchemaAction);
    rulesMenu->addAction(manageSchemasAction);
    rulesMenu->addSeparator();
    rulesMenu->addAction(undoOverrideAction);
    rulesMenu->addAction(clearOverridesAction);
    rulesMenu->addSeparator();
    rulesMenu->addAction(previousAnomalyAction);
    rulesMenu->addAction(nextAnomalyAction);

    tableModel_ = new MergedTableModel(this);
    table_ = new QTableView(this);
    table_->setModel(tableModel_);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    setCentralWidget(table_);
    connect(table_, &QTableView::clicked, this, &MainWindow::inspectCell);

    auto* filesDock = new QDockWidget(tr("文件"), this);
    fileList_ = new QListWidget(filesDock);
    filesDock->setWidget(fileList_);
    addDockWidget(Qt::LeftDockWidgetArea, filesDock);

    auto* inspectorDock = new QDockWidget(tr("来源"), this);
    inspector_ = new QTextEdit(inspectorDock);
    inspector_->setReadOnly(true);
    inspectorDock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    auto* diagnosticsDock = new QDockWidget(tr("诊断"), this);
    diagnostics_ = new QTextEdit(diagnosticsDock);
    diagnostics_->setReadOnly(true);
    diagnosticsDock->setWidget(diagnostics_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock);

    statusLabel_ = new QLabel(tr("拖入 Excel/CSV 文件，或点击打开开始。"), this);
    statusBar()->addPermanentWidget(statusLabel_, 1);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    loadFiles(paths, !selectedPaths_.isEmpty());
}

QStringList MainWindow::chooseInputFiles() const
{
    return QFileDialog::getOpenFileNames(
        nullptr,
        tr("选择工作簿"),
        {},
        tr("Excel/CSV Files (*.xlsx *.xls *.csv *.tsv);;All Files (*)")
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
    baseResults_.clear();
    results_.clear();
    currentOverrides_.clear();
    forgottenOverrides_.clear();
    overrideHistory_.clear();
    workspaceBaseOverrides_.clear();
    workspaceBaseSchemaId_.reset();
    workspaceActiveSchemaId_.reset();
    fileList_->clear();
    sheetCombo_->clear();
    tableModel_->setResult({});
    inspector_->clear();
    diagnostics_->clear();
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

    fileList_->clear();
    for (const auto& path : selectedPaths_) {
        fileList_->addItem(path);
    }

    recomputeWorkspace();
}

void MainWindow::recomputeWorkspace()
{
    validation_ = validator_.validate(parsedFiles_, parseFailures_);
    showValidationSummary();
    updateDiagnostics();

    sheetCombo_->blockSignals(true);
    sheetCombo_->clear();
    sheetCombo_->addItems(validation_.report.commonSheetNames);
    sheetCombo_->blockSignals(false);

    baseResults_.clear();
    results_.clear();
    if (validation_.report.readiness != xlsone::MergeReadiness::Ready) {
        tableModel_->setResult({});
        inspector_->setPlainText(tr("没有可参与汇总的同构工作表。"));
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
    if (!results_.empty()) {
        showResult(results_.front());
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
    fileList_->clear();
    for (const auto& report : validation_.report.files) {
        const QString marker = report.isTemplate ? tr("模板") : fileStatusName(report.status);
        auto* item = new QListWidgetItem(tr("[%1] %2").arg(marker, report.filename), fileList_);
        item->setToolTip(report.filepath);
    }

    QStringList lines;
    if (validation_.report.files.empty()) {
        diagnostics_->setPlainText(tr("暂无诊断。"));
        return;
    }

    lines << tr("状态: %1").arg(readinessName(validation_.report.readiness));
    lines << tr("可合并工作表: %1").arg(
        validation_.report.commonSheetNames.isEmpty()
            ? tr("无")
            : validation_.report.commonSheetNames.join(QStringLiteral(", "))
    );
    lines << tr("跳过工作表: %1").arg(
        validation_.report.skippedSheetNames.isEmpty()
            ? tr("无")
            : validation_.report.skippedSheetNames.join(QStringLiteral(", "))
    );

    lines << QString();
    lines << tr("文件:");
    for (const auto& report : validation_.report.files) {
        const QString templateFlag = report.isTemplate ? tr(" / 模板") : QString();
        lines << tr("- %1: %2%3").arg(report.filename, fileStatusName(report.status), templateFlag);
        for (const auto& issue : report.issues) {
            lines << tr("  [%1] %2").arg(severityName(issue.severity), issue.message);
        }
    }

    if (!validation_.report.skippedSheetIssues.empty()) {
        lines << QString();
        lines << tr("工作表问题:");
        for (const auto& issue : validation_.report.skippedSheetIssues) {
            lines << tr("- %1 / %2: %3").arg(issue.fileName, issue.sheetName, issue.message);
        }
    }

    diagnostics_->setPlainText(lines.join(QLatin1Char('\n')));
}

void MainWindow::selectSheet(int index)
{
    if (index < 0 || index >= static_cast<int>(results_.size())) {
        return;
    }
    showResult(results_[static_cast<size_t>(index)]);
}

void MainWindow::showResult(const xlsone::MergedResult& result)
{
    tableModel_->setResult(result);
    inspector_->setPlainText(tr("选择一个单元格查看来源。"));
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

    QStringList lines;
    lines << tr("%1: %2").arg(xlsone::cellReference(index.row(), index.column()), cell->displayValue);
    lines << tr("类型: %1").arg(xlsone::cellKindName(cell->type.kind));
    if (cell->isOverridden) {
        lines << tr("状态: 已应用规则修正");
    }
    if (!cell->decision.decisionReasons.isEmpty()) {
        lines << QString();
        lines << tr("决策:");
        for (const auto& reason : cell->decision.decisionReasons) {
            lines << tr("- %1").arg(reason);
        }
    }
    lines << QString();
    lines << tr("来源:");
    for (const auto& source : cell->sources) {
        const auto state = source.state == xlsone::CellSourceState::Value
            ? tr("值")
            : source.state == xlsone::CellSourceState::Empty ? tr("空") : tr("缺失");
        lines << tr("%1 [%2]: %3").arg(source.filename, state, source.value);
    }
    inspector_->setPlainText(lines.join(QLatin1Char('\n')));
}

int MainWindow::currentResultIndex() const
{
    if (results_.empty()) {
        return -1;
    }
    int index = sheetCombo_ == nullptr ? 0 : sheetCombo_->currentIndex();
    if (index < 0) {
        index = 0;
    }
    if (index >= static_cast<int>(results_.size())) {
        return -1;
    }
    return index;
}

void MainWindow::rebuildResultsWithCurrentOverrides()
{
    results_ = baseResults_;
    xlsone::MergeSchema transientSchema;
    transientSchema.name = tr("当前手动规则");
    transientSchema.overrides = currentOverrides_;
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
    QString baseName;
    if (!validation_.report.files.empty()) {
        baseName = QFileInfo(validation_.report.files.front().filename).completeBaseName();
    } else if (!selectedPaths_.isEmpty()) {
        baseName = QFileInfo(selectedPaths_.front()).completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("xlsOne");
    }
    if (baseName.endsWith(QStringLiteral("_汇总"))) {
        baseName.chop(QStringLiteral("_汇总").size());
    } else if (baseName.endsWith(QStringLiteral("汇总"))) {
        baseName.chop(QStringLiteral("汇总").size());
    }
    return baseName + tr("调整记忆");
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
        QMessageBox::information(this, tr("规则"), tr("请先选择一个或多个汇总单元格。"));
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
        QMessageBox::warning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
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

void MainWindow::markSelectedAsMixed()
{
    applyOverrideForSelection(xlsone::SchemaCellOverrideType::Mixed);
}

void MainWindow::restoreAutomaticDecisionForSelection()
{
    const int resultIndex = currentResultIndex();
    const QModelIndex index = table_ == nullptr ? QModelIndex() : table_->currentIndex();
    if (resultIndex < 0 || !index.isValid()) {
        QMessageBox::information(this, tr("恢复自动"), tr("请先选择一个汇总单元格。"));
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
        QMessageBox::information(this, tr("恢复自动"), tr("当前单元格没有手动或已记住的修正。"));
        return;
    }

    overrideHistory_.push_back({currentOverrides_, forgottenOverrides_});
    removeOverrideCell(currentOverrides_, key);
    if (containsOverrideCell(workspaceBaseOverrides_, key) && !containsOverrideCell(forgottenOverrides_, key)) {
        forgottenOverrides_.push_back(key);
    }

    rebuildResultsWithCurrentOverrides();
    showResult(results_[static_cast<size_t>(resultIndex)]);
    const QModelIndex refreshed = tableModel_->index(index.row(), index.column());
    table_->setCurrentIndex(refreshed);
    inspectCell(refreshed);
    statusLabel_->setText(tr("已将 %1 恢复为自动判定。").arg(xlsone::cellReference(index.row(), index.column())));
    try {
        persistAdjustmentMemory();
        statusLabel_->setText(statusLabel_->text() + tr(" 已记住调整。"));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::undoLastOverride()
{
    if (overrideHistory_.empty()) {
        QMessageBox::information(this, tr("撤销修正"), tr("当前没有可撤销的修正。"));
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
    statusLabel_->setText(tr("已撤销上一步修正。"));
    try {
        persistAdjustmentMemory();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::clearOverrides()
{
    if (currentOverrides_.empty() && forgottenOverrides_.empty()) {
        QMessageBox::information(this, tr("清除修正"), tr("当前没有手动修正。"));
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
    statusLabel_->setText(tr("已清除当前工作区的手动修正。"));
    try {
        persistAdjustmentMemory();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("记住调整失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::jumpToNextAnomaly()
{
    jumpAcrossAnomalies(1);
}

void MainWindow::jumpToPreviousAnomaly()
{
    jumpAcrossAnomalies(-1);
}

void MainWindow::jumpAcrossAnomalies(int step)
{
    const int resultIndex = currentResultIndex();
    if (resultIndex < 0) {
        QMessageBox::information(this, tr("异常导航"), tr("当前没有可检查的汇总结果。"));
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
        QMessageBox::information(this, tr("异常导航"), tr("当前工作表没有混合或可疑单元格。"));
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
        QMessageBox::information(this, tr("保存规则"), tr("当前还没有手动修正的单元格。"));
        return;
    }
    if (validation_.mergeableFiles.empty() || validation_.report.commonSheetNames.isEmpty()) {
        QMessageBox::information(this, tr("保存规则"), tr("当前没有可用于生成规则指纹的同构工作簿。"));
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this,
        tr("保存规则"),
        tr("规则名称"),
        QLineEdit::Normal,
        tr("新规则"),
        &accepted
    ).trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }

    xlsone::MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = name;
    schema.version = 2;
    schema.fingerprint = xlsone::fingerprintFor(validation_.mergeableFiles, validation_.report.commonSheetNames);
    schema.overrides = effectiveWorkspaceOverrides();
    schema.createdAt = QDateTime::currentDateTimeUtc();
    schema.updatedAt = schema.createdAt;

    try {
        schemaRepository_.save(schema);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("保存失败"), QString::fromUtf8(error.what()));
        return;
    }

    workspaceBaseOverrides_ = schema.overrides;
    workspaceBaseSchemaId_ = schema.id;
    workspaceActiveSchemaId_ = schema.id;
    currentOverrides_.clear();
    forgottenOverrides_.clear();
    overrideHistory_.clear();
    rebuildResultsWithCurrentOverrides();

    statusLabel_->setText(tr("已保存规则“%1”，包含 %2 个修正。").arg(schema.name).arg(static_cast<int>(schema.overrides.size())));
    QMessageBox::information(this, tr("保存规则"), tr("规则已保存，后续相同结构文件会自动匹配应用。"));
}

void MainWindow::manageSchemas()
{
    SchemaManagerDialog dialog(schemaRepository_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto schema = dialog.selectedSchema();
    if (!schema.has_value()) {
        return;
    }
    if (results_.empty()) {
        QMessageBox::information(this, tr("应用规则"), tr("当前没有可应用规则的汇总结果。"));
        return;
    }

    currentOverrides_.clear();
    forgottenOverrides_.clear();
    overrideHistory_.clear();
    syncWorkspaceSchemaBase(schema);
    rebuildResultsWithCurrentOverrides();

    const int resultIndex = currentResultIndex();
    if (resultIndex >= 0) {
        showResult(results_[static_cast<size_t>(resultIndex)]);
    }
    statusLabel_->setText(tr("已应用规则“%1”。").arg(schema->name));
}

void MainWindow::exportResult()
{
    if (results_.empty()) {
        QMessageBox::information(this, tr("导出"), tr("当前没有可导出的汇总结果。"));
        return;
    }

    const auto path = QFileDialog::getSaveFileName(
        this,
        tr("导出汇总"),
        QStringLiteral("xlsone-summary.xlsx"),
        tr("Excel Workbook (*.xlsx);;CSV Files (*.csv);;All Files (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    try {
        const QString templatePath = selectedPaths_.isEmpty() ? QString() : selectedPaths_.front();
        xlsone::TemplateWorkbookExporter().exportWorkbook(templatePath, results_, path);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("导出失败"), QString::fromUtf8(error.what()));
    }
}
