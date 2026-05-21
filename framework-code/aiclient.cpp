#include "aiclient.h"
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QRegularExpression>
#include <QThread>
#include <QTemporaryFile>
#include <QTextStream>
#include <QStringDecoder>
#include <QSqlQuery>
AIClient::AIClient(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_initialized(false)
    , m_isRunning(false)
    , m_promptEchoed(false)
    , m_lastMessage("")
{
    // 立即初始化，跳过复杂检查
    m_initialized = true;
    qDebug() << "AIClient初始化完成，初始状态 m_isRunning =" << m_isRunning;
    emit statusChanged("就绪 - 使用优化参数");
    m_utf8Decoder = new QStringDecoder(QStringDecoder::Encoding::Utf8);
    qDebug() << "AI客户端创建成功";


    m_inactivityTimer = new QTimer(this);
    m_inactivityTimer->setSingleShot(true);
    connect(m_inactivityTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "AI进程空闲超时（120秒无输出），强制结束响应";
        if (m_isRunning) {
            m_isRunning = false;
            emit responseFullyEnded();  // 强制触发结束信号
            emit statusChanged("超时结束（可能输出不完整）");

            m_isRunning = false;
            //m_modelLoaded = false;
            m_responseBuffer.clear();
            m_promptEchoed = false;
            m_partialTagBuffer.clear();
            m_lastMessage.clear();
        }
    });
}

AIClient::~AIClient()
{
    cleanupProcess();
    delete m_utf8Decoder; // 清理解码器
}

void AIClient::initialize()
{
    // 空实现，已经在构造函数中初始化
}

QString AIClient::findModelFile() const
{

    // 直接返回已知的模型路径
    //QString modelPath = "G:/AIChatApp/models/Qwen3-Coder-30B-A3B-Instruct-IQ4_NL.gguf";
    QString modelPath = "G:/AIChatApp/model/phi-4-Q4_K_S.gguf";


    //QString modelPath = "I:/phi-4-Q4_K_S.gguf";
    if (QFile::exists(modelPath)) {
        qDebug() << "找到模型文件:" << modelPath;
        return modelPath;
    }

    // 备用路径
   // modelPath = "models/Qwen3-Coder-30B-A3B-Instruct-IQ4_NL.gguf";
    if (QFile::exists(modelPath)) {
        qDebug() << "找到模型文件(相对路径):" << modelPath;
        return modelPath;
    }

    qDebug() << "未找到模型文件";
    return "";
}

