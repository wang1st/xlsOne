#include "inspector_panel.hpp"
#include "xlsone/core/models.hpp"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <algorithm>

using namespace xlsone;

class InspectorPanelTests final : public QObject {
    Q_OBJECT

private slots:
    void presentsSourceValuesAndInteractions();
};

void InspectorPanelTests::presentsSourceValuesAndInteractions()
{
    MergedCell cell;
    cell.type.kind = CellKind::Sum;
    cell.type.sum = 1406.0;
    cell.displayValue = QStringLiteral("1,406");
    cell.decision.decisionReasons = QStringList{
        QStringLiteral("所有有效来源均为数值，按求和处理")
    };
    cell.sources = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), QStringLiteral("100"), std::nullopt, CellSourceState::Value, 100.0},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), QStringLiteral("101"), std::nullopt, CellSourceState::Value, 101.0},
        {QStringLiteral("c.xlsx"), QStringLiteral("/c.xlsx"), QStringLiteral("102"), std::nullopt, CellSourceState::Value, 102.0},
        {QStringLiteral("d.xlsx"), QStringLiteral("/d.xlsx"), QStringLiteral("103"), std::nullopt, CellSourceState::Value, 103.0},
        {QStringLiteral("e.xlsx"), QStringLiteral("/e.xlsx"), QStringLiteral("1,000"), std::nullopt, CellSourceState::Value, 1000.0},
        {QStringLiteral("empty.xlsx"), QStringLiteral("/empty.xlsx"), {}, std::nullopt, CellSourceState::Empty, std::nullopt},
        {QStringLiteral("missing.xlsx"), QStringLiteral("/missing.xlsx"), {}, std::nullopt, CellSourceState::Missing, std::nullopt},
    };

    InspectorPanel panel;
    panel.resize(380, 700);
    panel.showCell(QStringLiteral("E6"), cell, false);
    panel.show();
    QApplication::processEvents();

    const auto toggles = panel.findChildren<QToolButton*>();
    const auto sourceToggle = std::find_if(toggles.begin(), toggles.end(), [](const auto* button) {
        return button->property("sourceToggle").toBool();
    });
    QVERIFY(sourceToggle != toggles.end());
    QVERIFY((*sourceToggle)->isChecked());

    const auto buttons = panel.findChildren<QPushButton*>();
    const auto sumButton = std::find_if(buttons.begin(), buttons.end(), [](const auto* button) {
        return button->property("choiceKind").toString() == QStringLiteral("sum");
    });
    const auto labelButton = std::find_if(buttons.begin(), buttons.end(), [](const auto* button) {
        return button->property("choiceKind").toString() == QStringLiteral("label");
    });
    QVERIFY(sumButton != buttons.end());
    QVERIFY(labelButton != buttons.end());
    QVERIFY((*sumButton)->isChecked());
    QVERIFY(!(*labelButton)->isChecked());

    QSignalSpy labelSpy(&panel, &InspectorPanel::markLabelRequested);
    QTest::mouseClick(*labelButton, Qt::LeftButton);
    QCOMPARE(labelSpy.count(), 1);

    const auto labels = panel.findChildren<QLabel*>();
    const auto sourceValueCount = std::count_if(labels.begin(), labels.end(), [](const auto* label) {
        return label->property("sourceValue").toBool();
    });
    const auto outlierCount = std::count_if(labels.begin(), labels.end(), [](const auto* label) {
        return label->property("outlierBadge").toBool();
    });
    const auto emptyStateCount = std::count_if(labels.begin(), labels.end(), [](const auto* label) {
        return label->property("sourceState").toString() == QStringLiteral("empty");
    });
    const auto missingStateCount = std::count_if(labels.begin(), labels.end(), [](const auto* label) {
        return label->property("sourceState").toString() == QStringLiteral("missing");
    });
    QCOMPARE(sourceValueCount, static_cast<qsizetype>(cell.sources.size()));
    QCOMPARE(outlierCount, static_cast<qsizetype>(1));
    QCOMPARE(emptyStateCount, static_cast<qsizetype>(1));
    QCOMPARE(missingStateCount, static_cast<qsizetype>(1));

    const auto copyValueButton = std::find_if(toggles.begin(), toggles.end(), [](const auto* button) {
        return button->property("sourceAction").toBool()
            && button->text() == QStringLiteral("复制值")
            && button->isEnabled();
    });
    QVERIFY(copyValueButton != toggles.end());
    QTest::mouseClick(*copyValueButton, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("100"));
}

QTEST_MAIN(InspectorPanelTests)
#include "inspector_panel_tests.moc"
