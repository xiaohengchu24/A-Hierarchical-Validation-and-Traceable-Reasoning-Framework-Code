#include "mainwindow.h"
#include "aiclient.h"

#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDateTime>
#include <QDebug>
#include <QTextBlock>
#include <QTimer>
#include <QSqlError>
#include <QHeaderView>
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QInputDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QJsonDocument>
#include <QComboBox>
#include<QRandomGenerator>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_aiClient(new AIClient(this))
    , m_inputBuilder(new AIInputBuilder(this))
    , m_autoTimer(new QTimer(this))
    , m_validator(new Validator(QSqlDatabase::database("qt_sql_default_connection"), this))
    , m_statsManager(new StatisticsManager(this))
    , exportStatsBtn(nullptr)
{

    setupUI();
    setupConnections();

    // 初始化AI客户端
    m_aiClient->initialize();

    // MainWindow 构造函数里加一个下拉框




    // === 自动模式：每 60 秒触发一次 ===
    connect(m_autoTimer, &QTimer::timeout, this, &MainWindow::onAutoSchedule);
   // m_autoTimer->start(60000);  // 60秒，可调

    // 可选：首次立即运行
  //  QTimer::singleShot(1000, this, &MainWindow::onAutoSchedule);
    connect(m_validator, &Validator::validationFailed, this, &MainWindow::onValidationFailed);


    connect(m_aiClient,&AIClient::statusChanged,this,&MainWindow::recheck_Aistart);
    //void MainWindow::recheck_Aistart(QString recv_text)
}

MainWindow::~MainWindow()
{
    delete m_statsManager;
}

void MainWindow::setupUI()
{
    setWindowTitle("AI观测助手_本地模型");
    setMinimumSize(600, 500);
    resize(800, 600);

    // 创建中央部件
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    // 创建主布局
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setSpacing(10);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);

    // 创建聊天显示区域
    m_chatDisplay = new QTextEdit(this);
    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setMinimumHeight(300);
    m_mainLayout->addWidget(m_chatDisplay);

    // 创建输入区域
    m_inputLayout = new QHBoxLayout();

    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setPlaceholderText("输入您的问题...");

    m_sendButton = new QPushButton("发送", this);
    m_stopButton = new QPushButton("停止", this);
    m_stopButton->setEnabled(false);
       // 1. 创建 m_resetButton
    m_resetButton = new QPushButton("重置", this);

    m_inputLayout->addWidget(m_inputEdit, 1);
    m_inputLayout->addWidget(m_sendButton);
    m_inputLayout->addWidget(m_stopButton);

    // 在 setupUI() 中，m_inputLayout 已有 send/stop/reset
    m_autoToggleButton = new QPushButton("启动自动", this);
    m_autoToggleButton->setCheckable(true);  // 可切换
    m_autoToggleButton->setStyleSheet(
        "QPushButton:checked { background-color: #4caf50; color: gray; }"
        "QPushButton:!checked { background-color: #e0e0e0; }"
        );
    m_inputLayout->addWidget(m_autoToggleButton);


    // ======== 新增：测试用例下拉框 ========
    m_testCombo = new QComboBox(this);
    m_testCombo->setToolTip("选择一个测试用例，切换后会自动清理并注入对应的错误/边界数据");
    m_testCombo->addItem("正常模式（不注入测试数据）", -1);

    // 12 条用例，一条不少
    /*m_testCombo->addItem("1-正常月相干扰（应该通过）",                1);
    m_testCombo->addItem("2-坐标错位30°（AI不该说干扰）",            2);
    m_testCombo->addItem("3-坐标错位8°（边界测试）",                3);
    m_testCombo->addItem("4-幻觉pk_id（AI引用不存在的记录）",        4);
    m_testCombo->addItem("5-非法坐标（RA/DEC超范围）",              5);
    m_testCombo->addItem("6-遗漏卫星干扰（AI不引用已有记录）",      6);
    m_testCombo->addItem("7-滤镜缺失（最常见的坑）",                7);
    m_testCombo->addItem("8-verification_hints为空",                8);
    m_testCombo->addItem("9-错误的SQL表名/字段",                    9);
    m_testCombo->addItem("10-幻觉天气记录",                        10);*/
    m_testCombo->addItem("1-异常设备、物理量场景",                       11);
    //m_testCombo->addItem("12-黄金规则强制复用",                   12);
    m_testCombo->addItem("2-ToO 响应观测（高优先级 + 合理窗口）", 13);  // 已有
   // m_testCombo->addItem("14-ToO 窗口已过（不应中断）", 14);
    //m_testCombo->addItem("15-ToO 切换成本过高（坐标差大）", 15);
    m_testCombo->addItem("3-ToO 低优先级（忽略警报）", 16);
    m_testCombo->addItem("4-多ToO竞争（重排程多个警报）", 17);

    m_testCombo->addItem("5-复杂环境下的高优先TOO（相关干扰增加）", 18);
    m_testCombo->addItem("6-多无关干扰下的低优先TOO（噪声数据增加）", 19);

    // 把下拉框塞到输入区（你原来的 m_inputLayout 是 QHBoxLayout）
    m_inputLayout->insertWidget(m_inputLayout->count()-1, m_testCombo);  // 插到【发送】按钮前面


    // 2. 将 m_resetButton 添加到布局
    m_inputLayout->addWidget(m_resetButton);

    exportStatsBtn = new QPushButton("导出统计数据", this);
    exportStatsBtn->setToolTip("点击后将当前所有验证统计结果导出为 CSV 文件，用于论文分析");
    m_inputLayout->addWidget(exportStatsBtn);

    //批量测试按钮
    m_batchTestBtn = new QPushButton("启动批量测试", this);
    m_batchTestBtn->setToolTip("自动运行所有12个测试用例，每个100次");
    m_inputLayout->addWidget(m_batchTestBtn);

    connect(exportStatsBtn, &QPushButton::clicked, this, [this]() {
        m_statsManager->exportToCSV();
        appendMessage("统计数据已成功导出到 CSV 文件（在程序同目录下）", "system");
    });

    m_mainLayout->addLayout(m_inputLayout);



    // 创建状态标签
    m_statusLabel = new QLabel("正在初始化...", this);
    m_mainLayout->addWidget(m_statusLabel);

    // 添加欢迎消息
    appendMessage("欢迎使用AI观测助手！此版本使用纯本地模型，无需网络连接。", "system");


    // 新增：JSON表格区域
    m_jsonTable = new QTableWidget(this);
    m_jsonTable->setRowCount(0);
    m_jsonTable->setColumnCount(3);
    m_jsonTable->setHorizontalHeaderLabels({"字段名", "值", "类型"});
    m_jsonTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);  // 自动拉伸列宽
    m_jsonTable->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 只读
    m_jsonTable->setMinimumHeight(200);
    m_mainLayout->insertWidget(2, m_jsonTable);  // 插入到聊天显示下方

    // 新增：导出按钮
    m_exportButton = new QPushButton("导出JSON到CSV", this);
    m_inputLayout->addWidget(m_exportButton);
    connect(m_exportButton, &QPushButton::clicked, [this]() { exportToCSV(); });



    //专家知识库
    QGroupBox *kbGroup = new QGroupBox("专家知识库（黄金规则强制复用）", this);
    QVBoxLayout *kbLayout = new QVBoxLayout(kbGroup);

    // 按钮栏
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_btnRefreshKB = new QPushButton("刷新列表", this);
    m_btnMarkGolden = new QPushButton("标记为黄金规则", this);
    m_btnUnmarkGolden = new QPushButton("取消黄金标记", this);
    m_btnDeleteRule = new QPushButton("删除选中规则", this);
    m_btnAddNote = new QPushButton("添加备注", this);

    btnLayout->addWidget(m_btnRefreshKB);
    btnLayout->addWidget(m_btnMarkGolden);
    btnLayout->addWidget(m_btnUnmarkGolden);
    btnLayout->addWidget(m_btnDeleteRule);
    btnLayout->addWidget(m_btnAddNote);
    btnLayout->addStretch();

    // 表格
    m_knowledgeTable = new QTableWidget(this);
    m_knowledgeTable->setColumnCount(9);
    QStringList headers = {
        "ID", "使用次数", "置信度", "是否可靠最佳规则", "高频Graph", "累计原子命中", "完整原子匹配次数","首次使用", "末次使用",
        "推理摘要", "归一化Graph", "备注"
    };
    m_knowledgeTable->setHorizontalHeaderLabels(headers);
    m_knowledgeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_knowledgeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_knowledgeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_knowledgeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    kbLayout->addLayout(btnLayout);
    kbLayout->addWidget(m_knowledgeTable);

    // 把知识库面板放到 JSON 表格下面
    m_mainLayout->insertWidget(3, kbGroup);  // 3 = JSON表格下面

    // ============ 新增：原子规则统计面板 ============
    QGroupBox *atomicGroup = new QGroupBox("原子规则统计（高频因果链）", this);
    QVBoxLayout *atomicLayout = new QVBoxLayout(atomicGroup);

    QPushButton *btnRefreshAtomic = new QPushButton("刷新原子统计", this);
    connect(btnRefreshAtomic, &QPushButton::clicked, this, &MainWindow::refreshAtomicTable);

    m_atomicTable = new QTableWidget(this);
    m_atomicTable->setColumnCount(4);
    QStringList atomicHeaders = {"原子链", "使用次数", "首次出现", "末次使用"};
    m_atomicTable->setHorizontalHeaderLabels(atomicHeaders);
    m_atomicTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_atomicTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_atomicTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    atomicLayout->addWidget(btnRefreshAtomic);
    atomicLayout->addWidget(m_atomicTable);

    m_mainLayout->insertWidget(4, atomicGroup);
    // ======================================

    // 初始刷新一次
    refreshKnowledgeTable();

}

void MainWindow::setupConnections()
{
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_stopButton, &QPushButton::clicked, m_aiClient, &AIClient::stopGeneration);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);

    connect(m_aiClient, &AIClient::responseReceived, this, &MainWindow::onResponseReceived);
    connect(m_aiClient, &AIClient::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_aiClient, &AIClient::statusChanged, this, &MainWindow::onStatusChanged);

    connect(m_resetButton, &QPushButton::clicked, this, &MainWindow::onResetClicked);

    connect(m_autoToggleButton, &QPushButton::toggled, this, &MainWindow::onAutoToggle);


    connect(m_jsonTable, &QTableWidget::cellClicked, this, &MainWindow::onTableCellClicked);  // 点击事件

    //专家知识库
    connect(m_btnRefreshKB, &QPushButton::clicked, this, &MainWindow::refreshKnowledgeTable);
    connect(m_btnMarkGolden, &QPushButton::clicked, this, &MainWindow::onMarkAsGolden);
    connect(m_btnUnmarkGolden, &QPushButton::clicked, this, &MainWindow::onUnmarkGolden);
    connect(m_btnDeleteRule, &QPushButton::clicked, this, &MainWindow::onDeleteRule);
    connect(m_btnAddNote, &QPushButton::clicked, this, &MainWindow::onAddNote);
    connect(m_testCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index){
                Q_UNUSED(index);
                int caseId = m_testCombo->currentData().toInt();

                // 调用 AIInputBuilder 的测试模式（后面会实现）
                m_inputBuilder->enableTestMode(caseId);

                if (caseId > 0) {
                    appendMessage(QString("已切换到测试用例 #%1，数据已注入，点击【发送】或等待自动调度即可测试").arg(caseId),
                                  "system");
                } else {
                    appendMessage("已恢复正常模式（不注入测试数据）", "system");
                }
            });

    connect(m_inputBuilder, &AIInputBuilder::requestRunTestCase,
            this, &MainWindow::runTestCase);

    connect(m_aiClient,&AIClient::responseFullyEnded,this,&MainWindow::endx);

    connect(exportStatsBtn, &QPushButton::clicked, this, [this]() {
        m_statsManager->exportToCSV();
        appendMessage("统计数据已成功导出到 CSV 文件（在程序同目录下）", "system");
    });

    // 新增：批量测试按钮连接
    connect(m_batchTestBtn, &QPushButton::clicked, this, &MainWindow::startBatchTest);
    connect(m_validator, &Validator::validationFailed, this, &MainWindow::onValidationFailed);
}