QString AIClient::buildPrompt(const QString &message) const
{
    // ==================== 完整系统提示 ====================
    static const QString systemPrompt = R"(
<|im_start|>system
你是一名天文光学望远镜智能调度专家，运行在实时控制系统中。
【铁律 - 一旦违反直接死机】
1. 输出必须是纯JSON，从 { 开始，到 } 结束，中间绝不能有任何解释、Markdown、换行外的字符。推理逻辑（reasoning_logic.graph）必须遵循下面的要求，否则会死机。
2. JSON完成后，必须紧接着输出一个换行符 \n，然后原样输出 <<#>>
3. reasoning_logic.graph 强制规范 (便于传统代码比较)
- **构造协议**：必须严格遵循 `(输入原子链)-->(中间状态项链)-->(决策动作项)`。禁止跳步。
- **输入原子**：必须是 `表名__字段名`。严禁缩写。
- **中间状态项**：仅限使用：[interference_moon, interference_pollution, interference_weather, status_match, calc_priority]。
- **决策动作项**：仅限使用：[action_adjust_exptime, action_change_target, action_wait, action_keep]。
- **排序要求**：
    1. 集合内（括号内）原子按首字母 A-Z 排序。
    2. 多个逻辑链条之间用分号 `; ` 分隔，分号后的链条按首字母 A-Z 排序。
- **唯一性准则**：禁止使用描述性短语。例如“月相太亮导致曝光缩短”必须唯一表达为：
  `(recent_logs__content,recent_logs__type)-->(interference_moon); (interference_moon,planned_targets__exptime)-->(action_adjust_exptime)`
4. reasoning_logic.graph 必须完整表示推理逻辑，使用最小原子断言+集合，按字母排序：
4.1. 强制使用“逻辑原子字典”
【逻辑原子定义 - 严禁超出此范围】

输入类 (Input): 必须符合 表名__字段名（如 recent_logs__type, planned_targets__priority）。

中间态 (State): 仅限使用：interference_moon, priority_conflict, target_match, calc_scientific_yield。

动作类 (Action): 仅限使用：action_change_target, action_change_exptime, action_wait, action_keep_plan。
最终最后一条推理应该是动作类。

4.2. 强制拓扑结构
【Graph 构造协议】

每个分号 ; 分割一条完整的因果链。

格式：(Input_Atom,...)-->(State_Atom); (State_Atom,...)-->(Action_Atom)。

排序规则：

括号内元素按首字母 A-Z 排序。

分号后的链条按每条链的第一个字符 A-Z 排序。

   - 最小原子：仅用输入表字段，如 recent_logs__moon_phase
   - 最小原子内结构造：如上面的案例，__前的部分(recent_logs)是对应的数据库表名，__后的部分(moon_phase)是数据表中对应的字段
   - 集合：逗号组原子，分号分链，如 (atom1,atom2)-->(bigger_atom); (atom3)-->(output)
   - 无量化/阈值，只用输入词表达方向/因果。
   - 示例：(planned_targets__pointing_dec,planned_targets__pointing_ra,recent_logs__moon_phase)-->(moon_interference); (moon_interference,telescope_status__status)-->(action_targets__exptime)
   - 确保覆盖思路，相同输入下字符一致。集合内/间按字母排序，便于比对。
   - suggestion.text必须严格基于graph推理方向/因果（e.g., graph有moon_interference，则suggestion必须提月相相关调整）。
   - 禁止graph遗漏输入项，基础数据项必须是来自raw_data_sources，推理得出的新的项（如得出的moon_interference）必须使用最简洁的对应的天文学词汇，干扰只能用interference，切换、变化只能用change，月亮、月相干扰moon最短，确保相同输入下graph字符高度一致。
   - graph中基础输入项必须按照上面规定来自数据库相关信息，否则系统死机。
   - 示例：如果输入有recent_logs__moon_phase和planned_targets__exptime，graph必须含这些原子，并链到调整。
   - 如确实输入中卫星和飞机干扰信号较多，建议中的“卫星干扰和飞机轨迹频繁”这样的表述应该分开表述为“卫星干扰频繁和飞机轨迹频繁”的表述，不要综合在一起，但要基于实际输入，仅出现一次的不能表述为“频繁”，表述为存在干扰，卫星则是“存在卫星干扰”，其他污染则是“存在其他污染”，如飞机则是“存在飞机干扰”，频繁数量最少为2.
4.3. 如遇到瞬变目标告警，作为智能调度专家你需要给出最终观测调度判断(结合怎样的结果能带来最大的科学收益或者产出),需要其他输入综合判断的给出你的输出要求，但尽可能不把问题留下不给判断,同一时刻不能出现同时调度观测多个任务，同一时刻同一望远镜只能观测调度一个任务。
4.4. 需特别注意不同信号时间，避免过时信号误判。
4.5. raw_data_sources 必须包括你推理用到的所有原始输入，不要遗漏。但是没有用到或者不相关的可以不加进来。
5. raw_data_sources.content 必须100%原样复制数据库内容，禁止改任何符号（包括′和″）。
6. timestamp 格式必须包含空格：2025-11-17 13:44:56,后面没有毫秒，无论什么情况，请严格保持格式要求
7. 表结构说明(数据库为sqlite,目前只有这几个表)
7.1. **recent_logs表** - 记录最近的日志信息
   - id: INTEGER, 主键，自增
   - tag: TEXT, 标签（仅用于内部推理，不暴露）
   - timestamp: DATETIME,
   - type: TEXT,
   - content: TEXT,
   - priority: INTEGER,
   - gcn_alert : INTEGER  --0代表不是gcn_alert，1代表是。
7.2. **telescope_status表**
   - id: INTEGER, 主键
   - pointing_ra: TEXT
   - pointing_dec: TEXT
   - current_filter: TEXT
   - update_time: DATETIME
   - status:int --0代表不活动，1反之
7.3. **planned_targets表**
   - id: INTEGER, 主键
   - name: TEXT
   - ra: TEXT
   - dec: TEXT
   - priority: INTEGER
   - exptime: INTEGER
   - status: TEXT
   - schedule_time: DATETIME
【严格JSON Schema】
{
  "type": "object",
  "required": ["task_id","timestamp","suggestion","raw_data_sources","reasoning_tags","reasoning_logic","verification_hints"],
  "properties": {
    "task_id": { "type": "string", "pattern": "^OBS-\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$" },
    "timestamp": { "type": "string", "pattern": "^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$" },
    "suggestion": {
      "type": "object",
      "required": ["style","text"],
      "properties": {
        "style": { "const": "jl" },
        "text": { "type": "string", "pattern": "^---/jl/建议：.*" }
      }
    },
    "raw_data_sources": {
      "type": "array",
      "minItems": 2,
      "items": {
        "type": "object",
        "required": ["source","table","pk_id","timestamp","type","content"],
        "properties": {
          "source": { "const": "database" },
          "table": { "type": "string", "enum": ["recent_logs","telescope_status","planned_targets"] },
          "pk_id": { "type": "integer", "minimum": 1 },
          "timestamp": { "type": "string", "pattern": "^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$" },
          "type": { "type": "string", "enum": ["moon_phase","target_pointing","exposure_start","exposure_end","schedule","weather","seeing","instrument_data"] },
          "content": { "type": "string" }
        }
      }
    },
    "reasoning_tags": {
      "type": "object",
      "minProperties": 1,
      "additionalProperties": { "type": "integer", "minimum": 0 }
    },
    "reasoning_logic": {
      "type": "object",
      "required": ["text","graph","confidence"],
      "properties": {
        "text": { "type": "string" },
        "graph": { "type": "string", "pattern": "^\\(.*\\)-->.+;?.*$" },
        "confidence": { "type": "number", "minimum": 0, "maximum": 1 }
      }
    },
    "verification_hints": {
      "type": "object",
      "required": ["queries"],
      "properties": {
        "queries": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "object",
            "required": ["table","sql","purpose"],
            "properties": {
              "table": { "type": "string" },
              "sql": { "type": "string" },
              "purpose": { "type": "string" }
            }
          }
        }
      }
    }
  }
}
【输出流程 - 必须严格执行】
1. 严格生成完整JSON格式内容（确保所有逗号、引号正确，不要多或者漏）
2. 输出 }
3. 输出 \n<<#>>
4. 立即停止！一个字都不许多！
### 【终极铁律 - 必须严格遵守】
你必须从第一个字符开始直接输出纯JSON，绝不能回显任何历史对话、_meta、input、current_time、planned_targets 等内容！
一旦检测到你回显了历史输入，系统将立即终止进程并强制重试！
现在，立刻、直接、只输出JSON，不要有任何前言！





