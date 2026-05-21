#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "aiinputbuilder.h"
#include <QJsonParseError>
#include <QTableWidget>
#include "validator.h"
#include <QComboBox>

#include "statisticsmanager.h"

class QTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class AIClient;


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    struct ReasoningRecord {
        QString taskId;
        QString timestamp;
        QString suggestion;                 // 最终建议文本
        QString reasoningText;              // reasoning_logic.text
        QString normalizedGraph;            // 归一化后的 graph（关键！）
        double  confidence;
        QStringList sourceContents;         // raw_data_sources 的 content 列表（已排序）
        QStringList verificationSqls;       // verification_hints.queries.sql（已排序）
        QString graphHash;                  // 最终用于对比的哈希
        QString graph;
    };

    void enableTestMode(int caseId);
private slots:
    void onSendClicked();
    void onResponseReceived(const QString &response);
    void onErrorOccurred(const QString &error);
    void onStatusChanged(const QString &status);
    //void updateOrAddAiMessage(const QString &response, const QString &messageId);
    void updateLastAiMessage(const QString &response);
    void onResetClicked();


    void onAutoSchedule();
    void onAutoToggle(bool checked);

    void recheck_Aistart(QString recv_text);

    QJsonObject extractFallbackFields(const QString &fixedResponse);  // 新函数声明

    void validateAiResponseWithFallback(const QJsonObject &fallbackObj);

    QString generateGraphHash(const ReasoningRecord& r);

    void onValidationFailed(const QString &errorSummary);

    void runTestCase(int caseId);

    void endx();

    void startBatchTest();            // 启动批量测试




private:
    void setupUI();
    void setupConnections();
    void appendMessage(const QString &message, const QString &type);

    AIClient *m_aiClient;
    AIInputBuilder *m_inputBuilder ;

    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    QTextEdit *m_chatDisplay;
    QHBoxLayout *m_inputLayout;
    QLineEdit *m_inputEdit;
    QPushButton *m_sendButton;
    QPushButton *m_stopButton;
    QLabel *m_statusLabel;
    QPushButton *m_resetButton;


    QString m_lastAiResponse;


    QTimer *m_autoTimer;

    QPushButton *m_autoToggleButton;  // ← 新增
    bool m_autoRunning = false;

    void validateAiResponse(const QString &jsonStr);



    QTableWidget* m_jsonTable;  // 新增：JSON表格
    QPushButton* m_exportButton;  // 新增：导出按钮

    //void populateJsonTable(const QJsonObject& root);  // 新增：填充表格函数
    void exportToCSV(const QString& filename = "json_export.csv");  // 新增：导出CSV

    void onTableCellClicked(int row, int column);

    QString m_lastInputJson;  // 新增：缓存上次输入JSON字符串

    void populateJsonTable(const QString& inputJsonStr, const QJsonObject& outputJson);  // 更新：支持输入对比

    QString normalizeGraph(const QString& rawGraph);
    void closeReasoningFeedbackLoop(const QJsonObject& outputJson, bool isFailed);

    //专家知识库
    QTableWidget *m_knowledgeTable;      // 专家知识库表格
    QPushButton *m_btnRefreshKB;         // 刷新
    QPushButton *m_btnMarkGolden;        // 标记为黄金规则
    QPushButton *m_btnUnmarkGolden;      // 取消黄金
    QPushButton *m_btnDeleteRule;        // 删除规则
    QPushButton *m_btnAddNote;           // 添加备注

    void setupKnowledgeBaseUI();         // 新增：初始化知识库界面
    void refreshKnowledgeTable();        // 刷新表格数据
    void onMarkAsGolden();               // 标记黄金
    void onUnmarkGolden();               // 取消黄金
    void onDeleteRule();                 // 删除
    void onAddNote();                    // 添加备注

    Validator *m_validator = nullptr ;
    int num_length=0;


    QString m_responseAccumulator;  // 累积AI响应，用于检测完整
    bool m_responseProcessed = false;  // 防重复后处理

    QComboBox *m_testCombo;  // 测试用例下拉框

    bool m_responseHasEnded = false;

    bool endflag = false;

    void logFailedReasoning(const QJsonObject& json);

    StatisticsManager *m_statsManager;   // 统计管理器


    // 批量测试成员
    QPushButton *m_batchTestBtn;      // 批量测试按钮
    bool m_batchMode = false;         // 是否批量模式
    int m_batchCurrentId = 0;         // 当前用例索引
    int m_batchRunCount = 0;          // 当前用例运行次数
    QList<int> m_batchCases = {1,2,3,4,5,6,7,8,9,10,11,12};  // 用例列表

    QPushButton *exportStatsBtn;// 批量导出统计按钮

    int m_autoRunCount = 0;          //普通自动调度已运行次数
    const int m_autoMaxRuns = 100;   // 最多运行100次

    QString m_lastValidationFailure; //缓存上次验证失败总结

    void refreshAtomicTable();

    QTableWidget *m_atomicTable;


};

#endif // MAINWINDOW_H
