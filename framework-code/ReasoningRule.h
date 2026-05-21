// ReasoningRule.h
#pragma once

#include <QString>
#include <QList>
#include <QJsonArray>
#include <functional>

// 一个轻量级的“规则”结构体，全定义在头文件里，不需要 .cpp
struct ReasoningRule
{
    QString ruleId;                    // 规则唯一ID，用于日志
    QString description;               // 人类可读的描述
    QString triggerKeywords;           // 多个关键词用 | 分隔，AI text 包含任意一个就触发
    std::function<bool(const QJsonArray &sources, QStringList &errors)> checker;

    // 构造函数（让你写 lambda 的时候很舒服）
    ReasoningRule(const QString &id,
                  const QString &desc,
                  const QString &keywords,
                  std::function<bool(const QJsonArray&, QStringList&)> func)
        : ruleId(id), description(desc), triggerKeywords(keywords), checker(func)
    {}
};

// 为了让 lambda 里能直接调用你已有的私有函数，
// 再顺手把提取月相/目标的函数也写成私有工具（直接放 Validator 里）
class Validator;  // 前向声明

// 把提取逻辑抽出来，方便规则复用（写在 Validator 的 private 里就行）
bool extractMoonAndTarget(const QJsonArray &sources,
                          QString &moonRa, QString &moonDec,
                          QString &targetRa, QString &targetDec,
                          Validator *validator);   // 需要传 this 才能调用 qDebug/正则
