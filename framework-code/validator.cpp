#include "validator.h"
#include <QRegularExpression>
#include <cmath>  // 用于角距计算
#include <QSqlError>
#include <QSqlRecord>
Validator::Validator(QSqlDatabase db, QObject *parent)
    : QObject(parent), m_db(db)
    {
        qDebug() << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!";
        qDebug() << "Validator 构造函数被调用！";
        qDebug() << "数据库连接名:" << m_db.connectionName();
        qDebug() << "数据库是否有效:" << m_db.isValid();
        qDebug() << "数据库是否打开:" << m_db.isOpen();
        qDebug() << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!";

        qDebug()<<"-------关联推理开始------";
        initReasoningRules();
        qDebug()<<"-------关联推理结束------";

    }

bool Validator::validateDataSources(const QJsonArray &sources, QStringList &errors)
{
    errors.clear();
    for (const auto &srcVal : sources) {
        QJsonObject src = srcVal.toObject();
        QString table = src["table"].toString();
        int pkId = src["pk_id"].toInt();
        QString aiContent = src["content"].toString();

        qDebug()<<"cccSources:table---->"<<table<<"pk_id---->"<<pkId<<"content---->"<<aiContent;

        // 防护：无效来源直接报错
        if (table.isEmpty() || pkId <= 0) {
            errors << QString("无效来源: table='%1' pk_id=%2（JSON提取失败或模型幻觉）").arg(table).arg(pkId);
            continue;
        }

        QSqlQuery query(QSqlDatabase::database(m_db.connectionName()));

        if (table == "planned_targets") {
            // 对于 planned_targets 表，需要组合多个字段来验证
            QString sql = "SELECT name, ra, dec, priority, exptime FROM planned_targets WHERE id = ?";
            if (!query.prepare(sql)) {
                errors << QString("准备查询失败[%1:%2]: %3").arg(table).arg(pkId).arg(query.lastError().text());
                continue;
            }
            query.addBindValue(pkId);

            if (!query.exec()) {
                errors << QString("查询失败[%1:%2]: %3").arg(table).arg(pkId).arg(query.lastError().text());
                continue;
            }
            if (!query.next()) {
                errors << QString("来源不存在[%1:%2]").arg(table).arg(pkId);
                continue;
            }

            // 从数据库组合内容
            QString name = query.value(0).toString();
            QString ra = query.value(1).toString();
            QString dec = query.value(2).toString();
            int priority = query.value(3).toInt();
            int exptime = query.value(4).toInt();

            QString dbContent = QString("%1 RA:%2 DEC:%3 优先级:%4 曝光:%5s")
                                    .arg(name).arg(ra).arg(dec).arg(priority).arg(exptime);
            QString dbContent2 = QString("%1 RA:%2 DEC:%3 优先级:%4 曝光:%5")
                                    .arg(name).arg(ra).arg(dec).arg(priority).arg(exptime);

            // 语义相等校验
            if (normalizeContent(aiContent) == normalizeContent(dbContent) || normalizeContent(aiContent) == normalizeContent(dbContent2)
                    || (((aiContent.trimmed().contains(dec.trimmed()) && aiContent.trimmed().contains(ra.trimmed()))
                    || (aiContent.trimmed().contains(dec.replace(QRegularExpression("00"), "0")) &&
                    aiContent.trimmed().contains(ra.replace(QRegularExpression("00"), "0")) )
                     )
                    &&
                    aiContent.trimmed().contains(name) &&
                    aiContent.trimmed().contains(QString::number(priority)) &&
                    aiContent.trimmed().contains(QString::number(exptime))))
            {
                qDebug()<<"RA DEC 优先级 曝光 内容一致";
            }else if((dbContent.contains("GCN") ==false || dbContent.contains("GRB")==false )&& (aiContent.contains("Survey") || aiContent.contains("planned") ))
            {


                errors << QString("内容不一致[%1:%2]\nAI: %3\nDB: %4 debug:%5::%6::%7::%8::%9")
                              .arg(table).arg(pkId)
                              .arg(aiContent.simplified())
                              .arg(dbContent.simplified())
                              .arg(name).arg(ra).arg(dec).arg(priority).arg(exptime);
            }

        } else if (table == "recent_logs") {
            // recent_logs 表有 content 列
            QString sql = "SELECT content FROM recent_logs WHERE id = ?";
            if (!query.prepare(sql)) {
                errors << QString("准备查询失败[%1:%2]: %3").arg(table).arg(pkId).arg(query.lastError().text());
                continue;
            }
            query.addBindValue(pkId);

            if (!query.exec()) {
                errors << QString("查询失败[%1:%2]: %3").arg(table).arg(pkId).arg(query.lastError().text());
                continue;
            }
            if (!query.next()) {
                errors << QString("来源不存在[%1:%2]").arg(table).arg(pkId);
                continue;
            }

            QString dbContent = query.value(0).toString();

            // 语义相等校验
            if (normalizeContent(aiContent) != normalizeContent(dbContent)) {
                errors << QString("内容不一致[%1:%2]\nAI: %3\nDB: %4")
                              .arg(table).arg(pkId)
                              .arg(aiContent.simplified())
                              .arg(dbContent.simplified());
            }

        } else if (table == "telescope_status") {
            // 使用直接执行的方式避免参数绑定问题
            QString sql = QString("SELECT pointing_ra, pointing_dec, current_filter FROM telescope_status WHERE id = %1").arg(pkId);

            if (!query.exec(sql)) {
                errors << QString("查询失败[%1:%2]: %3").arg(table).arg(pkId).arg(query.lastError().text());
                continue;
            }
            if (!query.next()) {
                errors << QString("来源不存在[%1:%2]").arg(table).arg(pkId);
                continue;
            }

            // 从数据库组合内容
            QString pointing_ra = query.value(0).toString();
            QString pointing_dec = query.value(1).toString();
            QString current_filter = query.value(2).toString();

            QString dbContent = QString("pointing_ra: RA:%1 pointing_dec: DEC:%2 current_filter: %3")
                                    .arg(pointing_ra).arg(pointing_dec).arg(current_filter);

            qDebug() << "telescope_status 验证:";
            qDebug() << "AI内容:" << aiContent;
            qDebug() << "DB内容:" << dbContent;
            aiContent.remove(QRegularExpression("当前指向 "));
            aiContent.remove(QRegularExpression("当前滤镜 "));
            aiContent.remove(QRegularExpression("滤镜 "));
            QString dbContent2=QString("RA:%1 DEC:%2 %3")
                                     .arg(pointing_ra).arg(pointing_dec).arg(current_filter);
            // 语义相等校验
            if ((normalizeContent(aiContent.trimmed()) == normalizeContent(dbContent.trimmed()) ) || (normalizeContent(dbContent).contains(normalizeContent(aiContent.trimmed())))
                || (normalizeContent(aiContent.trimmed()) == normalizeContent(dbContent2.trimmed()) ) || (normalizeContent(dbContent2).contains(normalizeContent(aiContent.trimmed()))))
            {
                qDebug()<<"AI与SQL对比相等";
            }else if(aiContent.trimmed().contains(pointing_ra) &&
                       aiContent.trimmed().contains(pointing_dec) &&
                       aiContent.trimmed().contains(current_filter))
            {
                qDebug()<<"AI与SQL对比相等-.-";
            }
            else
            {
                errors << QString("cc内容不一致[%1:%2]\nAI: %3\nDB: %4")
                              .arg(table).arg(pkId)
                              .arg(aiContent)
                              .arg(dbContent);
            }
        } else {
            errors << QString("未知表类型: %1").arg(table);
        }
    }

    m_lastErrors = errors;  // 存储最后错误
    return errors.isEmpty();
}
bool Validator::validateReasoningLogic(const QJsonObject &outputJson,const QJsonObject &logic, const QJsonArray &sources, QStringList &errors) {
    //initReasoningRules();
    qDebug()<<"inininini";
    errors.clear();

    QString ruleTriggerText;


    QString graph = logic["graph"].toString();
    QString text = logic["text"].toString();
    double confidence = logic["confidence"].toDouble();
    // 示例：独立验证月相干扰（提取来源，重新计算角距）
    QString moonRa, moonDec, targetRa, targetDec;
    bool hasMoonPhase = false;
    bool hasTargetPointing = false;
    for (const auto &srcVal : sources) {
        QJsonObject src = srcVal.toObject();
        QString content = src["content"].toString();
        QString type = src["type"].toString();
        if (type == "moon_phase" && content.contains("RA:")) {
            hasMoonPhase = true;
            QRegularExpression reRa(R"(RA:\s*([0-9hms :]+))");  // 修复：字面0-9 h m s : 空格
            QRegularExpression reDec(R"(DEC:\s*([+-]?[0-9°′″ :]+))");
            QRegularExpressionMatch mRa = reRa.match(content);
            QRegularExpressionMatch mDec = reDec.match(content);
            if (mRa.hasMatch() && mDec.hasMatch()) {
                moonRa = mRa.captured(1).trimmed();
                moonDec = mDec.captured(1).trimmed();
                qDebug() << "Extracted moon: RA=" << moonRa << " DEC=" << moonDec;  // 新增调试
            } else {
                qDebug() << "Moon extract failed for content:" << content;
            }
        } else if (type == "target_pointing" && content.contains("RA:")) {
            hasTargetPointing = true;
            QRegularExpression reRa(R"(RA:\s*([0-9hms :]+))");          //
            QRegularExpression reDec(R"(DEC:\s*([+-]?[0-9°′″ :]+))");
            QRegularExpressionMatch mRa = reRa.match(content);
            QRegularExpressionMatch mDec = reDec.match(content);
            if (mRa.hasMatch() && mDec.hasMatch()) {
                targetRa = mRa.captured(1).trimmed();
                targetDec = mDec.captured(1).trimmed();
                qDebug() << "Extracted target: RA=" << targetRa << " DEC=" << targetDec;
            } else {
                qDebug() << "Target extract failed for content:" << content;
            }
        }
    }

    // 简单单一逻辑校验
    if (!validateRawData(sources, errors)) {
        return false;
    }


    if (hasMoonPhase && hasTargetPointing) {

    if (moonRa.isEmpty() || targetRa.isEmpty()) {
        errors << "缺少月相或目标坐标，无法独立验证";
        return false;
    }
    double dist = calculateAngularDistance(moonRa, moonDec, targetRa, targetDec);
    if (dist < 0) {
        errors << "坐标解析失败，无法计算角距";
        return false;
    }
    const double threshold = 5.0;
    bool hasInterference = dist < threshold;
    // 检查text/graph一致性
    bool textHasInterference = text.contains("月相距离相近")|| text.contains("月相距离小于5度") || text.contains("月相距离小于5°")
                               || text.contains("存在月相干扰")
                               || text.contains("月相干扰较大") || text.contains("月相干扰频繁")
                               || text.contains("月相干扰高")
                               || text.contains("与月相位置接近") || text.contains("与月相角距小")
                               || text.contains("与月相距离小") ;

    bool textHasInterference2 = text.contains("干扰较低") || text.contains("月相照度低") || text.contains("与月相距离较远") || text.contains("与月相角距较大") || text.contains("与月相角距大") || text.contains("与月相距离大");
    if (textHasInterference != hasInterference && textHasInterference2 == false) {
        errors << QString("推理不一致x: 计算角距%1度（干扰%2），但text描述干扰%3")
                      .arg(dist).arg(hasInterference ? "存在" : "不存在")
                      .arg(textHasInterference ? "存在" : "不存在");
        qDebug()<<"textHasInterference2====="<<textHasInterference2<<"textHasInterference===="<<textHasInterference;
    }
    }else {
        qDebug() << "无月相/指向来源，跳过角距验证";
    }
    // 置信度阈值检查
    if (confidence < 0.5 ) {
        errors << "置信度过低(<0.8)但存在干扰，建议重试";
    }

    // 【新增：关联推理规则统一校验】
    QString reasoningText = logic["text"].toString();

    // ============ 新增：构建只用于规则触发的纯结论文本 ============


    // 1. 优先使用 suggestion.text（最明确的用户可见建议）
    QJsonObject suggestionObj = outputJson.value("suggestion").toObject();
    if (suggestionObj.contains("text")) {
        ruleTriggerText += suggestionObj["text"].toString() + " ";
    }

    // 2. 再加上 reasoning_logic.text（核心推理描述）
    ruleTriggerText += logic["text"].toString() + " ";

    // 3. 可选：加上 verification_hints 的 purpose（如果需要更严格）
    QJsonArray queries = outputJson.value("verification_hints").toObject()
                             .value("queries").toArray();
    for (const auto &qVal : queries) {
        QJsonObject q = qVal.toObject();
        ruleTriggerText += q.value("purpose").toString() + " ";
    }

    // 去除多余空格，得到纯结论文本
    ruleTriggerText = ruleTriggerText.simplified();

    qDebug() << "【规则触发文本】:" << ruleTriggerText;
    m_ruleTriggerText = ruleTriggerText;



    // ============ 新增的调试信息 ============

    qDebug() << "==================================================================";
    qDebug() << "【关联推理校验开始】规则总数:" << m_reasoningRules.size();
    qDebug() << "reasoning_text:" << reasoningText;
    qDebug() << "sources 数量:" << sources.size();
    qDebug() << "==================================================================";
    // =====================================================




    for (const auto &rule : qAsConst(m_reasoningRules)) {
        qDebug() << "【规则系统】正在检查规则:" << rule.ruleId << "关键词模式:" << rule.triggerKeywords;

        QString excludePattern;
        QString triggerPattern = rule.triggerKeywords;

        // === 新增：解析是否启用了排除模式（以 EXCLUDE: 开头）===
        if (triggerPattern.startsWith("EXCLUDE:")) {
            int separatorPos = triggerPattern.indexOf("||");
            if (separatorPos != -1) {
                excludePattern = triggerPattern.mid(8, separatorPos - 8);  // EXCLUDE: 之后到 | 之前
                triggerPattern = triggerPattern.mid(separatorPos + 2);    // | 之后是真实触发正则
                qDebug() << "【规则配置】" << rule.ruleId << " 启用排除模式，排除正则:" << excludePattern;
            } else {
                qDebug() << "【警告】" << rule.ruleId << " 的 EXCLUDE: 格式错误，缺少 || 分隔符，降级为普通触发";
            }
        }

        // === 第一步：检查触发关键词是否匹配 ===
        QRegularExpression triggerRe(triggerPattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch triggerMatch = triggerRe.match(m_ruleTriggerText);
        if (!triggerMatch.hasMatch() || triggerMatch.captured(0).trimmed().isEmpty()) { // 新增：防空匹配
            qDebug() << "【规则系统】规则未触发或空匹配:" << rule.ruleId;
            continue;
        }

        // 新增：打印匹配关键字
        if (triggerMatch.hasMatch()) {
            qDebug() << "【匹配关键字】规则:" << rule.ruleId << " 匹配到:" << triggerMatch.captured(0);
        }

        // === 第二步：如果配置了排除关键词，且文本命中排除，则豁免规则 ===
        if (!excludePattern.isEmpty()) {
            QRegularExpression excludeRe(excludePattern, QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch excludeMatch = excludeRe.match(m_ruleTriggerText);
            if (excludeMatch.hasMatch()) {
                qDebug() << "【规则豁免】" << rule.ruleId << " 被排除关键词触发，跳过校验（匹配：" << excludeMatch.captured(0) << "）";
                continue;  // 直接跳过，不执行 checker
            }
        }

        // === 第三步：正式执行规则校验 ===
        qDebug() << "【关联推理】触发规则:" << rule.ruleId << "（匹配文本：" << m_ruleTriggerText << "）";
        if (!rule.checker(sources, errors)) {
            qDebug() << "【关联推理】规则校验失败:" << rule.ruleId;
            errors.prepend(QString("关联推理失败[%1]: %2").arg(rule.ruleId, rule.description));
            return false;
        }
        qDebug() << "【关联推理】规则校验通过:" << rule.ruleId;
    }
    m_lastErrors = errors;  // 存储最后错误
    return errors.isEmpty();
}

QStringList Validator::executeVerificationHints(const QJsonArray &queries) {
    QStringList results;
    for (const auto &qVal : queries) {
        QJsonObject qObj = qVal.toObject();
        QString table = qObj["table"].toString();
        QString sql = qObj["sql"].toString();
        QString purpose = qObj["purpose"].toString();

        qDebug() << "validateDataSources:" << "sql" << sql << "purpose-->" << purpose << "table-->" << table;

        // 安全检查：只允许SELECT，防注入
        if (!sql.trimmed().startsWith("SELECT", Qt::CaseInsensitive) ||
            sql.contains("DROP", Qt::CaseInsensitive) ||
            sql.contains("UPDATE", Qt::CaseInsensitive) ||
            sql.contains("DELETE", Qt::CaseInsensitive) ||
            sql.contains("INSERT", Qt::CaseInsensitive)) {
            results << QString("跳过不安全SQL [%1]: %2").arg(purpose, sql);
            continue;
        }

        // 修复 SQL 中的列名错误
        if (sql.contains("pk_id") && (table == "recent_logs" || table == "planned_targets" || table == "telescope_status")) {
            sql = sql.replace("pk_id", "id");
            qDebug() << "修复SQL列名:" << sql;
        }

        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            results << QString("验证失败hi [%1]: %2\nSQL: %3").arg(purpose, query.lastError().text(), sql);

        } else {
            int count = 0;
            while (query.next()) {
                count++;
                // 对于有返回值的查询，可以记录具体内容
                if (query.record().count() > 0) {
                    QStringList rowValues;
                    for (int i = 0; i < query.record().count(); ++i) {
                        rowValues << query.value(i).toString();
                    }
                    qDebug() << "查询结果行" << count << ":" << rowValues;
                }
            }
            results << QString("验证通过hi [%1] → %2 条记录\nSQL: %3").arg(purpose).arg(count).arg(sql);
        }
    }
    return results;
}

double Validator::calculateAngularDistance(const QString &ra1, const QString &dec1, const QString &ra2, const QString &dec2) {

    // 参数检查
    if (ra1.isEmpty() || dec1.isEmpty() || ra2.isEmpty() || dec2.isEmpty()) {
        qDebug() << "【calculateAngularDistance】空参数，跳过计算";
        return -1.0;
    }

    double ra1Deg = parseRaToDegrees(ra1);
    double dec1Deg = parseDecToDegrees(dec1);
    double ra2Deg = parseRaToDegrees(ra2);
    double dec2Deg = parseDecToDegrees(dec2);

    double ra1Rad = qDegreesToRadians(ra1Deg);
    double dec1Rad = qDegreesToRadians(dec1Deg);
    double ra2Rad = qDegreesToRadians(ra2Deg);
    double dec2Rad = qDegreesToRadians(dec2Deg);

    return qRadiansToDegrees(std::acos(std::sin(dec1Rad) * std::sin(dec2Rad) +
                                       std::cos(dec1Rad) * std::cos(dec2Rad) * std::cos(ra1Rad - ra2Rad)));
}

double Validator::parseRaToDegrees(const QString &ra) {
    QString cleaned = ra.trimmed().remove(' ');  // 先移除空格统一处理
    // 支持冒号格式 (e.g., "18:31:20")
    if (cleaned.contains(':')) {
        QStringList parts = cleaned.split(':');
        if (parts.size() >= 2 && parts.size() <= 3) {
            double h = parts[0].toDouble();
            double m = parts[1].toDouble();
            double s = (parts.size() == 3) ? parts[2].toDouble() : 0.0;
            return (h + m/60.0 + s/3600.0) * 15.0;
        }
    }
    // HMS格式，支持可选s (e.g., "18h32m" or "18h31m20s")
    QRegularExpression re(R"((\d+)h(\d+)m(\d*)s?)");
    QRegularExpressionMatch match = re.match(cleaned);
    if (match.hasMatch()) {
        int h = match.captured(1).toInt();
        int m = match.captured(2).toInt();
        int s = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
        return (h + m/60.0 + s/3600.0) * 15.0;
    }
    qDebug() << "RA解析失败:" << ra;
    return -1.0;  // 失败返回-1
}

double Validator::parseDecToDegrees(const QString &dec) {
    QString cleaned = dec.trimmed().remove(' ');  // 移除空格
    // 支持冒号格式 (e.g., "+22:10:15")
    if (cleaned.contains(':')) {
        int sign = cleaned.startsWith('-') ? -1 : 1;
        cleaned.remove('-').remove('+');
        QStringList parts = cleaned.split(':');
        if (parts.size() >= 2 && parts.size() <= 3) {
            double d = parts[0].toDouble();
            double m = parts[1].toDouble();
            double s = (parts.size() == 3) ? parts[2].toDouble() : 0.0;
            return sign * (d + m/60.0 + s/3600.0);
        }
    }
    // DMS格式，支持可选″ (e.g., "+22°14′" or "+22°10′15″")
    QRegularExpression re(R"(([+-]?)(\d+)°(\d+)′(\d*)″?)");
    QRegularExpressionMatch match = re.match(cleaned);
    if (match.hasMatch()) {
        int sign = match.captured(1) == "-" ? -1 : 1;
        int d = match.captured(2).toInt();
        int m = match.captured(3).toInt();
        int s = match.captured(4).isEmpty() ? 0 : match.captured(4).toInt();
        return sign * (d + m/60.0 + s/3600.0);
    }
    qDebug() << "DEC解析失败:" << dec;
    return -1.0;
}

// 新增辅助函数：内容归一化（可根据你的数据特点继续加规则）
QString Validator::normalizeContent(const QString &s) {
    QString t = s;
    // 1. 统一空格
    t = t.simplified(); // 多空格 → 单空格
    t.replace(" ", " "); // 剩下的双空格也干掉
    // 2. 统一常见符号（全角转半角、度符号统一）
    t.replace("：", ":");
    t.replace("；", ";");
    t.replace("，", ",");
    t.replace("′", "'"); // 弧分符号
    t.replace("″", "\""); // 弧秒符号
    t.replace("°", "°"); // 度符号统一
    t.replace("照度:", "照度: "); // 常见冒号后加空格
    t.replace("优先级:", "优先级: ");
    t.replace("曝光:", "曝光: ");
    // 3. 去掉开头可能的“月相位置 ”“目标指向 ”等前缀（可选，更宽容）
    t.remove(QRegularExpression("^月相位置\\s*"));
    t.remove(QRegularExpression("^目标指向\\s*"));
    // 新增：telescope_status统一（忽略前缀，合并filter）
    t.replace(QRegularExpression("pointing_ra:\\s*RA:"), "RA:");
    t.replace(QRegularExpression("pointing_dec:\\s*DEC:"), "DEC:");
    t.replace(QRegularExpression("current_filter:\\s*"), "");  // 移除filter前缀
    t.replace(QRegularExpression("当前滤镜 "), "");  // 移除filter前缀
    t.replace(QRegularExpression("当滤镜 "), "");
    return t.trimmed();
}

bool Validator::validateRawData(const QJsonArray &sources, QStringList &errors) {
    const double maxExp = 3600.0;  // 最大曝光时间（s），可配置
    const double maxHumidity = 80.0;  // 湿度阈值（%）
    const double maxWind = 10.0;  // 风速阈值（m/s）
    const double maxMag = 30.0;  // 星等阈值

    for (const auto &srcVal : sources) {
        QJsonObject src = srcVal.toObject();
        QString type = src["type"].toString();
        QString content = src["content"].toString();

        if (type == "target_pointing") {
            // 提取RA/Dec
            QRegularExpression reRa(R"(RA:\s*([\d\h\m\s\:]+))");
            QRegularExpression reDec(R"(DEC:\s*([\d°′″+-\s\:]+))");
            auto mRa = reRa.match(content);
            auto mDec = reDec.match(content);
            if (mRa.hasMatch() && mDec.hasMatch()) {
                double raHours = parseRaToDegrees(mRa.captured(1).trimmed()) / 15.0;  // 转小时
                if (raHours < 0 || raHours > 24) {
                    errors << QString("RA超出[0,24]h: %1").arg(raHours);
                    return false;
                }
                double dec = parseDecToDegrees(mDec.captured(1).trimmed());
                if (dec < -90 || dec > 90) {
                    errors << QString("DEC超出[-90,90]°: %1").arg(dec);
                    return false;
                }
            }
        } else if (type == "schedule") {
            // 曝光时间
            QRegularExpression reExp(R"(曝光:(\d+)s)");
            auto m = reExp.match(content);
            if (m.hasMatch()) {
                double exp = m.captured(1).toDouble();
                if (exp < 0 || exp > maxExp) {
                    errors << QString("曝光时间超出[0,%1]s: %2").arg(maxExp).arg(exp);
                    return false;
                }
            }
        } else if (type == "weather") {
            // 湿度
            QRegularExpression reHum(R"(湿度:(\d+)%?)");
            auto m = reHum.match(content);
            if (m.hasMatch() && m.captured(1).toDouble() > maxHumidity) {
                errors << QString("湿度过高(>%1%): %2").arg(maxHumidity).arg(m.captured(1));
                return false;
            }
            // 风速
            QRegularExpression reWind(R"(风速:(\d+) m/s)");
            m = reWind.match(content);
            if (m.hasMatch() && m.captured(1).toDouble() > maxWind) {
                errors << QString("风速过高(>%1 m/s): %2").arg(maxWind).arg(m.captured(1));
                return false;
            }
        } else if (type == "target_pointing" || type == "moon_phase") {  // 星等（如果有）
            QRegularExpression reMag(R"(V=([\d.]+) mag)");
            auto m = reMag.match(content);
            if (m.hasMatch() && m.captured(1).toDouble() > maxMag) {
                errors << QString("星等过高(>%1): %2").arg(maxMag).arg(m.captured(1));
                return false;
            }
        }
        // 添加其他单一校验（如温度、焦距等）类似以上模式
    }
    return errors.isEmpty();
}




bool Validator::extractMoonAndTarget(const QJsonArray &sources,
                                     QString &moonRa, QString &moonDec,
                                     QString &targetRa, QString &targetDec)
{
    moonRa.clear(); moonDec.clear(); targetRa.clear(); targetDec.clear();
    bool hasMoon = false, hasTarget = false;

    for (const auto &v : sources) {
        QJsonObject o = v.toObject();
        QString content = o["content"].toString();
        QString type    = o["type"].toString();

        if (type == "moon_phase" && content.contains("RA:")) {
            hasMoon = true;
            QRegularExpression reRa(R"(RA:\s*([0-9hms :]+))");
            QRegularExpression reDec(R"(DEC:\s*([+-]?[0-9°′″ :]+))");
            auto mRa = reRa.match(content);
            auto mDec = reDec.match(content);
            if (mRa.hasMatch() && mDec.hasMatch()) {
                moonRa = mRa.captured(1).trimmed();
                moonDec = mDec.captured(1).trimmed();
            }
        }
        else if (type == "target_pointing" && content.contains("RA:")) {
            hasTarget = true;
            QRegularExpression reRa(R"(RA:\s*([0-9hms :]+))");
            QRegularExpression reDec(R"(DEC:\s*([+-]?[0-9°′″ :]+))");
            auto mRa = reRa.match(content);
            auto mDec = reDec.match(content);
            if (mRa.hasMatch() && mDec.hasMatch()) {
                targetRa = mRa.captured(1).trimmed();
                targetDec = mDec.captured(1).trimmed();
            }
        }
    }
    return hasMoon && hasTarget && !moonRa.isEmpty() && !targetRa.isEmpty();
}

// 3. 规则初始化（核心）
void Validator::initReasoningRules()
{
    if (m_rulesInited) return;   // 防止重复初始化
    m_reasoningRules = {
        // 规则1：月相距离
        ReasoningRule(
            "moon_interference",
            "AI 提到月光/月相干扰时，实际角距必须 <5°",
            "EXCLUDE:与月相距离较大|与月相距离较远|按原计划执行|干扰较低|无显著干扰|月相干扰低|背景干扰较低|月相照度低|目标指向清晰|与月相角距较大|月相干扰中等|无明显干扰|中等干扰"
            "||"
            "月相.*(干扰强|靠近|相近|接近|较近|很近|明亮)|"
            "月球.*(干扰强|靠近|相近|接近|较近|很近|明亮)|"
            "月光.*(干扰强|强|明亮)|"
            "(月相干扰强|月相靠近|月相接近|月相较近|月相明亮|月光明亮)",
            //"(干扰|靠近|相近|接近|较近|很近|明亮).*(月相|月球|月光)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
               /* QString mRa, mDec, tRa, tDec;
                if (!extractMoonAndTarget(sources, mRa, mDec, tRa, tDec))
                    return true;  // 无坐标不检查

                double dist = calculateAngularDistance(mRa, mDec, tRa, tDec);
                if (dist < 0) return true;

                if (dist >= 5.0) {
                    errors << QString("【月相干扰规则失败】模型认为有月光干扰，但实际角距 %.2f° ≥ 5°").arg(dist);
                    return false;
                }
                qDebug()<<"关联推理:月相干扰规则校验通过！";
                return true;*/


    QString mRa, mDec, currentRa, currentDec;

        // 先提取月相坐标和当前指向（你的原函数）
        if (!extractMoonAndTarget(sources, mRa, mDec, currentRa, currentDec)) {
            qDebug() << "【moon_interference】无月相或当前指向数据，跳过规则";
            return true;  // 没有数据就不校验
        }

        // 提取所有“其他”潜在目标坐标（TOO、计划、多警报等）
        auto otherPairs = extractAllOtherRaDecPairs(sources);

        // 如果没有其他坐标，就 fallback 到当前指向
        if (otherPairs.isEmpty()) {
            qDebug() << "【moon_interference】无其他潜在目标，使用当前指向校验";
            double dist = calculateAngularDistance(mRa, mDec, currentRa, currentDec);
            if (dist < 0) return true;
            if (dist >= 5.0) {
                errors << QString("【月相干扰规则失败】AI声称存在月相干扰，但当前指向角距 %.2f° ≥ 5°（无其他目标坐标）").arg(dist);
                return false;
            }
            return true;
        }

        // 有其他坐标：检查是否至少有一个与月相距离 <5°
        bool hasCloseTarget = false;
        QStringList distList;

        for (const auto &pair : otherPairs) {
            double dist = calculateAngularDistance(mRa, mDec, pair.first, pair.second);
            if (dist < 0) continue;  // 解析失败跳过

            distList << QString::number(dist, 'f', 2) + "°";

            if (dist < 5.0) {
                hasCloseTarget = true;
                //提前退出，找到一个就够了
                 break;
            }
        }

        if (!hasCloseTarget) {
            errors << QString("【月相干扰规则失败】AI声称存在月相干扰，但所有候选目标角距（%1）均 ≥5°")
                          .arg(distList.join(", "));
            return false;
        }

        qDebug() << "【moon_interference】校验通过：至少有一个候选目标与月相距离 <5°";
        return true;
    }
           // }
            ),

        // 规则2：卫星干扰数量
        ReasoningRule(
            "satellite_many",
            "AI 明确称卫星干扰较多/频繁时，记录数量应 ≥2",
            "EXCLUDE:无卫星干扰|卫星干扰较低|无显著卫星干扰|卫星轨迹稀少"
            "||"
            "(卫星干扰较多|卫星干扰频繁|卫星干扰多次|大量卫星干扰|多条卫星干扰|高频卫星干扰|"
            "较多卫星干扰|频繁卫星干扰|多次卫星干扰|大量卫星干扰|"
            "卫星轨迹频繁|多次卫星轨迹|大量卫星轨迹|"
            "多条卫星穿越|多条卫星干扰)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                if (!m_ruleTriggerText.contains(QRegularExpression("卫星|starlink|轨迹穿越", QRegularExpression::CaseInsensitiveOption))) {
                    qDebug() << "【satellite_many】快速过滤：文本中无卫星相关词，跳过规则校验";
                    return true;
                }

                int cnt = 0;
                for (const auto &v : sources) {
                    if (v.toObject()["type"].toString().contains("satellite", Qt::CaseInsensitive)) {
                        cnt++;
                    }
                }
                if (cnt < 2) {
                    errors << QString("【卫星干扰规则失败】AI声称卫星干扰较多，但实际只有 %1 条记录").arg(cnt);
                    return false;
                }
                qDebug() << "卫星干扰较多规则校验通过，记录数:" << cnt;
                return true;
            }
            ),

        ReasoningRule(
            "plant_many",
            "AI 明确称飞机干扰较多/频繁时，记录数量应 ≥2",
            "(飞机|aircraft)(较多|频繁|多次|大量|多架|几架|高频)|"
            "(较多|频繁|多次|大量|多架|几架|高频)(飞机|aircraft)|"
            "飞机干扰(较多|频繁)|飞机轨迹(频繁|多次)",
            [](const QJsonArray &sources, QStringList &errors) -> bool {
                int cnt = 0;
                for (const auto &v : sources) {
                    if (v.toObject()["type"].toString().contains("aircraft", Qt::CaseInsensitive)) {
                        cnt++;
                    }
                }
                if (cnt < 2) {
                    errors << QString("【飞机干扰规则失败】AI声称飞机干扰较多，但实际只有 %1 条记录").arg(cnt);
                    return false;
                }
                qDebug() << "飞机干扰较多规则校验通过，记录数:" << cnt;
                return true;
            }
            ),

        ReasoningRule(
            "interference_consistency",
            "AI 提到存在某种干扰时，raw_data_sources 中必须至少有一条相关记录",
            "EXCLUDE:按原计划执行|干扰较低"
            "||"
            "(存在|出现|检测到|受到|影响|干扰|风险|问题).*(飞机|aircraft|卫星|星链|轨迹穿越|反射光|"
            "视宁度|seeing|抖动|大气条件|云覆盖|云量|云雾|能见度|风速|强风|大风)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                // 通过 this 调用成员函数，传入当前已构建好的 ruleTriggerText
                return this->checkInterferenceConsistency(sources, errors);
            }
            ),

        /*ReasoningRule(
            "equatorial_to_horizontal",
            "验证赤道坐标到地平坐标转换的准确性（高度角计算）",
            "(地平|水平|altitude|alt|azimuth|azi|坐标转换|换算)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                // 示例：提取RA, DEC, 假设LST=当前时间计算，lat=默认观测站纬度（如30°）
                double lat = 30.0; // 默认纬度（可配置）
                QString raStr, decStr;
                // 从sources提取RA/DEC（类似extractMoonAndTarget）
                for (const auto &v : sources) {
                    QString content = v.toObject()["content"].toString();
                    if (content.contains("RA:")) {
                        raStr = extractRa(content);
                        decStr = extractDec(content);
                    }
                }
                if (raStr.isEmpty() || decStr.isEmpty()) return true; // 无数据不校验

                double raDeg = parseRaToDegrees(raStr);
                double decDeg = parseDecToDegrees(decStr);
                double lst = getCurrentLST(); // 假设函数计算本地恒星时（需实现）
                double ha = lst - raDeg / 15.0; // Hour Angle in hours
                ha = ha * 15.0; // to degrees

                double sinAlt = sin(qDegreesToRadians(decDeg)) * sin(qDegreesToRadians(lat)) +
                                cos(qDegreesToRadians(decDeg)) * cos(qDegreesToRadians(lat)) * cos(qDegreesToRadians(ha));
                double alt = qRadiansToDegrees(asin(sinAlt));

                // 校验：如果文本提到高度角，比较是否一致（±1°容差）
                double textAlt = extractAltitudeFromText(sources); // 需实现提取函数
                if (qAbs(alt - textAlt) > 1.0) {
                    errors << QString("地平坐标转换不准：计算高度角 %.2f° vs 文本 %.2f°").arg(alt).arg(textAlt);
                    return false;
                }
                return true;
            }
            ),
            */






        // 5. 原始数据校验：RA [0,24]
        ReasoningRule(
            "ra_valid_range",
            "赤经RA必须在[0,24]小时内",
            //"RA|赤经",
            ".*",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                //QRegularExpression re(R"(RA[:\s]*([\d\h\m\s\:]+))");
                QRegularExpression re(R"(RA[:\s]*([0-9hms: ]+))");
                for (const auto &v : sources) {
                    QString c = v.toObject()["content"].toString();
                    qDebug() << "【ra_valid_range】检查 content:" << c.left(100);  // 打印前100字符
                    auto m = re.match(c);
                    if (m.hasMatch()) {
                        double ra = parseRaToDegrees(m.captured(1)) / 15.0; // 小时
                        if (ra < 0 || ra >= 24) {
                            errors << QString("无效RA: %.2f h 超出[0,24)").arg(ra);
                            qDebug() << "【ra_valid_range】检测到越界RA，添加错误";
                            return false;
                        }
                    }
                }
                return true;
            }
            ),

        // 6. 原始数据校验：DEC [-90,90]
        ReasoningRule(
            "dec_valid_range",
            "赤纬DEC必须在[-90,90]度内",
            ".*",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                //QRegularExpression re(R"(DEC[:\s]*([\d°′″+-\s\:]+))");
                QRegularExpression re(R"(DEC[:\s]*([+\-0-9°′″: ]+))");
                for (const auto &v : sources) {
                    QString c = v.toObject()["content"].toString();
                    auto m = re.match(c);
                    if (m.hasMatch()) {
                        double dec = parseDecToDegrees(m.captured(1));
                        if (dec < -90 || dec > 90) {
                            errors << QString("无效DEC: %.2f° 超出[-90,90]").arg(dec);
                            return false;
                        }
                    }
                }
                return true;
            }
            ),

        // 7. 曝光时间 [0, max]
        ReasoningRule(
            "exposure_time_valid",
            "曝光时间必须在[0,允许最大]内",
            "曝光|exptime|exposure",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double maxExp = 3600.0; // 从搜索：常见上限
                QRegularExpression re(R"(曝光[:\s]*(\d+)s?)");
                for (const auto &v : sources) {
                    QString c = v.toObject()["content"].toString();
                    auto m = re.match(c);
                    if (m.hasMatch()) {
                        double exp = m.captured(1).toDouble();
                        if (exp <= 0 || exp > maxExp) {
                            errors << QString("无效曝光: %.0fs 超出[0,%1]").arg(exp).arg(maxExp);
                            return false;
                        }
                    }
                }
                return true;
            }
            ),

        // 8. 滤光片位置合法
        ReasoningRule(
            "filter_valid",
            "滤光片必须在设备已有标签内",
            "滤光片|filter|current_filter",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                QSet<QString> validFilters = {"u", "g", "r", "i", "z", "V", "B", "clear"}; // 常见天文滤镜
                QRegularExpression re(R"(滤光片[:\s]*([a-zA-Z]+)|filter[:\s]*([a-zA-Z]+))");
                for (const auto &v : sources) {
                    QString c = v.toObject()["content"].toString();
                    auto m = re.match(c);
                    if (m.hasMatch()) {
                        QString f = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
                        if (!validFilters.contains(f.toLower())) {
                            errors << QString("无效滤镜: %1").arg(f);
                            return false;
                        }
                    }
                }
                return true;
            }
            ),

        // 9. 焦距合理范围（假设[100,10000]mm）
        ReasoningRule(
            "focal_length_valid",
            "焦距必须在合理范围内",
            "焦距|focal length",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double minF = 100.0, maxF = 10000.0;
                QRegularExpression re(R"(焦距[:\s]*(\d+)mm?)");
                // 类似提取并判断
                return true; // 实现类似曝光
            }
            ),

        // 10. 相机温度安全范围（如[-100,30]°C）
        ReasoningRule(
            "camera_temp_safe",
            "相机温度必须在安全范围内",
            "温度|temp|camera temp",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double minT = -100.0, maxT = 30.0; // 天文CCD常见
                // 提取并判断
                return true;
            }
            ),

        // 11. 观测时间 >= 当前
        ReasoningRule(
            "observe_time_future",
            "观测时间必须大于等于当前时间",
            "观测时间|schedule time|时间",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                QDateTime now = QDateTime::currentDateTime();
                QRegularExpression re(R"(时间[:\s]*(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}))");
                for (const auto &v : sources) {
                    QString c = v.toObject()["content"].toString();
                    auto m = re.match(c);
                    if (m.hasMatch()) {
                        QDateTime obs = QDateTime::fromString(m.captured(1), "yyyy-MM-dd hh:mm:ss");
                        if (obs < now) {
                            errors << QString("观测时间已过: %1 < 当前").arg(obs.toString());
                            return false;
                        }
                    }
                }
                return true;
            }
            ),

        // 12. 目标星等 <=30
        ReasoningRule(
            "mag_limit",
            "目标星等不超过30",
            "星等|mag|magnitude",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double maxMag = 30.0;
                QRegularExpression re(R"(星等[:\s]*([\d.]+)|mag[:\s]*([\d.]+))");
                // 提取并判断 >30 报错
                return true;
            }
            ),

        // 13. 湿度 <阈值（如80%）
        ReasoningRule(
            "humidity_safe",
            "湿度小于安全阈值，避免结霜",
            "湿度|humidity",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double maxHum = 80.0; // 从搜索
                QRegularExpression re(R"(湿度[:\s]*(\d+)%?)");
                // 提取并判断
                return true;
            }
            ),

        // 14. 望远镜移动速度合理（假设<10°/s）
        ReasoningRule(
            "slew_speed_valid",
            "望远镜移动速度合理",
            "移动速度|slew speed|speed",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double maxSpeed = 10.0; // °/s
                // 类似提取
                return true;
            }
            ),

        // 15. 数据关联性：云量>80% 与观测互斥
        ReasoningRule(
            "cloud_cover_mutex",
            "云量超过80%与观测互斥",
            "云量|cloud cover|云覆盖",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                bool hasObserve = false; // 检查是否有观测计划
                double cloud = 0.0;
                for (const auto &v : sources) {
                    QString t = v.toObject()["type"].toString();
                    QString c = v.toObject()["content"].toString();
                    if (t == "weather" && c.contains("云覆盖")) {
                        QRegularExpression re(R"(云覆盖[:\s]*(\d+)%?)");
                        auto m = re.match(c);
                        if (m.hasMatch()) cloud = m.captured(1).toDouble();
                    }
                    if (t == "schedule") hasObserve = true;
                }
                if (hasObserve && cloud > 80.0) {
                    errors << QString("云量 %.0f%% >80%%，不应调度观测").arg(cloud);
                    return false;
                }
                return true;
            }
            ),

        // 16. 风速超阈值（如10 m/s）互斥
        ReasoningRule(
            "wind_speed_mutex",
            "风速超过阈值与观测互斥",
            "风速|wind speed",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double maxWind = 10.0; // 从搜索
                // 类似云量，检查风速 + 观测计划
                return true;
            }
            ),

        // 17. 目标指向与安全高度角互斥（<15°）
        ReasoningRule(
            "altitude_mutex",
            "目标高度角低于阈值（15°）与观测互斥",
            "高度角|altitude|alt",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                double minAlt = 15.0; // 从搜索
                // 计算或提取高度角，检查是否 < minAlt + 有观测
                return true;
            }
            ),

        // 规则1: 必须响应高优先级ToO（扩展支持多警报：至少响应最高优先的一个）
        ReasoningRule(
            "too_mandatory_response",
            "AI必须响应至少一个高优先级(≥8)且窗口有效的ToO，建议插入/排序",
            "EXCLUDE:窗口已过|成本过高|保守继续|优先级不足"
            "||"
            "(中断|转向|优先观测|插入|立即观测|打断|切换到|响应|重排程|应立即调度|优先处理|立即切换|立即调度|立刻切换|优先调度|调度至|执行GRB).*"
            "(ToO|GRB|GCN|LIGO|引力波|伽马暴|突发事件|机会目标|警报)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                // 统计高优先ToO数量
                int highToOCount = 0;
                QStringList tooDetails;
                for (const auto &v : sources) {
                    QJsonObject o = v.toObject();
                    QString content = o["content"].toString().toLower();
                    if (content.contains("gcn alert") || content.contains("too") || content.contains("gcn_alert")) {
                        int pri = extractPriority(content);  // 辅助函数：从content提取优先级
                        double win = extractWindow(content); // 辅助函数：提取窗口小时
                        if (pri >= 8 && win >= 0.5) {
                            highToOCount++;
                            tooDetails << "win:" << QString::number(win) <<content.left(50);
                        }
                    }
                }
                // 检查AI文本是否提到响应（中断/插入/排序）
                bool aiResponds = m_ruleTriggerText.contains(QRegularExpression("(中断|插入|排序|重排程|优先响应|立即调度|优先处理|立即切换|立刻切换|优先调度|高优先级，建议切换观测目标|切换目标至GRB|切换观测目标至GRB|调整观测目标至GRB|切换至GRB|高优先级事件|切换观测目标至GRB|高优先级|优先级高|优先观测)", QRegularExpression::CaseInsensitiveOption));
                if (highToOCount > 0 && !aiResponds) {
                    errors << QString("【ToO强制响应失败】有%1个高优先ToO（%2），但AI未建议响应").arg(highToOCount).arg(tooDetails.join(", "));
                    return false;
                }
                if (highToOCount > 1 && !aiResponds && (m_ruleTriggerText.contains(QRegularExpression("中科学价值更高的一个")) || !m_ruleTriggerText.contains(QRegularExpression("，优先级高且科学价值大")))) {
                    errors << "【多ToO失败】多个ToO，但AI未建议排序插入";
                    return false;
                }
                return true;
            }
            ),
        //AI必须给出一个有效决策，否则应该丢弃
        ReasoningRule(
            "good_scheule_response",
            "AI必须给出一个有效决策，或者提示需要进一步输入数据，否则应该丢弃",
            "EXCLUDE:执行原计划|推荐执行例行巡天任务|切换为高优先|保守继续|应维持当前|指向高价值例行观测任务|执行现有计划|继续执行原计划|待瞬变事件窗口结束后|应调度观测|并切换至|请继续执行|返回原计划|调度执行|应继续执行|维持原计划|推荐继续执行原定的|暂不切换观测任务|建议继续观测|(继续|调度|执行|观测).*(高价值例行任务)|(继续|调度|执行|观测).*(高价值常规任务)"
            "||"
            "请评估是否|请确认是否|应考虑调度至更高科学收益的任务|暂停当前调度|暂不调度观测|请确认是否优先调度观测|请确认|请评估",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                // 统计高优先ToO数量
                int highToOCount = 0;
                QStringList tooDetails;
                for (const auto &v : sources) {
                    QJsonObject o = v.toObject();
                    QString content = o["content"].toString().toLower();
                    if (content.contains("gcn alert") || content.contains("too")) {
                        //int pri = extractPriority(content);  // 辅助函数：从content提取优先级
                        //double win = extractWindow(content); // 辅助函数：提取窗口小时

                            highToOCount++;
                            tooDetails << content.left(150);

                    }
                }
                // 检查AI文本是否提到响应（中断/插入/排序）
                bool aiResponds = m_ruleTriggerText.contains(QRegularExpression("(中断|插入|排序|重排程|优先响应|立即调度|优先处理|立即切换|立刻切换|优先调度|建议继续执行高价值常规任务|当前望远镜指向高价值常规观测任务，不切换调度)", QRegularExpression::CaseInsensitiveOption));
                bool aiResponds2 = m_ruleTriggerText.contains(QRegularExpression("(ToO|GRB|GCN|LIGO|引力波|伽马暴|突发事件|机会目标|警报)"));
                bool aiResponds3 = m_ruleTriggerText.contains(QRegularExpression("(请评估是否|请确认是否|应考虑调度至更高科学收益的任务|暂停当前调度|暂不调度观测|请确认是否优先调度观测)"));
                if (highToOCount > 0 && !aiResponds && aiResponds2 && aiResponds3) {
                    errors << QString("【ToO决策建议价值不大，应丢弃】（%1），但AI未建议响应").arg(tooDetails.join(", "));
                    return false;
                }
                qDebug()<<"=========aiResponds:"<<aiResponds<<"  ===================aiResponds2:"<<aiResponds2;
                return true;
            }
            ),

        ReasoningRule(
            "normal_scheule_response",
            "AI必须给出一个有效决策，或者提示需要进一步输入数据，否则应该丢弃",
            "EXCLUDE:执行原计划|切换为高优先|保守继续|应维持当前|指向高价值例行观测任务|执行现有计划|继续执行原计划|待瞬变事件窗口结束后|应调度观测|并切换至|请继续执行|返回原计划|调度执行|应继续执行|维持原计划|推荐继续执行原定的|暂不切换观测任务|建议继续观测|推荐执行例行巡天任务|(继续|调度|执行|观测).*(高价值例行任务)|(继续|调度|执行|观测).*(高价值常规任务)"
            "||"
            "请评估是否|请确认是否|应考虑调度至更高科学收益的任务|请确认是否优先调度观测|请考虑推迟或调整调度",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {



                qDebug() << "AI必须给出一个有效决策,而不是给出模棱两可的东西" ;
                return true;
            }
            ),
        // 规则2: 禁止窗口已过中断（支持多警报：逐个检查）
        ReasoningRule(
            "too_window_expired",
            "AI不应在任何ToO窗口已过(<0.5h)时建议中断",
            "(中断|转向|插入|切换到|优先观测).*"
            "(ToO|GRB|GCN|LIGO|引力波|伽马暴|机会目标)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                QStringList expiredToOs;
                for (const auto &v : sources) {
                    QString content = v.toObject()["content"].toString().toLower();
                    double win = extractWindow(content);
                    if (win < 0.5) expiredToOs << content.left(50);
                }
                if (!expiredToOs.isEmpty() && m_ruleTriggerText.contains("中断|转向|插入")) {
                    errors << QString("【ToO窗口已过错误】有%1个过期ToO（%2），不应建议中断").arg(expiredToOs.size()).arg(expiredToOs.join(", "));
                    return false;
                }
                return true;
            }
            ),

        // 规则3: 切换成本过高时拒绝（扩展多警报：检查平均/最大成本）
        ReasoningRule(
            "too_slew_cost_too_high",
            "AI不应在ToO切换成本过高(角距>60°)时建议中断",
            "EXCLUDE:成本低|slew time short"
            "||"
            "(中断|转向|切换到|插入).*"
            "(ToO|GRB|GCN|LIGO|引力波|伽马暴)",
            [this](const QJsonArray &sources, QStringList &errors) -> bool {
                QString currentRa = extractCurrentRa(sources);  // 辅助：从telescope_status提取当前坐标
                QString currentDec = extractCurrentDec(sources);
                QStringList highCostToOs;
                for (const auto &v : sources) {
                    QString c = v.toObject()["content"].toString();
                    QString tooRa = extractRa(c);
                    QString tooDec = extractDec(c);
                    if (!tooRa.isEmpty()) {
                        double dist = calculateAngularDistance(currentRa, currentDec, tooRa, tooDec);
                        if (dist > 60.0) highCostToOs << QString("ToO角距%.1f°").arg(dist);
                    }
                }
                if (!highCostToOs.isEmpty() && m_ruleTriggerText.contains("中断|转向|插入")) {
                    errors << QString("【ToO成本过高错误】有%1个高切换成本ToO（%2），不应中断").arg(highCostToOs.size()).arg(highCostToOs.join(", "));
                    return false;
                }
                return true;
            }
            ),




        // ... 其他类似规则，根据需要添加
    };

    m_rulesInited = true;
    qDebug() << "关联推理规则初始化完成，共" << m_reasoningRules.size() << "条";
}



