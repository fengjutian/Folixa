#include "doc/ChunkBuilder.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace aipdf::doc {

namespace {

// 行与行之间的行距（点）：上一行的下沿到下一行的上沿。
// PDF user space 的 y 向上，所以「下一行更靠下」= y 更小。
double verticalGap(const ChunkLine& upper, const ChunkLine& lower) {
    const double upperBottom = upper.box.y();
    const double lowerTop = lower.box.y() + lower.box.height();
    return upperBottom - lowerTop;
}

// 行高里的中位数：用它当正文字高。
double medianHeight(const QVector<ChunkLine>& lines) {
    QVector<double> heights;
    heights.reserve(lines.size());
    for (const ChunkLine& line : lines) {
        if (line.charHeight > 0.0) {
            heights.append(line.charHeight);
        }
    }
    if (heights.isEmpty()) {
        return 0.0;
    }
    std::sort(heights.begin(), heights.end());
    return heights.at(heights.size() / 2);
}

// 行距的中位数：正文行距。
double medianGap(const QVector<ChunkLine>& lines) {
    QVector<double> gaps;
    gaps.reserve(lines.size());
    for (int index = 1; index < lines.size(); ++index) {
        const double gap = verticalGap(lines.at(index - 1), lines.at(index));
        if (gap > 0.0) {
            gaps.append(gap);
        }
    }
    if (gaps.isEmpty()) {
        return 0.0;
    }
    std::sort(gaps.begin(), gaps.end());
    return gaps.at(gaps.size() / 2);
}

// 字高比 → 标题层级。比例越大，标题层级越高（1 是最大的）。
//
// 阈值比较留一点余量：字高是从字符盒量出来的，不是标称字号，
// 19.2 / 12 在二进制浮点里可能是 1.5999999999999999，
// 卡在阈值上被判成低一级并不是想要的结果。
int headingLevelFor(double ratio) {
    constexpr double kEpsilon = 1e-6;
    if (ratio >= 2.0 - kEpsilon) {
        return 1;
    }
    if (ratio >= 1.6 - kEpsilon) {
        return 2;
    }
    if (ratio >= 1.35 - kEpsilon) {
        return 3;
    }
    return 4;
}

bool isCjk(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) ||   // CJK 统一表意文字
           (code >= 0x3400 && code <= 0x4DBF) ||   // 扩展 A
           (code >= 0xF900 && code <= 0xFAFF) ||   // 兼容表意文字
           (code >= 0x3040 && code <= 0x30FF);     // 日文假名
}

}  // namespace

// 按字符数加权的字高中位数：当正文字高。
//
// 为什么不用普通中位数：一页上往往只有一两个大标题，普通中位数会被标题带走。
// 例如「标题 32pt + 正文 14pt + 正文 14pt」普通中位数是 14（碰巧对），
// 但「标题 18pt + 正文 14pt」普通中位数是 18——正文被当成了标题。
// 按字符数加权之后，谁的字符多谁说话权重就大，标题压不住正文。
double ChunkBuilder::estimateBodyHeight(const QVector<HeightSample>& samples) {
    if (samples.isEmpty()) {
        return 0.0;
    }
    QVector<HeightSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(),
              [](const HeightSample& left, const HeightSample& right) {
                  return left.charHeight < right.charHeight;
              });
    int total = 0;
    for (const HeightSample& sample : sorted) {
        total += sample.charCount;
    }
    if (total <= 0) {
        return sorted.at(sorted.size() / 2).charHeight;
    }
    int cumulative = 0;
    for (const HeightSample& sample : sorted) {
        cumulative += sample.charCount;
        if (cumulative * 2 >= total) {
            return sample.charHeight;
        }
    }
    return sorted.last().charHeight;
}

QRectF DocumentChunk::bounds() const {
    QRectF result;
    for (const QRectF& box : lineBoxes) {
        result = result.isNull() ? box : result.united(box);
    }
    return result;
}

int ChunkBuilder::countWords(const QString& text) {
    int words = 0;
    bool inWord = false;
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            inWord = false;
            continue;
        }
        if (isCjk(ch)) {
            // 中文按字算：每个字就是一个词。
            ++words;
            inWord = false;
            continue;
        }
        if (!inWord) {
            ++words;
            inWord = true;
        }
    }
    return words;
}

QVector<DocumentChunk> ChunkBuilder::build(int pageIndex,
                                           const QVector<ChunkLine>& lines,
                                           const QString& sectionPath,
                                           int startOrder) {
    return build(pageIndex, lines, sectionPath, startOrder, Options(), QString());
}

