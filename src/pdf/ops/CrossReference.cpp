#include "pdf/ops/CrossReference.h"

#include "pdf/annot/PageAnnotsInjector.h"

#include <QRegularExpression>

#include <algorithm>

namespace aipdf::pdf {

namespace {

// 一处「提到某页 / 某节」的说法。
struct Mention {
    // 在页面文字里的起点与长度（QString 字符下标）。
    int start = 0;
    int length = 0;
    CrossReferenceKind kind = CrossReferenceKind::Unknown;
    // 页码（1 基）；章节型为 -1。
    int pageNumber = -1;
    // 章节号 / 章号，例如「3.2」「3」。
    QString number;
    // 命中的原文（已经并进触发词）。
    QString text;
};

// 一处引用（可能提到多个目标：「见第 12 页和第 13 页」）。
struct ReferenceSpan {
    int start = 0;
    int end = 0;
    QVector<Mention> mentions;
};

// ---------------------------------------------------------------------------
// 识别用的正则
// ---------------------------------------------------------------------------
//
// 为什么中英文分开写：混在一个大正则里，捕获组编号会乱，
// 出错时很难看出是哪一条规则误判。分开写一眼就能对上。

// 中文页码：「第 12 页」「第12页」。
const QRegularExpression& cjkPagePattern() {
    static const QRegularExpression pattern(QStringLiteral("第\\s*([0-9]+)\\s*页"));
    return pattern;
}

// 中文章号：「第 3 章」。
const QRegularExpression& cjkChapterPattern() {
    static const QRegularExpression pattern(QStringLiteral("第\\s*([0-9]+)\\s*章"));
    return pattern;
}

// 中文章节号：「3.2 节」「3 节」。
const QRegularExpression& cjkSectionPattern() {
    static const QRegularExpression pattern(QStringLiteral("([0-9]+(?:\\.[0-9]+)*)\\s*节"));
    return pattern;
}

// 英文页码："page 12"。
const QRegularExpression& enPagePattern() {
    static const QRegularExpression pattern(QStringLiteral("\\bpage\\s+([0-9]+)\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 英文页码缩写："p. 12"。
const QRegularExpression& enPageAbbrevPattern() {
    static const QRegularExpression pattern(QStringLiteral("\\bp\\.\\s*([0-9]+)\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 英文章号："chapter 3"。
const QRegularExpression& enChapterPattern() {
    static const QRegularExpression pattern(QStringLiteral("\\bchapter\\s+([0-9]+)\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 英文章节号："section 3.2" / "sec. 3.2"。
const QRegularExpression& enSectionPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("\\b(?:section|sec\\.?)\\s+([0-9]+(?:\\.[0-9]+)*)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 触发词：出现在引用前面，说明"这里是一处引用"。
//
// 为什么回头找而不是写进正则里：写进正则就得把它设成可选，
// 可选之后捕获组编号会跟着变；而且「在」「如」这种词到处都有，
// 当成引用的一部分会让整句话都被圈进去。
const QRegularExpression& triggerPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("(详见|参见|请参见|请看|见|如|在|转到|翻到|see|on|refer\\s+to|shown\\s+on|found\\s+on)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 连接词：两处页码说法之间的那点文字。
//
// 「见第 12 页和第 13 页」是一处引用；「见第 12 页。第 13 页另说」是两处。
// 区别就在中间是不是连接词。
const QRegularExpression& connectorPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(?:和|与|、|及|以及|还有|，|,|and|as\\s+well\\s+as)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// 把某个正则的全部匹配收进来。
void appendMatches(QVector<Mention>* mentions, const QRegularExpression& pattern,
                   CrossReferenceKind kind, const QString& text) {
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(text);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        Mention mention;
        mention.kind = kind;
        mention.start = match.capturedStart();
        mention.length = match.capturedLength();
        const QString number = match.captured(1);
        if (kind == CrossReferenceKind::Page) {
            mention.pageNumber = number.toInt();
        } else {
            mention.number = number;
        }
        mention.text = match.captured();
        mentions->append(mention);
    }
}

// 把触发词并进命中的原文。
void attachTrigger(Mention* mention, const QString& text) {
    const int before = qMax(0, mention->start - 16);
    const QString prefix = text.mid(before, mention->start - before);
    const QRegularExpressionMatch trigger = triggerPattern().match(prefix);
    if (!trigger.hasMatch()) {
        return;
    }
    const int triggerStart = before + trigger.capturedStart();
    mention->length = (mention->start + mention->length) - triggerStart;
    mention->start = triggerStart;
    mention->text = text.mid(mention->start, mention->length);
}

// 这一页里所有「提到某页 / 某节」的说法，按位置排好。
QVector<Mention> collectMentions(const QString& text) {
    QVector<Mention> mentions;
    appendMatches(&mentions, cjkPagePattern(), CrossReferenceKind::Page, text);
    appendMatches(&mentions, cjkChapterPattern(), CrossReferenceKind::Chapter, text);
    appendMatches(&mentions, cjkSectionPattern(), CrossReferenceKind::Section, text);
    appendMatches(&mentions, enPagePattern(), CrossReferenceKind::Page, text);
    appendMatches(&mentions, enPageAbbrevPattern(), CrossReferenceKind::Page, text);
    appendMatches(&mentions, enChapterPattern(), CrossReferenceKind::Chapter, text);
    appendMatches(&mentions, enSectionPattern(), CrossReferenceKind::Section, text);
    for (Mention& mention : mentions) {
        attachTrigger(&mention, text);
    }
    std::sort(mentions.begin(), mentions.end(), [](const Mention& left, const Mention& right) {
        return left.start < right.start;
    });
    return mentions;
}

// 把相邻的说法合并成一处引用。
QVector<ReferenceSpan> groupMentions(const QString& text, const QVector<Mention>& mentions) {
    QVector<ReferenceSpan> spans;
    for (const Mention& mention : mentions) {
        if (!spans.isEmpty()) {
            ReferenceSpan& last = spans.last();
            const QString gap = text.mid(last.end, mention.start - last.end);
            if (connectorPattern().match(gap).hasMatch()) {
                last.end = mention.start + mention.length;
                last.mentions.append(mention);
                continue;
            }
        }
        ReferenceSpan span;
        span.start = mention.start;
        span.end = mention.start + mention.length;
        span.mentions.append(mention);
        spans.append(span);
    }
    return spans;
}

// 标题归一化：去掉空白、转小写。
//
// 为什么去空格：「第 3.2 节」与「3.2节」指的是同一节，
// 比对之前得先抹掉排版差异。
QString normalizeTitle(const QString& title) {
    QString out;
    for (const QChar ch : title) {
        if (ch.isSpace()) {
            continue;
        }
        out += ch.toLower();
    }
    return out;
}

// 这个标题能不能算「指向这个章节号 / 章号」的那一项。
//
// 为什么按"标题里带着这个编号"来判：文档里的章节标题通常就写成
// 「3.2 概述」或「第 3 章 概述」，编号本身就是标题的一部分。
//
// 两条防误判的规矩：
//   * 编号后面紧跟数字时不匹配（"3.2" 不能配到 "3.21"）；
//   * 编号后面紧跟小数点时不匹配（"第 3 章" 不能配到 "3.2 概述"，
//     那是 3 章下面的小节，不是 3 章本身）。
bool titleMatchesSection(const QString& normalizedTitle, const QString& number, bool chapter) {
    if (normalizedTitle.isEmpty() || number.isEmpty()) {
        return false;
    }
    const QString wanted = normalizeTitle(number);
    if (normalizedTitle.contains(QStringLiteral("第") + wanted + QStringLiteral("节"))) {
        return true;
    }
    if (normalizedTitle.contains(QStringLiteral("第") + wanted + QStringLiteral("章"))) {
        return true;
    }
    if (chapter && normalizedTitle.contains(QStringLiteral("chapter") + wanted)) {
        return true;
    }
    if (normalizedTitle.startsWith(wanted)) {
        const int next = normalizedTitle.size() > wanted.size()
                             ? normalizedTitle.at(wanted.size()).unicode()
                             : 0;
        if (next >= '0' && next <= '9') {
            return false;
        }
        if (next == '.') {
            return false;
        }
        return true;
    }
    return false;
}

// 两段置信度合成一段时，取**最不乐观**的那个。
//
// 为什么取最不乐观：一句里提到了两个目标，只要有一个没解析出来，
// 整句就不该被当成"已解析"。报乐观比报保守危险——
// 用户点过去发现页码不对，比没扫出来更让人不信任这个功能。
//
// 枚举值越大越不乐观（None > Medium > High），所以这里取大的。
CrossReferenceConfidence leastConfident(CrossReferenceConfidence left,
                                        CrossReferenceConfidence right) {
    return left > right ? left : right;
}

// PDF 字面字符串里的转义。
QByteArray escapeLiteralString(const QString& text) {
    QByteArray out;
    const QByteArray utf8 = text.toUtf8();
    for (const char ch : utf8) {
        if (ch == '(' || ch == ')' || ch == '\\') {
            out += '\\';
        }
        out += ch;
    }
    return out;
}

}  // namespace

bool CrossReference::isResolved() const {
    for (const CrossReferenceCandidate& candidate : candidates) {
        if (candidate.confidence != CrossReferenceConfidence::None) {
            return true;
        }
    }
    return false;
}

CrossReferenceScanResult scanCrossReferences(const QVector<CrossReferencePage>& pages,
                                             const std::vector<OutlineItem>& outline,
                                             int pageCount) {
    CrossReferenceScanResult result;
    const std::vector<OutlineEntry> entries = flattenOutline(outline);
    if (entries.empty()) {
        result.notes.append(QStringLiteral("这份文档没有书签，章节号与章号引用都只能算未解析"));
    }

    for (const CrossReferencePage& page : pages) {
        if (page.text.isEmpty()) {
            continue;
        }
        const QVector<Mention> mentions = collectMentions(page.text);
        if (mentions.isEmpty()) {
            continue;
        }
        const QVector<ReferenceSpan> spans = groupMentions(page.text, mentions);
        for (const ReferenceSpan& span : spans) {
            CrossReference reference;
            reference.text = page.text.mid(span.start, span.end - span.start);
            reference.sourcePageIndex = page.pageIndex;
            reference.startOffset = span.start;
            reference.length = span.end - span.start;

            CrossReferenceConfidence overall = CrossReferenceConfidence::High;
            for (const Mention& mention : span.mentions) {
                CrossReferenceCandidate candidate;
                candidate.referenceText = mention.text;
                if (mention.kind == CrossReferenceKind::Page) {
                    candidate.referencedPageNumber = mention.pageNumber;
                    if (pageCount > 0 && mention.pageNumber >= 1 && mention.pageNumber <= pageCount) {
                        // 文档里有这一页：直接命中。
                        candidate.pageIndex = mention.pageNumber - 1;
                        candidate.match = CrossReferenceMatch::PageNumber;
                        candidate.confidence = CrossReferenceConfidence::High;
                    } else if (pageCount <= 0) {
                        candidate.note = QStringLiteral("不知道这份文档有多少页，没法确认这个页码");
                    } else {
                        candidate.note = QStringLiteral("这份文档一共 %1 页，没有第 %2 页")
                                             .arg(pageCount)
                                             .arg(mention.pageNumber);
                    }
                } else {
                    candidate.sectionNumber = mention.number;
                    // 按标题找：书签里的标题一般就带着这个编号。
                    bool matched = false;
                    for (const OutlineEntry& entry : entries) {
                        const bool chapter = mention.kind == CrossReferenceKind::Chapter;
                        if (!titleMatchesSection(normalizeTitle(entry.title), mention.number, chapter)) {
                            continue;
                        }
                        candidate.pageIndex = entry.pageIndex;
                        candidate.title = entry.title;
                        candidate.match = CrossReferenceMatch::Title;
                        candidate.confidence = CrossReferenceConfidence::Medium;
                        if (entry.pageIndex < 0) {
                            candidate.note = QStringLiteral("书签「%1」没有有效的目标页").arg(entry.title);
                        }
                        matched = true;
                        break;
                    }
                    if (!matched) {
                        candidate.note = QStringLiteral("书签里找不到与「%1」对应的标题")
                                             .arg(mention.text);
                    }
                }
                // 整段的置信度取候选里最不乐观的那个。
                overall = leastConfident(overall, candidate.confidence);
                reference.candidates.append(candidate);
            }
            reference.confidence = overall;
            if (reference.isResolved()) {
                ++result.resolvedCount;
            } else {
                ++result.unresolvedCount;
                reference.note = QStringLiteral("没能解析出目标，需要人工确认");
            }
            result.references.append(reference);
        }
    }
    return result;
}

CrossReferenceLinkSpec crossReferenceLinkSpec(const CrossReference& reference, const QRectF& rect) {
    CrossReferenceLinkSpec spec;
    spec.sourcePageIndex = reference.sourcePageIndex;
    spec.rect = rect;
    spec.displayText = reference.text;
    for (const CrossReferenceCandidate& candidate : reference.candidates) {
        if (candidate.pageIndex >= 0) {
            spec.targetPageIndex = candidate.pageIndex;
            break;
        }
    }
    return spec;
}

CrossReferenceLinkResult insertCrossReferenceLink(const QByteArray& pdfBytes,
                                                  const CrossReferenceLinkSpec& spec) {
    CrossReferenceLinkResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("输入的 PDF 内容为空");
        return result;
    }
    if (spec.sourcePageIndex < 0 || spec.targetPageIndex < 0) {
        result.error = QStringLiteral("引用所在页或目标页无效");
        return result;
    }
    if (spec.rect.width() <= 0.0 || spec.rect.height() <= 0.0) {
        result.error = QStringLiteral("引用文字在页面上的矩形无效");
        return result;
    }

    QByteArray dictionary = "<< /Type /Annot /Subtype /Link /Rect [";
    dictionary += QByteArray::number(spec.rect.left(), 'f', 2) + ' ';
    dictionary += QByteArray::number(spec.rect.top(), 'f', 2) + ' ';
    dictionary += QByteArray::number(spec.rect.right(), 'f', 2) + ' ';
    dictionary += QByteArray::number(spec.rect.bottom(), 'f', 2);
    dictionary += "] /Border [0 0 0] /Contents (";
    dictionary += escapeLiteralString(spec.displayText);
    dictionary += ") /Dest [@PAGE0 0 R /Fit] >>";

    InlineAnnotationInsert insert;
    insert.pageIndex = spec.sourcePageIndex;
    insert.dictionary = dictionary;
    insert.targetPages.append(spec.targetPageIndex);
    const InlineWriteResult written = injectInlineAnnotations(pdfBytes, {insert});
    if (!written.ok) {
        result.error = written.error;
        return result;
    }
    result.bytes = written.bytes;
    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