// ==================== 辅助函数实现 ====================

QString Validator::extractRa(const QString &content) const
{
    // 支持 "RA:18h32m" "RA:18h31m20s" "RA: 18:31:20" 等格式
   /* QRegularExpression re(R"(RA[:\s]*([0-9hms:\s]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    return QString();*/

    // 修复：支持更多格式，增加调试
    QString cleaned = content.toLower(); // 转为小写匹配

    // 尝试多种 RA 格式
    QRegularExpression patterns[] = {
        QRegularExpression(R"(ra[:\s]*([0-9]+h[0-9]+m[0-9]+s?))"),  // RA:18h31m20s
        QRegularExpression(R"(ra[:\s]*([0-9]+h[0-9]+m))"),          // RA:15h00m
        QRegularExpression(R"(ra[:\s]*([0-9]+:[0-9]+:[0-9]+))"),    // RA:18:31:20
        QRegularExpression(R"(ra[:\s]*([0-9]+:[0-9]+))"),           // RA:15:00
        QRegularExpression(R"(ra[:\s]*(\d+h\d+m\d+s?))", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"(ra[:\s]*(\d+h\d+m))", QRegularExpression::CaseInsensitiveOption)
    };

    for (const auto &re : patterns) {
        QRegularExpressionMatch match = re.match(cleaned);
        if (match.hasMatch()) {
            QString result = match.captured(1).trimmed();
            qDebug() << "【extractRa】成功提取:" << result << "from:" << cleaned.left(50);
            return result;
        }
    }

    qDebug() << "【extractRa】所有正则失败，content:" << cleaned.left(100);
    return QString();
}