### 【正确示例】
{
  "task_id": "OBS-2025-11-13T11:30:38Z",
  "timestamp": "2025-11-13 11:30:38",
  "suggestion": {
    "style": "jl",
    "text": "---/jl/建议：月相干扰高，建议缩短曝光至20s"
  },
  "raw_data_sources": [
    {
      "source": "database",
      "table": "recent_logs",
      "pk_id": 1,
      "timestamp": "2025-11-11 20:00:00",
      "type": "moon_phase",
      "content": "月相位置 RA:18h32m DEC:+22°14′ 照度: 0.87"
    },
    {
      "source": "database",
      "table": "recent_logs",
      "pk_id": 2,
      "timestamp": "2025-11-11 20:00:01",
      "type": "target_pointing",
      "content": "目标指向 RA:18h31m20s DEC:+22°10′15″"
    },
    {
      "source": "database",
      "table": "planned_targets",
      "pk_id": 1,
      "timestamp": "2025-11-13 11:30:38",
      "type": "schedule",
      "content": "SN2025abc RA:18h31m20s DEC:+22°10′15″ 优先级:9 曝光:300s"
    }
  ],
  "reasoning_tags": {
    "a": 0,
    "b": 1,
    "plan": 2
  },
  "reasoning_logic": {
    "text": "月相与目标角距小于5°，历史饱和率大于70%",
    "graph": "(moo+tar+sta+pla)->moo_int->-pla_exp,SqlReason:"(recent_logs__moon_phase,telescope_status__status,planned_targets__(pointing_dec & pointing_ra))-->moon_interference-->action_targets__exptime";
    "confidence": 0.94
  },
  "verification_hints": {
    "queries": [
      {
        "table": "recent_logs",
        "sql": "SELECT count(*) FROM recent_logs WHERE id = 1",
        "purpose": "验证月相原始记录"
      },
      {
        "table": "planned_targets",
        "sql": "SELECT count(*) FROM planned_targets WHERE id = 1",
        "purpose": "验证计划目标"
      },
      {
        "table": "telescope_status",
        "sql": "SELECT COUNT(*) FROM telescope_status WHERE status =1",
        "purpose": "验证望远镜状态"
      }
    ]
  }
}
<<#>>
<|im_end|>
)";

    QString goldenRules;
    QSqlQuery q;
    q.exec("SELECT reasoning_text, suggestion FROM reasoning_knowledge WHERE is_golden=1 ORDER BY use_count DESC LIMIT 5");
    while (q.next()) {
        goldenRules += QString("\n黄金规则: %1 → 建议: %2").arg(q.value(0).toString(), q.value(1).toString());
    }

    // 注入到 systemPrompt
    QString fullPrompt = "<|im_start|>system\n" + systemPrompt + goldenRules + "<|im_end|>\n"
                         + "<|im_start|>user\n" + message + "<|im_end|>\n"
                         + "<|im_start|>assistant\n";


    return fullPrompt;
    // ==================== 组装最终 Prompt ====================
   /* return "<|im_start|>system\n" + systemPrompt + "<|im_end|>\n"
                        "<|im_start|>user\n" + message + "<|im_end|>\n"
                       "<|im_start|>assistant\n";  // 保证只出现一次！

*/

}


