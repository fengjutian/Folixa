#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>

namespace aipdf::ipc {

class WorkerClient final : public QObject {
    Q_OBJECT

public:
    explicit WorkerClient(QObject* parent = nullptr);
    ~WorkerClient() override;

    void start();
    QString sendRequest(const QString& operation,
                        const QJsonObject& payload = {},
                        int timeoutMs = 10'000);

    // 发起一次流式请求。
    //
    // 与 sendRequest 的区别：流式请求没有「等一个结果」的时限，
    // 它的结果由一串事件帧陆续送回来（见 eventReceived）。
    // 返回的 requestId 用来取消这次请求。
    QString sendStreamRequest(const QString& operation,
                              const QJsonObject& payload = {});
    // 取消一次流式请求（发 ai.chat.cancel 给 Worker）。
    void cancelStreamRequest(const QString& requestId);
    // 某个流式请求是不是还在进行中。
    bool isStreamActive(const QString& requestId) const;

    // 连接到外部已运行的 Worker，而不是由本客户端拉起进程。
    void setServerName(const QString& serverName);
    // 覆盖 Worker 可执行文件位置，默认在应用同目录查找。
    void setWorkerExecutable(const QString& executablePath);
    // 关闭自动拉起 Worker 进程，用于连接已存在的 Worker 实例。
    void setAutoLaunchWorker(bool enabled);

signals:
    void stateChanged(const QString& state);
    void responseReceived(const QString& requestId, const QJsonObject& response);
    void requestFailed(const QString& requestId, const QString& message);
    // 流式请求的中间事件。
    void eventReceived(const QString& requestId,
                       const QString& event,
                       const QJsonObject& payload);

private slots:
    void connectToWorker();
    void readFrames();
    void handleSocketError(QLocalSocket::LocalSocketError error);
    void handleWorkerFinished(int exitCode, QProcess::ExitStatus status);

private:
    void failPendingRequests(const QString& message);
    QString workerExecutablePath() const;

    QString serverName_;
    QString workerExecutableOverride_;
    bool autoLaunchWorker_ = true;
    QProcess workerProcess_;
    QLocalSocket socket_;
    QByteArray receiveBuffer_;
    QHash<QString, QTimer*> pendingTimers_;
    // 正在进行的流式请求（没有超时定时器，靠事件与 result 收尾）。
    QSet<QString> activeStreams_;
    int connectionAttempts_ = 0;
    bool stopping_ = false;
};

}  // namespace aipdf::ipc

