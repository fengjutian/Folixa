#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace aipdf::ipc {

inline constexpr int kProtocolVersion = 1;
inline constexpr qsizetype kMaximumFrameBytes = 1024 * 1024;

QJsonObject makeRequest(const QString& requestId,
                        const QString& operation,
                        const QJsonObject& payload = {});

QJsonObject makeResult(const QString& requestId,
                       const QJsonObject& payload = {});

QJsonObject makeError(const QString& requestId,
                      const QString& code,
                      const QString& message);

QByteArray encodeFrame(const QJsonObject& message);

}  // namespace aipdf::ipc