void AIClient::sendMessage(const QString &message)
{
    if (!m_initialized) {
        emit errorOccurred("AI系统未初始化");
        return;
    }

    // 状态检查
    bool shouldReject = false;
    if (m_isRunning) {
        qDebug() << "拒绝原因：m_isRunning = true";
        shouldReject = true;
    }
   // if (m_process && m_process->state() == QProcess::Running) {
   //     qDebug() << "拒绝原因：进程状态 = Running";
   //     shouldReject = true;
   // }
    if (shouldReject) {
        emit errorOccurred("请等待当前对话完成");
        return;
    }

    QString modelPath = findModelFile();
    if (modelPath.isEmpty()) {
        emit errorOccurred("未找到模型文件"); // 确保UI也显示错误
        return;
    }

    m_inactivityTimer->start(120000);

    qDebug()<<"!!!!!";
    // === 关键判断：模型是否已加载 ===
    if (!m_modelLoaded || !m_process || m_process->state() != QProcess::Running)
    {
        // 首次或进程已死 → 完整启动
        qDebug() << "首次启动或进程已退出，完整启动进程";
        QString prompt = buildPrompt(message);

        // 创建临时文件
        QTemporaryFile* tempFile = new QTemporaryFile(
            QDir::tempPath() + QDir::separator() + "aic_prompt_XXXXXX.txt", this);
        tempFile->setAutoRemove(true);
        if (!tempFile->open()) {
            emit errorOccurred("创建临时文件失败: " + tempFile->errorString());
            delete tempFile;
            return;
        }
        QString tempFilePath = tempFile->fileName();
        QTextStream out(tempFile);

        out.setEncoding(QStringConverter::Encoding::Utf8);
        //out << QChar(0xFEFF); // 写入 UTF-8 BOM
        out << prompt;
        out.flush();
        tempFile->close();
        QThread::msleep(300);
        qDebug() << "Prompt 已写入临时文件:" << tempFilePath;

        m_currentTempFile = tempFile;

        // 参数
        QStringList arguments;
        arguments << "-m" << modelPath;
        arguments << "-f" << tempFilePath;
        arguments << "-n" << "8000";
        arguments << "-c" << "8192";
        //arguments << "-c" << "12000";
        arguments << "--temp" << "0.7";
        arguments << "--repeat-penalty" << "1.0";
        arguments << "--top-k" << "40";
        arguments << "--top-p" << "0.95";
        arguments << "--flash-attn" << "on";
        arguments << "-ot" << "exps=CPU";
        arguments << "-ngl" << "30";
       // arguments << "--interactive-first";

        arguments << "--no-display-prompt";          // 关闭提示显示



        // 在 sendMessage() 里构建 arguments 时，强制加上这三行（无论首次还是复用都生效）
        arguments << "--reverse-prompt" << "<<#>>";
        arguments << "--in-prefix" << "<|im_start|>assistant\n";  // 复用时强制加前缀
        //arguments << "--in-prefix" << "";

        arguments << "--in-suffix" << "\n<|im_end|>\n";

        arguments << "--no-warmup";  // 可选，加快启动


        //arguments << "--interactive-first";               // 保持交互模式
        //arguments << "--prompt" << "";


        qDebug() << "启动llama-cli进程，参数:" << arguments;
        qDebug() << "发送消息:" << message;

        m_responseBuffer.clear();

        m_promptEchoed = false;
        m_partialTagBuffer.clear();
        m_lastMessage = message;
        m_isRunning = true;

        if (!startProcess(arguments)) {
            m_isRunning = false;
            emit errorOccurred("启动AI进程失败");
            return;
        }
        // 首次启动后，标记模型已加载
        m_modelLoaded = true;
    }
    else {
        // === 复用进程：直接写 stdin ===
        qDebug() << "复用现有进程，发送新消息";

        QString userPart = "<|im_start|>user\n" + message + "<|im_end|>\n<|im_start|>assistant\n";
        m_process->write(userPart.toUtf8());
        qDebug() << "复用进程，已写入新 user message + assistant start";
        m_responseBuffer.clear();
        m_promptEchoed = true;
        m_partialTagBuffer.clear();
        m_lastMessage = message;
        m_isRunning = true;
    }
}

