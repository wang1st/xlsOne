#include "schema_manager_dialog.hpp"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace {

QString overrideTypeName(xlsone::SchemaCellOverrideType type)
{
    switch (type) {
    case xlsone::SchemaCellOverrideType::Label:
        return QObject::tr("标签");
    case xlsone::SchemaCellOverrideType::Sum:
        return QObject::tr("求和");
    case xlsone::SchemaCellOverrideType::Mixed:
        return QObject::tr("混合");
    case xlsone::SchemaCellOverrideType::Single:
        return QObject::tr("单值");
    }
    return QObject::tr("未知");
}

QString formatDate(const QDateTime& value)
{
    if (!value.isValid()) {
        return QObject::tr("未知");
    }
    return value.toLocalTime().toString(Qt::ISODate);
}

xlsone::SchemaCellOverrideType overrideTypeFromString(const QString& value)
{
    if (value == QStringLiteral("sum")) {
        return xlsone::SchemaCellOverrideType::Sum;
    }
    if (value == QStringLiteral("mixed")) {
        return xlsone::SchemaCellOverrideType::Mixed;
    }
    return xlsone::SchemaCellOverrideType::Label;
}

xlsone::MergeSchema swiftSchemaFromJson(const QJsonObject& object)
{
    xlsone::MergeSchema schema;
    schema.name = object.value(QStringLiteral("name")).toString(QObject::tr("导入规则"));
    schema.version = 2;

    const auto workbookFingerprint = object.value(QStringLiteral("workbookFingerprint")).toObject();
    const auto sheetFingerprints = workbookFingerprint.value(QStringLiteral("sheetFingerprints")).toArray();
    for (const auto& item : sheetFingerprints) {
        const auto sheet = item.toObject();
        xlsone::SheetRuleFingerprint fingerprint;
        fingerprint.sheetName = sheet.value(QStringLiteral("sheetName")).toString();
        fingerprint.rowCount = sheet.value(QStringLiteral("rowCount")).toInt();
        fingerprint.columnCount = sheet.value(QStringLiteral("columnCount")).toInt();
        fingerprint.layoutHash = sheet.value(QStringLiteral("layoutHash")).toString();
        fingerprint.formatHash = sheet.value(QStringLiteral("formatHash")).toString();
        if (!fingerprint.sheetName.isEmpty()) {
            schema.fingerprint.sheetNames.append(fingerprint.sheetName);
            schema.fingerprint.sheetFingerprints.push_back(fingerprint);
        }
    }

    if (schema.fingerprint.sheetFingerprints.empty()) {
        const auto legacy = object.value(QStringLiteral("fingerprint")).toObject();
        const auto sheetName = legacy.value(QStringLiteral("sheetName")).toString();
        if (!sheetName.isEmpty()) {
            schema.fingerprint.sheetNames.append(sheetName);
            schema.fingerprint.sheetFingerprints.push_back({
                sheetName,
                legacy.value(QStringLiteral("rowCount")).toInt(),
                legacy.value(QStringLiteral("colCount")).toInt(),
                legacy.value(QStringLiteral("headerHash")).toString(),
                legacy.value(QStringLiteral("sampleDataHash")).toString()
            });
        }
    }

    const auto overrides = object.value(QStringLiteral("cellOverrides")).toArray();
    for (const auto& item : overrides) {
        const auto override = item.toObject();
        schema.overrides.push_back({
            {
                override.value(QStringLiteral("rowIndex")).toInt(),
                override.value(QStringLiteral("colIndex")).toInt()
            },
            overrideTypeFromString(override.value(QStringLiteral("cellType")).toString()),
            override.value(QStringLiteral("sheetName")).toString()
        });
    }
    return schema;
}

xlsone::MergeSchema importableSchemaFromJson(const QJsonObject& root)
{
    const QJsonObject object = root.value(QStringLiteral("schema")).isObject()
        ? root.value(QStringLiteral("schema")).toObject()
        : root;
    if (object.contains(QStringLiteral("overrides"))) {
        return xlsone::schemaFromJson(object);
    }
    if (object.contains(QStringLiteral("cellOverrides"))) {
        return swiftSchemaFromJson(object);
    }
    throw std::runtime_error("不支持的规则 JSON 格式");
}

} // namespace

SchemaManagerDialog::SchemaManagerDialog(const xlsone::SchemaRepository& repository, QWidget* parent) :
    QDialog(parent),
    repository_(repository)
{
    setWindowTitle(tr("规则管理"));
    resize(720, 420);

    list_ = new QListWidget(this);
    list_->setMinimumWidth(220);

    details_ = new QTextEdit(this);
    details_->setReadOnly(true);

    applyButton_ = new QPushButton(tr("应用"), this);
    deleteButton_ = new QPushButton(tr("删除"), this);
    importButton_ = new QPushButton(tr("导入"), this);
    exportButton_ = new QPushButton(tr("导出"), this);
    auto* closeButton = new QPushButton(tr("关闭"), this);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(applyButton_);
    buttonLayout->addWidget(deleteButton_);
    buttonLayout->addWidget(importButton_);
    buttonLayout->addWidget(exportButton_);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);

    auto* rightLayout = new QVBoxLayout;
    rightLayout->addWidget(details_, 1);
    rightLayout->addLayout(buttonLayout);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(list_, 0);
    layout->addLayout(rightLayout, 1);

    connect(list_, &QListWidget::currentRowChanged, this, &SchemaManagerDialog::updateDetails);
    connect(applyButton_, &QPushButton::clicked, this, &SchemaManagerDialog::applySelected);
    connect(deleteButton_, &QPushButton::clicked, this, &SchemaManagerDialog::deleteSelected);
    connect(importButton_, &QPushButton::clicked, this, &SchemaManagerDialog::importSchema);
    connect(exportButton_, &QPushButton::clicked, this, &SchemaManagerDialog::exportSelected);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    refresh();
}