void MainWindow::startBatchTest()
{
    if (m_batchMode) {
        appendMessage("批量测试已在运行中", "system");
        return;
    }

    m_batchMode = true;
    m_batchCurrentId = 0;
    m_batchRunCount = 0;

    appendMessage("批量测试启动：所有12个用例，每用例100次", "system");

    // 清理旧数据 + 注入第一个用例
    runTestCase(m_batchCases[m_batchCurrentId]);

    // 启动定时器（每270秒运行一次 onAutoSchedule）
    m_autoTimer->start(200000);  // 您的原有定时器
}


void MainWindow::onSendClicked()
{
    QString message = m_inputEdit->text().trimmed().toUtf8();
    if (message.isEmpty()) {
        return;
    }

    // 显示用户消息
    appendMessage(message, "user");
    m_inputEdit->clear();

    // 强制禁用UI
    m_inputEdit->setEnabled(false);
    m_sendButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_resetButton->setEnabled(false);

    // 更新状态
    m_statusLabel->setText("状态: AI正在思考...");

    m_lastInputJson = message;

        m_aiClient->sendMessage(message);

}

void MainWindow::onResponseReceived(const QString &response) {
    qDebug()<<"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$";
    if(m_responseProcessed)return;
qDebug()<<"333333333333";
   // 先进行原有修复（时间戳等）
    QString fixedResponse = response;
    fixedResponse.remove(QChar(0xFEFF)); // 移除BOM



    fixedResponse.replace(QRegularExpression("高度(\\d+)km"), "高度 \\1km");
    fixedResponse.replace(QRegularExpression("亮度 V=([\\d.]+) mag"), "亮度 V=\\1 mag");
    fixedResponse.replace(QRegularExpression("持续(\\d+)s"), "持续 \\1s");
    fixedResponse.replace(QRegularExpression("数量:(\\d+)亮度"), "数量:\\1 亮度");
    fixedResponse.replace(QRegularExpression("arcsec温度"), "arcsec 温度");
    fixedResponse.replace(QRegularExpression("云覆盖:(\\d+)%湿度"), "云覆盖:\\1% 湿度");
    fixedResponse.replace(QRegularExpression("风速:(\\d+) m/s"), "风速:\\1 m/s");
    fixedResponse.replace(QRegularExpression("\"(照度):([\\d.]+)\""), "\"\\1: \\2\"");
    fixedResponse.replace(QRegularExpression("优先级:(\\d+)曝光"), "优先级:\\1 曝光");

    //fixedResponse.replace(QRegularExpression("{{"), "{");

    fixedResponse.remove(QRegularExpression("[\\x{0000}-\\x{001F}]")); // 移除ASCII控制符
    fixedResponse.remove(QRegularExpression("<<#>>}"));
    fixedResponse.remove(QRegularExpression("<<#>>>}"));
    fixedResponse.remove(QRegularExpression("<<#>>>}"));
    fixedResponse.remove(QRegularExpression("<<#>>> }"));
    fixedResponse.remove(QRegularExpression("<<>>>}"));
    fixedResponse.remove(QRegularExpression("<<#>>> }"));

    fixedResponse.remove(QRegularExpression(R"(<<#>>.*$)", QRegularExpression::CaseInsensitiveOption));
    fixedResponse.remove(QRegularExpression(R"(}<<#>>.*$)", QRegularExpression::CaseInsensitiveOption));
    fixedResponse = fixedResponse.trimmed();
    fixedResponse.remove(QRegularExpression(R"(<<#>>[\s}]*$)"));  // 删除结尾的变体
    fixedResponse.replace(QRegularExpression(R"({{)"), "{");
    //fixedResponse.replace(QRegularExpression(R"(} ] }})"), "{");

    // 再保险：如果缓冲区末尾有残留，也清除
    if (fixedResponse.endsWith("<<#>>}") || fixedResponse.contains("<<#>>}")  || fixedResponse.endsWith("<<#>> }") || fixedResponse.contains("<<#>> }")) {
        fixedResponse.replace(QRegularExpression(R"(<<#>>}.*)"), "");
        fixedResponse.replace(QRegularExpression(R"(<<#>> }.*)"), "");
        //fixedResponse.replace(QRegularExpression(R"(<<#>>.*)"), "");

    }
    if (fixedResponse.endsWith("<<#>>>}") || fixedResponse.contains("<<#>>>}")  || fixedResponse.endsWith("<<#>>> }") || fixedResponse.contains("<<#>>> }")) {
        fixedResponse.replace(QRegularExpression(R"(<<#>>>}.*)"), "");
        fixedResponse.replace(QRegularExpression(R"(<<#>>> }.*)"), "");
        //fixedResponse.replace(QRegularExpression(R"(<<#>>>.*)"), "");

    }

    fixedResponse.remove(QRegularExpression(R"(<<#>>}.*$)", QRegularExpression::CaseInsensitiveOption));
    fixedResponse.remove(QRegularExpression(R"(<<#>> }.*$)", QRegularExpression::CaseInsensitiveOption));
    fixedResponse = fixedResponse.trimmed();

    fixedResponse.replace(QRegularExpression("}>}"), "}}");
    fixedResponse.replace(QRegularExpression(R"(} ] }}}.*)"), "} ] }}");
    fixedResponse.replace(QRegularExpression(QRegularExpression::escape("} ] }}}")),
        "} ] }}");
    //fixedResponse.remove(QRegularExpression("#"));
    // 添加缺失闭合（简单计数）
    //int openBraces = fixedResponse.count('{') - fixedResponse.count('}');
    //int openBrackets = fixedResponse.count('[') - fixedResponse.count(']');
   // fixedResponse += QString('}').repeated(qMax(0, openBraces));
    //fixedResponse += QString(']').repeated(qMax(0, openBrackets));
    // 提取纯JSON子串（从第一个{到最后一个}，忽略污染）

    int startIdx = fixedResponse.indexOf('{');
    if (startIdx != -1) {
        int endIdx = fixedResponse.lastIndexOf('}');
        if (endIdx > startIdx) {
            fixedResponse = fixedResponse.mid(startIdx, endIdx - startIdx + 1);
            //qDebug() << "提取JSON子串成功，长:" << fixedResponse.length();
        }
    }
qDebug()<<"1111111111111";
    //迭代提取最大完整JSON（防重复/垃圾/Extra data）
    auto extract_largest_json = [](const QString &s) -> QString {
        QString max_json;
        for (int start = 0; start < s.length(); ++start) {
            if (s[start] != '{') continue;
            for (int end = s.length(); end > start; --end) {
                QString sub = s.mid(start, end - start);
                if (sub.endsWith('}')) {
                    QJsonParseError err;
                    QJsonDocument::fromJson(sub.toUtf8(), &err);
                    if (err.error == QJsonParseError::NoError && sub.length() > max_json.length()) {
                        max_json = sub;
                    }
                }
            }
        }
        return max_json;
    };
qDebug()<<"22222222222";
    QString extracted = extract_largest_json(fixedResponse);
    if (!extracted.isEmpty() ) {
        if((num_length !=0) && (num_length != extracted.length()))
        {
            fixedResponse = extracted;
            qDebug() << "提取最大JSON成功，长:" << fixedResponse.length()<<extracted.length();
            num_length=fixedResponse.length();
        }
    } else {
        qDebug() << "无完整JSON，跳过累积";
    }
qDebug()<<"4444444";
    //用fixedResponse更新UI
    QString htmlResponse = fixedResponse; // 改用fixedResponse
    htmlResponse.replace("&", "&amp;"); // 先替换 &
    htmlResponse.replace("<", "&lt;");
    // htmlResponse.replace(">", "&gt;");
    htmlResponse.replace("\n", "<br>");
    htmlResponse.remove('\r');
    // 更新 UI（您的原有逻辑）
    QTextDocument *doc = m_chatDisplay->document();
    QTextBlock lastBlock = doc->lastBlock();
    bool isLastBlockAI = lastBlock.isValid() && lastBlock.text().contains("AI观测助手");
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (isLastBlockAI) {
        // 修改：从 lastBlock 提取旧时间戳，固定它
        QRegularExpression re(R"(\((\d{2}:\d{2}:\d{2})\))");
        QRegularExpressionMatch match = re.match(lastBlock.text());
        if (match.hasMatch()) {
            timestamp = match.captured(1);  // 复用旧时间戳
        }
    }
    QString formattedMessage = QString("<div style='margin: 5px; padding: 8px; background: #f0f4c3; border-radius: 10px; color: yellow;'>"
                                       "<b>AI观测助手 (%1):</b><br>%2</div>")
                                   .arg(timestamp, htmlResponse);
    QTextCursor cursor(doc);
    if (isLastBlockAI) {
        cursor.setPosition(lastBlock.position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.insertHtml(formattedMessage);
    } else {
        cursor.movePosition(QTextCursor::End);
        cursor.insertHtml(formattedMessage);
    }
    QScrollBar *scrollBar = m_chatDisplay->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());

    // 新增：解析并验证 AI 输出（用fixedResponse）

    if (!extracted.isEmpty()) {
        m_responseAccumulator = fixedResponse;  // 替换而非+=
        m_responseAccumulator.replace("}}}", "}}");
    }
    //qDebug() << "Received response:" << fixedResponse.left(200) << "...";
    QJsonParseError err;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(m_responseAccumulator.toUtf8(), &err);
    QJsonObject outputObj;
    if (err.error == QJsonParseError::NoError) {
        outputObj = jsonDoc.object();
        // 新增：检查outputObj非空且含关键字段（防空/无效JSON触发）
        if (outputObj.isEmpty() || !outputObj.contains("reasoning_logic") || outputObj["reasoning_logic"].toObject()["graph"].toString().isEmpty()) {
            qDebug() << "有效JSON但空/无效，跳过后处理";
            m_responseAccumulator.clear();  // 清零防下次
            return;  // 跳过
        }

        if ( !endflag)
        {
            qDebug() << "hints为空，视为不完整，跳过验证";
            m_responseProcessed = false; // 重置允许下次
            return;
        }else{
        // 修改：用现有 m_responseProcessed 防重复验证
        if (m_responseProcessed) {
            qDebug() << "已处理完整响应，跳过重复验证";
            return;  // 防止多次
        }
        //m_responseProcessed = true;
        qDebug()<<"结束---->";



        if (outputObj["verification_hints"].toObject()["queries"].toArray().isEmpty()) {
            qDebug() << "hints为空，视为不完整，跳过验证";
            return;
        }

        appendMessage("JSON解析成功，已验证响应", "system");
        // 新验证逻辑：使用 Validator 模块（去除旧 validateAiResponse）
        QStringList sourceErrors, logicErrors;
        bool sourcesValid = m_validator->validateDataSources(outputObj["raw_data_sources"].toArray(), sourceErrors);
        bool logicValid = m_validator->validateReasoningLogic(outputObj,outputObj["reasoning_logic"].toObject(), outputObj["raw_data_sources"].toArray(), logicErrors);
        QStringList hintResults = m_validator->executeVerificationHints(outputObj["verification_hints"].toObject()["queries"].toArray());
        QStringList allErrors = sourceErrors + logicErrors;
        qDebug()<<"23444";
        bool hasHintFailure = false;
        for (const QString &result : hintResults) {
            if (result.contains("失败")) {  // 检查是否包含 "失败"（匹配 "验证失败hi"）
                hasHintFailure = true;
                break;
            }
        }
       // QStringList allErrors = sourceErrors + logicErrors;

        if (!sourcesValid || !logicValid || hasHintFailure) { // 检查 hints 结果中是否有失败
            qDebug()<<"@@@@@@@@@@@@@@@@@@1";
            QString errorSummary = allErrors.join("\n") + "\nHints结果:\n" + hintResults.join("\n");
            if(m_responseProcessed)return;
            m_responseProcessed = true;
            emit m_validator->validationFailed(errorSummary); // 触发信号
            qDebug()<<"errorSummary"<<errorSummary;
            closeReasoningFeedbackLoop(outputObj, true); // 失败时存储（带标记）
            m_statsManager->recordFailure(outputObj, allErrors);  // ← 统计：失败一次
            appendMessage("验证失败: " + errorSummary + "...", "error");  // 新增：可见失败消息
            qDebug()<<"@@@@@@@@@@@@@@@@@@";
        } else {
            appendMessage("验证通过:\n来源匹配 | 逻辑一致 | Hints:\n" + hintResults.join("\n"), "system");
            qDebug()<<"!!!!!!!!!!!!!!!!!!";
            closeReasoningFeedbackLoop(outputObj, false); // 成功存储
            //m_statsManager->recordSuccess(outputObj);          // ← 统计：成功一次
            qDebug()<<"!!!!!!!!!!!!!!!!!!";
            m_lastValidationFailure.clear();  // 成功清空，避免旧错误残留

        }
        qDebug()<<"44444";


        populateJsonTable(m_lastInputJson, outputObj);
        if (outputObj.isEmpty()) {
            int row = 0;
            m_jsonTable->setRowCount(1);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem("error"));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(err.errorString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("parse_error"));
            populateJsonTable(m_lastInputJson, QJsonObject());
        } else {
            qDebug() << "JSON表格已填充，行数:" << m_jsonTable->rowCount();
        }
        // 完整后，重置累积器
        m_responseAccumulator.clear();
        m_responseProcessed = false;
        qDebug()<<"vvvv+vvvv";

        // ============批量模式计数逻辑 ============
        if (m_batchMode) {
            m_batchRunCount++;
            appendMessage(QString("批量测试进度：用例#%1 已运行%2/100次").arg(m_batchCases[m_batchCurrentId]).arg(m_batchRunCount), "system");

            if (m_batchRunCount >= 100) {
                m_batchRunCount = 0;
                m_batchCurrentId++;

                if (m_batchCurrentId < m_batchCases.size()) {
                    // 切换下一个用例
                    appendMessage(QString("用例#%1 完成，切换到#%2").arg(m_batchCases[m_batchCurrentId-1]).arg(m_batchCases[m_batchCurrentId]), "system");
                    runTestCase(m_batchCases[m_batchCurrentId]);
                } else {
                    // 所有用例结束
                    m_autoTimer->stop();
                    m_batchMode = false;
                    appendMessage("批量测试完成！所有用例已运行100次", "system");
                    m_statsManager->exportToCSV("batch_test_results.csv");  // 自动导出统计
                }
            }
        }
        // ============ 结束 ============

        // ============ 普通自动模式链式调度 ============
        if (!m_batchMode && m_autoToggleButton->isChecked()) {
            // 响应完全处理完毕（校验、统计、知识库已更新）
            // 延迟1~2秒触发下一次，避免UI卡顿或状态冲突
            QTimer::singleShot(2000, this, [this]() {
                appendMessage("本次调度响应处理完成，立即触发下一次自动调度...", "system");
                onAutoSchedule();
            });
        }

        }} else {
        qDebug() << "累积长度:" << m_responseAccumulator.length() << " 解析错误:" << err.errorString();
        // 部分响应：不执行验证/知识库等，只更新UI（已在上方处理）
        // 可选：如果fallback需要，也可加，但最小化先忽略
    }
}

