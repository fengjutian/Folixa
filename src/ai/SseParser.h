#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::ai {

// SSE（Server-Sent Events）增量解析器。
//
// 这是个纯函数式的状态机：喂进去一段字节（可能只有半个事件），
// 吐出来若干个已经完整的事件。没有网络、没有 Qt 事件循环，
// 所以能直接单测——流式解析最容易错的就是「半行」和「半事件」。
//
// 规则（EventSource 的标准）：
//   - 行以 \n 或 \r\n 结尾；
//   - 以 ':' 开头的行是注释，忽略；
//   - "字段名: 值"，冒号后可选的一个空格要去掉；
//   - "data:" 可以出现多次，多个值用 \n 连接；
//   - 空行表示一个事件结束；
//   - "event:" 给事件命名，"id:" 与 "retry:" 本类忽略。
class SseParser {
public:
    struct Event {
        // 事件名（没有 event: 字段时为空）。
        QString event;
        // data 字段的内容（多行用 \n 连接）。
        QString data;
    };

    // 喂一段字节，返回这次解析出来的完整事件。
    QVector<Event> feed(const QByteArray& bytes);

    // 流结束时把还挂着的事件吐出来。
    //
    // 标准规定「文件结束时丢弃未完成的事件」，但真实服务偶尔少发一个空行。
    // 这里选择吐出来：多给半句好过整段丢失。
    Event flush();

    // 缓冲里还留着半个事件吗（诊断用）。
    bool hasPending() const;

    void reset();

private:
    void handleLine(const QString& line, QVector<Event>* events);

    QByteArray buffer_;
    QString eventName_;
    QStringList dataLines_;
    bool pending_ = false;
};

}  // namespace aipdf::ai