QString Validator::extractDec(const QString &content) const
{
    /*QRegularExpression re(R"(DEC[:\s]*([+-]?[0-9°′″:\s]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    return QString();*/

    QString cleaned = content.toLower();

    // 尝试多种 DEC 格式
    QRegularExpression patterns[] = {
        QRegularExpression(R"(dec[:\s]*([+-]?\d+°\d+′\d+″?))"),  // DEC:+22°10′15″
        QRegularExpression(R"(dec[:\s]*([+-]?\d+°\d+′))"),       // DEC:-20°00′
        QRegularExpression(R"(dec[:\s]*([+-]?\d+:\d+:\d+))"),    // DEC:+22:10:15
        QRegularExpression(R"(dec[:\s]*([+-]?\d+:\d+))"),        // DEC:-20:00
        QRegularExpression(R"(dec[:\s]*([+-]?\d+°[0-9]+′[0-9]+″?))", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"(dec[:\s]*([+-]?\d+°[0-9]+′))", QRegularExpression::CaseInsensitiveOption)
    };

    for (const auto &re : patterns) {
        QRegularExpressionMatch match = re.match(cleaned);
        if (match.hasMatch()) {
            QString result = match.captured(1).trimmed();
            qDebug() << "【extractDec】成功提取:" << result << "from:" << cleaned.left(50);
            return result;
        }
    }

    qDebug() << "【extractDec】所有正则失败，content:" << cleaned.left(100);
    return QString();
}

