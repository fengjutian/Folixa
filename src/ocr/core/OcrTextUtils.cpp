#include "ocr/OcrTextUtils.h"

namespace aipdf::ocr {

bool isCjkChar(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0x3040 && code <= 0x30FF) ||
           (code >= 0xAC00 && code <= 0xD7AF);
}

QString joinWords(const QStringList& words) {
    QString joined;
    for (const QString& word : words) {
        if (word.isEmpty()) {
            continue;
        }
        if (!joined.isEmpty()) {
            const QChar previous = joined.at(joined.size() - 1);
            const QChar next = word.at(0);
            const bool needsSpace = !isCjkChar(previous) && !isCjkChar(next) &&
                                    previous != QLatin1Char('-') && next != QLatin1Char(',') &&
                                    next != QLatin1Char('.');
            if (needsSpace) {
                joined.append(QLatin1Char(' '));
            }
        }
        joined.append(word);
    }
    return joined;
}

}  // namespace aipdf::ocr