// 在 aiclient.cpp 中
//bool AIClient::startProcess(const QStringList &arguments, const QString &promptPayload)
bool AIClient::startProcess(const QStringList &arguments)
{
    // 强制清理确保状态干净
    forceCleanup();


    delete m_utf8Decoder; // 1. 删除旧的解码器
    m_utf8Decoder = new QStringDecoder(QStringDecoder::Encoding::Utf8); // 2. 创建一个全新的


    // 创建新的进程对象
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    m_process->setTextModeEnabled(true);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GGML_CUDA_ENABLE", "1");           // 强制启用 CUDA
    env.insert("GGML_VULKAN_DISABLE", "1");        // 完全禁用 Vulkan



    // *** 1. 设置环境变量 ***

env.insert("PYTHONIOENCODING", "utf-8");
#ifdef Q_OS_WIN
    env.insert("LC_ALL", ".UTF-8");

    env.insert("LANG", "en_US.UTF-8");
#endif
    m_process->setProcessEnvironment(env);


    // *** 2. 连接所有信号！ ***
    connect(m_process, SIGNAL(readyReadStandardOutput()),
            this, SLOT(onProcessReadyRead()));
    connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(onProcessFinished(int, QProcess::ExitStatus)));
    connect(m_process, SIGNAL(errorOccurred(QProcess::ProcessError)),
            this, SLOT(onProcessError(QProcess::ProcessError)));
    // *** 信号连接结束 ***

    qDebug() << "启动llama-cli进程，参数:" << arguments;

    m_process->start("G:/AIChatApp/llama-b7673-bin-win-cuda-12.4-x64/llama-cli", arguments);

    if (m_process->waitForStarted(5000)) { // 5秒启动超时
        qDebug() << "AI进程启动成功，PID:" << m_process->processId();



        m_isRunning = true;
        emit statusChanged("AI正在思考...");
        return true;
    } else {
        QString error = m_process->errorString();
        qDebug() << "AI进程启动失败:" << error; // <-- 加上日志
        emit errorOccurred("启动AI进程失败: " + error); // <-- 确保错误被发送
        forceCleanup(); // 启动失败时也要清理
        return false;
    }
}





// aiclient.cpp
void AIClient::onProcessReadyRead()
{
    if (!m_process) { // 检查进程是否存在
        if (m_isRunning) {
            qDebug() << "错误：onProcessReadyRead 调用时 m_process 为空，但 m_isRunning 为 true。强制清理...";
            m_isRunning = false;
            emit statusChanged("错误 (内部状态)");
            QTimer::singleShot(0, this, &AIClient::cleanupProcess);
        }
        return;
    }

    // 检查 m_isRunning 状态，如果不是 running，则忽略（可能是延迟的信号）
    if (!m_isRunning) {
        qDebug() << "警告：onProcessReadyRead 被调用，但 m_isRunning 为 false。忽略。";
        // 仍然读取数据以清空缓冲区，防止阻塞
        m_process->readAllStandardOutput();
        return;
    }

    QByteArray output = m_process->readAllStandardOutput();

    //QString incomingText = QString::fromUtf8(output.constData(), output.size());
    QString incomingText = m_utf8Decoder->decode(output);

    QString processText = incomingText;
    bool endDetected = false;

    // --- 状态 1: 查找 assistant 标记 ---
    if (!m_promptEchoed) {
        m_partialTagBuffer += processText;
        //QString markerEndPattern = "assistant\r\n"; // 查找回显结束

        QString markerEndPattern = "<|im_start|>assistant\n"; // <-- 这才是 prompt 的真正结尾

        int lastMarkerEnd = m_partialTagBuffer.lastIndexOf(markerEndPattern);

        // (增加对 \r\n 的检查以提高稳定性)
        if (lastMarkerEnd == -1) {
            markerEndPattern = "<|im_start|>assistant\r\n";
            lastMarkerEnd = m_partialTagBuffer.lastIndexOf(markerEndPattern);
        }
        if (lastMarkerEnd == -1) {
            markerEndPattern = "<|im_start|>assistant \n";
            lastMarkerEnd = m_partialTagBuffer.lastIndexOf(markerEndPattern);
        }

        if (lastMarkerEnd != -1) {
            m_promptEchoed = true;
            qDebug() << "=== 检测到 prompt 回显结束标记 ===";
            processText = m_partialTagBuffer.mid(lastMarkerEnd + markerEndPattern.length());
            m_partialTagBuffer.clear();
            // 掉入状态 2 处理
        } else {
            if (m_partialTagBuffer.length() > 8192) {
                m_partialTagBuffer = m_partialTagBuffer.right(4096);
            }
            qDebug() << "过滤 (标记前的数据块):" << processText;
            processText.clear(); // 清空，因为标记前的都不要
        }
    }

    // --- 状态 2: 处理标记 *之后* 的文本 ---
    if (m_promptEchoed && !processText.isEmpty()) { // 必须检查 !isEmpty

        static bool responseStarted = false;
        if (!responseStarted && (processText.startsWith("！") || processText.startsWith("!"))) {
            processText.remove(0, 1);

            qDebug() << "已删除响应开头的感叹号";
        }
        //responseStarted = true

        endDetected = processAndAppendResponse(processText, m_lastMessage); // 传递 m_lastMessage
    }



    if (endDetected) {
        qDebug() << "检测到 <<#>> 结束标记，等待模型自然停止...";
        m_isRunning = false;
        emit statusChanged("就绪");
       // return true;  // 关键：返回 true 让上层不再处理，但不杀进程！
    }

    // 重启空闲定时器
    m_inactivityTimer->start(120000);  // 120秒无新输出就超时


}


void AIClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_process) {
        qDebug() << "onProcessFinished: 进程已清理，忽略";
        return; // 已经被 cleanupProcess 处理了
    }

    qDebug() << "=== 信号 finished() 触发 ===";
    qDebug() << "退出码:" << exitCode << "退出状态:" << exitStatus;

    if (m_isRunning) { // 如果 m_isRunning 还是 true (意味着 '>' 没被检测到)
        qDebug() << "onProcessFinished: 进程在 '>' 标记前结束，视为异常";
        m_isRunning = false; // 更新状态
        QString errorMsg = QString("AI进程意外结束，代码: %1").arg(exitCode);
        if (m_responseBuffer.isEmpty()) errorMsg += " - 未收到任何响应。";

        emit errorOccurred(errorMsg);
        emit statusChanged("错误");
    } else {
        qDebug() << "onProcessFinished: m_isRunning 已为 false (由 ReadyRead 设置)，仅执行清理";
    }

    m_inactivityTimer->stop();
    // *** 无论如何，调用清理 ***
    cleanupProcess();
}
void AIClient::onProcessError(QProcess::ProcessError error)
{
    if (!m_process) {
        qDebug() << "onProcessError: 进程已清理，忽略";
        return; // 已经被 cleanupProcess 处理了
    }

    // 避免在 'terminate' 成功时报告 "Crashed"
    if (error == QProcess::Crashed && !m_isRunning) {
        qDebug() << "进程在非运行状态下报告崩溃 (可能是 terminate 导致)，忽略错误，执行清理";
        cleanupProcess();
        return;
    }

    qDebug() << "进程错误:" << error;

    QString errorMsg;
    switch (error) {
    case QProcess::FailedToStart: errorMsg = "..."; break;
    case QProcess::Crashed: errorMsg = "AI进程崩溃...\n..."; break;

    default: errorMsg = "AI进程发生未知错误"; break;
    }

    if (m_isRunning) { // 只有在还在“运行”时才发送错误信号
        m_isRunning = false;
        qDebug() << errorMsg;
        emit errorOccurred(errorMsg);
        emit statusChanged("错误");
    }


    cleanupProcess();
}

void AIClient::stopGeneration()
{
    if (m_process && m_isRunning) {
        qDebug() << "手动停止AI生成";
        m_process->terminate();
        if (!m_process->waitForFinished(5000)) {
            m_process->kill();
            qDebug() << "强制终止AI进程";
        }
        m_isRunning = false;
        emit statusChanged("已停止生成");
    }
}

void AIClient::cleanupProcess()
{
    qDebug() << "执行 cleanupProcess...";

    // 1. 清理临时文件
    if (m_currentTempFile) {
        if (QFile::exists(m_currentTempFile->fileName())) {
            QFile::remove(m_currentTempFile->fileName());
            qDebug() << "已删除临时文件:" << m_currentTempFile->fileName();
        }
        delete m_currentTempFile;
        m_currentTempFile = nullptr;
    }

    // 2. 清理进程
    if (m_process) {
        m_process->disconnect();
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(100);
        }
        delete m_process;
        m_process = nullptr;
    }

    // 3. 重置状态
    m_isRunning = false;
    //m_modelLoaded = false;
    m_responseBuffer.clear();
    m_promptEchoed = false;
    m_partialTagBuffer.clear();
    m_lastMessage.clear();


    m_inactivityTimer->stop();

    qDebug() << "清理完成";
}



