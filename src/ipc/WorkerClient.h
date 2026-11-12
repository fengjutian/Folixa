#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
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

signals:
    void stateChanged(const QString& state);
    void responseReceived(const QString& requestId, const QJsonObject& response);
    void requestFailed(const QString& requestId, const QString& message);

private slots:
    void connectToWorker();
    void readFrames();
    void handleSocketError(QLocalSocket::LocalSocketError error);
    void handleWorkerFinished(int exitCode, QProcess::ExitStatus status);

private:
    void failPendingRequests(const QString& message);
    QString workerExecutablePath() const;

    QString serverName_;
    QProcess workerProcess_;
    QLocalSocket socket_;
    QByteArray receiveBuffer_;
    QHash<QString, QTimer*> pendingTimers_;
    int connectionAttempts_ = 0;
    bool stopping_ = false;
};

}  // namespace aipdf::ipc

