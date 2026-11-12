#include "WorkerServer.h"

#include "ipc/IpcProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <utility>

namespace aipdf::worker {

WorkerServer::WorkerServer(QString serverName, QObject* parent)
    : QObject(parent), serverName_(std::move(serverName)) {
    connect(&server_, &QLocalServer::newConnection, this, &WorkerServer::acceptConnection);
}

bool WorkerServer::start() {
    QLocalServer::removeServer(serverName_);
    return server_.listen(serverName_);
}

void WorkerServer::acceptConnection() {
    while (server_.hasPendingConnections()) {
        QLocalSocket* candidate = server_.nextPendingConnection();
        if (client_ != nullptr) {
            candidate->disconnectFromServer();
            candidate->deleteLater();
            continue;
        }
        client_ = candidate;
        connect(client_, &QLocalSocket::readyRead, this, &WorkerServer::readFrames);
        connect(client_, &QLocalSocket::disconnected, this, [this] {
            client_->deleteLater();
            client_ = nullptr;
            receiveBuffer_.clear();
        });
    }
}

void WorkerServer::readFrames() {
    if (client_ == nullptr) {
        return;
    }
    receiveBuffer_.append(client_->readAll());
    if (receiveBuffer_.size() > ipc::kMaximumFrameBytes) {
        client_->write(ipc::encodeFrame(ipc::makeError(
            {}, "frame_too_large", "IPC frame exceeds the 1 MiB limit")));
        client_->disconnectFromServer();
        receiveBuffer_.clear();
        return;
    }

    qsizetype newline = -1;
    while ((newline = receiveBuffer_.indexOf('\n')) >= 0) {
        const QByteArray frame = receiveBuffer_.left(newline);
        receiveBuffer_.remove(0, newline + 1);
        if (frame.isEmpty()) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            client_->write(ipc::encodeFrame(ipc::makeError(
                {}, "invalid_json", "Request must be a JSON object")));
            continue;
        }
        handleMessage(client_, document.object());
    }
}

void WorkerServer::handleMessage(QLocalSocket* socket, const QJsonObject& request) {
    const QString requestId = request.value("request_id").toString();
    if (request.value("protocol_version").toInt(-1) != ipc::kProtocolVersion) {
        socket->write(ipc::encodeFrame(ipc::makeError(
            requestId, "protocol_version_unsupported", "Unsupported IPC protocol version")));
        return;
    }

    const QString operation = request.value("operation").toString();
    if (operation == "system.health") {
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId, {{"status", "ready"}, {"worker_version", "0.1.0"}})));
        return;
    }
    if (operation == "ai.echo") {
        const QString text = request.value("payload").toObject().value("text").toString();
        socket->write(ipc::encodeFrame(ipc::makeResult(
            requestId, {{"text", text}, {"placeholder", true}})));
        return;
    }
    socket->write(ipc::encodeFrame(ipc::makeError(
        requestId, "operation_not_supported", "The requested operation is not available")));
}

}  // namespace aipdf::worker

