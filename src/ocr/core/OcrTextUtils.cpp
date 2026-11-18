#include "ocr/core/OcrTextUtils.h"

#include <QRegularExpression>

namespace aipdf::ocr {

bool isCjkChar(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0x3040 && code <= 0x30FF) ||
           (code >= 0xAC00 && code <= 0xD7AF);
}

QString normalizePunctuation(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == 0x3000) {
            // 全角空格
            out.append(QLatin1Char(' '));
        } else if (code >= 0xFF01 && code <= 0xFF5E) {
            // 全角 ASCII：FF01（！）对应 0x21，整段是线性偏移。
            out.append(QChar(static_cast<ushort>(code - 0xFF01 + 0x21)));
        } else {
            out.append(ch);
        }
    }
    return out;
}

QString compactNumericSpacing(const QString& text) {
    // 先归一化：调用方直接拿引擎原始输出进来（全角 `．`、`，`）也要能合上。
    QString out = normalizePunctuation(text);

    // 1. 数字之间夹的标点：去掉两边的空格（8 , 640 . 00 → 8,640.00）。
    static const QRegularExpression punctuationGap(
        QStringLiteral("(\\d)\\s*([.,:%])\\s*(\\d)"));
    out.replace(punctuationGap, QStringLiteral("\\1\\2\\3"));

    // 2. 千分位分组：左边 1–3 位数字、右边正好 3 位数字、中间只有空格。
    //    只认这种形状，避免把「2024 5」这种相邻的两个数并成一个。
    static const QRegularExpression thousandsGap(
        QStringLiteral("(\\d{1,3})\\s+(\\d{3})(?!\\d)"));
    // 反复跑到不再变化： `1 234 567` 需要两轮才能并完。
    for (int i = 0; i < 8; ++i) {
        const QString next = QString(out).replace(thousandsGap, QStringLiteral("\\1\\2"));
        if (next == out) {
            break;
        }
        out = next;
    }
    return out;
}

QString joinWords(const QStringList& words) {
    QString joined;
    for (const QString& raw : words) {
        const QString word = normalizePunctuation(raw);
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
    return compactNumericSpacing(joined);
}

}  // namespace aipdf::ocr
