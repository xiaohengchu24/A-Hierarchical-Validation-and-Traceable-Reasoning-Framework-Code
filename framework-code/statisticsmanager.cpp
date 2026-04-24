#include "statisticsmanager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
StatisticsManager::StatisticsManager(QObject *parent) : QObject(parent) {}

void StatisticsManager::recordSuccess(const QJsonObject &json) {
    m_totalTests++;
    m_successes++;
    QJsonObject logicObj = json.value("reasoning_logic").toObject();
    double conf = logicObj.value("confidence").toDouble(0.0);  // 默认 0.0

    m_confidences << qMakePair(QDateTime::currentDateTime(), conf);
    // ... 其他记录
}

void StatisticsManager::recordFailure(const QJsonObject &json, const QStringList &errors) {
    m_totalTests++;
    m_failures++;

    QJsonObject logicObj = json["reasoning_logic"].toObject();
    double conf = logicObj["confidence"].toDouble(0.0);

    m_confidences << qMakePair(QDateTime::currentDateTime(), conf);

    // 从 errors 中提取规则 ID（如 [moon_interference]）
    QRegularExpression reRule(R"(\[([^\]]+)\])");  // 捕获方括号内的内容
    for (const QString &err : errors) {
        QRegularExpressionMatch match = reRule.match(err);
        if (match.hasMatch()) {
            QString ruleId = match.captured(1);
            m_ruleFailureCounts[ruleId]++;
        }
    }
}

void StatisticsManager::exportToCSV(const QString &filename) {
    QString fileName = filename;
    if (fileName.isEmpty()) {
        fileName = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_stats_results.csv";
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "无法打开统计文件进行写入:" << fileName;
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8); // 确保中文不乱码

    // CSV 表头
    out << "Timestamp,TotalTests,Successes,Failures,SuccessRate(%),InterceptRate(%),Confidence_Avg\n";

    // 汇总行
    double successRate = m_totalTests > 0 ? (double)m_successes * 100.0 / m_totalTests : 0.0;
    double interceptRate = 100.0 - successRate;

    double avgConf = 0.0;
    if (!m_confidences.isEmpty()) {
        for (const auto &p : m_confidences) {
            avgConf += p.second;
        }
        avgConf /= m_confidences.size();
    }

    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
        << "," << m_totalTests
        << "," << m_successes
        << "," << m_failures
        << "," << QString::number(successRate, 'f', 2)
        << "," << QString::number(interceptRate, 'f', 2)
        << "," << QString::number(avgConf, 'f', 3)
        << "\n\n";

    // 各规则失败次数统计
    out << "RuleID,FailureCount\n";
    // 按失败次数降序排序（可选，美观）
    QList<QString> keys = m_ruleFailureCounts.keys();
    std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b) {
        return m_ruleFailureCounts[a] > m_ruleFailureCounts[b];
    });

    for (const QString &key : keys) {
        out << key << "," << m_ruleFailureCounts[key] << "\n";
    }

    file.close();
    qDebug() << "统计数据已导出到:" << fileName;
}

double StatisticsManager::successRate() const {
    return m_totalTests > 0 ? (double)m_successes / m_totalTests * 100.0 : 0.0;
}

double StatisticsManager::interceptRate() const {
    return 100.0 - successRate();
}

QMap<QString, int> StatisticsManager::ruleFailures() const {
    return m_ruleFailureCounts;
}
