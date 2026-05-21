#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include <QSqlError>




void initDatabase()
{
    // 1. 数据库路径：程序同目录下的 telescope.db
    QString dbPath = QCoreApplication::applicationDirPath() + "/telescope.db";
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qCritical() << "无法打开数据库:" << db.lastError().text();
        return;
    }

    QSqlQuery query;

    // 2. 创建 recent_logs 表
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS recent_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            tag TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            type TEXT,
            content TEXT
        )
    )");

    // 3. 创建 telescope_status 表
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS telescope_status (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            pointing_ra TEXT,
            pointing_dec TEXT,
            current_filter TEXT,
            update_time DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");

    // 4. 创建 planned_targets 表
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS planned_targets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            ra TEXT,
            dec TEXT,
            priority INTEGER,
            exptime INTEGER,
            status TEXT DEFAULT 'active',
            schedule_time DATETIME
        )
    )");

    query.exec(R"(
    CREATE TABLE IF NOT EXISTS reasoning_knowledge (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        graph_hash TEXT NOT NULL UNIQUE,           -- 唯一约束，相同逻辑只存一条
        first_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
        last_used DATETIME DEFAULT CURRENT_TIMESTAMP,
        use_count INTEGER DEFAULT 1,               -- 命中次数（核心指标）
        reasoning_text TEXT NOT NULL,              -- 推理描述（人工阅读用）
        normalized_graph TEXT NOT NULL,            -- 归一化后的graph（用于唯一性判断）
        suggestion TEXT NOT NULL,                  -- 最终建议文本
        is_golden INTEGER DEFAULT 0,               -- 0=待审核, 1=人工确认为黄金规则
        manual_reviewed INTEGER DEFAULT 0,         -- 新增：人工审核状态 0=未审, 1=确认正确, -1=确认错误
        note TEXT,                                 -- 人工备注
        CONSTRAINT unique_normalized_graph UNIQUE(normalized_graph)  -- 双保险：归一化graph也唯一
    );
)");

    // 在 initDatabase() 函数末尾添加
    query.exec("ALTER TABLE reasoning_knowledge ADD COLUMN is_high_freq_graph INTEGER DEFAULT 0");
    query.exec("ALTER TABLE reasoning_knowledge ADD COLUMN atomic_cumulative_hits INTEGER DEFAULT 0");
    query.exec("ALTER TABLE reasoning_knowledge ADD COLUMN full_atomic_match_count INTEGER DEFAULT 0");


    // 索引加速查询
    query.exec("CREATE INDEX IF NOT EXISTS idx_hash ON reasoning_knowledge(graph_hash)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_use_count ON reasoning_knowledge(use_count DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_golden ON reasoning_knowledge(is_golden)");
    //  清空表（删除所有记录，但保留表结构）
    query.exec("DELETE FROM recent_logs");
    query.exec("DELETE FROM telescope_status");
    query.exec("DELETE FROM planned_targets");
    query.exec("DELETE FROM reasoning_knowledge");

    query.exec(R"(
    CREATE TABLE IF NOT EXISTS atomic_chains (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        chain_text TEXT NOT NULL UNIQUE,        -- 单条原子链原文（已trim）
        normalized_chain TEXT,                  -- 预留：归一化版本（后续如需更严格唯一性）
        use_count INTEGER DEFAULT 1,
        first_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
        last_used DATETIME DEFAULT CURRENT_TIMESTAMP
    )
)");

    // 索引加速高频查询
    query.exec("CREATE INDEX IF NOT EXISTS idx_atomic_use ON atomic_chains(use_count DESC)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_atomic_chain ON atomic_chains(chain_text)");


    // 5. 插入模拟数据（首次运行）
    if (query.exec("SELECT COUNT(*) FROM recent_logs") && query.next() && query.value(0).toInt() == 0) {
        query.exec(R"(INSERT INTO recent_logs (tag, timestamp, type, content) VALUES
            ('a', '2025-11-11 20:00:00', 'moon_phase', '月相位置 RA:18h32m DEC:+22°14′ 照度: 0.87'),
            ('b', '2025-11-11 20:00:01', 'target_pointing', '目标指向 RA:18h31m20s DEC:+22°10′15″')
        )");

        query.exec(R"(INSERT INTO telescope_status (pointing_ra, pointing_dec, current_filter) VALUES
            ('18h31m20s', '+22°10′15″', 'r')
        )");

        query.exec(R"(INSERT INTO planned_targets (name, ra, dec, priority, exptime, schedule_time) VALUES
        ('SN2025abc', '18h31m20s', '+22°10′15″', 9, 300, '2025-11-14 22:00:00')
        )");
    }

    // 插入3条垃圾记录（无效pk_id/content，tag='g'用于推理）
    if (query.exec(R"(INSERT INTO recent_logs (tag, timestamp, type, content) VALUES
        ('g1', '2025-11-14 11:00:00', 'moon_phase', '垃圾月相: RA:999h DEC:+999° 照度:999%'),  -- 无效坐标
        ('g2', '2025-11-14 11:01:00', 'target_pointing', '垃圾指向: 随机噪声 RA:abc DEC:xyz'),  -- 非数字
        ('g3', '2025-11-14 11:02:00', 'schedule', '垃圾计划: 无效目标 pk_id:999 exptime:-100s')  -- 负值
    )")) {
        qDebug() << "垃圾数据插入成功，触发自动调度测试";


    }



        query.exec(R"(INSERT INTO recent_logs (tag, timestamp, type, content) VALUES
            ('c', '2025-11-14 11:30:00', 'weather', '云覆盖: 30% 湿度: 65% 风速: 5 m/s'),  -- 轻微云干扰
            ('s', '2025-11-14 11:31:00', 'seeing', '大气抖动: 1.2 arcsec 温度: 15°C'),  -- 中等seeing
            ('sat', '2025-11-14 11:32:00', 'satellite_interference', '卫星干扰: 轨迹穿越视场 数量: 3 亮度: V=4.5 mag'),  -- 卫星反射光
            ('air', '2025-11-14 11:33:00', 'aircraft_interference', '飞机轨迹: 高度 10km 亮度 V=2.5 mag 持续 5s')  -- 新增：飞机干扰
        )");
        qDebug() << "已添加5条干扰模拟数据到 recent_logs";


    qDebug() << "SQLite 数据库初始化完成：" << dbPath;
}


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    initDatabase();  // 自动建库建表
    // 设置应用程序名称和版本
    app.setApplicationName("AI观测助手");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("本地Qwen AI 模型--lijiangzhan");

    // 强制使用深色主题
    app.setStyle(QStyleFactory::create("Fusion"));

    // 设置深色调色板
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    app.setPalette(darkPalette);

    // 设置样式表
    app.setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");

    MainWindow window;
    window.show();

    return app.exec();
}
