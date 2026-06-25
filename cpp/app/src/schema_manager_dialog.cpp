#include "schema_manager_dialog.hpp"

#include "dialog_utils.hpp"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    schema.name = object.value(QStringLiteral("name")).toString(QObject::tr("导入调整记忆"));
    schema.version = 2;

    const auto wf = object.value(QStringLiteral("workbookFingerprint")).toObject();
    const auto sheets = wf.value(QStringLiteral("sheetFingerprints")).toArray();
    for (const auto& item : sheets) {
        const auto s = item.toObject();
        xlsone::SheetRuleFingerprint fp;
        fp.sheetName = s.value(QStringLiteral("sheetName")).toString();
        fp.rowCount = s.value(QStringLiteral("rowCount")).toInt();
        fp.columnCount = s.value(QStringLiteral("columnCount")).toInt();
        fp.layoutHash = s.value(QStringLiteral("layoutHash")).toString();
        fp.formatHash = s.value(QStringLiteral("formatHash")).toString();
        if (!fp.sheetName.isEmpty()) {
            schema.fingerprint.sheetNames.append(fp.sheetName);
            schema.fingerprint.sheetFingerprints.push_back(fp);
        }
    }

    if (schema.fingerprint.sheetFingerprints.empty()) {
        const auto legacy = object.value(QStringLiteral("fingerprint")).toObject();
        const auto sn = legacy.value(QStringLiteral("sheetName")).toString();
        if (!sn.isEmpty()) {
            schema.fingerprint.sheetNames.append(sn);
            schema.fingerprint.sheetFingerprints.push_back({
                sn,
                legacy.value(QStringLiteral("rowCount")).toInt(),
                legacy.value(QStringLiteral("colCount")).toInt(),
                legacy.value(QStringLiteral("headerHash")).toString(),
                legacy.value(QStringLiteral("sampleDataHash")).toString()
            });
        }
    }

    const auto overrides = object.value(QStringLiteral("cellOverrides")).toArray();
    for (const auto& item : overrides) {
        const auto o = item.toObject();
        schema.overrides.push_back({
            { o.value(QStringLiteral("rowIndex")).toInt(), o.value(QStringLiteral("colIndex")).toInt() },
            overrideTypeFromString(o.value(QStringLiteral("cellType")).toString()),
            o.value(QStringLiteral("sheetName")).toString()
        });
    }
    return schema;
}

xlsone::MergeSchema importableSchemaFromJson(const QJsonObject& root)
{
    const QJsonObject object = root.value(QStringLiteral("schema")).isObject()
        ? root.value(QStringLiteral("schema")).toObject() : root;
    if (object.contains(QStringLiteral("overrides"))) {
        return xlsone::schemaFromJson(object);
    }
    if (object.contains(QStringLiteral("cellOverrides"))) {
        return swiftSchemaFromJson(object);
    }
    throw std::runtime_error("不支持的调整记忆 JSON 格式");
}

} // namespace

SchemaManagerDialog::SchemaManagerDialog(const xlsone::SchemaRepository& repository,
                                         const std::optional<xlsone::MergeSchema>& currentSchema,
                                         QWidget* parent) :
    QDialog(parent),
    repository_(repository),
    currentSchema_(currentSchema)
{
    setWindowTitle(tr("当前调整记忆"));
    resize(500, 400);

    details_ = new QTextEdit(this);
    details_->setReadOnly(true);

    exportButton_ = new QPushButton(tr("导出"), this);
    importButton_ = new QPushButton(tr("导入"), this);
    clearButton_ = new QPushButton(tr("清除当前调整记忆"), this);
    auto* closeButton = new QPushButton(tr("关闭"), this);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(exportButton_);
    buttonLayout->addWidget(importButton_);
    buttonLayout->addWidget(clearButton_);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(details_, 1);
    layout->addLayout(buttonLayout);

    connect(exportButton_, &QPushButton::clicked, this, &SchemaManagerDialog::exportCurrent);
    connect(importButton_, &QPushButton::clicked, this, &SchemaManagerDialog::importSchema);
    connect(clearButton_, &QPushButton::clicked, this, &SchemaManagerDialog::clearCurrent);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    refreshDetails();
}

