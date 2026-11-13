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

QJsonObject makeEvent(const QString& requestId,
                      const QString& event,
                      const QJsonObject& payload) {
    // 事件帧刻意**不带** ok 字段：ok 只属于最终结果。
    // 谁要是把中间事件当成「请求成功了」，一眼就能看出来。
    return {
        {"protocol_version", kProtocolVersion},
        {"request_id", requestId},
        {"type", "event"},
        {"event", event},
        {"payload", payload},
    };
}

QByteArray encodeFrame(const QJsonObject& message) {
    QByteArray frame = QJsonDocument(message).toJson(QJsonDocument::Compact);
    frame.append('\n');
    return frame;
}

}  // namespace aipdf::ipc