double Validator::extractAltitudeFromText(const QJsonArray &sources) const
{
    // 在所有 sources 中查找类似 "高度角: 45°" 或 "altitude: 45 deg"
    QRegularExpression re(R"((高度角|altitude|alt)[:\s]*([\d.]+))", QRegularExpression::CaseInsensitiveOption);
    for (const auto &v : sources) {
        QString c = v.toObject()["content"].toString();
        auto m = re.match(c);
        if (m.hasMatch()) {
            return m.captured(2).toDouble();
        }
    }
    return -999; // 未找到，返回无效值
}

double Validator::extractDistanceFromText(const QJsonArray &sources) const
{
    QRegularExpression re(R"((角距|距离|distance|separation)[:\s]*([\d.]+))", QRegularExpression::CaseInsensitiveOption);
    for (const auto &v : sources) {
        QString c = v.toObject()["content"].toString();
        auto m = re.match(c);
        if (m.hasMatch()) {
            return m.captured(2).toDouble();
        }
    }
    return -999;
}

// 暂时占位：本地恒星时（LST）计算需要经度，这里返回一个固定值或抛出警告
double Validator::getCurrentLST() const
{
    // 实际项目中可以用 astropy 或自己实现，这里先返回一个无效值让规则跳过
    qDebug() << "getCurrentLST 未实现，使用占位值";
    return 0.0; // 导致高度角计算无效，规则会因坐标缺失而跳过
}

