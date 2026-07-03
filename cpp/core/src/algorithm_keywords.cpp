#include "algorithm_keywords.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace xlsone {

AlgorithmKeywords& AlgorithmKeywords::instance()
{
    static AlgorithmKeywords s_instance;
    return s_instance;
}

void AlgorithmKeywords::initFallbacks()
{
    m_amountPatterns = QStringList()
        << QStringLiteral("合计") << QStringLiteral("总计") << QStringLiteral("小计")
        << QStringLiteral("金额") << QStringLiteral("数额") << QStringLiteral("额度")
        << QStringLiteral("数量") << QStringLiteral("单价") << QStringLiteral("总价")
        << QStringLiteral("价格") << QStringLiteral("数值") << QStringLiteral("预算")
        << QStringLiteral("收入") << QStringLiteral("支出") << QStringLiteral("成本")
        << QStringLiteral("费用") << QStringLiteral("利润") << QStringLiteral("执行")
        << QStringLiteral("决算") << QStringLiteral("款") << QStringLiteral("税金")
        << QStringLiteral("人数") << QStringLiteral("人口") << QStringLiteral("户数")
        << QStringLiteral("家数") << QStringLiteral("个数") << QStringLiteral("人员")
        << QStringLiteral("编制") << QStringLiteral("职工")
        << QStringLiteral("sum") << QStringLiteral("total") << QStringLiteral("subtotal")
        << QStringLiteral("amount") << QStringLiteral("quantity") << QStringLiteral("qty")
        << QStringLiteral("price") << QStringLiteral("value") << QStringLiteral("budget")
        << QStringLiteral("revenue") << QStringLiteral("income") << QStringLiteral("expense")
        << QStringLiteral("cost") << QStringLiteral("fee") << QStringLiteral("profit")
        << QStringLiteral("tax") << QStringLiteral("fund") << QStringLiteral("population")
        << QStringLiteral("headcount") << QStringLiteral("staff");

    m_weakAmountPatterns = QStringList()
        << QStringLiteral("数") << QStringLiteral("额") << QStringLiteral("值")
        << QStringLiteral("量") << QStringLiteral("价");

    m_codePatterns = QStringList()
        << QStringLiteral("代码") << QStringLiteral("编码") << QStringLiteral("编号")
        << QStringLiteral("序号") << QStringLiteral("号码") << QStringLiteral("证号")
        << QStringLiteral("区划") << QStringLiteral("邮编") << QStringLiteral("邮政编码")
        << QStringLiteral("身份证") << QStringLiteral("电话") << QStringLiteral("传真")
        << QStringLiteral("期间") << QStringLiteral("年月") << QStringLiteral("年份")
        << QStringLiteral("日期") << QStringLiteral("时间") << QStringLiteral("学号")
        << QStringLiteral("工号") << QStringLiteral("账号") << QStringLiteral("户号")
        << QStringLiteral("卡号") << QStringLiteral("单号") << QStringLiteral("订单号")
        << QStringLiteral("票号") << QStringLiteral("发票号") << QStringLiteral("批号")
        << QStringLiteral("条码") << QStringLiteral("档案号") << QStringLiteral("许可证号")
        << QStringLiteral("code") << QStringLiteral("number") << QStringLiteral("no.")
        << QStringLiteral(" no ") << QStringLiteral(" id") << QStringLiteral("index")
        << QStringLiteral("serial") << QStringLiteral("zip") << QStringLiteral("postal code")
        << QStringLiteral("phone") << QStringLiteral("tel") << QStringLiteral("fax")
        << QStringLiteral("period") << QStringLiteral("date") << QStringLiteral("time")
        << QStringLiteral("year") << QStringLiteral("month");

    m_labelPatterns = QStringList()
        << QStringLiteral("名称") << QStringLiteral("名字") << QStringLiteral("描述")
        << QStringLiteral("说明") << QStringLiteral("备注") << QStringLiteral("标题")
        << QStringLiteral("内容") << QStringLiteral("详情") << QStringLiteral("类型")
        << QStringLiteral("性质") << QStringLiteral("状态")
        << QStringLiteral("name") << QStringLiteral("desc") << QStringLiteral("description")
        << QStringLiteral("title") << QStringLiteral("remark") << QStringLiteral("note")
        << QStringLiteral("type") << QStringLiteral("kind") << QStringLiteral("status");

    m_sumForcingPatterns = QStringList()
        << QStringLiteral("合计") << QStringLiteral("总计") << QStringLiteral("小计");

    m_dashMarkers = QStringList()
        << QStringLiteral("—") << QStringLiteral("-") << QStringLiteral("/")
        << QStringLiteral("NA") << QStringLiteral("N/A") << QStringLiteral("无")
        << QStringLiteral("null") << QStringLiteral("NULL") << QStringLiteral("~");

    m_metricAnchorPatterns = QStringList()
        << QStringLiteral("合计") << QStringLiteral("总计") << QStringLiteral("小计")
        << QStringLiteral("金额") << QStringLiteral("数额") << QStringLiteral("额度")
        << QStringLiteral("数量") << QStringLiteral("单价") << QStringLiteral("总价")
        << QStringLiteral("价格") << QStringLiteral("数值") << QStringLiteral("预算")
        << QStringLiteral("收入") << QStringLiteral("支出") << QStringLiteral("成本")
        << QStringLiteral("费用") << QStringLiteral("利润") << QStringLiteral("执行")
        << QStringLiteral("决算") << QStringLiteral("款") << QStringLiteral("税金")
        << QStringLiteral("人数") << QStringLiteral("人口") << QStringLiteral("户数")
        << QStringLiteral("家数") << QStringLiteral("个数") << QStringLiteral("人员")
        << QStringLiteral("编制") << QStringLiteral("职工")
        << QStringLiteral("数") << QStringLiteral("额") << QStringLiteral("值")
        << QStringLiteral("量") << QStringLiteral("价");

    m_codeAnchorPatterns = QStringList()
        << QStringLiteral("代码") << QStringLiteral("编码") << QStringLiteral("编号")
        << QStringLiteral("序号") << QStringLiteral("号码") << QStringLiteral("证号")
        << QStringLiteral("区划") << QStringLiteral("邮编") << QStringLiteral("邮政编码")
        << QStringLiteral("身份证") << QStringLiteral("电话") << QStringLiteral("传真")
        << QStringLiteral("期间") << QStringLiteral("年月") << QStringLiteral("年份")
        << QStringLiteral("日期") << QStringLiteral("时间") << QStringLiteral("学号")
        << QStringLiteral("工号") << QStringLiteral("账号") << QStringLiteral("户号")
        << QStringLiteral("卡号") << QStringLiteral("单号") << QStringLiteral("订单号")
        << QStringLiteral("票号") << QStringLiteral("发票号") << QStringLiteral("批号")
        << QStringLiteral("条码") << QStringLiteral("档案号") << QStringLiteral("许可证号");
}