void MainWindow::onErrorOccurred(const QString &error)
{
    // 强制重新启用UI
   // m_inputEdit->setEnabled(true);
   // m_sendButton->setEnabled(true);
   // m_stopButton->setEnabled(false);
   // m_resetButton->setEnabled(true);

    // 更新状态
    m_statusLabel->setText("状态: 错误");

    appendMessage("错误: " + error, "error");

    // 如果是状态问题，提供解决方案
    if (error.contains("请等待当前对话完成")) {
        appendMessage("提示: 点击'重置'按钮可以强制恢复系统状态", "system");
    }
}

void MainWindow::onStatusChanged(const QString &status)
{
    m_statusLabel->setText("状态: " + status);
    // 集中处理UI状态
    if (status.contains("就绪") || status.contains("错误") || status.contains("已停止"))
    {
        bool autoOn = m_autoToggleButton->isChecked();
        m_inputEdit->setEnabled(true);
        m_sendButton->setEnabled(true);
        m_stopButton->setEnabled(false);
        m_resetButton->setEnabled(true);
        // 自动模式保持运行
        if (autoOn && !m_autoTimer->isActive()) {
            m_autoTimer->start(200000);
        }
        // 新增：强制fallback未完整累积
        if (!m_responseAccumulator.isEmpty() && !m_responseProcessed) { // 保持您的原检查
            qDebug() << "就绪但累积未解析，强制fallback，长:" << m_responseAccumulator.length();
            QJsonObject fallbackObj = extractFallbackFields(m_responseAccumulator);
            if (!fallbackObj.isEmpty()) {
                // 新增：检查hints不空才验证
                if (fallbackObj["verification_hints"].toObject()["queries"].toArray().isEmpty()) {
                    qDebug() << "fallback hints为空，视为不完整，跳过";
                } else {
                    QStringList sourceErrors, logicErrors;
                    bool sourcesValid = m_validator->validateDataSources(fallbackObj["raw_data_sources"].toArray(), sourceErrors);
                    bool logicValid = m_validator->validateReasoningLogic(fallbackObj,fallbackObj["reasoning_logic"].toObject(), fallbackObj["raw_data_sources"].toArray(), logicErrors);
                    QStringList hintResults = m_validator->executeVerificationHints(fallbackObj["verification_hints"].toObject()["queries"].toArray());
                    QStringList allErrors = sourceErrors + logicErrors;
                    if (!sourcesValid || !logicValid || hintResults.contains("失败")) {
                        QString errorSummary = allErrors.join("\n") + "\nHints结果:\n" + hintResults.join("\n");
                        emit m_validator->validationFailed(errorSummary);
                        closeReasoningFeedbackLoop(fallbackObj, true); // 失败存储
                    } else {
                        appendMessage("Fallback验证通过:\n来源匹配 | 逻辑一致 | Hints:\n" + hintResults.join("\n"), "system");
                        closeReasoningFeedbackLoop(fallbackObj, false); // 成功存储
                        //m_responseProcessed = true;
                        num_length=0;
                    }
                    //populateJsonTable(m_lastInputJson, fallbackObj);
                }
            } else {
                appendMessage("Fallback提取失败，无法处理响应", "error");
            }
            m_responseAccumulator.clear(); // 清零
        }
        m_responseProcessed = false;  // 保持：在就绪时始终重置标志，准备下次响应
    }
}

void MainWindow::appendMessage(const QString &message, const QString &type)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedMessage;

    if (type == "user") {
        formattedMessage = QString("<div style='margin: 5px; padding: 8px; background: #e3f2fd; border-radius: 10px; color: #000000;'>" // <-- *** 最小化改动 2: 添加 color: #000000; ***
                                   "<b>您 (%1):</b><br>%2</div>")
                               .arg(timestamp, message);
    } else if (type == "ai") {
        formattedMessage = QString("<div style='margin: 5px; padding: 8px; background: #f0f4c3; border-radius: 10px; color: #000000;'>" // <-- *** 最小化改动 3: 添加 color: #000000; ***
                                   "<b>AI观测助手 (%1):</b><br>%2</div>")
                               .arg(timestamp, message);
    } else if (type == "error") {
        // (这一块已经有 color: red; 了，不用改)
        formattedMessage = QString("<div style='margin: 5px; padding: 8px; background: #ffebee; border-radius: 10px; color: red;'>"
                                   "<b>错误 (%1):</b><br>%2</div>")
                               .arg(timestamp, message);

    } else {
        // (系统消息是灰色背景，默认黑色字体也OK，不用改)
        formattedMessage = QString("<div style='margin: 5px; padding: 8px; background: gray; border-radius: 10px; text-align: center;'>"
                                   "<i>%1 - %2</i></div>")
                               .arg(message, timestamp);
    }

    m_chatDisplay->append(formattedMessage);

    m_chatDisplay->ensureCursorVisible();  //确保光标可见，自动滚动
    m_chatDisplay->repaint();  // 强制重绘
    QScrollBar *scrollBar = m_chatDisplay->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}



void MainWindow::updateLastAiMessage(const QString &response)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedMessage = QString("<div style='margin: 8px 0; padding: 12px; background: #2d2d2d; color: #e0e0e0; border: 1px solid #444; border-radius: 12px; max-width: 80%; float: left; clear: both;'>"
                                       "<b>AI观测助手 (%1):</b><br><span style='color: #a0e0a0;'>%2</span></div>"
                                       "<div style='clear: both;'></div>")
                                   .arg(timestamp, response);

    // 获取聊天文档
    QTextDocument *doc = m_chatDisplay->document();

    // 从后往前查找最后一个AI消息
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);

    QTextBlock lastAiBlock;
    while (cursor.movePosition(QTextCursor::PreviousBlock)) {
        QTextBlock block = cursor.block();
        if (block.text().contains("AI观测助手")) {
            lastAiBlock = block;
            break;
        }
    }

    if (lastAiBlock.isValid()) {
        // 选中并替换最后一个AI消息块
        cursor.setPosition(lastAiBlock.position());
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, 2); // 选中消息块和清除浮动块

        cursor.removeSelectedText();
        cursor.insertHtml(formattedMessage);
    }
}
// 添加重置功能
void MainWindow::onResetClicked()
{
    m_aiClient->forceCleanup();
    appendMessage("系统状态已强制重置", "system");
    m_statusLabel->setText("状态: 已重置");

    // 确保UI处于正确状态
    m_inputEdit->setEnabled(true);
    m_sendButton->setEnabled(true);
    m_stopButton->setEnabled(false);
}