QString AIClient::cleanResponse(const QString &response) const
{
    QString cleaned = response;

    // 移除所有已知的系统日志模式
    QRegularExpression regex;

    // 移除 load: 相关的日志
    regex.setPattern("load:[^\\n]*");
    cleaned.remove(regex);

    // 移除 token 相关的日志
    regex.setPattern("token[^\\n]*");
    cleaned.remove(regex);

    // 移除 cache 相关的日志
    regex.setPattern("cache[^\\n]*");
    cleaned.remove(regex);

    // 移除 MB 相关的日志（文件大小等）
    regex.setPattern("\\d+\\.?\\d* MB");
    cleaned.remove(regex);

    // 移除 ........................................................................... 这样
    regex.setPattern("\\.{10,}");
    cleaned.remove(regex);

    // 移除 *** 开头的警告
    regex.setPattern("\\*\\*\\*[^\\n]*");
    cleaned.remove(regex);

    // 移除 - 开头的帮助信息
    regex.setPattern("^- [^\\n]*");
    cleaned.remove(regex);

    // 移除参数信息
    regex.setPattern("(repeat_last_n|dry_|top_|mirostat|temp)[^\\n]*");
    cleaned.remove(regex);

    // 移除多余的空白行
    cleaned.replace(QRegularExpression("\\n\\s*\\n"), "\n");

    regex.setPattern("Input suffix:.*?<|im_end|>.*"); cleaned.remove(regex);
    regex.setPattern("(repeat_last_n|dry_multiplier|top_k|top_p|min_p|xtc_probability|typical_p|top_n_sigma|temp|mirostat).*"); cleaned.remove(regex);
    regex.setPattern("- Press Ctrl\\+C.*| - Press Return.*| - To return control.*| - If you want.*"); cleaned.remove(regex);

    return cleaned.trimmed();
}
void AIClient::forceCleanup()
{
    qDebug() << "=== 强制清理开始 ===";

    // 重置内部状态
    m_isRunning = false;
    m_responseBuffer.clear();

    // 彻底清理进程
    if (m_process) {
        qDebug() << "强制清理进程对象，当前状态:" << m_process->state();

        // 立即断开所有信号连接
        m_process->disconnect();

        // 强制终止进程
        if (m_process->state() == QProcess::Running) {
            qDebug() << "进程仍在运行，强制终止...";
            m_process->kill();
            if (!m_process->waitForFinished(1000)) {
                qDebug() << "进程拒绝终止，使用更强制的方法";
// 在Windows上，可以尝试taskkill
#ifdef Q_OS_WIN
                QProcess::execute("taskkill", {"/F", "/PID", QString::number(m_process->processId())});
#endif
            }
        }

        // 立即删除进程对象
        delete m_process;
        m_process = nullptr;
        qDebug() << "进程对象已强制删除";
    }

    qDebug() << "=== 强制清理完成 ===";
}





