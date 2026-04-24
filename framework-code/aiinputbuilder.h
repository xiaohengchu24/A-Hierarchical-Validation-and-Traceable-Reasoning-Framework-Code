// aiinputbuilder.h
#ifndef AIINPUTBUILDER_H
#define AIINPUTBUILDER_H

#include <QObject>
#include <QJsonDocument>
#include <QtSql/QSqlQuery>

#include <QJsonObject>      // 新增：修复 incomplete QJsonObject
#include <QJsonArray>       // 新增：如果用到数组（如 queries）
#include <QJsonParseError>

class QSqlDatabase;

class AIInputBuilder : public QObject
{
    Q_OBJECT

public:
    explicit AIInputBuilder(QObject *parent = nullptr);
    ~AIInputBuilder();

    /**
     * @brief 从 MySQL 构建 AI 输入 JSON
     * @return 紧凑格式的 JSON 字符串
     */
    QString buildInputJson() const;

    /**
     * @brief 设置数据库连接名（默认 "qt_sql_default_connection"）
     */
    void setConnectionName(const QString &name);

    void enableTestMode(int testCaseId = -1);   // -1=关闭测试模式
signals:
    void requestRunTestCase(int caseId);   // 只负责“发请求”
private:
    QString m_connectionName;

    // 内部查询函数
    QJsonDocument queryToJson(const QString &sql,
                              const std::function<QJsonObject(const QSqlQuery&)> &mapper) const;

    int m_testCaseId = -1;// -1=关闭测试模式


};

// 在类内部添加一个辅助结构体来管理表字段信息
struct TableInfo {
    QString tableName;
    QStringList fields;
};

#endif // AIINPUTBUILDER_H