void MainWindow::onAutoSchedule()
{

    if (!m_batchMode && !m_autoToggleButton->isChecked()) {
        return;  // 非批量、非自动 → 跳过
    }

    if (!m_autoToggleButton->isChecked()) {
        return; // 按钮已关闭，停止执行
    }

    m_autoRunCount++;

    m_statusLabel->setText(QString("状态: 自动调度中 (%1/%2)").arg(m_autoRunCount).arg(m_autoMaxRuns));
    appendMessage(QString("自动调度执行第 %1 次（最多100次）").arg(m_autoRunCount), "system");

    if (m_autoRunCount >= m_autoMaxRuns) {
        appendMessage("自动调度已达到100次上限，自动停止。如需继续请重新点击【启动自动】", "system");
        m_statusLabel->setText("状态: 就绪（自动调度已完成100次）");
        m_autoToggleButton->setChecked(false);  // 自动取消选中
        m_autoTimer->stop();
        m_autoRunCount = 0;
        m_statusLabel->setText("状态: 自动模式 已停止（达到100次）");
        return;
    }

    if (!m_inputEdit->isEnabled()) {
        qDebug() << "Input disabled, check if running";
        if (m_aiClient->isRunning()) {
            qDebug() << "Running and disabled, forceCleanup first";
            m_aiClient->forceCleanup();
            appendMessage("检测到进程卡住，已强制清理以恢复自动调度", "system");
        }
        qDebug() << "Not running but disabled, force enable inputs";
        m_inputEdit->setEnabled(true);
        m_sendButton->setEnabled(true);
        m_stopButton->setEnabled(false);
        m_resetButton->setEnabled(true);
        m_statusLabel->setText("状态: 强制恢复就绪");
        appendMessage("检测到UI卡住，已强制恢复输入可用", "system");
    }
    // 强制检查并清理可能的卡住进程（防止m_isRunning stuck true导致无法再次自动）

      if (m_aiClient->isRunning()) {
        qDebug() << "isRunning true, forceCleanup";
        m_aiClient->forceCleanup();
        appendMessage("检测到进程卡住，已强制清理以恢复自动调度", "system");
    }




    m_aiClient->init_stableBuffer();
    endflag = false;
    // 2. 构建输入 JSON
    QString jsonInput = m_inputBuilder->buildInputJson();
    qDebug() << "jsonInput length:" << jsonInput.length() << " first100:" << jsonInput.left(100);
    if (jsonInput.isEmpty()) {
        qDebug() << "jsonInput empty, skipped";
        qWarning() << "自动调度失败：数据库数据为空";
        return;
    }


    QString fullMessage = jsonInput;
    if (!m_lastValidationFailure.isEmpty()) {
        fullMessage = "\n\n{上次AI输出验证失败，必须避免同样错误，核实真实性:\n" + m_lastValidationFailure +"}" + fullMessage;
        appendMessage("已注入上次验证失败反馈到本次输入"+m_lastValidationFailure, "system");  // 可选日志
    }

    m_lastInputJson = fullMessage;

    //m_lastInputJson = jsonInput;  // 缓存输入JSON

    qDebug()<<"修正避免同样错误:"<<"---------输入："<<fullMessage;
    // 显示输入
    QString pretty = QJsonDocument::fromJson(jsonInput.toUtf8())
                         .toJson(QJsonDocument::Indented);
    appendMessage("自动调度输入:<br><pre>" + pretty + "</pre>", "system");

    // 4. 禁用输入（防止冲突）
    m_inputEdit->setEnabled(false);
    m_sendButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_resetButton->setEnabled(false);
    m_statusLabel->setText("状态: 自动调度中...");

    // 5. 发送


    m_aiClient->sendMessage(jsonInput);
}



void MainWindow::onAutoToggle(bool checked)
{
    /*
    if (checked) {
        m_autoToggleButton->setText("停止自动");
        m_autoRunCount = 0;  //每次启动时重置计数为0
        m_autoTimer->start(240000);  // 60秒
        appendMessage("自动调度已启动（每300秒）", "system");
        QTimer::singleShot(5000, this, &MainWindow::onAutoSchedule);  // 立即触发一次
    } else {
        m_autoToggleButton->setText("启动自动");
        m_autoTimer->stop();
        m_autoRunCount = 0;
        appendMessage("自动调度已停止", "system");
    }
    m_statusLabel->setText(QString("状态: 自动模式 %1").arg(checked ? "运行中" : "已停止"));
    */
    if (checked) {
        m_autoToggleButton->setText("停止自动");
        m_autoRunCount = 0; // 重置计数
        appendMessage("自动调度已启动（响应式链式调度，每次响应完成后立即进行下一次）", "system");

        // 只触发第一次，不再启动固定间隔定时器
        QTimer::singleShot(3000, this, &MainWindow::onAutoSchedule); // 3秒后开始第一次
    } else {
        m_autoToggleButton->setText("启动自动");
        m_autoRunCount = 0;
        appendMessage("自动调度已停止", "system");
    }
    m_statusLabel->setText(QString("状态: 自动模式 %1").arg(checked ? "运行中" : "已停止"));

}


// 新增：AI 响应验证函数（放在 MainWindow 类的 .cpp 文件中）
void MainWindow::validateAiResponse(const QString &jsonStr)
{
    QJsonParseError err;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        qDebug() << "Validate parse error:" << err.errorString();
        appendMessage("AI 输出不是合法 JSON：" + err.errorString(), "error");
        return;
    }

    QJsonObject root = jsonDoc.object();
    if (!root.contains("verification_hints")) {
        appendMessage("AI 输出缺少 verification_hints 字段", "error");
        return;
    }
    QJsonObject hints = root["verification_hints"].toObject();
    QJsonArray queries = hints["queries"].toArray();
    if (queries.isEmpty()) {
        appendMessage("AI 输出缺少 verification_hints.queries 数组", "error");
        return;
    }

    QSqlDatabase db = QSqlDatabase::database();  // 使用默认连接
    if (!db.isOpen()) {
        qDebug() << "DB not open:" << db.lastError().text();
        appendMessage("数据库连接失败，无法执行验证", "error");
        return;
    }

    qDebug() << "DB open. Executing" << queries.size() << "queries.";

    QStringList results;
    for (const QJsonValue &v : queries) {
        QJsonObject q = v.toObject();
        QString table = q["table"].toString();
        QString sql = q["sql"].toString();
        QString purpose = q["purpose"].toString();

        if (table.isEmpty() || sql.isEmpty() || purpose.isEmpty()) {
            results << QString("跳过无效查询 [%1]: 缺少 table/sql/purpose").arg(purpose);
            continue;
        }

        QSqlQuery query(db);
        if (!query.exec(sql)) {
            results << QString("验证失败 [%1]: %2\nSQL: %3")
                           .arg(purpose, query.lastError().text(), sql);
        } else {
            // 处理 SELECT COUNT(*) 的结果
            int count = 0;
            if (query.next()) {
                count = query.value(0).toInt();
            }
            if(count > 0)
            {
                results << QString("验证通过 [%1] → %2 条记录\nSQL: %3")
                               .arg(purpose).arg(count).arg(sql);
            }else if (purpose.contains("高优先级") == false)
            {
                results << QString("验证失败,检索数量不通过 [%1]: %2\nSQL: %3")
                               .arg(purpose, query.lastError().text(), sql);
            }else
            {
            results << QString("验证通过,忽略此项验证 [%1] → %2 条记录\nSQL: %3")
                           .arg(purpose).arg(count).arg(sql);
            }
        }
    }

    if (!results.isEmpty()) {
        QString summary = results.join("\n");
        //qDebug() << "Validation summary:\n" << summary;
        appendMessage("AI 验证结果:\n<pre>" + summary + "</pre>", "system");
    } else {
        qDebug() << "No validation results.";
        appendMessage("无验证查询执行", "system");
    }
}



void MainWindow::populateJsonTable(const QString& inputJsonStr, const QJsonObject& outputJson)
{
    //qDebug() << "输入JSON:" << inputJsonStr.left(200);  // 检查输入是否完整
    m_jsonTable->clearContents();
    m_jsonTable->setRowCount(0);
    m_jsonTable->setColumnCount(3);  // 字段名 | 值 | 来源（输入/输出）
    m_jsonTable->setHorizontalHeaderLabels({"字段名", "值", "来源"});

    // 解析输入JSON
    QJsonParseError inputErr;
    QJsonDocument inputDoc = QJsonDocument::fromJson(inputJsonStr.toUtf8(), &inputErr);
    QJsonObject inputJson;
    if (inputErr.error == QJsonParseError::NoError) {
        inputJson = inputDoc.object();
    }

    // 输入核心字段
    QStringList inputFields = {"current_time", "telescope_status.pointing", "telescope_status.current_filter", "telescope_status.update_time"};
    for (const QString& field : inputFields) {
        QStringList parts = field.split('.');
        QJsonValue val = inputJson;
        bool valid = true;
        for (const QString& part : parts) {
            if (val.isObject()) val = val.toObject()[part];
            else { valid = false; break; }
        }
        if (valid && !val.isNull()) {
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem("输入: " + field));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(val.toString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输入"));
            m_jsonTable->item(row, 1)->setBackground(QColor(173, 216, 230));  // 蓝
            m_jsonTable->item(row, 1)->setForeground(QColor(0, 0, 139));

        }
    }

    // 输入数组展开（e.g., recent_logs）
    if (inputJson.contains("recent_logs")) {
        QJsonArray logs = inputJson["recent_logs"].toArray();
        if (logs.isEmpty()) {
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem("输入: recent_logs"));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem("无日志"));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输入"));
        } else {
        for (int i = 0; i < logs.size(); ++i) {
            QJsonObject log = logs[i].toObject();
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem(QString("输入: recent_logs[%1].data").arg(i)));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(log["data"].toString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输入"));
            m_jsonTable->item(row, 1)->setBackground(QColor(173, 216, 230));
            m_jsonTable->item(row, 1)->setForeground(QColor(0, 0, 139));
        }}
    }
    if (inputJson.contains("planned_targets")) {
        QJsonArray targets = inputJson["planned_targets"].toArray();
        for (int i = 0; i < targets.size(); ++i) {
            QJsonObject target = targets[i].toObject();
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem(QString("输入: planned_targets[%1].name").arg(i)));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(target["name"].toString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输入"));
            m_jsonTable->item(row, 1)->setBackground(QColor(173, 216, 230));
            m_jsonTable->item(row, 1)->setForeground(QColor(0, 0, 139));
        }
    }

    // 输出核心字段（您的原有逻辑）
    QStringList coreFields = {"task_id", "timestamp", "suggestion.text", "reasoning_logic.text", "reasoning_logic.graph", "reasoning_logic.confidence"};
    for (const QString& field : coreFields) {
        QStringList parts = field.split('.');
        QJsonValue val = outputJson;
        bool valid = true;
        for (const QString& part : parts) {
            if (val.isObject()) val = val.toObject()[part];
            else { valid = false; break; }
        }
        if (valid && !val.isNull()) {
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem("输出: " + field));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(val.toString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输出"));
            m_jsonTable->item(row, 1)->setBackground(QColor(144, 238, 144));  // 绿
            m_jsonTable->item(row, 1)->setForeground(QColor(255, 165, 0));
        }
    }

    // 输出数组展开（您的原有逻辑）
    if (outputJson.contains("raw_data_sources")) {
        QJsonArray sources = outputJson["raw_data_sources"].toArray();
        for (int i = 0; i < sources.size(); ++i) {
            QJsonObject source = sources[i].toObject();
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem(QString("输出: raw_data_sources[%1].content").arg(i)));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(source["content"].toString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输出"));
            m_jsonTable->item(row, 1)->setBackground(QColor(144, 238, 144));
            m_jsonTable->item(row, 1)->setForeground(QColor(255, 165, 0));
        }
    }
    if (outputJson.contains("verification_hints")) {
        QJsonObject hints = outputJson["verification_hints"].toObject();
        QJsonArray queries = hints["queries"].toArray();
        for (int i = 0; i < queries.size(); ++i) {
            QJsonObject query = queries[i].toObject();
            int row = m_jsonTable->rowCount();
            m_jsonTable->insertRow(row);
            m_jsonTable->setItem(row, 0, new QTableWidgetItem(QString("输出: verification_hints.queries[%1].sql").arg(i)));
            m_jsonTable->setItem(row, 1, new QTableWidgetItem(query["sql"].toString()));
            m_jsonTable->setItem(row, 2, new QTableWidgetItem("输出"));
            m_jsonTable->item(row, 1)->setBackground(QColor(144, 238, 144));
            m_jsonTable->item(row, 1)->setForeground(QColor(255, 165, 0));
        }
    }


    m_jsonTable->resizeColumnsToContents();
    m_jsonTable->setVisible(true);
}