std::optional<xlsone::MergeSchema> SchemaManagerDialog::selectedSchema() const
{
    return selectedSchema_;
}

void SchemaManagerDialog::refresh()
{
    schemas_ = repository_.loadAll();
    std::sort(schemas_.begin(), schemas_.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.updatedAt != rhs.updatedAt) {
            return lhs.updatedAt > rhs.updatedAt;
        }
        return lhs.name < rhs.name;
    });

    list_->clear();
    for (const auto& schema : schemas_) {
        list_->addItem(schema.name);
    }

    const bool hasSchemas = !schemas_.empty();
    applyButton_->setEnabled(hasSchemas);
    deleteButton_->setEnabled(hasSchemas);
    exportButton_->setEnabled(hasSchemas);
    if (hasSchemas) {
        list_->setCurrentRow(0);
    } else {
        details_->setPlainText(tr("暂无规则。"));
    }
}

void SchemaManagerDialog::updateDetails()
{
    const int row = list_->currentRow();
    const bool valid = row >= 0 && row < static_cast<int>(schemas_.size());
    applyButton_->setEnabled(valid);
    deleteButton_->setEnabled(valid);
    exportButton_->setEnabled(valid);
    if (!valid) {
        details_->clear();
        return;
    }

    details_->setPlainText(describeSchema(schemas_[static_cast<size_t>(row)]));
}

void SchemaManagerDialog::deleteSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(schemas_.size())) {
        return;
    }

    const auto schema = schemas_[static_cast<size_t>(row)];
    const auto choice = QMessageBox::question(
        this,
        tr("删除规则"),
        tr("删除规则“%1”？").arg(schema.name)
    );
    if (choice != QMessageBox::Yes) {
        return;
    }

    repository_.remove(schema.id);
    selectedSchema_.reset();
    refresh();
}

void SchemaManagerDialog::applySelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(schemas_.size())) {
        return;
    }

    selectedSchema_ = schemas_[static_cast<size_t>(row)];
    accept();
}

void SchemaManagerDialog::importSchema()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("导入规则"),
        {},
        tr("JSON Files (*.json);;All Files (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("导入失败"), file.errorString());
        return;
    }

    try {
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            throw std::runtime_error("JSON 根节点不是对象");
        }
        auto schema = importableSchemaFromJson(document.object());
        schema.id = QUuid::createUuid();
        schema.name = schema.name.isEmpty() ? tr("导入规则") : schema.name + tr(" (导入)");
        schema.createdAt = QDateTime::currentDateTimeUtc();
        schema.updatedAt = schema.createdAt;
        schema.matchCount = 0;
        repository_.save(schema);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("导入失败"), QString::fromUtf8(error.what()));
        return;
    }

    refresh();
}

void SchemaManagerDialog::exportSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(schemas_.size())) {
        return;
    }

    const auto& schema = schemas_[static_cast<size_t>(row)];
    const QString safeName = schema.name.trimmed().isEmpty() ? QStringLiteral("schema") : schema.name.trimmed();
    const auto path = QFileDialog::getSaveFileName(
        this,
        tr("导出规则"),
        safeName + QStringLiteral(".json"),
        tr("JSON Files (*.json);;All Files (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("导出失败"), file.errorString());
        return;
    }
    file.write(QJsonDocument(xlsone::toJson(schema)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::critical(this, tr("导出失败"), file.errorString());
    }
}

QString SchemaManagerDialog::describeSchema(const xlsone::MergeSchema& schema) const
{
    QStringList lines;
    lines << tr("名称: %1").arg(schema.name);
    lines << tr("版本: %1").arg(schema.version);
    lines << tr("创建: %1").arg(formatDate(schema.createdAt));
    lines << tr("更新: %1").arg(formatDate(schema.updatedAt));
    lines << tr("匹配次数: %1").arg(schema.matchCount);
    lines << tr("文件数: %1").arg(schema.fingerprint.fileCount);
    lines << tr("工作表: %1").arg(schema.fingerprint.sheetNames.join(QStringLiteral(", ")));
    lines << tr("签名: %1").arg(schema.fingerprint.signature);
    lines << QString();
    lines << tr("修正: %1 个").arg(static_cast<int>(schema.overrides.size()));
    for (const auto& override : schema.overrides) {
        const QString sheet = override.sheetName.isEmpty() ? tr("全部") : override.sheetName;
        lines << tr("- %1 %2 -> %3")
            .arg(sheet, xlsone::cellReference(override.position.row, override.position.column), overrideTypeName(override.type));
    }
    return lines.join(QLatin1Char('\n'));
}
