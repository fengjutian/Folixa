#include "speech/SpeechText.h"

#include <QChar>

namespace aipdf::speech {

namespace {

// CJK 相关区块：表意文字、假名、韩文、汉字标点、兼容表意文字等。
//
// 这些字符逐字成词：中文没有词间空格，按字读才不会把整段连成一个词，
// 高亮也能一个字一个字地推进。
bool isCjkLike(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11FF) ||   // Hangul Jamo
           (cp >= 0x2E80 && cp <= 0x2EFF) ||   // CJK Radicals Supplement
           (cp >= 0x3000 && cp <= 0x303F) ||   // CJK Symbols and Punctuation
           (cp >= 0x3040 && cp <= 0x30FF) ||   // Hiragana + Katakana
           (cp >= 0x3100 && cp <= 0x312F) ||   // Bopomofo
           (cp >= 0x3130 && cp <= 0x318F) ||   // Hangul Compatibility Jamo
           (cp >= 0x31F0 && cp <= 0x31FF) ||   // Katakana Phonetic Extensions
           (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK Extension A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK Unified Ideographs
           (cp >= 0xA000 && cp <= 0xA4CF) ||   // Yi Syllables
           (cp >= 0xAC00 && cp <= 0xD7AF) ||   // Hangul Syllables
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK Compatibility Ideographs
           (cp >= 0xFE30 && cp <= 0xFE4F) ||   // CJK Compatibility Forms
           (cp >= 0xFF00 && cp <= 0xFFEF) ||   // Halfwidth and Fullwidth Forms
           (cp >= 0x20000 && cp <= 0x2A6DF) || // CJK Extension B
           (cp >= 0x2A700 && cp <= 0x2EBEF) || // CJK Extensions C-F
           (cp >= 0x2F800 && cp <= 0x2FA1F);   // CJK Compatibility Ideographs Supplement
}

}  // namespace

QVector<SpeechWord> splitIntoWords(const QString& text) {
    QVector<SpeechWord> words;
    const int total = text.size();

    int index = 0;
    // 正在累积的「拉丁字母/数字」词段：-1 表示当前没有。
    int runStart = -1;
    int runLength = 0;

    const auto flushRun = [&]() {
        if (runStart < 0) {
            return;
        }
        words.append(SpeechWord{text.mid(runStart, runLength), runStart, runLength});
        runStart = -1;
        runLength = 0;
    };

    while (index < total) {
        const QChar unit = text.at(index);
        int unitLength = 1;
        char32_t code = unit.unicode();
        if (unit.isHighSurrogate() && index + 1 < total && text.at(index + 1).isLowSurrogate()) {
            // 非 BMP 字符（代理对）：整体算一个字符，长度按 UTF-16 码元算 2。
            code = QChar::surrogateToUcs4(unit, text.at(index + 1));
            unitLength = 2;
        }

        if (QChar::isSpace(code)) {
            // 空白不朗读，但位置照算（start 是原文下标）。
            flushRun();
            index += unitLength;
            continue;
        }

        if (isCjkLike(code)) {
            flushRun();
            words.append(SpeechWord{text.mid(index, unitLength), index, unitLength});
            index += unitLength;
            continue;
        }

        if (QChar::isLetter(code) || QChar::isNumber(code)) {
            // 拉丁字母/数字连成一个词（"PDF2024" 是一个词）。
            if (runStart < 0) {
                runStart = index;
                runLength = 0;
            }
            runLength += unitLength;
            index += unitLength;
            continue;
        }

        // 其余符号（标点等）单独成词：朗读时是个停顿，界面上可以不显示。
        flushRun();
        words.append(SpeechWord{text.mid(index, unitLength), index, unitLength});
        index += unitLength;
    }
    flushRun();

    return words;
}

QString joinWords(const QVector<SpeechWord>& words) {
    QString joined;
    for (const SpeechWord& word : words) {
        joined += word.text;
    }
    return joined;
}

}  // namespace aipdf::speech
