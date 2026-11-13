#include "doc/CitationVerifier.h"

#include "ai/LocalHashEmbedding.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace aipdf::doc {

namespace {

// 引用段的开头。
//
// 模型不一定严格按格式来，所以标题认得宽松一点：
// 「引用：」「参考：」「来源：」「Sources:」都算。
const QRegularExpression& citationSectionPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(?:引用|参考|来源|出处|References|Sources)\\s*[:：]\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 一条引用：[编号] 第 N 页 | 引文：xxx
//
// 页码与引文都写成可选：模型漏写页码时仍可用编号定位；
// 但**引文必须有**——没有引文就无法核对，这条引用只能作废。
const QRegularExpression& citationLinePattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*\\[(\\d+)\\]\\s*"
                       "(?:第\\s*(\\d+)\\s*页)?\\s*"
                       "(?:[|｜·\\-—:：]\\s*)?"
                       "(?:引文|原文|引用|quote)\\s*[:：]\\s*(.+?)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 正文里的 [n] 标记。
const QRegularExpression& markerPattern() {
    static const QRegularExpression pattern(QStringLiteral("\\[(\\d+)\\]"));
    return pattern;
}

// 比对时把空白全部去掉：模型可能给引文换行或加空格，
// 那不是「引文改了」，只是排版。
QString compactForCompare(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (QChar ch : ai::LocalHashEmbedding::normalize(text)) {
        if (ch.isSpace()) {
            continue;
        }
        result.append(ch);
    }
    return result;
}

// 去掉引文两端的引号（模型爱加）。
QString stripQuoteMarks(const QString& text) {
    QString result = text.trimmed();
    const QStringList marks = {QStringLiteral("「"), QStringLiteral("」"),
                               QStringLiteral("“"), QStringLiteral("”"),
                               QStringLiteral("\""), QStringLiteral("'"),
                               QStringLiteral("『"), QStringLiteral("』")};
    while (result.size() >= 2 && marks.contains(result.left(1)) &&
           marks.contains(result.right(1))) {
        result = result.mid(1, result.size() - 2).trimmed();
    }
    return result;
}

}  // namespace

const CitationBlock* CitationVerifier::blockByNumber(const QVector<CitationBlock>& blocks,
                                                     int number) {
    for (const CitationBlock& block : blocks) {
        if (block.number == number) {
            return &block;
        }
    }
    return nullptr;
}

QString CitationVerifier::answerBody(const QString& answer) {
    const QStringList lines = answer.split(QLatin1Char('\n'));
    int sectionStart = -1;
    for (int index = 0; index < lines.size(); ++index) {
        if (citationSectionPattern().match(lines.at(index)).hasMatch()) {
            sectionStart = index;
            break;
        }
    }
    if (sectionStart < 0) {
        return answer;
    }
    return lines.mid(0, sectionStart).join(QLatin1Char('\n')).trimmed();
}

QVector<ParsedCitation> CitationVerifier::parseCitations(const QString& answer) {
    QVector<ParsedCitation> citations;
    const QStringList lines = answer.split(QLatin1Char('\n'));

    bool inSection = false;
    for (const QString& line : lines) {
        if (!inSection) {
            if (citationSectionPattern().match(line).hasMatch()) {
                inSection = true;
            }
            continue;
        }
        const QRegularExpressionMatch match = citationLinePattern().match(line);
        if (!match.hasMatch()) {
            continue;
        }
        ParsedCitation citation;
        citation.number = match.captured(1).toInt();
        citation.pageNumber = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
        citation.quote = stripQuoteMarks(match.captured(3));
        citations.append(citation);
    }
    return citations;
}

QVector<int> CitationVerifier::extractMarkers(const QString& answer) {
    QVector<int> markers;
    QSet<int> seen;
    const QString body = answerBody(answer);
    auto matches = markerPattern().globalMatch(body);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const int number = match.captured(1).toInt();
        if (!seen.contains(number)) {
            seen.insert(number);
            markers.append(number);
        }
    }
    std::sort(markers.begin(), markers.end());
    return markers;
}

bool CitationVerifier::quoteAppearsIn(const QString& quote, const QString& blockText) {
    const QString needle = compactForCompare(stripQuoteMarks(quote));
    if (needle.isEmpty()) {
        return false;
    }
    const QString haystack = compactForCompare(blockText);
    if (haystack.isEmpty()) {
        return false;
    }
    return haystack.contains(needle);
}

CitationReport CitationVerifier::verify(const QString& answer,
                                         const QVector<CitationBlock>& blocks) {
    CitationReport report;
    report.citations = parseCitations(answer);
    report.markers = extractMarkers(answer);

    for (ParsedCitation& citation : report.citations) {
        const CitationBlock* block = blockByNumber(blocks, citation.number);
        if (block == nullptr) {
            // 1. 存在性：编号不在这次给模型的上下文里。
            citation.valid = false;
            citation.reason = QStringLiteral("引用编号 %1 不在本次提供的材料中").arg(citation.number);
            ++report.invalidCount;
            continue;
        }
        if (citation.quote.isEmpty()) {
            citation.valid = false;
            citation.reason = QStringLiteral("引用 %1 没有给出原文，无法核对").arg(citation.number);
            ++report.invalidCount;
            continue;
        }
        if (!quoteAppearsIn(citation.quote, block->text)) {
            // 2. 引文核对：编号对，但引的那段话不在这一块里。
            citation.valid = false;
            citation.reason = QStringLiteral("引用 %1 的引文在原文中找不到").arg(citation.number);
            ++report.invalidCount;
            continue;
        }
        if (citation.pageNumber > 0 && block->pageIndex >= 0 &&
            citation.pageNumber != block->pageIndex + 1) {
            // 3. 页码一致：模型写的页与这块实际所在页不同。
            citation.valid = false;
            citation.reason = QStringLiteral("引用 %1 的页码与原文不符（原文在第 %2 页）")
                                  .arg(citation.number)
                                  .arg(block->pageIndex + 1);
            ++report.invalidCount;
            continue;
        }
        citation.valid = true;
        citation.reason.clear();
        ++report.validCount;
    }

    // 正文里的标记必须指向一条**有效**引用，否则那句话没有出处。
    for (int marker : report.markers) {
        bool found = false;
        for (const ParsedCitation& citation : report.citations) {
            if (citation.number == marker && citation.valid) {
                found = true;
                break;
            }
        }
        if (!found) {
            report.hasUnsupportedMarker = true;
            break;
        }
    }
    return report;
}

}  // namespace aipdf::doc
