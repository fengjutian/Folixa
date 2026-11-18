#include "pdf/ops/OcrSuspicion.h"

#include <algorithm>

namespace aipdf::pdf {
namespace {

// 一对形近字符：字母形、数字形。
struct ConfusablePair {
    QString letter;  // 建议替换成的字母（规范写法）
    QString lower;   // 小写形式（匹配时不区分大小写）
    QString digit;   // 数字形
};

// 形近字表。
//
// 为什么只收这六组：形近字表越全，误报越多。这里的每一组都是 OCR 里真会混的
// （0/O、1/l/I、2/Z、5/S、8/B），而且在「数字串里混进字母」这个语境下，
// 建议替换成哪一个字是明确的。
const QVector<ConfusablePair>& confusablePairs() {
    static const QVector<ConfusablePair> kPairs = {
        {QStringLiteral("O"), QStringLiteral("o"), QStringLiteral("0")},
        {QStringLiteral("l"), QStringLiteral("l"), QStringLiteral("1")},
        {QStringLiteral("I"), QStringLiteral("i"), QStringLiteral("1")},
        {QStringLiteral("Z"), QStringLiteral("z"), QStringLiteral("2")},
        {QStringLiteral("S"), QStringLiteral("s"), QStringLiteral("5")},
        {QStringLiteral("B"), QStringLiteral("b"), QStringLiteral("8")},
    };
    return kPairs;
}

// 字母 → 数字（不区分大小写：o 与 O 都指向 0）。
QString digitForLetter(QChar ch) {
    const QString lower = ch.toLower();
    for (const ConfusablePair& pair : confusablePairs()) {
        if (pair.lower == lower) return pair.digit;
    }
    return QString();
}

// 数字 → 字母（取表里的规范写法）。
QString letterForDigit(QChar ch) {
    for (const ConfusablePair& pair : confusablePairs()) {
        if (pair.digit == QString(ch)) return pair.letter;
    }
    return QString();
}

// 一处可疑点（还没整理成对外结构）。
struct Candidate {
    int pageIndex = 0;
    int offset = 0;  // 在本段文本里的位置（用于排序）
    QString context;
    QString recognized;
    QString suggestion;
    QString reason;
};

// 上下文片段：可疑字符所在的那一串，前后各带一点上下文。
//
// 为什么带上下文：只给一个孤零零的字母，界面上的提示没法让人判断对错。
QString contextSnippet(const QString& text, int runStart, int runEnd) {
    const int left = qMax(0, runStart - 10);
    const int right = qMin(text.size(), runEnd + 10);
    return text.mid(left, right - left);
}

// 一段连续的 ASCII 字母数字。
bool isWordChar(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z') ||
           (code >= 'a' && code <= 'z');
}

// 检查一段「字母数字混排」的串。
void inspectRun(const QString& text, int runStart, int runEnd, int pageIndex,
                QVector<Candidate>* candidates) {
    int digits = 0;
    int letters = 0;
    for (int i = runStart; i < runEnd; ++i) {
        if (text.at(i).isDigit()) {
            ++digits;
        } else {
            ++letters;
        }
    }
    if (digits == 0 && letters == 0) return;

    const auto addCandidate = [&](int offset, const QString& recognized, const QString& suggestion,
                                  const QString& reason) {
        Candidate candidate;
        candidate.pageIndex = pageIndex;
        candidate.offset = offset;
        candidate.context = contextSnippet(text, runStart, runEnd);
        candidate.recognized = recognized;
        candidate.suggestion = suggestion;
        candidate.reason = reason;
        candidates->append(candidate);
    };

    if (digits > 0 && letters > 0) {
        // 数量少的那一类才是「混进来的」。
        //
        // 为什么这样判：英文单词里混进数字（l0g1n）或数字串里混进字母（2O24），
        // 都是 OCR 认错；如果两边一样多，就都当可疑（"2O24" 这种一眼就能看出来）。
        const bool digitsAreSuspects = digits <= letters;
        const bool lettersAreSuspects = letters <= digits;
        for (int i = runStart; i < runEnd; ++i) {
            const QChar ch = text.at(i);
            if (ch.isDigit()) {
                if (!digitsAreSuspects) continue;
                const QString suggestion = letterForDigit(ch);
                if (suggestion.isEmpty()) continue;
                addCandidate(i, QString(ch), suggestion,
                             QStringLiteral("英文单词里混进了数字 %1，可能是字母 %2 认错了（形近）")
                                 .arg(QString(ch))
                                 .arg(suggestion));
            } else {
                if (!lettersAreSuspects) continue;
                const QString suggestion = digitForLetter(ch);
                if (suggestion.isEmpty()) continue;
                addCandidate(i, QString(ch), suggestion,
                             QStringLiteral("数字串里混进了字母 %1，可能是数字 %2 认错了（形近）")
                                 .arg(QString(ch))
                                 .arg(suggestion));
            }
        }
        return;
    }

    // 全是字母：查多字符的形近。
    //
    // 为什么只查 rn → m：m 被认成 rn 是最常见的一种，但反过来（rn 被认成 m）
    // 在正常英文里非常常见（return、print），报出来全是噪声。
    if (digits == 0 && letters == runEnd - runStart) {
        for (int i = runStart; i + 1 < runEnd; ++i) {
            const QString pair = text.mid(i, 2);
            if (pair.compare(QStringLiteral("rn"), Qt::CaseInsensitive) != 0) continue;
            addCandidate(i, pair, QStringLiteral("m"),
                         QStringLiteral("单词里的连续字母 %1 可能是字母 %2 被拆开了（形近）")
                             .arg(pair)
                             .arg(QStringLiteral("m")));
        }
    }
}

// 扫一整段文本。
void scanText(const QString& text, int pageIndex, QVector<Candidate>* candidates) {
    int i = 0;
    while (i < text.size()) {
        if (!isWordChar(text.at(i))) {
            ++i;
            continue;
        }
        const int runStart = i;
        while (i < text.size() && isWordChar(text.at(i))) ++i;
        inspectRun(text, runStart, i, pageIndex, candidates);
    }
}

}  // namespace

QVector<SuspiciousChar> findSuspiciousCharacters(const QVector<TextPage>& pages,
                                                 const QString& recognizedText) {
    QVector<Candidate> candidates;

    // 先看各页有没有文本：文本层是首选来源，因为只有它带页号。
    bool hasPageText = false;
    for (const TextPage& page : pages) {
        if (!page.plainText().isEmpty()) {
            hasPageText = true;
            break;
        }
    }

    if (hasPageText) {
        for (int i = 0; i < pages.size(); ++i) {
            const TextPage& page = pages.at(i);
            const QString text = page.plainText();
            if (text.isEmpty()) continue;
            const int pageIndex = page.pageIndex >= 0 ? page.pageIndex : i;
            scanText(text, pageIndex, &candidates);
        }
    } else if (!recognizedText.isEmpty()) {
        // 还没有写回文字层时（例如刚跑完 OCR），整篇文本归到第 1 页。
        scanText(recognizedText, 0, &candidates);
    }

    // 排序：先页码，再页内位置。
    // 为什么用稳定排序：同样输入必须给出同样顺序的结论，界面上才不会闪烁。
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& left, const Candidate& right) {
                         if (left.pageIndex != right.pageIndex) {
                             return left.pageIndex < right.pageIndex;
                         }
                         return left.offset < right.offset;
                     });

    QVector<SuspiciousChar> result;
    result.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        SuspiciousChar item;
        item.pageIndex = candidate.pageIndex;
        item.context = candidate.context;
        item.recognized = candidate.recognized;
        item.suggestion = candidate.suggestion;
        item.reason = candidate.reason;
        result.append(item);
    }
    return result;
}

}  // namespace aipdf::pdf
