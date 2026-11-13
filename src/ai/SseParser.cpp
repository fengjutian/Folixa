#include "ai/SseParser.h"

namespace aipdf::ai {

QVector<SseParser::Event> SseParser::feed(const QByteArray& bytes) {
    QVector<Event> events;
    buffer_.append(bytes);

    // 一次可能带来好几行，也可能是半行。
    // 找不到换行就留着等下一段——这就是「半行」的处理。
    qsizetype newline = -1;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        QByteArray raw = buffer_.left(newline);
        buffer_.remove(0, newline + 1);
        // \r\n 的 \r 是行尾的一部分，去掉。
        if (raw.endsWith('\r')) {
            raw.chop(1);
        }
        handleLine(QString::fromUtf8(raw), &events);
    }
    return events;
}

SseParser::Event SseParser::flush() {
    Event event;
    if (pending_ && !dataLines_.isEmpty()) {
        event.event = eventName_;
        event.data = dataLines_.join(QLatin1Char('\n'));
    }
    reset();
    return event;
}

bool SseParser::hasPending() const {
    return pending_ || !buffer_.isEmpty();
}

void SseParser::reset() {
    buffer_.clear();
    eventName_.clear();
    dataLines_.clear();
    pending_ = false;
}

void SseParser::handleLine(const QString& line, QVector<Event>* events) {
    // 注释行：以 ':' 开头。
    if (line.startsWith(QLatin1Char(':'))) {
        return;
    }
    // 空行：事件结束。
    if (line.isEmpty()) {
        if (!dataLines_.isEmpty()) {
            Event event;
            event.event = eventName_;
            event.data = dataLines_.join(QLatin1Char('\n'));
            events->append(event);
        }
        eventName_.clear();
        dataLines_.clear();
        pending_ = false;
        return;
    }

    // "字段名: 值"。没有冒号的行按标准忽略。
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return;
    }
    QString field = line.left(colon);
    QString value = line.mid(colon + 1);
    // 冒号后可选的一个空格要去掉。
    if (value.startsWith(QLatin1Char(' '))) {
        value.remove(0, 1);
    }

    if (field == QStringLiteral("data")) {
        dataLines_.append(value);
        pending_ = true;
        return;
    }
    if (field == QStringLiteral("event")) {
        eventName_ = value;
        pending_ = true;
        return;
    }
    // id / retry：本类用不到，忽略。
}

}  // namespace aipdf::ai
