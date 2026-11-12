#pragma once

#include <QByteArray>
#include <QLocalServer>
#include <QObject>

class QLocalSocket;
class QJsonObject;

namespace aipdf::worker {

class WorkerServer final : public QObject {
    Q_OBJECT

public:
    explicit WorkerServer(QString serverName, QObject* parent = nullptr);
    bool start();

private:
    void acceptConnection();
    void readFrames();
    void handleMessage(QLocalSocket* socket, const QJsonObject& request);

    QString serverName_;
    QLocalServer server_;
    QLocalSocket* client_ = nullptr;
    QByteArray receiveBuffer_;
};

}  // namespace aipdf::worker