// 新增：导出CSV函数（便于自动化脚本调用）
void MainWindow::exportToCSV(const QString& filename)
{
    QString fileName = filename.isEmpty() ? QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_json.csv" : filename;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "导出失败:" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "字段名,值,类型\n";  // CSV头

    for (int row = 0; row < m_jsonTable->rowCount(); ++row) {
        out << m_jsonTable->item(row, 0)->text() << ","
            << "\"" << m_jsonTable->item(row, 1)->text().replace("\"", "\"\"") << "\","
            << m_jsonTable->item(row, 2)->text() << "\n";
    }

    file.close();
    qDebug() << "CSV导出成功:" << fileName;
    appendMessage("JSON数据已导出到: " + fileName, "system");
}


// 可选：表格点击事件（用于脚本交互）
void MainWindow::onTableCellClicked(int row, int column)
{
    if (column == 1) {  // 点击值列
        QString value = m_jsonTable->item(row, 1)->text();
        qDebug() << "选中值:" << value;  // 可emit信号给外部脚本
        // 示例：emit jsonValueSelected(value);
    }
}


QJsonObject MainWindow::extractFallbackFields(const QString &fixedResponse)
{
    QJsonObject obj;

    // 提取task_id（您的delimiter修正版）
    QRegularExpression reTask(R"re("task_id":\s*"([^"]+)")re");
    QRegularExpressionMatch match = reTask.match(fixedResponse);
    if (match.hasMatch()) obj["task_id"] = match.captured(1);

    // 提取timestamp
    QRegularExpression reTs(R"re("timestamp":\s*"([^"]+)")re");
    match = reTs.match(fixedResponse);
    if (match.hasMatch()) obj["timestamp"] = match.captured(1);

    // 提取suggestion.text
    QRegularExpression reSug(R"re("text":\s*"---/jl/建议：([^"]*)")re");
    match = reSug.match(fixedResponse);
    if (match.hasMatch()) {
        QJsonObject sug; sug["text"] = "---/jl/建议：" + match.captured(1);
        QJsonObject style; style["style"] = "jl";
        sug["style"] = style;
        obj["suggestion"] = sug;
    }

    // 提取raw_data_sources.content (取前5个)
    QRegularExpression reContent(R"re("content":\s*"([^"]+)")re");
    QRegularExpressionMatchIterator it = reContent.globalMatch(fixedResponse);
    QJsonArray sources;
    int count = 0;
    while (it.hasNext() && count < 5) {
        match = it.next();
        QJsonObject source; source["content"] = match.captured(1);
        sources.append(source);
        count++;
    }
    obj["raw_data_sources"] = sources;

    // 提取verification_hints.queries (增强: table/sql/purpose，取前3个)
    QRegularExpression reTable(R"re("table":\s*"([^"]+)")re");
    QRegularExpression reSql(R"re("sql":\s*"([^"]+)")re");
    QRegularExpression rePurpose(R"re("purpose":\s*"([^"]+)")re");

    QRegularExpressionMatchIterator itTable = reTable.globalMatch(fixedResponse);
    QRegularExpressionMatchIterator itSql = reSql.globalMatch(fixedResponse);
    QRegularExpressionMatchIterator itPurpose = rePurpose.globalMatch(fixedResponse);

    QJsonArray queries;
    count = 0;
    while (count < 3) {
        QJsonObject query;
        // 取table
        if (itTable.hasNext()) {
            QRegularExpressionMatch m = itTable.next();
            query["table"] = m.captured(1);
        }
        // 取sql
        if (itSql.hasNext()) {
            QRegularExpressionMatch m = itSql.next();
            query["sql"] = m.captured(1);
        }
        // 取purpose
        if (itPurpose.hasNext()) {
            QRegularExpressionMatch m = itPurpose.next();
            query["purpose"] = m.captured(1);
        }
        if (!query.isEmpty()) {
            queries.append(query);
            count++;
        } else {
            break;
        }
    }
    QJsonObject hints; hints["queries"] = queries;
    obj["verification_hints"] = hints;

    // 提取reasoning_logic
    QJsonObject logic;  // ← 关键：提前声明在外面！

    QRegularExpression reText("\"text\"\\s*:\\s*\"([^\"]+)\"");  // 去掉 lookahead，改用最稳全局匹配
    QRegularExpressionMatchIterator textIt = reText.globalMatch(fixedResponse);
    if (textIt.hasNext()) {
        QRegularExpressionMatch m = textIt.next();
        logic["text"] = m.captured(1);

        // 提取 graph（你已经写得非常好，保留）
        QRegularExpression reGraph("\"graph\"\\s*:\\s*\"([^\"]+)\"");
        QRegularExpressionMatchIterator graphIt = reGraph.globalMatch(fixedResponse);
        if (graphIt.hasNext()) {
            QRegularExpressionMatch gm = graphIt.next();
            logic["graph"] = gm.captured(1);
            qDebug() << "fallback 成功提取到 graph:" << gm.captured(1);
        }

        // 提取 confidence
        QRegularExpression reConf(R"re("confidence":\s*([\d.]+)")re");
        QRegularExpressionMatch confMatch = reConf.match(fixedResponse);
        if (confMatch.hasMatch()) {
            logic["confidence"] = confMatch.captured(1).toDouble();
        }

        // 只要提取到 text，就认为 reasoning_logic 有效
        obj["reasoning_logic"] = logic;
    }

    qDebug() << "fallback提取成功，键:" << obj.keys() << " sources:" << sources.size() << " queries:" << queries.size();
    return obj;
}


void MainWindow::validateAiResponseWithFallback(const QJsonObject &fallbackObj)
{
    if (!fallbackObj.contains("verification_hints")) {
        qDebug() << "fallback缺少verification_hints，跳过验证";
        appendMessage("验证跳过：缺少hints，但核心内容已提取", "system");  // 非error提示
        return;
    }
    QJsonObject hints = fallbackObj["verification_hints"].toObject();
    QJsonArray queries = hints["queries"].toArray();
    if (queries.isEmpty()) {
        appendMessage("fallback验证跳过：无queries", "system");
        return;
    }

    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        appendMessage("数据库连接失败，无法执行fallback验证", "error");
        return;
    }

    QStringList results;
    for (const QJsonValue &v : queries) {
        QJsonObject q = v.toObject();
        QString table = q["table"].toString();
        QString sql = q["sql"].toString();
        QString purpose = q["purpose"].toString();
        if (table.isEmpty() || sql.isEmpty() || purpose.isEmpty()) {
            results << QString("跳过无效查询 [%1]: 缺少table/sql/purpose").arg(purpose);
            continue;
        }
        QSqlQuery query(db);
        if (!query.exec(sql)) {
            results << QString("fallback验证失败 [%1]: %2\nSQL: %3")
                           .arg(purpose, query.lastError().text(), sql);
        } else {
            int count = 0;
            while (query.next()) {
                count++;
            }
            results << QString("fallback验证通过 [%1] → %2 条记录\nSQL: %3")
                           .arg(purpose).arg(count).arg(sql);
        }
    }
    if (!results.isEmpty()) {
        QString summary = results.join("\n");
        qDebug() << "Fallback验证 summary:\n" << summary;
        appendMessage("Fallback验证结果:\n<pre>" + summary + "</pre>", "system");
    } else {
        appendMessage("Fallback无验证查询", "system");
    }
}




// 在 MainWindow::onResponseReceived 最后，解析成功后调用这个函数
/*QString MainWindow::normalizeGraph(const QString& rawGraph)
{
    if (rawGraph.isEmpty()) return "empty_graph";

    QString g = rawGraph.simplified();

    // Step 1: 提取所有 table__field（最重要！这是你坚持可追溯的核心）
    QRegularExpression reField("(recent_logs|telescope_status|planned_targets)__[^\\s+,()<>]+");
    QStringList fields;
    QRegularExpressionMatchIterator i = reField.globalMatch(g);
    while (i.hasNext()) {
        QString f = i.next().captured();
        f.replace("recent_logs__", "rl__")
            .replace("telescope_status__", "ts__")
            .replace("planned_targets__", "pt__");
        fields << f.toLower();
    }

    // Step 2: 提取中间风险节点（支持 ->risk<- 和 -@risk<- 两种）
    QString riskNode;
    QRegularExpression reRisk("(->|-@)([^<]+)(<-)");
    QRegularExpressionMatch m = reRisk.match(g);
    if (m.hasMatch()) {
        riskNode = m.captured(2).simplified().toLower();
        riskNode.replace("high_background", "bg_high")
            .replace("satellite_interference", "sat_int")
            .replace("airmass", "airmass")
            .replace("moon", "moon");
    }

    // Step 3: SqlReason 部分也提取字段
    QRegularExpression reSql("SqlReason[^:]*:\\s*\\(([^)]+)\\)");
    m = reSql.match(g);
    if (m.hasMatch()) {
        QString sqlPart = m.captured(1);
        QRegularExpressionMatchIterator i2 = reField.globalMatch(sqlPart);
        while (i2.hasNext()) {
            QString f = i2.next().captured();
            f.replace("recent_logs__", "rl__")
                .replace("telescope_status__", "ts__")
                .replace("planned_targets__", "pt__");
            fields << f.toLower();
        }
    }

    // Step 4: 去重排序拼接
    fields.removeDuplicates();
    std::sort(fields.begin(), fields.end());
    QString normalized = fields.join("+");

    // Step 5: 如果有风险节点，加上
    if (!riskNode.isEmpty()) {
        normalized += "|risk:" + riskNode;
    }

    return normalized.isEmpty() ? "empty_graph" : normalized;
}
*/
QString MainWindow::normalizeGraph(const QString& rawGraph) {
    if (rawGraph.isEmpty()) return "empty_graph";
    QStringList atoms;
    QStringList chains = rawGraph.split(';', Qt::SkipEmptyParts);
    for (QString chain : chains) {
        chain = chain.simplified().toLower().remove('(').remove(')');
        // 泛化正则：支持任意__前缀或无前缀节点
        QRegularExpression re(R"(([a-z_]+__)?[^\s,<>]+)");
        QRegularExpressionMatchIterator i = re.globalMatch(chain);
        while (i.hasNext()) {
            QString atom = i.next().captured().trimmed();
            if (!atom.isEmpty() && atom != "-->" && atom != "&&") { // 排除纯符号
                atoms << atom;
            }
        }
    }
    atoms.removeDuplicates();
    std::sort(atoms.begin(), atoms.end());
    return atoms.join("+");
}

QString MainWindow::generateGraphHash(const ReasoningRecord& r)
{
    QString canonical = QString("%1|%2|%3")
    .arg(r.normalizedGraph)
        .arg(r.reasoningText.simplified().toLower())
        .arg(r.sourceContents.join("|"));  // 已经排序过

    return QString(QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}


void MainWindow::closeReasoningFeedbackLoop(const QJsonObject& outputJson, bool isFailed) {
    QJsonObject logicObj = outputJson.value("reasoning_logic").toObject();
    QString rawGraph = logicObj.value("graph").toString();
    // 1. 严格过滤垃圾graph
    if (rawGraph.isEmpty() || rawGraph.length() < 20 ) {
        qDebug() << "丢弃：graph无效或太短：---->"<<rawGraph;
        return;
    }

    // 失败时不写入知识库，只记录失败日志
    if (isFailed) {
        qDebug() << "验证失败，不写入知识库";
        logFailedReasoning(outputJson);  // 新增：记录到失败日志表（见下面实现）
        appendMessage("验证失败，已记录但不写入知识库", "system");

        return;  // 直接返回，不写入
    }

    ReasoningRecord rec;
    rec.reasoningText = logicObj.value("text").toString().trimmed();
    rec.suggestion = outputJson.value("suggestion").toObject().value("text").toString();
    qDebug()<<"rawGraph<<<<<<<<<<<<<<<<<"<<rawGraph;
    rec.normalizedGraph = normalizeGraph(rawGraph);
    rec.graph = rawGraph;
    // 2. 归一化后仍然无效 → 彻底丢弃
    if (rec.normalizedGraph.isEmpty() ||
        rec.normalizedGraph == "empty_graph" ||
        rec.normalizedGraph.length() < 10) {
        qDebug() << "丢弃：归一化后graph无效";
        return;
    }
    // sources 只用于更精确的hash
    QJsonArray sources = outputJson.value("raw_data_sources").toArray();
    for (const auto& v : sources)
        rec.sourceContents << v.toObject().value("content").toString();
    rec.sourceContents.sort();
    rec.graphHash = generateGraphHash(rec);

    qDebug()<<"***********"<<"rec.reasoningText:---*>"<<rec.reasoningText
             <<"rec.graphHash---*>"<<rec.graphHash<<"rec.graph---*>"<<rec.graph
             <<"rec.suggestion---*>"<<rec.suggestion<<"reviewed---*>"<<isFailed;

    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery q(db);
    // 3. 利用 UNIQUE 约束，一句 SQL 搞定所有逻辑
    q.prepare(R"(
        INSERT INTO reasoning_knowledge
        (graph_hash, reasoning_text, normalized_graph, suggestion, use_count, is_golden,graph)
        VALUES (:hash, :text, :norm_graph, :sug, 1, :golden, :gra)
        ON CONFLICT(graph_hash) DO UPDATE SET
            use_count = use_count + 1,
            last_used = CURRENT_TIMESTAMP,
            is_golden = :golden
    )");
    q.bindValue(":hash", rec.graphHash);
    q.bindValue(":text", rec.reasoningText);
    q.bindValue(":norm_graph", rec.normalizedGraph);
    q.bindValue(":sug", rec.suggestion);
    q.bindValue(":golden", isFailed ? 0 : 0);  // 根据你的业务逻辑调整：isFailed=true时设为0，false时也设为0（待审核）
    q.bindValue(":gra", rec.graph);

    if (!q.exec()) {
        qDebug() << "知识库插入/更新失败:" << q.lastError().text();
        qDebug() << "SQL:" << q.lastQuery();
        qDebug() << "Bound values:" << q.boundValues();
    } else {
        // 成功后判断是否需要提醒人工审核
        QSqlQuery cnt(db);
        cnt.prepare("SELECT use_count FROM reasoning_knowledge WHERE graph_hash = ?");
        cnt.addBindValue(rec.graphHash);
        cnt.exec();
        if (cnt.next() && cnt.value(0).toInt() >= 8) {
            appendMessage(QString("规则已命中 %1 次 → 建议人工确认为黄金规则").arg(cnt.value(0).toInt()), "system");
        }

        QStringList chains = rec.graph.split(';', Qt::SkipEmptyParts);
        for (QString rawChain : chains) {
            QString chain = rawChain.trimmed();
            if (chain.isEmpty()) continue;

            // 可选：轻度归一化（去除多余空格，统一大小写便于展示）
            QString displayChain = chain.simplified();

            QSqlQuery atomicQ(db);
            atomicQ.prepare(R"(
            INSERT INTO atomic_chains (chain_text, use_count)
            VALUES (:chain, 1)
            ON CONFLICT(chain_text) DO UPDATE SET
                use_count = use_count + 1,
                last_used = CURRENT_TIMESTAMP
        )");
            atomicQ.bindValue(":chain", displayChain);
            if (!atomicQ.exec()) {
                qDebug() << "原子链插入/更新失败:" << atomicQ.lastError().text();
            }
        }

    }


    // === 新增：高频 graph 标记与统计 ===
    QStringList chainList;
    for (QString rawChain : rec.graph.split(';', Qt::SkipEmptyParts)) {
        QString chain = rawChain.simplified();  // 与插入时的 displayChain 保持一致
        if (!chain.isEmpty()) {
            chainList << chain;
        }
    }

    bool allHighFreq = true;
    int cumulativeHits = 0;

    for (const QString& chain : chainList) {
        QSqlQuery chainQuery(db);
        chainQuery.prepare("SELECT use_count FROM atomic_chains WHERE chain_text = :chain");
        chainQuery.bindValue(":chain", chain);
        if (chainQuery.exec() && chainQuery.next()) {
            int uc = chainQuery.value(0).toInt();
            cumulativeHits += uc;
            if (uc <= 1) {
                allHighFreq = false;
            }
        } else {
            // 未找到该原子链 → 肯定不是高频
            allHighFreq = false;
        }
    }

    int incrementFullMatch = allHighFreq ? 1 : 0;

    // 更新 reasoning_knowledge 表
    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"(
    UPDATE reasoning_knowledge
    SET is_high_freq_graph = :is_high,
        atomic_cumulative_hits = :cum_hits,
        full_atomic_match_count = full_atomic_match_count + :inc_full
    WHERE graph_hash = :hash
)");
    updateQuery.bindValue(":is_high", allHighFreq ? 1 : 0);
    updateQuery.bindValue(":cum_hits", cumulativeHits);
    updateQuery.bindValue(":inc_full", incrementFullMatch);
    updateQuery.bindValue(":hash", rec.graphHash);

    if (!updateQuery.exec()) {
        qDebug() << "高频graph标记更新失败:" << updateQuery.lastError().text();
    } else {
        qDebug() << "高频graph标记更新成功: hash=" << rec.graphHash
                 << " allHighFreq=" << allHighFreq
                 << " cumulative=" << cumulativeHits
                 << " inc_full=" << incrementFullMatch;
    }


    refreshAtomicTable(); // 刷新原子统计表格
    m_statsManager->recordSuccess(outputJson);
    refreshKnowledgeTable(); // 立即刷新界面




}


//专家知识库
void MainWindow::refreshKnowledgeTable()
{
    QSqlQuery query("SELECT id, use_count, confidence, is_golden,is_high_freq_graph,atomic_cumulative_hits,full_atomic_match_count, "
                    "first_seen, last_used, reasoning_text, normalized_graph, note "
                    "FROM reasoning_knowledge ORDER BY use_count DESC, confidence DESC");

    m_knowledgeTable->setRowCount(0);
    int row = 0;
    while (query.next()) {
        m_knowledgeTable->insertRow(row);

        int col = 0;
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(0).toString()));
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(1).toString()));
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(QString::number(query.value(2).toDouble(), 'f', 3)));

        QTableWidgetItem *goldenItem = new QTableWidgetItem(query.value(3).toInt() ? "是" : "否");
        goldenItem->setBackground(query.value(3).toInt() ? QColor(255, 215, 0, 100) : Qt::white);
        goldenItem->setTextAlignment(Qt::AlignCenter);
        m_knowledgeTable->setItem(row, col++, goldenItem);

        // 高频Graph标记（新增）
        QTableWidgetItem *highFreqItem = new QTableWidgetItem(query.value(4).toInt() ? "是" : "否");
        highFreqItem->setBackground(query.value(4).toInt() ? QColor(255, 215, 0, 150) : Qt::white);
        highFreqItem->setTextAlignment(Qt::AlignCenter);
        m_knowledgeTable->setItem(row, col++, highFreqItem);

        // 累计原子命中次数（新增）
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(5).toString()));

        // 完整原子匹配次数（新增）
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(6).toString()));

        // 首次使用
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(7).toDateTime().toString("yyyy-MM-dd hh:mm")));

        // 末次使用
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(8).toDateTime().toString("yyyy-MM-dd hh:mm")));

        // 推理摘要
        QString text = query.value(9).toString();
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(text.left(50) + (text.length() > 50 ? "..." : "")));

        // 归一化Graph
        QString graph = query.value(10).toString();
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(graph.left(60) + (graph.length() > 60 ? "..." : "")));

        // 备注
        m_knowledgeTable->setItem(row, col++, new QTableWidgetItem(query.value(11).toString()));

        row++;
    }
    connect(m_btnRefreshKB, &QPushButton::clicked, this, [this]() {
        refreshKnowledgeTable();
        refreshAtomicTable();
    });
    appendMessage(QString("专家知识库已刷新，共 %1 条规则").arg(row), "system");
}

