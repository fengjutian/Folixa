#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace aipdf::ipc {

inline constexpr int kProtocolVersion = 2;
inline constexpr qsizetype kMaximumFrameBytes = 1024 * 1024;

QJsonObject makeRequest(const QString& requestId,
                        const QString& operation,
                        const QJsonObject& payload = {});

QJsonObject makeResult(const QString& requestId,
                       const QJsonObject& payload = {});

QJsonObject makeError(const QString& requestId,
                      const QString& code,
                      const QString& message);

// 事件帧：一次请求还没结束时的中间消息。
//
// 流式对话靠它：Worker 会为同一个 request_id 连发很多个事件
// （一段增量文本、结束、错误），最后再发一个 result。
// 客户端要分清「中间事件」和「最终结果」——看到 event 就不能当成请求结束。
QJsonObject makeEvent(const QString& requestId,
                      const QString& event,
                      const QJsonObject& payload = {});

QByteArray encodeFrame(const QJsonObject& message);

}  // namespace aipdf::ipc