QVector<DocumentChunk> ChunkBuilder::build(int pageIndex,
                                           const QVector<ChunkLine>& lines,
                                           const QString& sectionPath,
                                           int startOrder,
                                           const Options& options,
                                           const QString& idPrefix) {
    QVector<DocumentChunk> chunks;

    // 空行不参与分块。
    QVector<ChunkLine> usable;
    usable.reserve(lines.size());
    for (const ChunkLine& line : lines) {
        if (!line.text.trimmed().isEmpty()) {
            usable.append(line);
        }
    }
    if (usable.isEmpty()) {
        return chunks;
    }

    // 正文字高：优先用调用方给的参考值（整篇文档统一），
    // 没有就用本页行的中位数。
    const double bodyHeight = options.referenceCharHeight > 0.0
                                  ? options.referenceCharHeight
                                  : medianHeight(usable);
    const double bodyGap = medianGap(usable);
    // 正文行距为 0（整页就是几行大字）时，段落判定只能靠缩进。
    const double paragraphGap = bodyGap * options.paragraphGapFactor;

    // 第一步：按排版特征把行归成组。
    struct Group {
        QVector<int> lineIndexes;
        bool heading = false;
        double charHeight = 0.0;
    };
    QVector<Group> groups;

    for (int index = 0; index < usable.size(); ++index) {
        const ChunkLine& line = usable.at(index);
        // 阈值留一点余量：字高是量出来的，卡在边界上不该抖。
        const bool heading = bodyHeight > 0.0 && line.charHeight > 0.0 &&
                             line.charHeight >= bodyHeight * options.headingHeightFactor - 1e-6;
        bool startsGroup = groups.isEmpty() || heading || groups.last().heading;
        if (!startsGroup) {
            const ChunkLine& previous = usable.at(index - 1);
            const double gap = verticalGap(previous, line);
            const bool bigGap = bodyGap > 0.0 && gap > paragraphGap;
            const bool indentChanged =
                std::abs(previous.box.x() - line.box.x()) > options.indentTolerance;
            if (bigGap || indentChanged) {
                startsGroup = true;
            }
        }
        if (startsGroup) {
            groups.append(Group());
            groups.last().heading = heading;
            groups.last().charHeight = line.charHeight;
        }
        groups.last().lineIndexes.append(index);
    }

    // 第二步：太短的正文块并进下一块（标题块不并）。
    QVector<Group> merged;
    for (const Group& group : groups) {
        int chars = 0;
        for (int index : group.lineIndexes) {
            chars += usable.at(index).text.trimmed().size();
        }
        const bool tooShort = !group.heading && chars < options.minChunkChars;
        if (tooShort && !merged.isEmpty() && !merged.last().heading) {
            for (int index : group.lineIndexes) {
                merged.last().lineIndexes.append(index);
            }
            continue;
        }
        merged.append(group);
    }

    // 第三步：出块。太长的按行切开。
    int order = startOrder;
    int blockIndex = 0;
    const QString prefix = idPrefix.isEmpty() ? QStringLiteral("p%1").arg(pageIndex) : idPrefix;
    for (const Group& group : merged) {
        QVector<QVector<int>> pieces;
        QVector<int> current;
        int currentChars = 0;
        for (int index : group.lineIndexes) {
            const int chars = usable.at(index).text.trimmed().size();
            if (!current.isEmpty() && currentChars + chars > options.maxChunkChars) {
                pieces.append(current);
                current.clear();
                currentChars = 0;
            }
            current.append(index);
            currentChars += chars;
        }
        if (!current.isEmpty()) {
            pieces.append(current);
        }

        for (const QVector<int>& piece : pieces) {
            DocumentChunk chunk;
            chunk.pageIndex = pageIndex;
            chunk.order = order++;
            chunk.sectionPath = sectionPath;
            // id 用「内容前缀 + 页内块序号」：同一页重建索引后 id 不变，
            // 外部引用（搜索结果、引用出处）不会因为前面几页变了就错位。
            chunk.id = QStringLiteral("%1b%2").arg(prefix).arg(blockIndex);
            ++blockIndex;
            QStringList textParts;
            for (int index : piece) {
                const ChunkLine& line = usable.at(index);
                textParts.append(line.text.trimmed());
                chunk.lineBoxes.append(line.box);
            }
            chunk.text = textParts.join(QLatin1Char('\n'));
            chunk.wordCount = countWords(chunk.text);
            if (group.heading && group.charHeight > 0.0 && bodyHeight > 0.0) {
                chunk.headingLevel = headingLevelFor(group.charHeight / bodyHeight);
            }
            if (chunk.isValid()) {
                chunks.append(chunk);
            }
        }
    }
    return chunks;
}

}  // namespace aipdf::doc