void MainWindow::onMarkAsGolden()
{
    auto rows = m_knowledgeTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        appendMessage("请先在专家知识库中选中一行规则", "system");
        return;
    }
    int id = m_knowledgeTable->item(rows[0].row(), 0)->text().toInt();
    QSqlQuery q; q.prepare("UPDATE reasoning_knowledge SET is_golden = 1 WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    refreshKnowledgeTable();
    appendMessage("已手动标记为黄金规则，下次调度将强制复用！", "system");
}

void MainWindow::onUnmarkGolden()
{
    auto rows = m_knowledgeTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    int id = m_knowledgeTable->item(rows[0].row(), 0)->text().toInt();
    QSqlQuery q; q.prepare("UPDATE reasoning_knowledge SET is_golden = 0 WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    refreshKnowledgeTable();
}

void MainWindow::onDeleteRule()
{
    auto rows = m_knowledgeTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    if (QMessageBox::question(this, "确认删除", "删除后无法恢复，确定吗？") != QMessageBox::Yes)
        return;

    int id = m_knowledgeTable->item(rows[0].row(), 0)->text().toInt();
    QSqlQuery q; q.prepare("DELETE FROM reasoning_knowledge WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    refreshKnowledgeTable();
    appendMessage("规则已删除", "system");
}

void MainWindow::onAddNote()
{
    auto rows = m_knowledgeTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    int id = m_knowledgeTable->item(rows[0].row(), 0)->text().toInt();
    QString current = m_knowledgeTable->item(rows[0].row(), 8)->text();
    bool ok;
    QString note = QInputDialog::getMultiLineText(this, "添加备注", "备注内容：", current, &ok);
    if (ok) {
        QSqlQuery q;
        q.prepare("UPDATE reasoning_knowledge SET note = ? WHERE id = ?");
        q.addBindValue(note);
        q.addBindValue(id);
        q.exec();
        refreshKnowledgeTable();
    }
}

void MainWindow::onValidationFailed(const QString &errorSummary) {
/* 取消下面注释会启用校验反馈回AI模型  */
    m_lastValidationFailure = errorSummary.left(130);  // 保存失败总结
    m_lastValidationFailure = m_lastValidationFailure.replace(QRegularExpression("校验失败"), "上次错误");

   // appendMessage("验证失败，已缓存反馈到下次自动调度: " + errorSummary, "error");
    qDebug()<<"验证失败，已缓存反馈到下次自动调度: " <<errorSummary<<"error";
    //QMessageBox::warning(this,"验证失败","验证失败,已缓存反馈到下次自动调度");

   // appendMessage("验证失败x: " + errorSummary, "error");
    // 可选：重试 AI（根据需要取消注释）
    m_aiClient->sendMessage(m_lastInputJson + "\n上次错误: " + errorSummary + "，请修正");

}



void MainWindow::endx()
{
    endflag = true;
    qDebug()<<"endflag send--------------------------------->receive";
}

// 在 mainwindow.cpp 中实现
void MainWindow::logFailedReasoning(const QJsonObject& json) {
    QSqlQuery q;
    q.prepare(R"(
        INSERT INTO failed_reasonings (timestamp, json_data, error_reason)
        VALUES (CURRENT_TIMESTAMP, :json, :error)
    )");
    q.bindValue(":json", QJsonDocument(json).toJson(QJsonDocument::Compact));
    q.bindValue(":error", "验证失败: " + m_validator->lastErrors().join("; "));  // 假设Validator有lastErrors()方法
    q.exec();
    // 创建表（在initDatabase中添加）
    // query.exec("CREATE TABLE IF NOT EXISTS failed_reasonings (id INTEGER PRIMARY KEY, timestamp DATETIME, json_data TEXT, error_reason TEXT)");
}


void MainWindow::runTestCase(int caseId)
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        appendMessage("数据库未打开，无法注入测试数据", "error");
        return;
    }

    // ============ 第一步：清理旧测试数据 ============
    QSqlQuery clean(db);
    clean.exec("DELETE FROM recent_logs WHERE id >= 10000");
    clean.exec("DELETE FROM telescope_status WHERE id >= 10000");
    clean.exec("DELETE FROM planned_targets WHERE id >= 10000");
    clean.exec("DELETE FROM reasoning_knowledge WHERE graph_hash LIKE 'test_%'"); // 可选：清理测试黄金规则

    appendMessage(QString("已清理旧测试数据，准备注入用例 #%1").arg(caseId), "system");

    // ============ 第二步：逐条执行注入 ============
    QSqlQuery q(db);
    bool success = true;

    switch (caseId) {
    case 1: // 正常月相干扰（应该通过）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10001, '2025-12-22 12:00:00', 'moon_phase', '月相位置 RA:18h32m DEC:+22°14′ 照度: 0.87')");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10002, '2025-12-22 12:00:01', 'target_pointing', '目标指向 RA:18h31m20s DEC:+22°10′15″')");
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10001, 'SN2025abc', '18h31m20s', '+22°10′15″', 9, 300, 'active')");

        appendMessage("注入：正常月相干扰（角距<5°，应通过验证并写入）", "system");
        break;

    case 2: // 大角距无干扰（AI若说干扰应失败）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10011, '2025-12-22 12:00:00', 'moon_phase', '月相位置 RA:05h00m DEC:-60°00′ 照度: 0.10')");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10012, '2025-12-22 12:00:01', 'target_pointing', '目标指向 RA:18h31m20s DEC:+22°10′15″')");
        appendMessage("注入：大角距无干扰（>30°，AI若说干扰应被拦截）", "system");
        break;

    case 3: // 边界角距8°（测试阈值）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10021, '2025-12-22 12:00:00', 'moon_phase', '月相位置 RA:19h00m DEC:+30°00′ 照度: 0.6')");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10022, '2025-12-22 12:00:01', 'target_pointing', '目标指向 RA:18h31m20s DEC:+22°10′15″')");
        appendMessage("注入：边界角距≈8°（应不触发干扰规则）", "system");
        break;

    case 4: // 幻觉pk_id（AI引用不存在的记录）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10031, '2025-12-22 12:00:00', 'moon_phase', '正常月相数据')");
        appendMessage("注入：仅一条真实记录（AI若引用 pk_id=99999 等假id应失败）", "system");
        break;

    case 5: // 非法坐标（RA>24h）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10041, '2025-12-22 12:00:00', 'target_pointing', '目标指向 RA:25h00m DEC:+22°10′15″')");
        appendMessage("注入：非法RA=25h（应触发 ra_valid_range 失败）", "system");
        break;

    case 6: // 遗漏卫星干扰（有记录但AI不提）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10051, '2025-12-22 12:00:00', 'satellite_interference', '卫星干扰: 轨迹穿越视场 数量: 3')");
        appendMessage("注入：有卫星干扰记录（AI若不提则通过，若提“存在卫星干扰”但未引用应被一致性规则拦截）", "system");
        break;

    case 7: // 非法滤镜
        success &= q.exec("INSERT INTO telescope_status (id, pointing_ra, pointing_dec, current_filter, update_time) "
                          "VALUES (10061, '18h31m20s', '+22°10′15″', 'invalid_filter_xyz', CURRENT_TIMESTAMP)");
        appendMessage("注入：非法滤镜 invalid_filter_xyz（应触发 filter_valid 失败）", "system");
        break;

    case 8: // verification_hints为空（无需注入数据）
        appendMessage("用例8：verification_hints为空（无需注入数据，测试模型是否生成hints）", "system");
        break;

    case 9: // 错误的SQL表名/字段（无需注入，依赖模型生成错SQL）
        appendMessage("用例9：AI生成错误的SQL（如错表名/字段），应在 executeVerificationHints 报错", "system");
        break;

    case 10: // 幻觉天气记录（无记录但AI提到）
        // 故意不注入任何 weather 记录
        appendMessage("注入：无天气记录（AI若说“云覆盖高”“强风”等应被干扰一致性规则拦截）", "system");
        break;

    case 11: // 异常设备、物理量值
        /*越限坐标范围，设备卡片也在异常区间内*/
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Survey', '25h00m', '+30°00′', 5, 600, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '38h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'unknow_filter', update_time = CURRENT_TIMESTAMP WHERE id = 1");


        // 注入多个警报（排序测试：高优先先，低后）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,priority,gcn_alert) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert1: GRB High at RA:15h00m DEC:-20°00′ Priority:10 Exptime:300 Visible Window:1 hour Scientific Value: Very High',10,1)");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority,gcn_alert) "
                          "VALUES (10001, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert2: LIGO Medium at RA:18h00m DEC:+40°00′ Priority:8 Exptime:400 Visible Window:2 hours Scientific Value: Medium',8,1)");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority,gcn_alert) "
                          "VALUES (10002, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert3: Minor at RA:20h00m DEC:+50°00′ Priority:6 Exptime:500 Visible Window:4 hours Scientific Value: Low',6,1)");




        //逻辑无关信号--环境信息
        //月相
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                        "VALUES (10021, '2025-12-28 16:00:00', 'moon_phase', '月相位置 RA:15h00m DEC:-20°00′ 照度: 0.6')");
        //无效滤光片
        success &= q.exec("INSERT INTO telescope_status (id, pointing_ra, pointing_dec, current_filter, update_time) "
                          "VALUES (10061, '18h31m20s', '+22°10′15″', 'invalid_filter_xyz', CURRENT_TIMESTAMP)");
        appendMessage("注入：多ToO竞争（AI应排序插入：先高优先/短窗口，最大化总价值）", "system");
        break;

    case 12: // 黄金规则强制复用（预插入一条知识库记录）
        success &= q.exec("INSERT INTO reasoning_knowledge (graph_hash, reasoning_text, normalized_graph, suggestion, use_count, is_golden) "
                          "VALUES ('test_hash_123', '测试推理文本', 'rl__moon_phase+rl__target_pointing|risk:moon_high', '建议调整曝光', 5, 1) "
                          "ON CONFLICT(graph_hash) DO NOTHING");
        appendMessage("注入：预插入一条黄金规则（相同graph时应复用，use_count+1）", "system");
        break;

    case 13:  // ToO 响应观测：模拟突发警报
        // 清理（已有，不需改）
        // 注入当前常规任务
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Galaxy Survey', '12h00m', '+30°00′', 5, 600, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '12h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'r',status = 1, update_time = CURRENT_TIMESTAMP WHERE id = 532");

        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10021, '2025-12-28 16:00:00', 'moon_phase', '月相位置 RA:15h00m DEC:-20°00′ 照度: 0.6')");
        // 注入突发警报（ToO）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,priority,gcn_alert) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert: GRB detected at RA:15h00m DEC:-20°00′ Priority:10 Exptime:300 Visible Window:2 hours Scientific Value: High (Time-Domain Event)','10',1)");
        appendMessage("注入：ToO 突发警报（应打断当前任务，插入高优先级观测，评估收益/成本比）", "system");
        break;

    case 14:  // 窗口已过的ToO（不应中断，测试保守拒绝）
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Survey', '12h00m', '+30°00′', 5, 600, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '12h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'r',status = 1, update_time = CURRENT_TIMESTAMP WHERE id = 1");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,priority,gcn_alert) "
                          "VALUES (10000, DATE_SUB(CURRENT_TIMESTAMP, INTERVAL 3 HOUR), 'gcn_alert', "  // 时间戳设为3小时前，模拟已过
                          "'GCN Alert: GRB at RA:15h00m DEC:-20°00′ Priority:10 Exptime:300 Visible Window:2 hours (已过，窗口关闭) Scientific Value: High',10,1)");
        //增加一些无关信号干扰

        appendMessage("注入：ToO窗口已过（AI不应建议中断，避免无效操作）", "system");
        break;

    case 15:  // 切换成本过高（坐标差大，slew time长）
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Survey', '00h00m', '+00°00′', 5, 600, 'active')");  // 当前坐标设为天顶附近
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '00h00m', pointing_dec = '+00°00′', "
                          "current_filter = 'r',status = 1, update_time = CURRENT_TIMESTAMP WHERE id = 1");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert: GRB at RA:12h00m DEC:-80°00′ Priority:9 Exptime:300 Visible Window:1.5 hours Scientific Value: High (但坐标差大，slew cost高)')");  // 坐标差~90°，切换慢
        appendMessage("注入：ToO切换成本过高（AI应评估slew time，不建议中断）", "system");
        break;

    case 16:  // 低优先级ToO（忽略警报，继续原计划）
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'High Value Routine', '12h00m', '+30°00′', 7, 600, 'active')");  // 原任务优先级较高（7）
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '12h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'r',status = 1, update_time = CURRENT_TIMESTAMP WHERE id = 1");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,priority,gcn_alert) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert: Minor Event at RA:15h00m DEC:-20°00′ Priority:4 Exptime:300 Visible Window:3 hours Scientific Value: Low',10,1)");  // 优先级低（4<原7）
        appendMessage("注入：低优先级ToO（AI应忽略，继续原任务）", "system");
        break;

    case 17:  // 多ToO竞争（多个警报，重排程排序插入）
        /*正常坐标范围，设备卡片也在合理区间内*/
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Survey', '12h00m', '+30°00′', 5, 600, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '12h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'r',status = 1, update_time = CURRENT_TIMESTAMP WHERE id = 1");
        /*越限坐标范围，设备卡片也在异常区间内*/
        /*success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Survey', '25h00m', '+30°00′', 5, 600, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '38h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'unknow_filter', update_time = CURRENT_TIMESTAMP WHERE id = 1");
        */

        // 注入多个警报（排序测试：高优先先，低后）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,priority,gcn_alert) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert1: GRB High at RA:15h00m DEC:-20°00′ Priority:10 Exptime:300 Visible Window:1 hour Scientific Value: Very High',10,1)");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority,gcn_alert) "
                          "VALUES (10001, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert2: LIGO Medium at RA:18h00m DEC:+40°00′ Priority:8 Exptime:400 Visible Window:2 hours Scientific Value: Medium',8,1)");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority,gcn_alert) "
                          "VALUES (10002, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert3: Minor at RA:20h00m DEC:+50°00′ Priority:6 Exptime:500 Visible Window:4 hours Scientific Value: Low',6,1)");




        //逻辑无关信号--环境信息
        //月相
        //success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
          //                "VALUES (10021, '2025-12-28 16:00:00', 'moon_phase', '月相位置 RA:15h00m DEC:-20°00′ 照度: 0.6')");
        //无效滤光片
        //success &= q.exec("INSERT INTO telescope_status (id, pointing_ra, pointing_dec, current_filter, update_time) "
        //                  "VALUES (10061, '18h31m20s', '+22°10′15″', 'invalid_filter_xyz', CURRENT_TIMESTAMP)");
        appendMessage("注入：多ToO竞争（AI应排序插入：先高优先/短窗口，最大化总价值）", "system");
        break;

    case 18: // 复杂环境下的高优先TOO（相关干扰增加） - 完善版
        // 先清理（您的原代码已有，不需改）

        // 注入当前中优先例行任务（作为基线）
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Galaxy Survey', '12h00m', '+30°00′', 6, 600, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '12h00m', pointing_dec = '+30°00′', "
                          "current_filter = 'r', status = 1,update_time = CURRENT_TIMESTAMP WHERE id = 1");

        // 注入多个高优先TOO警报（竞争排序：优先级10 > 8 > 7，窗口渐短，科学价值量化）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert1: GRB High at RA:15h00m DEC:-20°00′ Priority:10 Exptime:300 Visible Window:1.5 hours Scientific Value: Very High (Expected Papers: 5, Time-Critical)',10)");
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority) "
                          "VALUES (10001, datetime('now', '-30 minutes'), 'gcn_alert', "  // 早30min，窗口更紧迫
                          "'GCN Alert2: LIGO Medium at RA:18h00m DEC:+40°00′ Priority:8 Exptime:400 Visible Window:1 hours Scientific Value: Medium (Expected Papers: 2)',8)");
         success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content,Priority) "
                          "VALUES (10002, datetime('now', '-60 minutes'), 'gcn_alert', "  // 早1h，窗口较长但优先低
                          "'GCN Alert3: Minor Event at RA:20h00m DEC:+50°00′ Priority:7 Exptime:500 Visible Window:3 hours Scientific Value: Low (Expected Papers: 1)',7)");


        // 注入天气时间序列（渐变恶化：从好到坏，模拟动态）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10003, datetime('now', '-120 minutes'), 'weather', '云覆盖: 20% 湿度: 50% 风速: 3 m/s')");  // 2h前：好天气
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10004, datetime('now', '-60 minutes'), 'weather', '云覆盖: 40% 湿度: 65% 风速: 5 m/s')");  // 1h前：中等
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10005, CURRENT_TIMESTAMP, 'weather', '云覆盖: 60% 湿度: 85% 风速: 7 m/s')");  // 当前：渐恶化（湿度高风险结霜）

        // 注入大气条件序列（seeing渐差）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10006, datetime('now', '-60 minutes'), 'seeing', '大气抖动: 1.0 arcsec 温度: 15°C')");  // 好
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10007, CURRENT_TIMESTAMP, 'seeing', '大气抖动: 1.8 arcsec 温度: 18°C')");  // 当前：差

        // 注入飞机干扰（替换卫星：轨迹穿越，低空影响）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10008, CURRENT_TIMESTAMP, 'aircraft_interference', '飞机轨迹: 高度 10km 亮度 V=2.5 mag 持续 5s')");  // 当前干扰
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10009, datetime('now', '-30 minutes'), 'aircraft_interference', '飞机轨迹: 高度 8km 亮度 V=3.0 mag 持续 10s')");  // 早干扰

        // 注入光污染（新增：背景噪声，城市附近常见）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10010, CURRENT_TIMESTAMP, 'light_pollution', '光污染: 背景亮度 20 mag/arcsec² (中等城市光害)')");

        // 注入设备约束（相机冷却慢、湿度高风险结霜、指向抖动）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10011, CURRENT_TIMESTAMP, 'instrument_data', '滤镜切换中: 从 g 到 r 预计15s 相机温度: -45°C (冷却中，风险噪声增加)')");  // 忙碌+温度稍高
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10012, CURRENT_TIMESTAMP, 'instrument_data', '湿度高: 85% (风险结霜)')");  // 湿度约束

        // 注入旧月相（无关噪声）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10013, '2025-12-01 00:00:00', 'moon_phase', '月相位置 RA:00h00m DEC:+00°00′ 照度: 0.1')");

        // 注入10条无关噪声日志（历史垃圾数据，模拟日志洪水）
        {
            QStringList enumTypes = {"moon_phase", "weather", "seeing", "instrument_data"}; // 随机选有效 type
            for (int i = 1; i <= 15; ++i) {
                int randIndex = QRandomGenerator::global()->bounded(enumTypes.size());
                QString randType = enumTypes[randIndex];
                QString content;
                if (randType == "moon_phase") {
                    content = "月相位置 RA:random DEC:random 照度: random";
                } else if (randType == "weather") {
                    content = "云覆盖: random% 湿度: random% 风速: random m/s";
                } else if (randType == "seeing") {
                    content = "大气抖动: random arcsec 温度: random°C";
                } else if (randType == "instrument_data") {
                    content = "滤镜: random 相机温度: random°C";
                }
                success &= q.exec(QString("INSERT INTO recent_logs (id, timestamp, type, content) "
                                          "VALUES (11%1, '2024-11-01 00:00:00', '%2', '%3')").arg(i).arg(randType).arg(content));
            }
        }


        appendMessage("注入：复杂环境下的高优先TOO（完善版：多TOO竞争 + 天气/大气渐变 + 飞机/光污染/湿度/冷却约束 + 10条噪声）。AI应排序响应最高价值TOO，但评估干扰/成本。输入总量: ~25条", "system");
        break;

    case 19: // 多无关干扰下的低优先TOO（噪声数据增加）
        // 先注入原计划（中优先例行任务）
        success &= q.exec("INSERT INTO planned_targets (id, name, ra, dec, priority, exptime, status) "
                          "VALUES (10000, 'Routine Survey', '18h00m', '+40°00′', 6, 900, 'active')");
        success &= q.exec("UPDATE telescope_status SET pointing_ra = '18h00m', pointing_dec = '+40°00′', "
                          "current_filter = 'i', status = 1,update_time = CURRENT_TIMESTAMP WHERE id = 1");

        // 注入低优先TOO警报（窗口长但优先低，成本高）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10000, CURRENT_TIMESTAMP, 'gcn_alert', "
                          "'GCN Alert: Minor LIGO Event at RA:15h00m DEC:+20°00′ Priority:4 Exptime:500 Visible Window:4 hours Scientific Value: Low (Non-Critical)')");

        // 增加大量不相关噪声：旧日志、垃圾数据、历史卫星/天气
        {
            QStringList enumTypes = {"moon_phase", "weather", "seeing", "instrument_data"}; // 随机选有效 type
            for (int i = 1; i <= 15; ++i) {
                int randIndex = QRandomGenerator::global()->bounded(enumTypes.size());
                QString randType = enumTypes[randIndex];
                QString content;
                if (randType == "moon_phase") {
                    content = "月相位置 RA:random DEC:random 照度: random";
                } else if (randType == "weather") {
                    content = "云覆盖: random% 湿度: random% 风速: random m/s";
                } else if (randType == "seeing") {
                    content = "大气抖动: random arcsec 温度: random°C";
                } else if (randType == "instrument_data") {
                    content = "滤镜: random 相机温度: random°C";
                }
                success &= q.exec(QString("INSERT INTO recent_logs (id, timestamp, type, content) "
                                          "VALUES (100%1, '2025-11-01 00:00:00', '%2', '%3')").arg(i).arg(randType).arg(content));
            }
        }
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10016, '2025-11-15 12:00:00', 'satellite_interference', '1条卫星轨迹穿越')");

        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10017, '2025-11-20 18:00:00', 'weather', '云覆盖:10% 风速:2 m/s')");

        // 增加少量相关：当前设备正常（不干扰）
        success &= q.exec("INSERT INTO recent_logs (id, timestamp, type, content) "
                          "VALUES (10018, CURRENT_TIMESTAMP, 'instrument_data', '设备正常: 滤镜 i 相机温度: -60°C')");

        appendMessage("注入：多无关干扰下的低优先TOO（噪声增加：15+条旧日志/垃圾 + 旧卫星/天气）。AI不应响应TOO，但可能被噪声误导。输入总量: ~20条", "system");
        break;

    default:
        appendMessage(QString("未知测试用例ID: %1").arg(caseId), "error");
        return;
    }

    if (!success && caseId != 8 && caseId != 9 && caseId != 10) {
        appendMessage("测试数据注入失败: " + q.lastError().text(), "error");
    } else {
        appendMessage(QString("测试用例 #%1 注入完成，请点击发送或等待自动调度").arg(caseId), "system");
    }
}