QStringList Validator::lastErrors() const
{//返回最终错误
    qDebug()<<"+++++++++++++++"<<m_lastErrors<<"+++++++++++++++";
    return m_lastErrors;
}


// validator.cpp

bool Validator::checkInterferenceConsistency(const QJsonArray &sources,
                                             QStringList &errors) const
{

    const QString &triggerText = m_ruleTriggerText;
    // 统计 sources 中是否存在各种干扰记录
    bool hasAircraft = false;
    bool hasSatellite = false;
    bool hasSeeing = false;
    bool hasWeather = false;

    for (const auto &v : sources) {
        QJsonObject obj = v.toObject();
        QString type = obj["type"].toString().toLower();

        if (type.contains("aircraft")) hasAircraft = true;
        if (type.contains("satellite")) hasSatellite = true;
        if (type.contains("seeing")) hasSeeing = true;
        if (type.contains("weather")) hasWeather = true;
    }

    // 检查 triggerText 是否提到各种干扰（分散出现也支持）
    bool mentionsAircraft = triggerText.contains(QRegularExpression("(飞机|aircraft)", QRegularExpression::CaseInsensitiveOption));
    bool mentionsSatellite = triggerText.contains(QRegularExpression("(卫星|星链|轨迹穿越|反射光)", QRegularExpression::CaseInsensitiveOption));
    bool mentionsSeeing = triggerText.contains(QRegularExpression("(视宁度|seeing|抖动|大气条件)", QRegularExpression::CaseInsensitiveOption));
    bool mentionsCloud = triggerText.contains(QRegularExpression("(云覆盖|云量|云雾|能见度)", QRegularExpression::CaseInsensitiveOption));
    bool mentionsWind = triggerText.contains(QRegularExpression("(风速|强风|大风)", QRegularExpression::CaseInsensitiveOption));

    QStringList missing;
    if (mentionsAircraft && !hasAircraft) missing << "飞机干扰";
    if (mentionsSatellite && !hasSatellite) missing << "卫星干扰";
    if (mentionsSeeing && !hasSeeing) missing << "视宁度/大气抖动";
    if (mentionsCloud && !hasWeather) missing << "云覆盖/云雾";
    if (mentionsWind && !hasWeather) missing << "强风";

    if (!missing.isEmpty()) {
        errors << QString("【干扰一致性失败】AI提到存在%1，但raw_data_sources中缺少对应记录").arg(missing.join("、"));
        return false;
    }

    qDebug() << "干扰存在一致性校验通过";
    return true;
}


