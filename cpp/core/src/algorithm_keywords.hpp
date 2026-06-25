#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace xlsone {

class AlgorithmKeywords {
public:
    static AlgorithmKeywords& instance();

    void load();
    bool isLoaded() const { return m_loaded; }

    QStringList amountPatterns() const { return m_amountPatterns; }
    QStringList weakAmountPatterns() const { return m_weakAmountPatterns; }
    QStringList codePatterns() const { return m_codePatterns; }
    QStringList labelPatterns() const { return m_labelPatterns; }
    QStringList sumForcingPatterns() const { return m_sumForcingPatterns; }
    QStringList metricAnchorPatterns() const { return m_metricAnchorPatterns; }
    QStringList codeAnchorPatterns() const { return m_codeAnchorPatterns; }
    QStringList dashMarkers() const { return m_dashMarkers; }

private:
    AlgorithmKeywords() { initFallbacks(); }
    void initFallbacks();
    void mergeArray(const QJsonObject& obj, const QString& key, QStringList& target);
    void mergeArrays(const QJsonObject& obj, const QStringList& keys, QStringList& target);

    QStringList m_amountPatterns;
    QStringList m_weakAmountPatterns;
    QStringList m_codePatterns;
    QStringList m_labelPatterns;
    QStringList m_sumForcingPatterns;
    QStringList m_metricAnchorPatterns;
    QStringList m_codeAnchorPatterns;
    QStringList m_dashMarkers;
    bool m_loaded = false;
};

} // namespace xlsone
