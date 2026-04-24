#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QDebug>
#include "ReasoningRule.h"
class Validator : public QObject {
    Q_OBJECT
public:
    explicit Validator(QSqlDatabase db, QObject *parent = nullptr);

    // 数据来源真实性验证：检查raw_data_sources是否100%匹配数据库
    bool validateDataSources(const QJsonArray &sources, QStringList &errors);

    // 推理逻辑校验：独立重新计算逻辑一致性（不依赖AI hints）
    bool validateReasoningLogic(const QJsonObject &outputJson,const QJsonObject &logic, const QJsonArray &sources, QStringList &errors);

    // 执行verification_hints中的SQL查询
    QStringList executeVerificationHints(const QJsonArray &queries);

    QStringList lastErrors() const;// 存储最近错误

signals:
    void validationFailed(const QString &errorSummary);

private:
    QSqlDatabase m_db;

    // 辅助函数：计算天球角距（独立实现）
    double calculateAngularDistance(const QString &ra1, const QString &dec1, const QString &ra2, const QString &dec2);

    // 解析RA/DEC字符串到度（处理h m s和° ′ ″格式）
    double parseRaToDegrees(const QString &ra);
    double parseDecToDegrees(const QString &dec);

    QString normalizeContent(const QString &s);

    bool validateRawData(const QJsonArray &sources, QStringList &errors);


    // ===== 新增：关联推理规则系统 =====
    QList<ReasoningRule> m_reasoningRules;
    void initReasoningRules();

    // 提取月相和目标坐标的通用工具（供规则调用）
    bool extractMoonAndTarget(const QJsonArray &sources,
                              QString &moonRa, QString &moonDec,
                              QString &targetRa, QString &targetDec);
    // =================================

    bool m_rulesInited = false;

    // 辅助提取函数
    QString extractRa(const QString &content) const;
    QString extractDec(const QString &content) const;
    double extractAltitudeFromText(const QJsonArray &sources) const;
    double extractDistanceFromText(const QJsonArray &sources) const;

    double getCurrentLST() const;  //预留 获取LST


    QStringList m_lastErrors;



    // 各种干扰一致性信号校验专用函数
    bool checkInterferenceConsistency(const QJsonArray &sources,
                                      QStringList &errors) const;
    QString m_ruleTriggerText;


    int extractPriority(const QString &content) const;
    double extractWindow(const QString &content) const;
    QString extractCurrentRa(const QJsonArray &sources) const;
    QString extractCurrentDec(const QJsonArray &sources) const;

    QList<QPair<QString, QString>> extractAllOtherRaDecPairs(const QJsonArray &sources) const;
};

#endif // VALIDATOR_H