int Validator::extractPriority(const QString &content) const
{
    qDebug()<<"extractPriority--content:"<<content;
    QRegularExpression re(R"(priority[:\s]*(\d+)|优先级[:\s]*(\d+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        QString cap1 = match.captured(1);
        if (!cap1.isEmpty()) {qDebug()<<"extractPriority:"<<cap1;return cap1.toInt();}
        QString cap2 = match.captured(2);
        if (!cap2.isEmpty()) {qDebug()<<"extractPriority:"<<cap2;return cap2.toInt();}
    }
    qDebug()<<"match extractPriority fail";
    return 5; // 默认中优先级
}

double Validator::extractWindow(const QString &content) const
{
    QRegularExpression re(R"(visible window[:\s]*([\d.]+)\s*hour[s]?|窗口[:\s]*([\d.]+)\s*小时|window[:\s]*([\d.]+))",
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        for (int i = 1; i <= 3; ++i) {
            QString cap = match.captured(i);
            if (!cap.isEmpty()) return cap.toDouble();
        }
    }
    // 如果明确提到“已过”“不足”“关闭”等，视为 0
    if (content.contains(QRegularExpression("已过|不足|关闭|expired|closed", QRegularExpression::CaseInsensitiveOption))) {
        return 0.1;
    }
    qDebug()<<"match extractWindow fail";
    return 3.0; // 默认 3 小时
}

