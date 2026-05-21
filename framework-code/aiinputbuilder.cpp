// aiinputbuilder.cpp
#include "aiinputbuilder.h"
#include <QtSql/qsqlerror.h>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>
#include <QtSql/QSqlDatabase>
#include <QSqlQuery>


AIInputBuilder::AIInputBuilder(QObject *parent)
    : QObject(parent)
    , m_connectionName("qt_sql_default_connection")
{
}

AIInputBuilder::~AIInputBuilder() = default;

void AIInputBuilder::setConnectionName(const QString &name)
{
    m_connectionName = name;
}

QString AIInputBuilder::buildInputJson() const
{
    // 如果开启了测试模式，先执行测试数据注入
    if (m_testCaseId > 0) {
        qDebug()<<"已经由 enableTestMode 完成了";
    }
    QJsonObject root;

    // 1. 当前时间
    root["current_time"] = QDateTime::currentDateTime()
                               .toString("yyyy-MM-dd hh:mm:ss");

    // 2. 最近日志（recent_logs）
    QString logsSql = R"(
        SELECT id, tag, timestamp, type, content,priority
        FROM recent_logs
        ORDER BY timestamp DESC
        LIMIT 16
    )";

    auto logMapper = [](const QSqlQuery &q) -> QJsonObject {
        QJsonObject obj;
        obj["pk_id"] = q.value("id").toInt();
        obj["tag"] = q.value("tag").toString();
        obj["timestamp"] = q.value("timestamp").toString();  //timestamp
        obj["type"] = q.value("type").toString();
        obj["content"] = q.value("content").toString();  //content
        obj["priority"] = q.value("priority").toString();
        obj["gcn_alert"] = q.value("gcn_alert").toInt();
        // 添加元数据
        QJsonObject meta;
        meta["table"] = "recent_logs";
        meta["fields"] = QJsonArray::fromStringList({"id", "tag", "timestamp", "type", "content","priority","gcn_alert"});
        obj["_meta"] = meta;

        return obj;
    };

    QJsonDocument logsDoc = queryToJson(logsSql, logMapper);
    root["recent_logs"] = logsDoc.array();

    // 3. 望远镜状态（telescope_status）
    QString statusSql = R"(
    SELECT id, pointing_ra, pointing_dec, current_filter, update_time
    FROM telescope_status
    ORDER BY update_time DESC
    LIMIT 1
    )";

    auto statusMapper = [](const QSqlQuery &q) -> QJsonObject {
        QJsonObject obj;
        obj["pk_id"] = q.value("id").toInt();
        QString ra = q.value("pointing_ra").toString();
        QString dec = q.value("pointing_dec").toString();
        obj["pointing"] = QString("RA:%1 DEC:%2").arg(ra, dec);  // 匹配坐标格式
        obj["current_filter"] = q.value("current_filter").toString();
        obj["update_time"] = q.value("update_time").toString();  // 添加

        // 添加元数据
        QJsonObject meta;
        meta["table"] = "telescope_status";
        meta["fields"] = QJsonArray::fromStringList({"id", "pointing_ra", "pointing_dec", "current_filter", "update_time"});
        obj["_meta"] = meta;


        return obj;
    };

    QJsonDocument statusDoc = queryToJson(statusSql, statusMapper);
    if (!statusDoc.isEmpty()) {
        root["telescope_status"] = statusDoc.array().isEmpty() ? QJsonObject() : statusDoc.array().first().toObject();
    }

    // 4. 计划目标（planned_targets）
    QString targetsSql = R"(
        SELECT id, name, ra, dec, priority, exptime, status, schedule_time
        FROM planned_targets
        WHERE status = 'active'
        ORDER BY priority DESC, schedule_time
    )";

    auto targetMapper = [](const QSqlQuery &q) -> QJsonObject {
        QJsonObject obj;
        obj["pk_id"] = q.value("id").toInt();
        obj["name"] = q.value("name").toString();
        obj["ra"] = q.value("ra").toString();
        obj["dec"] = q.value("dec").toString();
        obj["priority"] = q.value("priority").toInt();
        obj["exptime"] = q.value("exptime").toInt();
        obj["status"] = q.value("status").toString();  // 添加
        obj["schedule_time"] = q.value("schedule_time").toString();  // 添加


        // 添加元数据
        QJsonObject meta;
        meta["table"] = "planned_targets";
        meta["fields"] = QJsonArray::fromStringList({"id", "name", "ra", "dec", "priority", "exptime", "status", "schedule_time"});
        obj["_meta"] = meta;


        return obj;
    };

    QJsonDocument targetsDoc = queryToJson(targetsSql, targetMapper);
    root["planned_targets"] = targetsDoc.array();

    // 转为紧凑 JSON 字符串
    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Compact);
}

// ====================== 内部工具函数 ======================
QJsonDocument AIInputBuilder::queryToJson(
    const QString &sql,
    const std::function<QJsonObject(const QSqlQuery&)> &mapper) const
{
    QJsonArray array;

    QSqlDatabase db = QSqlDatabase::database("qt_sql_default_connection");
    if (!db.isValid()) {
        qWarning() << "Database not valid:" << m_connectionName;
        return QJsonDocument();
    }

    QSqlQuery query(db);
    if (!query.exec(sql)) {
        qWarning() << "SQL Error:" << query.lastError().text()
        << "\nSQL:" << sql;
        return QJsonDocument();
    }

    while (query.next()) {
        array.append(mapper(query));
    }

    return QJsonDocument(array);
}



// aiinputbuilder.cpp - 完整函数
void AIInputBuilder::enableTestMode(int testCaseId)
{
    emit requestRunTestCase(testCaseId);
}
