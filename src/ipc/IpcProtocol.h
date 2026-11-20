#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace aipdf::ipc {

// v0.11.0 加了整篇问答：ai.chat.stream 的载荷多了 context（带编号的上下文块），
// 任务类型多了 "qa"。旧 Worker 收到这个载荷会不知道怎么处理，版本号必须跟着涨。
//
// 2026-11-21：安全文档从「只读」变成「可编辑」。隔离解析进程（PdfParseWorker）
// 多了两个操作：
//   - pdf.edit：按结构化 JSON 指令填表单域 / 加批注（指令里只有页号、矩形、文字，
//     不放媒体字节，所以一条帧永远远小于 1 MiB 上限）。
//   - pdf.save：把解析进程里那份文档写回磁盘（路径由主进程给，原子替换）。
// 旧 Worker 不认识这两个操作（收到只会回 operation_not_supported），
// 载荷语义也变了，所以版本号跟着涨到 4。
inline constexpr int kProtocolVersion = 4;
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

