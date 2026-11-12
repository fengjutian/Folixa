#include "ipc/IpcProtocol.h"

#include <QJsonDocument>

namespace aipdf::ipc {

QJsonObject makeRequest(const QString& requestId,
                        const QString& operation,
                        const QJsonObject& payload) {
    return {
        {"protocol_version", kProtocolVersion},
        {"request_id", requestId},
        {"operation", operation},
        {"payload", payload},
    };
}

QJsonObject makeResult(const QString& requestId, const QJsonObject& payload) {
    return {
        {"protocol_version", kProtocolVersion},
        {"request_id", requestId},
        {"type", "result"},
        {"ok", true},
        {"payload", payload},
    };
}

QJsonObject makeError(const QString& requestId,
                      const QString& code,
                      const QString& message) {
    return {
        {"protocol_version", kProtocolVersion},
        {"request_id", requestId},
        {"type", "error"},
        {"ok", false},
        {"error", QJsonObject{{"code", code}, {"message", message}}},
    };
}

QByteArray encodeFrame(const QJsonObject& message) {
    QByteArray frame = QJsonDocument(message).toJson(QJsonDocument::Compact);
    frame.append('\n');
    return frame;
}

}  // namespace aipdf::ipc

