#ifndef AICLIENT_H
#define AICLIENT_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>
#include <QStringDecoder>
#include <QTimer>
class AIClient : public QObject
{
    Q_OBJECT

public:
    explicit AIClient(QObject *parent = nullptr);
    ~AIClient();

    void initialize();
    void sendMessage(const QString &message);
    void stopGeneration();
    bool isReady() const { return m_initialized; }
    bool isGenerating() const { return m_isRunning; }

    void forceCleanup();

    bool isRunning() const;

    void init_stableBuffer();

signals:
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void statusChanged(const QString &status);
    void responseFullyEnded();




private slots:
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void requestProcessTermination();
    void forceProcessKill();

    //void processCompleteResponse(const QString& fullText);

private:
    bool startProcess(const QStringList &arguments);
    void cleanupProcess();
    QString findModelFile() const;
    QString buildPrompt(const QString &message) const;

    QProcess *m_process;
    QString m_responseBuffer;
    bool m_initialized;
    bool m_isRunning;

    QString cleanResponse(const QString &response) const;

    bool m_promptEchoed;
    QString m_partialTagBuffer;

    QString m_lastMessage;
    QString m_promptToSend;

    bool processAndAppendResponse(const QString& rawText, const QString& userInput);

    QTemporaryFile* m_currentTempFile = nullptr;

    bool m_modelLoaded = false;


    QString m_stableBuffer;  // 稳定的已确认内容
    QString m_cleanedBuffer;
    QString m_pendingBuffer; // 待确认的内容（最后几个 token）
    static const int STABLE_THRESHOLD = 3;
    QStringDecoder *m_utf8Decoder;

   QString m_fullOutputBuffer;

   QString m_finalCleanedChunk;

   //void partialResponseReceived(const QString &partial);

   bool isNoiseLine(const QString& trimmedLine, const QString& userInput) const;

   bool endMarkerDetected = false;

   QTimer *m_inactivityTimer = nullptr;
};

#endif // AICLIENT_H