std::optional<xlsone::MergeSchema> SchemaManagerDialog::importedSchema() const
{
    return importedSchema_;
}

bool SchemaManagerDialog::clearRequested() const
{
    return clearRequested_;
}

void SchemaManagerDialog::refreshDetails()
{
    const bool hasSchema = currentSchema_.has_value();
    exportButton_->setEnabled(hasSchema);
    clearButton_->setEnabled(hasSchema);

    if (hasSchema) {
        details_->setPlainText(describeSchema(*currentSchema_));
    } else {
        details_->setPlainText(tr("当前同构结构暂无调整记忆。\n\n在汇总结果中手动修正单元格后，点击\"调整记忆\"菜单中的\"保存当前调整记忆\"即可创建。"));
    }
}

void SchemaManagerDialog::exportCurrent()
{
    if (!currentSchema_.has_value()) { return; }

    const auto& schema = *currentSchema_;
    const QString safeName = schema.name.trimmed().isEmpty()
        ? QStringLiteral("schema") : schema.name.trimmed();
    const auto path = xlsone::ui::getSaveFileNameCentered(
        this, tr("导出调整记忆"), safeName + QStringLiteral(".json"),
        tr("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) { return; }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        xlsone::ui::showCritical(this, tr("导出失败"), file.errorString());
        return;
    }
    file.write(QJsonDocument(xlsone::toJson(schema)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        xlsone::ui::showCritical(this, tr("导出失败"), file.errorString());
    }
}

void SchemaManagerDialog::importSchema()
{
    const auto path = xlsone::ui::getOpenFileNameCentered(
        this, tr("导入调整记忆"), {}, tr("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) { return; }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        xlsone::ui::showCritical(this, tr("导入失败"), file.errorString());
        return;
    }

    try {
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            throw std::runtime_error("JSON 根节点不是对象");
        }
        auto schema = importableSchemaFromJson(document.object());
        schema.id = QUuid::createUuid();
        schema.createdAt = QDateTime::currentDateTimeUtc();
        schema.updatedAt = schema.createdAt;
        importedSchema_ = schema;
        currentSchema_ = schema;
        refreshDetails();
        accept();
    } catch (const std::exception& error) {
        xlsone::ui::showCritical(this, tr("导入失败"), QString::fromUtf8(error.what()));
    }
}

void SchemaManagerDialog::clearCurrent()
{
    const auto choice = xlsone::ui::askQuestion(
        this, tr("清除当前调整记忆"),
        tr("删除当前调整记忆后，汇总表将恢复自动判断。确定清除？"));
    if (choice != QMessageBox::Yes) { return; }
    clearRequested_ = true;
    accept();
}

QString SchemaManagerDialog::describeSchema(const xlsone::MergeSchema& schema) const
{
    QStringList lines;
    lines << tr("名称: %1").arg(schema.name);
    lines << tr("更新: %1").arg(formatDate(schema.updatedAt));
    lines << tr("文件数: %1").arg(schema.fingerprint.fileCount);
    lines << tr("工作表: %1").arg(schema.fingerprint.sheetNames.join(QStringLiteral(", ")));
    lines << QString();
    lines << tr("修正: %1 个").arg(static_cast<int>(schema.overrides.size()));
    for (const auto& override : schema.overrides) {
        const QString sheet = override.sheetName.isEmpty() ? tr("全部") : override.sheetName;
        lines << tr("- %1 %2 -> %3")
            .arg(sheet, xlsone::cellReference(override.position.row, override.position.column),
                 overrideTypeName(override.type));
    }
    return lines.join(QLatin1Char('\n'));
}