QString Validator::extractCurrentRa(const QJsonArray &sources) const
{
    for (const auto &v : sources) {
        QJsonObject obj = v.toObject();
        QString table = obj["table"].toString();
        QString content = obj["content"].toString();
        if (table == "telescope_status" || content.contains("当前指向") || content.contains("pointing")) {
            return this->extractRa(content);  // 你已有 extractRa 函数
        }
    }
    return QString();
}

QString Validator::extractCurrentDec(const QJsonArray &sources) const
{
    for (const auto &v : sources) {
        QJsonObject obj = v.toObject();
        QString table = obj["table"].toString();
        QString content = obj["content"].toString();
        if (table == "telescope_status" || content.contains("当前指向") || content.contains("pointing")) {
            return this->extractDec(content);  // 你已有 extractDec 函数
        }
    }
    return QString();
}

// 新增：提取所有“非当前指向”的 RA/DEC 对
// 只要 content 同时包含 RA: 和 DEC: 的记录，都算潜在目标（TOO、计划目标、多警报等）
QList<QPair<QString, QString>> Validator::extractAllOtherRaDecPairs(const QJsonArray &sources) const
{
    QList<QPair<QString, QString>> pairs;

    for (const auto &v : sources) {
        QJsonObject o = v.toObject();
        QString table = o["table"].toString();
        QString type  = o["type"].toString();
        QString content = o["content"].toString();

        // 排除“当前指向”记录：telescope_status 表 或 type 是 target_pointing 的
        if (table == "telescope_status" ||
            type.compare("target_pointing", Qt::CaseInsensitive) == 0) {
            continue;
        }

        // 必须同时包含 RA 和 DEC 关键字
        if (content.contains(QRegularExpression("RA:", QRegularExpression::CaseInsensitiveOption)) &&
            content.contains(QRegularExpression("DEC:", QRegularExpression::CaseInsensitiveOption))) {

            QString ra  = extractRa(content);   // 你已有的函数
            QString dec = extractDec(content);  // 你已有的函数

            if (!ra.isEmpty() && !dec.isEmpty()) {
                pairs << qMakePair(ra, dec);
            }
        }
    }
    return pairs;
}