void MainWindow::refreshAtomicTable()
{
    QSqlQuery query("SELECT chain_text, use_count, first_seen, last_used "
                    "FROM atomic_chains "
                    "ORDER BY use_count DESC, last_used DESC "
                    "LIMIT 100");  // 显示前100条最热门的

    m_atomicTable->setRowCount(0);
    int row = 0;
    while (query.next()) {
        m_atomicTable->insertRow(row);
        m_atomicTable->setItem(row, 0, new QTableWidgetItem(query.value(0).toString()));
        m_atomicTable->setItem(row, 1, new QTableWidgetItem(query.value(1).toString()));

        QTableWidgetItem *countItem = m_atomicTable->item(row, 1);
        countItem->setTextAlignment(Qt::AlignCenter);
        if (query.value(1).toInt() >= 10) {  // 高频高亮
            countItem->setBackground(QColor(255, 200, 100, 150));
        }

        m_atomicTable->setItem(row, 2, new QTableWidgetItem(
                                           query.value(2).toDateTime().toString("yyyy-MM-dd hh:mm")));
        m_atomicTable->setItem(row, 3, new QTableWidgetItem(
                                           query.value(3).toDateTime().toString("yyyy-MM-dd hh:mm")));
        row++;
    }
    appendMessage(QString("原子规则统计已刷新，共 %1 条高频链").arg(row), "system");
}


void MainWindow::recheck_Aistart(QString recv_text)
{
    if(recv_text == "超时结束（可能输出不完整）")
    {
        onAutoSchedule();
        QTimer::singleShot(6000,this,&MainWindow::onAutoSchedule);
    }
}
