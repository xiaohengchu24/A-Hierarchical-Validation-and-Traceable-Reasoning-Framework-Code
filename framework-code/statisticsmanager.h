#ifndef STATISTICSMANAGER_H
#define STATISTICSMANAGER_H

#include <QObject>
#include <QMap>
#include <QDateTime>

class StatisticsManager : public QObject {
    Q_OBJECT
public:
    explicit StatisticsManager(QObject *parent = nullptr);
    void recordSuccess(const QJsonObject &json);
    void recordFailure(const QJsonObject &json, const QStringList &errors);
    void exportToCSV(const QString &filename = "stats_results.csv");
    // 指标获取（用于论文）
    double successRate() const;  // 通过率
    double interceptRate() const;  // 拦截错误率（=1 - 通过率）
    QMap<QString, int> ruleFailures() const;  // 每个规则失败次数

private:
    int m_totalTests = 0;
    int m_successes = 0;
    int m_failures = 0;
    QMap<QString, int> m_ruleFailureCounts;  // 规则ID -> 失败次数
    QList<QPair<QDateTime, double>> m_confidences;  // 时间戳 + confidence（用于趋势图）
    //扩展其他统计
};

#endif