void AlgorithmKeywords::mergeArray(const QJsonObject& obj, const QString& key, QStringList& target)
{
    const auto it = obj.constFind(key);
    if (it == obj.constEnd() || !it->isArray()) {
        return;
    }
    const QJsonArray arr = it->toArray();
    QSet<QString> existing;
    for (const QString& s : target) existing.insert(s);
    for (const auto& val : arr) {
        if (val.isString()) {
            const QString s = val.toString().trimmed();
            if (!s.isEmpty() && !existing.contains(s)) {
                target.append(s);
                existing.insert(s);
            }
        }
    }
}

void AlgorithmKeywords::mergeArrays(const QJsonObject& obj, const QStringList& keys, QStringList& target)
{
    for (const auto& key : keys) {
        mergeArray(obj, key, target);
    }
}

void AlgorithmKeywords::load()
{
    m_loaded = true;

    QString filePath;

    if (QCoreApplication::instance() != nullptr) {
        filePath = QStringLiteral(":/i18n/algorithm_keywords.json");
        if (!QFile::exists(filePath)) {
            filePath = QCoreApplication::applicationDirPath() + QStringLiteral("/../i18n/algorithm_keywords.json");
        }
        if (!QFile::exists(filePath)) {
            filePath = QCoreApplication::applicationDirPath() + QStringLiteral("/i18n/algorithm_keywords.json");
        }
    }

    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();

    QStringList newAmountPatterns;
    QStringList newWeakAmountPatterns;
    QStringList newCodePatterns;
    QStringList newLabelPatterns;
    QStringList newSumForcingPatterns;
    QStringList newDashMarkers;

    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject langObj = it->toObject();
        mergeArrays(langObj, QStringList()
            << QStringLiteral("amountPatterns") << QStringLiteral("amountKeywordsEn"),
            newAmountPatterns);
        mergeArray(langObj, QStringLiteral("weakAmountPatterns"), newWeakAmountPatterns);
        mergeArrays(langObj, QStringList()
            << QStringLiteral("codePatterns") << QStringLiteral("codeKeywordsEn"),
            newCodePatterns);
        mergeArrays(langObj, QStringList()
            << QStringLiteral("labelPatterns") << QStringLiteral("labelKeywordsEn"),
            newLabelPatterns);
        mergeArray(langObj, QStringLiteral("sumForcingPatterns"), newSumForcingPatterns);
        mergeArray(langObj, QStringLiteral("dashMarkers"), newDashMarkers);
    }

    if (!newAmountPatterns.isEmpty()) {
        m_amountPatterns = newAmountPatterns;
    }
    if (!newWeakAmountPatterns.isEmpty()) {
        m_weakAmountPatterns = newWeakAmountPatterns;
    }
    if (!newCodePatterns.isEmpty()) {
        m_codePatterns = newCodePatterns;
    }
    if (!newLabelPatterns.isEmpty()) {
        m_labelPatterns = newLabelPatterns;
    }
    if (!newSumForcingPatterns.isEmpty()) {
        m_sumForcingPatterns = newSumForcingPatterns;
    }
    if (!newDashMarkers.isEmpty()) {
        m_dashMarkers = newDashMarkers;
    }

    m_metricAnchorPatterns.clear();
    m_metricAnchorPatterns = m_amountPatterns;
    {
        QSet<QString> existing;
        for (const QString& s : m_amountPatterns) existing.insert(s);
        for (const auto& s : m_weakAmountPatterns) {
            if (!existing.contains(s)) {
                m_metricAnchorPatterns.append(s);
            }
        }
    }

    m_codeAnchorPatterns.clear();
    {
        QStringList codeOnly;
        for (auto langIt = root.constBegin(); langIt != root.constEnd(); ++langIt) {
            const QJsonObject langObj = langIt->toObject();
            mergeArray(langObj, QStringLiteral("codePatterns"), codeOnly);
        }
        if (!codeOnly.isEmpty()) {
            m_codeAnchorPatterns = codeOnly;
        } else {
            m_codeAnchorPatterns = m_codePatterns;
        }
    }
}

} // namespace xlsone