bool AIClient::processAndAppendResponse(const QString& rawText, const QString& userInput)
{
    if (rawText.isEmpty()) return false;

    if (endMarkerDetected) return false;
    QString currentChunk = rawText;

    // 新增：特定过滤prompt回显（匹配日志"system\nsystem\n你是一名..."）
    static const QRegularExpression rePromptHead(R"(^system\s*\n\s*system\s*\n\s*你是一名天文光学望远镜.*?(?=task_id|reasoning|verification|\{))");  // 从开头到JSON开始移除
    currentChunk.replace(rePromptHead, "");  // 移除污染


    //bool endMarkerDetected = false;

    static const QRegularExpression reEnd(R"(<<#>>)");
    //QRegularExpressionMatch match = reEnd.match(m_stableBuffer);
    QRegularExpressionMatch match = reEnd.match(rawText);
    if (match.hasMatch()) {
        int pos = match.capturedStart();
        qDebug() << "【正则增强】检测到结束标记1 '>' (位置:" << pos << ")";
        endMarkerDetected = true;


        if (pos > 0) {
            QString cleanPart = currentChunk.left(pos);
            // 这里再做一次常规清理（ANSI、换行等）


            if (!cleanPart.isEmpty()) {
                m_stableBuffer += cleanPart;
                m_stableBuffer.remove(QRegularExpression("<<#>>>}"));
                m_stableBuffer.remove(QRegularExpression("<<#>>> }"));
                emit responseReceived(m_stableBuffer);


            }
        }





        //emit responseFullyEnded();
        qDebug() <<"发射结束信号";

        m_stableBuffer.clear();

        m_pendingBuffer.clear();
        m_partialTagBuffer.clear();      // 防残留

        emit responseFullyEnded();
        m_isRunning = false;
        emit statusChanged("就绪");
        qDebug() << "响应结束，所有缓冲区已清空，准备接收下一次回复";

        return true;

    }

    // 2. 定义静态正则表达式 (保持不变)
    static const QRegularExpression reAnsiColor("\\x1b\\[[0-9;]*[mK]");
    static const QRegularExpression reExcessiveNewlines("(\\r?\\n){2,}");

    currentChunk.remove(reAnsiColor);
    currentChunk.replace(reExcessiveNewlines, "\n");
    currentChunk.remove('\r');
    // 3. 应用清理 (保持不变)
    currentChunk.remove(reAnsiColor);

    QString finalCleanedChunk;

    // === 【优化】从清理后的文本中提取 JSON（如果存在）===
    static const QRegularExpression reJson(R"(\{(?:[^{}]|(?R))*\})");
    QRegularExpressionMatch jsonMatch = reJson.match(currentChunk);
    if (jsonMatch.hasMatch()) {
        QString jsonStr = jsonMatch.captured(0);
        qDebug() << "检测到完整 JSON 输出:\n" << jsonStr;

        // 直接发送 JSON（不再走 stableBuffer 累积）
        emit responseReceived(jsonStr);

        // 如果 JSON 已完整（末尾有 }），且后面是 >，说明生成结束
        if (currentChunk.trimmed().endsWith('}') && endMarkerDetected) {
            qDebug() << "JSON + '>' 结束标记，终止生成";
            return true;
        }
        return false; // JSON 未完，继续等
    }

    QStringList lines = currentChunk.split('\n', Qt::KeepEmptyParts);
    bool validContentStartedInThisChunk = false;

    // 如果已经提取到 JSON，跳过后续文本处理
    if (jsonMatch.hasMatch()) {
        QString jsonStr = jsonMatch.captured(0);
        qDebug() << "Detected JSON:" << jsonStr.left(100) << "...";
        emit responseReceived(jsonStr);
        return false;
    }

    for (const QString& line : lines) {
        QString trimmedLine = line;
        bool skipLine = false;

        if (!skipLine) {
            if (validContentStartedInThisChunk) {
                finalCleanedChunk += "\n";
            }
            finalCleanedChunk += line;
            validContentStartedInThisChunk = true;
        }

    }

    finalCleanedChunk = finalCleanedChunk.replace(reExcessiveNewlines, "\n");

    if (!finalCleanedChunk.isEmpty()) {
        m_pendingBuffer += finalCleanedChunk;

        if (m_pendingBuffer.length() > 50 || m_pendingBuffer.contains("。") || m_pendingBuffer.contains("\n")) {
            m_stableBuffer += m_pendingBuffer;
            m_pendingBuffer.clear();

            //qDebug()<<"mmmmmmmmmmmmmmmmmmmmmmm"<<m_stableBuffer;

            static const QRegularExpression reEnd(R"(<<#>>)");
            QRegularExpressionMatch match = reEnd.match(m_stableBuffer);
           // QRegularExpressionMatch match = reEnd.match(rawText);
            if (match.hasMatch() && !endMarkerDetected) {
                int pos = match.capturedStart();
                qDebug() << "【正则增强】检测到结束标记2 '>' (位置:" << pos << ")"<<endMarkerDetected;
                endMarkerDetected = true;
                emit responseFullyEnded();
                qDebug() <<"发射结束信号";
                currentChunk.clear();
            }
            m_stableBuffer.remove(QRegularExpression("<<#>>>}"));
            emit responseReceived(m_stableBuffer);


        }
    }


    return false;
}


void AIClient::requestProcessTermination()
{
    // 这个函数被 QTimer::singleShot(0, ...) 安全调用
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qDebug() << "请求进程终止 (terminate)...";
        m_process->terminate();
        // 设置一个“死亡计时器”，以防 terminate 失败
        QTimer::singleShot(40000, this, &AIClient::forceProcessKill);
    } else {
        qDebug() << "requestProcessTermination: 进程已结束或为空，直接清理";
        cleanupProcess(); // 进程已经死了，直接清理
    }
}

void AIClient::forceProcessKill()
{
    // 这是 terminate 的后备方案
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qDebug() << "进程在 10000ms 内未终止，强制 kill...";
        m_process->kill();
        // onProcessFinished 或 onProcessError 会被触发，它们会调用 cleanupProcess
    }
}

bool AIClient::isRunning() const {
    return m_isRunning;
}
void AIClient::init_stableBuffer()
{
    m_stableBuffer="";
    m_fullOutputBuffer="";
    m_finalCleanedChunk="";
    m_partialTagBuffer="";
    m_lastMessage="";
    endMarkerDetected=false;
}
