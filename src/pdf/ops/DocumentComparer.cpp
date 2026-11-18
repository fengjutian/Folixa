#include "pdf/ops/DocumentComparer.h"

#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/TextPage.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

// 页面相似度矩阵最多算多少个格子（页数 × 页数）。
//
// 为什么要有上限：矩阵是 O(n×m) 的，两份各 500 页就是 25 万个格子，
// 再多就会让「比较」变成一件要等很久的事。超限时退化成按下标配对，
// 多出来的页照报「新增/删除」，只是配对不再那么聪明。
constexpr qint64 kMaxPageAlignmentCells = 200000;

// 默认的页数上限：页数超过它就不做序列对齐。
//
// 为什么按页数再卡一道：格子数那关挡的是「两份都很长」，
// 按页数卡更直观，界面上也能把「这份文档太长，没做对齐」直接说清楚。
constexpr int kDefaultMaxAlignmentPages = 400;

// 一页最多取多少个词元参与相似度计算。
//
// 为什么截断：一页的文字可以上万字，词元集合跟着涨；
// 相似度矩阵是页数×页数，每多一个词元就多一次哈希查找。
constexpr int kMaxTokensPerPage = 128;

// 单页最多取多少个块参与比较（见 DocumentCompareOptions::maxBlocksPerPage）。
//
// 取 400 是权衡出来的：块对齐是 O(n×m) 的动态规划，
// 400×400 是 16 万个格子（1.3 MB），还能接受；
// 再往上，一页塞满文字的畸形文档就能把内存吃光。
constexpr int kDefaultMaxBlocksPerPage = 400;

// 两个文本都短于这个长度时用 Levenshtein，更长时改用二元组 Dice。
constexpr int kLevenshteinMaxLength = 64;

// 一行里两段文字的横向间隔超过这个点数就当成两块。
constexpr double kMinimumBlockGapPoints = 12.0;

// 归一化：连续空白压成一个空格，首尾空白去掉。
//
// 为什么比较前要归一化：PDF 里同一个词可能被拆成好几个文本对象，
// 排版软件改版后空白也会变。不压掉空白的话，一处纯排版改动
// 会被报成「文字被改过」，把真正的差异淹掉。
QString normalizedForCompare(const QString& text) {
    QString result;
    result.reserve(text.size());
    bool pendingSpace = false;
    for (const QChar character : text) {
        if (character.isSpace()) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !result.isEmpty()) {
            result.append(QLatin1Char(' '));
        }
        pendingSpace = false;
        result.append(character);
    }
    return result;
}

// 归一化 Levenshtein：1 - 编辑距离 / 较长串长度。
double levenshteinRatio(const QString& left, const QString& right) {
    const int leftSize = left.size();
    const int rightSize = right.size();
    if (leftSize == 0 && rightSize == 0) {
        return 1.0;
    }
    if (leftSize == 0 || rightSize == 0) {
        return 0.0;
    }

    QVector<int> previous(rightSize + 1);
    QVector<int> current(rightSize + 1);
    for (int index = 0; index <= rightSize; ++index) {
        previous[index] = index;
    }
    for (int row = 1; row <= leftSize; ++row) {
        current[0] = row;
        const QChar leftCharacter = left.at(row - 1);
        for (int column = 1; column <= rightSize; ++column) {
            const int cost = (leftCharacter == right.at(column - 1)) ? 0 : 1;
            const int deletion = current[column - 1] + 1;
            const int insertion = previous[column] + 1;
            const int substitution = previous[column - 1] + cost;
            current[column] = qMin(deletion, qMin(insertion, substitution));
        }
        previous.swap(current);
    }
    const int distance = previous.at(rightSize);
    const int longest = qMax(leftSize, rightSize);
    return 1.0 - static_cast<double>(distance) / static_cast<double>(longest);
}

// 二元组 Dice 系数：2 × 共同二元组数 / 两串二元组总数。
//
// 为什么长文本不用 Levenshtein：一万字的段落做编辑距离要一亿次比较，
// 界面上会直接卡住。二元组 Dice 是 O(n) 的，牺牲一点精度换来不卡。
double bigramDice(const QString& left, const QString& right) {
    if (left == right) {
        return 1.0;
    }
    if (left.size() < 2 || right.size() < 2) {
        return 0.0;
    }
    QHash<quint32, int> leftCounts;
    leftCounts.reserve(left.size());
    for (int index = 0; index + 1 < left.size(); ++index) {
        const quint32 key = (static_cast<quint32>(left.at(index).unicode()) << 16)
            | static_cast<quint32>(left.at(index + 1).unicode());
        leftCounts[key] += 1;
    }
    int common = 0;
    for (int index = 0; index + 1 < right.size(); ++index) {
        const quint32 key = (static_cast<quint32>(right.at(index).unicode()) << 16)
            | static_cast<quint32>(right.at(index + 1).unicode());
        auto iterator = leftCounts.find(key);
        if (iterator != leftCounts.end() && iterator.value() > 0) {
            ++common;
            iterator.value() -= 1;
        }
    }
    const double total = static_cast<double>(left.size() + right.size() - 2);
    return total > 0.0 ? (2.0 * common) / total : 0.0;
}

// FNV-1a：给块文本算一个哈希，用来快速比页面。
quint64 hashText(const QString& text) {
    quint64 hash = 1469598103934665603ULL;
    for (const QChar character : text) {
        hash ^= static_cast<quint64>(character.unicode());
        hash *= 1099511628211ULL;
    }
    return hash;
}

// 两个指纹的 Jaccard 相似度（共同 / 并集）。
//
// 为什么做成模板：页级相似度要同时算「块文本哈希集合」与「词元集合」的重合度，
// 两者都是 QSet，只是元素类型不同。
template <typename T>
double jaccard(const QSet<T>& left, const QSet<T>& right) {
    if (left.isEmpty() && right.isEmpty()) {
        // 两页都没有文字：内容一样（都可能是扫描件），不能因为空集就说成毫不相干。
        return 1.0;
    }
    if (left.isEmpty() || right.isEmpty()) {
        return 0.0;
    }
    int common = 0;
    for (const T& value : left) {
        if (right.contains(value)) {
            ++common;
        }
    }
    const int unionSize = left.size() + right.size() - common;
    return unionSize > 0 ? static_cast<double>(common) / static_cast<double>(unionSize) : 0.0;
}

// 一对配对（保序对齐的结果）。
struct SequencePair {
    int oldIndex = -1;
    int newIndex = -1;
    double similarity = 0.0;
};

// 保序对齐：在两个序列之间配对，使总相似度最大。
//
// 为什么不用「找相同项」那种 diff：只找相同项的话，一段被改过的文字
// 会被拆成「一整段删除 + 一整段新增」，用户看不出「这里是改了几个字」。
// 按相似度配对才能报成「修改」，并给出相似度。
QVector<SequencePair> alignBySimilarity(const QVector<QVector<double>>& similarity) {
    QVector<SequencePair> pairs;
    const int oldCount = similarity.size();
    const int newCount = oldCount > 0 ? similarity.at(0).size() : 0;
    if (oldCount == 0 || newCount == 0) {
        return pairs;
    }

    QVector<QVector<double>> best(oldCount + 1, QVector<double>(newCount + 1, 0.0));
    for (int oldIndex = 1; oldIndex <= oldCount; ++oldIndex) {
        for (int newIndex = 1; newIndex <= newCount; ++newIndex) {
            const double paired = best.at(oldIndex - 1).at(newIndex - 1)
                + similarity.at(oldIndex - 1).at(newIndex - 1);
            const double skipOld = best.at(oldIndex - 1).at(newIndex);
            const double skipNew = best.at(oldIndex).at(newIndex - 1);
            best[oldIndex][newIndex] = qMax(paired, qMax(skipOld, skipNew));
        }
    }

    // 回溯。配对与「跳过」同分时优先配对：
    // 相似度为 0 的两个页面配成一对，后面会被阈值判成「删页 + 加页」，
    // 结果与不配对一样，但不会把后面的页错位。
    int oldIndex = oldCount;
    int newIndex = newCount;
    while (oldIndex > 0 && newIndex > 0) {
        const double paired = best.at(oldIndex - 1).at(newIndex - 1)
            + similarity.at(oldIndex - 1).at(newIndex - 1);
        if (best.at(oldIndex).at(newIndex) <= paired + 1e-9) {
            pairs.append({oldIndex - 1, newIndex - 1,
                          similarity.at(oldIndex - 1).at(newIndex - 1)});
            --oldIndex;
            --newIndex;
        } else if (best.at(oldIndex).at(newIndex) <= best.at(oldIndex - 1).at(newIndex) + 1e-9) {
            --oldIndex;
        } else {
            --newIndex;
        }
    }
    std::reverse(pairs.begin(), pairs.end());
    return pairs;
}

// ------------------------------------------------------------ 页面指纹与对齐 ----

// 一页的指纹：判断「这两页是不是同一页」的全部依据。
//
// 为什么指纹里要同时放块数与字符数：光看块文本的哈希集合，
// 「一页只有一个块」与「一页有两个一样的块」会算出同一个指纹。
struct PageFingerprint {
    // 归一化后的页文本（块按阅读顺序拼接，空白压成单空格）。
    QString normalizedText;
    // 页里出现过的块文本哈希（算块级重合度用）。
    QSet<quint64> blockHashes;
    // 页里出现过的词元（算词级重合度用）。
    QSet<QString> tokens;
    // 块数与字符数（字符数 = 各块归一化之后的长度之和）。
    int blockCount = 0;
    int characterCount = 0;
    // 内容哈希：排序后的块哈希 + 块数 + 字符数。
    //
    // 为什么先排序再算：指纹必须与块在页上的先后顺序无关，
    // 否则同一页仅仅换了排版顺序，就会被当成「整页被替换」。
    quint64 contentHash = 0;
};

// 把文本切成词元：小写化后按非字母数字切开。
//
// 为什么按词切而不是按字切：中文按字切的话，两页中文的常用字都一样，
// 重合度会虚高到「看起来是同一页」。按词切至少能分清「条款」和「金额」。
QSet<QString> tokenizeForPage(const QString& text) {
    QSet<QString> tokens;
    QString current;
    current.reserve(32);
    const auto flush = [&]() {
        if (!current.isEmpty()) {
            tokens.insert(current);
            current.clear();
        }
    };
    for (const QChar character : text) {
        if (tokens.size() >= kMaxTokensPerPage) {
            break;
        }
        if (character.isLetterOrNumber()) {
            current.append(character.toLower());
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

// 给一页算指纹。
//
// 为什么指纹要稳定：同一页在两份文档里内容相同就必须算出同一个指纹，
// 否则「这一页没动」会被报成「这一页被替换了」。
PageFingerprint fingerprintOfPage(const QVector<TextBlock>& blocks) {
    PageFingerprint fingerprint;
    fingerprint.blockCount = blocks.size();

    QStringList normalizedBlocks;
    normalizedBlocks.reserve(blocks.size());
    for (const TextBlock& block : blocks) {
        const QString normalized = normalizedForCompare(block.text);
        normalizedBlocks.append(normalized);
        fingerprint.characterCount += normalized.size();
        fingerprint.blockHashes.insert(hashText(normalized));
    }
    fingerprint.normalizedText = normalizedBlocks.join(QLatin1Char(' '));
    fingerprint.tokens = tokenizeForPage(fingerprint.normalizedText);

    // 内容哈希：排序后的块哈希 + 块数 + 字符数。
    QVector<quint64> sortedHashes = fingerprint.blockHashes.values();
    std::sort(sortedHashes.begin(), sortedHashes.end());
    quint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](quint64 value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (quint64 value : sortedHashes) {
        mix(value);
    }
    mix(static_cast<quint64>(fingerprint.blockCount));
    mix(static_cast<quint64>(fingerprint.characterCount));
    fingerprint.contentHash = hash;
    return fingerprint;
}

// 两页的相似度（0..1）。
//
// 为什么是「块级重合」与「词级重合」各占一半：
//   - 块级重合（同一页里有多少块一个字不差）是最硬的证据；
//   - 词级重合能认出「一页只有一个块、那个块改了几个字」的情况，
//     这时块级重合是 0，只看块会把明明是同一页的判成「整页替换」；
//   - 各占一半是为了不让任何一边单独做主：只看词级会把两页样板文字
//     当成同一页，只看块级又会漏掉单块页的改字。
double pageSimilarity(const PageFingerprint& left, const PageFingerprint& right) {
    const double blockOverlap = jaccard(left.blockHashes, right.blockHashes);
    const double tokenOverlap = jaccard(left.tokens, right.tokens);
    return 0.5 * blockOverlap + 0.5 * tokenOverlap;
}

// 一次页对齐的结果。
struct PageAlignment {
    // 配上的页对（oldIndex / newIndex / 相似度）。
    //
    // 这里只放「配上了哪些页」：采不采用这套配对由调用方比过按位置的口径再定，
    // 结果字段（alignmentApplied / alignmentMode / alignmentNote）也由调用方填。
    QVector<SequencePair> pairs;
};

// 跨文档的页序列对齐：先按指纹精确配对，再按相似度配剩下的页。
//
// 为什么分两步：
//   - 指纹相同的页直接配上，而且**不看先后顺序** —— 这样「把第 2 页挪到最后」
//     也能配上。只用保序的动态规划（LCS/NW）在这种情况下最多配上一半，
//     挪走的那一页会被说成「删了一页又加了一页」。
//   - 剩下的页再按相似度从高到低贪心配，让「改过几个字的同一页」也能配上。
//
// 为什么相似度要设门槛：毫不相干的两页配成一对，
// 用户看到的是「这两页被互相改过」，比「一页删了一页」更难懂。
PageAlignment alignPageSequences(const QVector<PageFingerprint>& oldFingerprints,
                                  const QVector<PageFingerprint>& newFingerprints,
                                  double pageThreshold) {
    PageAlignment alignment;
    const int oldCount = oldFingerprints.size();
    const int newCount = newFingerprints.size();
    if (oldCount == 0 || newCount == 0) {
        return alignment;
    }

    // 相似度矩阵：页对页一次算完，后面的配对都从这里取值。
    QVector<QVector<double>> similarity(oldCount, QVector<double>(newCount, 0.0));
    for (int oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
        for (int newIndex = 0; newIndex < newCount; ++newIndex) {
            similarity[oldIndex][newIndex] =
                pageSimilarity(oldFingerprints.at(oldIndex), newFingerprints.at(newIndex));
        }
    }

    QVector<bool> oldUsed(oldCount, false);
    QVector<bool> newUsed(newCount, false);

    // 第一步：指纹完全相同的页配上（不看成对顺序，所以支持页移动）。
    //
    // 为什么同一个指纹按「第 k 份配第 k 份」而不是随便挑：
    // 文档里可能有几页长得一模一样，随机挑会让重复页的配对飘忽不定。
    QHash<quint64, QVector<int>> newByHash;
    for (int newIndex = 0; newIndex < newCount; ++newIndex) {
        newByHash[newFingerprints.at(newIndex).contentHash].append(newIndex);
    }
    QHash<quint64, int> usedByHash;
    for (int oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
        const quint64 hash = oldFingerprints.at(oldIndex).contentHash;
        const QVector<int> candidates = newByHash.value(hash);
        const int used = usedByHash.value(hash, 0);
        if (used >= candidates.size()) {
            continue;
        }
        usedByHash.insert(hash, used + 1);
        oldUsed[oldIndex] = true;
        newUsed[candidates.at(used)] = true;
        alignment.pairs.append({oldIndex, candidates.at(used), 1.0});
    }

    // 第二步：剩下的页按相似度从高到低配（只配相似度够得着的）。
    struct Candidate {
        int oldIndex = -1;
        int newIndex = -1;
        double similarity = 0.0;
    };
    QVector<Candidate> candidates;
    for (int oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
        if (oldUsed.at(oldIndex)) {
            continue;
        }
        for (int newIndex = 0; newIndex < newCount; ++newIndex) {
            if (newUsed.at(newIndex)) {
                continue;
            }
            const double score = similarity.at(oldIndex).at(newIndex);
            if (score >= pageThreshold) {
                candidates.append({oldIndex, newIndex, score});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& left, const Candidate& right) {
                  // 同分时按页码定序：同样的输入必须给出同样的配对。
                  if (left.similarity != right.similarity) {
                      return left.similarity > right.similarity;
                  }
                  if (left.oldIndex != right.oldIndex) {
                      return left.oldIndex < right.oldIndex;
                  }
                  return left.newIndex < right.newIndex;
              });
    for (const Candidate& candidate : candidates) {
        if (oldUsed.at(candidate.oldIndex) || newUsed.at(candidate.newIndex)) {
            continue;
        }
        oldUsed[candidate.oldIndex] = true;
        newUsed[candidate.newIndex] = true;
        alignment.pairs.append({candidate.oldIndex, candidate.newIndex, candidate.similarity});
    }

    // 页对按原文页码排序：后面的差异排序与像素比对都按这个顺序走。
    std::sort(alignment.pairs.begin(), alignment.pairs.end(),
              [](const SequencePair& left, const SequencePair& right) {
                  return left.oldIndex < right.oldIndex;
              });
    return alignment;
}

// 把一页的字符聚成文字块。
//
// PDFium 给的字符顺序是内容流顺序（不保证视觉顺序），所以这里按坐标重排：
// 先按纵向位置分行，行内再按横向位置排序，横向间隔过大处切开成不同的块。
QVector<TextBlock> groupIntoBlocks(const TextPage& textPage, int pageIndex) {
    struct Item {
        QString text;
        QRectF box;
    };

    QVector<Item> items;
    items.reserve(textPage.chars.size());
    for (const TextChar& character : textPage.chars) {
        if (character.text.isEmpty()) {
            continue;
        }
        items.append({character.text, character.box});
    }
    if (items.isEmpty()) {
        return {};
    }

    std::sort(items.begin(), items.end(), [](const Item& left, const Item& right) {
        const double leftCenter = left.box.center().y();
        const double rightCenter = right.box.center().y();
        if (std::abs(leftCenter - rightCenter) > 1e-6) {
            return leftCenter > rightCenter;  // y 大的在上面（PDF user space 向上为正）
        }
        return left.box.left() < right.box.left();
    });

    // 分行：两个字符的竖直区间重叠超过较矮者的 40% 就算同一行。
    struct Line {
        QVector<int> indices;
        double top = 0.0;
        double bottom = 0.0;
    };
    QVector<Line> lines;
    for (int index = 0; index < items.size(); ++index) {
        const QRectF& box = items.at(index).box;
        int target = -1;
        double bestOverlap = 0.0;
        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const Line& line = lines.at(lineIndex);
            const double overlap = qMin(line.bottom, box.bottom()) - qMax(line.top, box.top());
            const double minHeight = qMin(line.bottom - line.top, box.height());
            if (minHeight > 0.0 && overlap > bestOverlap) {
                bestOverlap = overlap;
                target = lineIndex;
            }
        }
        const double charHeight = items.at(index).box.height();
        // 竖直区间重叠不到较矮者的 40% 就另起一行。
        const double requiredOverlap = 0.4 * (charHeight > 0.0 ? charHeight : 1.0);
        if (target < 0 || bestOverlap < requiredOverlap) {
            Line line;
            line.indices.append(index);
            line.top = box.top();
            line.bottom = box.bottom();
            lines.append(line);
            continue;
        }
        Line& line = lines[target];
        line.indices.append(index);
        line.top = qMin(line.top, box.top());
        line.bottom = qMax(line.bottom, box.bottom());
    }

    QVector<TextBlock> blocks;
    for (const Line& line : lines) {
        QVector<int> ordered = line.indices;
        std::sort(ordered.begin(), ordered.end(), [&items](int left, int right) {
            return items.at(left).box.left() < items.at(right).box.left();
        });

        // 行的典型字高：用来判断「多大的空隙算换块」。
        QVector<double> heights;
        heights.reserve(ordered.size());
        for (int index : ordered) {
            const double height = items.at(index).box.height();
            if (height > 0.0) {
                heights.append(height);
            }
        }
        std::sort(heights.begin(), heights.end());
        const double medianHeight = heights.isEmpty() ? 0.0 : heights.at(heights.size() / 2);
        const double gapThreshold = qMax(kMinimumBlockGapPoints, 1.5 * medianHeight);

        QString currentText;
        QRectF currentBounds;
        int currentCount = 0;
        double previousRight = 0.0;
        bool hasPrevious = false;

        const auto flush = [&]() {
            if (currentCount == 0) {
                return;
            }
            TextBlock block;
            block.pageIndex = pageIndex;
            block.blockIndex = blocks.size();
            block.text = currentText.trimmed();
            block.bounds = currentBounds;
            if (!block.text.isEmpty()) {
                blocks.append(block);
            }
            currentText.clear();
            currentBounds = QRectF();
            currentCount = 0;
        };

        for (int index : ordered) {
            const Item& item = items.at(index);
            if (hasPrevious) {
                const double gap = item.box.left() - previousRight;
                if (gap > gapThreshold) {
                    flush();
                }
            }
            currentText += item.text;
            if (currentCount == 0) {
                currentBounds = item.box;
            } else {
                currentBounds = currentBounds.united(item.box);
            }
            ++currentCount;
            previousRight = qMax(previousRight, item.box.right());
            hasPrevious = true;
        }
        flush();
    }

    return blocks;
}

// 页面的摘要文字：差异条目里要给人看的一段。
//
// 为什么截断：整页文字可能有几千字，塞进一个表格单元格会没法看。
QString pageSnippet(const QVector<TextBlock>& blocks, int maximumChars = 240) {
    QString text;
    for (const TextBlock& block : blocks) {
        if (!text.isEmpty()) {
            text.append(QLatin1Char('\n'));
        }
        text.append(block.text);
        if (text.size() >= maximumChars) {
            break;
        }
    }
    text = text.trimmed();
    if (text.size() > maximumChars) {
        text = text.left(maximumChars) + QStringLiteral("…");
    }
    return text;
}

// 比一页里的块。
void compareBlocksOfPage(const QVector<TextBlock>& oldBlocks,
                         const QVector<TextBlock>& newBlocks,
                         int oldPageIndex,
                         int newPageIndex,
                         const DocumentCompareOptions& options,
                         DocumentCompareResult* result) {
    if (oldBlocks.isEmpty() && newBlocks.isEmpty()) {
        return;
    }

    QVector<QVector<double>> similarity(oldBlocks.size(), QVector<double>(newBlocks.size(), 0.0));
    for (int oldIndex = 0; oldIndex < oldBlocks.size(); ++oldIndex) {
        for (int newIndex = 0; newIndex < newBlocks.size(); ++newIndex) {
            similarity[oldIndex][newIndex] = textSimilarity(oldBlocks.at(oldIndex).text,
                                                            newBlocks.at(newIndex).text);
        }
    }

    const QVector<SequencePair> pairs = alignBySimilarity(similarity);
    QVector<bool> oldMatched(oldBlocks.size(), false);
    QVector<bool> newMatched(newBlocks.size(), false);

    for (const SequencePair& pair : pairs) {
        oldMatched[pair.oldIndex] = true;
        newMatched[pair.newIndex] = true;
        const TextBlock& oldBlock = oldBlocks.at(pair.oldIndex);
        const TextBlock& newBlock = newBlocks.at(pair.newIndex);

        if (pair.similarity < options.modifyThreshold) {
            // 相似度太低，说「修改」会误导：按「删掉 + 新增」报。
            CompareDifference deleted;
            deleted.kind = DiffKind::Deleted;
            deleted.oldPageIndex = oldPageIndex;
            deleted.newPageIndex = newPageIndex;
            deleted.oldBlockIndex = oldBlock.blockIndex;
            deleted.oldText = oldBlock.text;
            deleted.similarity = pair.similarity;
            deleted.oldBounds = oldBlock.bounds;
            result->differences.append(deleted);
            ++result->deletedBlockCount;

            CompareDifference inserted;
            inserted.kind = DiffKind::Inserted;
            inserted.oldPageIndex = oldPageIndex;
            inserted.newPageIndex = newPageIndex;
            inserted.newBlockIndex = newBlock.blockIndex;
            inserted.newText = newBlock.text;
            inserted.similarity = pair.similarity;
            inserted.newBounds = newBlock.bounds;
            result->differences.append(inserted);
            ++result->insertedBlockCount;
            continue;
        }

        if (oldBlock.text == newBlock.text) {
            ++result->unchangedBlockCount;
            continue;
        }

        CompareDifference modified;
        modified.kind = DiffKind::Modified;
        modified.oldPageIndex = oldPageIndex;
        modified.newPageIndex = newPageIndex;
        modified.oldBlockIndex = oldBlock.blockIndex;
        modified.newBlockIndex = newBlock.blockIndex;
        modified.oldText = oldBlock.text;
        modified.newText = newBlock.text;
        modified.similarity = pair.similarity;
        modified.oldBounds = oldBlock.bounds;
        modified.newBounds = newBlock.bounds;
        result->differences.append(modified);
        ++result->modifiedBlockCount;
    }

    // 没有配上的块：一边有一边没有。
    for (int oldIndex = 0; oldIndex < oldBlocks.size(); ++oldIndex) {
        if (oldMatched.at(oldIndex)) {
            continue;
        }
        const TextBlock& oldBlock = oldBlocks.at(oldIndex);
        CompareDifference deleted;
        deleted.kind = DiffKind::Deleted;
        deleted.oldPageIndex = oldPageIndex;
        deleted.newPageIndex = newPageIndex;
        deleted.oldBlockIndex = oldBlock.blockIndex;
        deleted.oldText = oldBlock.text;
        deleted.oldBounds = oldBlock.bounds;
        result->differences.append(deleted);
        ++result->deletedBlockCount;
    }
    for (int newIndex = 0; newIndex < newBlocks.size(); ++newIndex) {
        if (newMatched.at(newIndex)) {
            continue;
        }
        const TextBlock& newBlock = newBlocks.at(newIndex);
        CompareDifference inserted;
        inserted.kind = DiffKind::Inserted;
        inserted.oldPageIndex = oldPageIndex;
        inserted.newPageIndex = newPageIndex;
        inserted.newBlockIndex = newBlock.blockIndex;
        inserted.newText = newBlock.text;
        inserted.newBounds = newBlock.bounds;
        result->differences.append(inserted);
        ++result->insertedBlockCount;
    }
}

// 差异条目的排序键：先按要显示的页码，再按块序号。
int displayPageOf(const CompareDifference& difference) {
    return difference.newPageIndex >= 0 ? difference.newPageIndex : difference.oldPageIndex;
}

int displayBlockOf(const CompareDifference& difference) {
    return difference.newBlockIndex >= 0 ? difference.newBlockIndex : difference.oldBlockIndex;
}

int kindOrder(DiffKind kind) {
    switch (kind) {
        case DiffKind::Modified:
            return 0;
        case DiffKind::Inserted:
            return 1;
        case DiffKind::Deleted:
            return 2;
        case DiffKind::PageAdded:
            return 3;
        case DiffKind::PageRemoved:
            return 4;
    }
    return 5;
}

// ============================================================ 像素级比对 =========

// 像素比对的默认页数上限。
//
// 渲染是 O(页数 × 像素数) 的重活：612×400 的页面在 72 DPI 下就有 24 万像素，
// 一页一页画出来再逐像素比，几百页的文档能跑上好几分钟。
constexpr int kDefaultMaxPixelComparedPages = 200;

// 「像素坐标 → PDF user space」的换算表。
//
// 为什么自己算：PdfPage::render 出来的图是**应用过页面 /Rotate 之后**的，
// 而差异包围盒要报在未旋转的用户空间里（和文字块框、MediaBox 同一坐标系）。
// 两者之间差一个旋转，不换算就会把差异框报在页面的错误位置。
struct PixelToUserMap {
    // 页面的有效盒子（PDF user space，左下原点）：PDFium 渲染的就是它。
    MediaBox box;
    // 页面自带的 /Rotate（顺时针度数）：0 / 90 / 180 / 270。
    int rotation = 0;
    // 一个用户空间单位对应多少像素（沿用户空间的 x / y 轴）。
    double pixelsPerUnitX = 1.0;
    double pixelsPerUnitY = 1.0;
};

// 图像上的一个点（像素坐标，左上原点）换算回 PDF user space。
QPointF pixelToUser(const PixelToUserMap& map, double px, double py) {
    const MediaBox& box = map.box;
    switch (map.rotation) {
        case 90:
            // 顺时针转了 90°：用户空间的 y 变成图像的 x，用户空间的 x 变成图像的 y。
            return {box.left + py / map.pixelsPerUnitX,
                    box.bottom + px / map.pixelsPerUnitY};
        case 180:
            return {box.right - px / map.pixelsPerUnitX,
                    box.bottom + py / map.pixelsPerUnitY};
        case 270:
            return {box.right - py / map.pixelsPerUnitX,
                    box.top - px / map.pixelsPerUnitY};
        case 0:
        default:
            return {box.left + px / map.pixelsPerUnitX,
                    box.top - py / map.pixelsPerUnitY};
    }
}

// 图像上的一个矩形换算回 PDF user space 的矩形。
//
// 差异像素的包围盒用的是「最后一个不同像素」的下标（闭区间），
// 所以右边界与下边界要 +1 像素才是真正的外沿。
QRectF rectToUserSpace(const PixelToUserMap& map, const QRect& rect) {
    if (rect.isNull()) {
        return {};
    }
    const QPointF topLeft = pixelToUser(map, rect.left(), rect.top());
    const QPointF bottomRight = pixelToUser(map, rect.right() + 1.0, rect.bottom() + 1.0);
    return QRectF(topLeft, bottomRight).normalized();
}

// 渲染一页，并准备好像素 → 用户空间的换算表。
bool renderForPixelCompare(const PdfPage& page,
                            double zoom,
                            QImage* image,
                            PixelToUserMap* map,
                            QString* reason) {
    const QImage rendered = page.render(zoom);
    if (rendered.isNull()) {
        *reason = QStringLiteral("page render failed (zoom=%1)").arg(zoom);
        return false;
    }
    const MediaBox box = page.contentBox();
    if (box.width() <= 0.0 || box.height() <= 0.0) {
        *reason = QStringLiteral("page has an empty content box");
        return false;
    }
    map->box = box;
    map->rotation = page.rotationDegrees();
    if (map->rotation == 90 || map->rotation == 270) {
        // 页面转了 90°/270°：图像的宽对应用户空间的「高」，反之亦然。
        map->pixelsPerUnitX = static_cast<double>(rendered.height()) / box.width();
        map->pixelsPerUnitY = static_cast<double>(rendered.width()) / box.height();
    } else {
        map->pixelsPerUnitX = static_cast<double>(rendered.width()) / box.width();
        map->pixelsPerUnitY = static_cast<double>(rendered.height()) / box.height();
    }
    *image = rendered;
    return true;
}

// 两个像素的通道差（只看 RGB：渲染图是不透明的，alpha 恒为 255）。
int channelDistance(QRgb left, QRgb right) {
    const int red = qAbs(qRed(left) - qRed(right));
    const int green = qAbs(qGreen(left) - qGreen(right));
    const int blue = qAbs(qBlue(left) - qBlue(right));
    return qMax(red, qMax(green, blue));
}

// 两张位图比对的结果。
struct BitmapDiff {
    // 比不比得了。false 时 reason 里写明原因（ASCII 技术细节）。
    bool comparable = false;
    QString reason;
    // 差异比例（0..1）与两边的像素计数。
    double differenceRatio = 0.0;
    qint64 comparedPixels = 0;
    qint64 differingPixels = 0;
    // 差异像素的包围盒（图像坐标）。
    QRect differingRect;
    // 两张图尺寸是否不同（不同 → 只比了共同区域）。
    bool sizeMismatch = false;
};

// 逐像素比两张位图。
//
// 两页尺寸不同时**只比共同区域**（左上角对齐的那一块），不做缩放归一化。
// 为什么这么选：缩放要插值，放大与缩小用的核还不一样，
// 插值本身就会造出成片的「假差异」，把真正要找的图形改动淹掉。
// 只比共同区域能保证两张图都是 PDFium 原样渲染出来的，
// 「像素不同」就真的是渲染结果不同；代价是超出共同区域的部分不比（已知边界）。
//
// 逐行直接读扫描线，不用 QImage::pixel()：后者每次都要做一次边界与格式检查，
// 24 万像素一页够慢的。
BitmapDiff compareBitmaps(const QImage& left, const QImage& right, int tolerance) {
    BitmapDiff diff;
    if (left.isNull() || right.isNull()) {
        diff.reason = QStringLiteral("one of the pages produced no bitmap");
        return diff;
    }
    const QImage leftImage = left.format() == QImage::Format_ARGB32
                                 ? left
                                 : left.convertToFormat(QImage::Format_ARGB32);
    const QImage rightImage = right.format() == QImage::Format_ARGB32
                                  ? right
                                  : right.convertToFormat(QImage::Format_ARGB32);

    const int width = qMin(leftImage.width(), rightImage.width());
    const int height = qMin(leftImage.height(), rightImage.height());
    if (width <= 0 || height <= 0) {
        diff.reason = QStringLiteral("the two pages have no overlapping pixels");
        return diff;
    }
    diff.sizeMismatch = leftImage.size() != rightImage.size();

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    qint64 differing = 0;
    for (int y = 0; y < height; ++y) {
        const QRgb* leftLine = reinterpret_cast<const QRgb*>(leftImage.constScanLine(y));
        const QRgb* rightLine = reinterpret_cast<const QRgb*>(rightImage.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (channelDistance(leftLine[x], rightLine[x]) <= tolerance) {
                continue;
            }
            ++differing;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }

    diff.comparable = true;
    diff.comparedPixels = static_cast<qint64>(width) * static_cast<qint64>(height);
    diff.differingPixels = differing;
    diff.differenceRatio = diff.comparedPixels > 0
        ? static_cast<double>(differing) / static_cast<double>(diff.comparedPixels)
        : 0.0;
    if (maxX >= minX && maxY >= minY) {
        diff.differingRect = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    }
    return diff;
}

// 像素级比对：把配对的每一页渲染成位图再逐像素比。
//
// 配对用的是文字层那一轮的页对齐结果。为什么连「文字层认为不是同一页」的
// 配对也比：纯图形改动最典型的场景就是「这一页的图被换掉了」，
// 文字层会认为它跟另一页毫无关系，但视觉上恰恰是这里变了。
void comparePagesByPixels(const QByteArray& oldBytes,
                          const QByteArray& newBytes,
                          const DocumentCompareOptions& options,
                          const QVector<SequencePair>& pagePairs,
                          DocumentCompareResult* result) {
    result->pixelsCompared = true;

    // 文字层那两份文档已经在 extractTextBlocks 里关掉了，这里再开一份。
    // 为什么不再心疼一次解析：像素比对本来就要把每页画一遍，
    // 重新解析 PDF 的开销相对渲染可以忽略。
    const PdfOpenResult oldOpened =
        openDocumentFromBytes(oldBytes, QString(), options.oldPassword);
    const PdfOpenResult newOpened =
        openDocumentFromBytes(newBytes, QString(), options.newPassword);
    if (!oldOpened.document.isValid() || !newOpened.document.isValid()) {
        result->pixelError =
            QStringLiteral("cannot reopen documents for pixel comparison: old=%1 new=%2")
                .arg(oldOpened.errorMessage, newOpened.errorMessage);
        return;
    }

    const int maxPages = options.maxPixelComparedPages > 0
                             ? options.maxPixelComparedPages
                             : kDefaultMaxPixelComparedPages;
    const double zoom = options.pixelZoom > 0.0 ? options.pixelZoom : 1.0;
    const int tolerance = qBound(0, options.pixelColorTolerance, 255);

    int processed = 0;
    double ratioSum = 0.0;
    int ratioCount = 0;
    for (const SequencePair& pair : pagePairs) {
        if (processed >= maxPages) {
            result->pixelNote =
                QStringLiteral("pixel comparison stopped after %1 pages (limit reached)")
                    .arg(maxPages);
            break;
        }
        ++processed;

        VisualPageDifference visual;
        visual.oldPageIndex = pair.oldIndex;
        visual.newPageIndex = pair.newIndex;

        const std::optional<PdfPage> oldPage = oldOpened.document.page(pair.oldIndex);
        const std::optional<PdfPage> newPage = newOpened.document.page(pair.newIndex);
        if (!oldPage.has_value() || !newPage.has_value()) {
            // 渲染不出来不算致命：记成这一页比不了，写明原因，继续比下一页。
            visual.comparable = false;
            visual.reason = QStringLiteral("page handle unavailable");
            ++result->pixelIncomparablePageCount;
            result->visualDifferences.append(visual);
            continue;
        }

        QImage oldImage;
        QImage newImage;
        PixelToUserMap oldMap;
        PixelToUserMap newMap;
        QString oldReason;
        QString newReason;
        const bool oldRendered =
            renderForPixelCompare(*oldPage, zoom, &oldImage, &oldMap, &oldReason);
        const bool newRendered =
            renderForPixelCompare(*newPage, zoom, &newImage, &newMap, &newReason);
        if (!oldRendered || !newRendered) {
            visual.comparable = false;
            visual.reason = !oldRendered ? QStringLiteral("old page: %1").arg(oldReason)
                                          : QStringLiteral("new page: %1").arg(newReason);
            ++result->pixelIncomparablePageCount;
            result->visualDifferences.append(visual);
            continue;
        }

        const BitmapDiff diff = compareBitmaps(oldImage, newImage, tolerance);
        if (!diff.comparable) {
            visual.comparable = false;
            visual.reason = diff.reason;
            ++result->pixelIncomparablePageCount;
            result->visualDifferences.append(visual);
            continue;
        }

        ++result->pixelComparedPageCount;
        visual.comparable = true;
        visual.differenceRatio = diff.differenceRatio;
        visual.comparedPixelCount = diff.comparedPixels;
        visual.differingPixelCount = diff.differingPixels;
        visual.sizeMismatch = diff.sizeMismatch;
        if (diff.differingPixels > 0) {
            visual.oldBounds = rectToUserSpace(oldMap, diff.differingRect);
            visual.newBounds = rectToUserSpace(newMap, diff.differingRect);
        }

        ratioSum += visual.differenceRatio;
        ++ratioCount;
        result->maxVisualDifferenceRatio =
            qMax(result->maxVisualDifferenceRatio, visual.differenceRatio);
        // 尺寸不同的页也要留一条：这次比较只覆盖了共同区域，
        // 把它吞掉的话，0% 的差异会被读成「整页都一样」。
        if (visual.differenceRatio > options.pixelDifferenceThreshold) {
            ++result->visualDifferencePageCount;
            result->visualDifferences.append(visual);
        } else if (visual.sizeMismatch) {
            result->visualDifferences.append(visual);
        }
    }

    if (ratioCount > 0) {
        result->averageVisualDifferenceRatio = ratioSum / static_cast<double>(ratioCount);
    }
}

}  // namespace

double textSimilarity(const QString& left, const QString& right) {
    const QString normalizedLeft = normalizedForCompare(left);
    const QString normalizedRight = normalizedForCompare(right);
    if (normalizedLeft == normalizedRight) {
        return 1.0;
    }
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) {
        return 0.0;
    }
    if (normalizedLeft.size() <= kLevenshteinMaxLength
        && normalizedRight.size() <= kLevenshteinMaxLength) {
        return levenshteinRatio(normalizedLeft, normalizedRight);
    }
    return bigramDice(normalizedLeft, normalizedRight);
}

bool extractTextBlocks(const QByteArray& bytes,
                       QVector<QVector<TextBlock>>* pages,
                       QString* error,
                       const QString& password) {
    if (pages == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    pages->clear();
    if (bytes.isEmpty()) {
        setError(error, QStringLiteral("没有可比较的内容"));
        return false;
    }

    const PdfOpenResult opened = openDocumentFromBytes(bytes, QString(), password);
    if (!opened.document.isValid()) {
        if (opened.passwordRequired) {
            setError(error, QStringLiteral("文件已加密：比较前需要提供打开密码"));
        } else {
            setError(error, QStringLiteral("打不开这份 PDF：%1").arg(opened.errorMessage));
        }
        return false;
    }

    const int pageCount = opened.document.pageCount();
    pages->resize(pageCount);
    for (int index = 0; index < pageCount; ++index) {
        const std::optional<PdfPage> page = opened.document.page(index);
        if (!page.has_value()) {
            continue;
        }
        const std::optional<TextPage> textPage = page->extractText();
        if (!textPage.has_value()) {
            continue;
        }
        (*pages)[index] = groupIntoBlocks(*textPage, index);
    }
    return true;
}

bool compareDocuments(const QByteArray& oldBytes,
                      const QByteArray& newBytes,
                      const DocumentCompareOptions& options,
                      DocumentCompareResult* result,
                      QString* error) {
    if (result == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    *result = DocumentCompareResult();

    QVector<QVector<TextBlock>> oldPages;
    QVector<QVector<TextBlock>> newPages;
    QString oldError;
    QString newError;
    if (!extractTextBlocks(oldBytes, &oldPages, &oldError, options.oldPassword)) {
        const QString message = QStringLiteral("原文读不出来：%1").arg(oldError);
        result->error = message;
        setError(error, message);
        return false;
    }
    if (!extractTextBlocks(newBytes, &newPages, &newError, options.newPassword)) {
        const QString message = QStringLiteral("新文读不出来：%1").arg(newError);
        result->error = message;
        setError(error, message);
        return false;
    }

    result->oldPageCount = oldPages.size();
    result->newPageCount = newPages.size();

    const int maxBlocks = options.maxBlocksPerPage > 0 ? options.maxBlocksPerPage
                                                        : kDefaultMaxBlocksPerPage;
    for (QVector<TextBlock>& page : oldPages) {
        if (page.size() > maxBlocks) {
            page.resize(maxBlocks);
        }
        result->oldBlockCount += page.size();
    }
    for (QVector<TextBlock>& page : newPages) {
        if (page.size() > maxBlocks) {
            page.resize(maxBlocks);
        }
        result->newBlockCount += page.size();
    }
    result->oldHasText = result->oldBlockCount > 0;
    result->newHasText = result->newBlockCount > 0;

    if (!result->oldHasText && !result->newHasText && !options.comparePixels) {
        // 两边都是扫描件：没有文字可比。
        // 这里必须报「比不了」，不能报「没有差异」——
        // 把「没法比」说成「一样」，用户就会以为两份合同真的逐字相同。
        //
        // 但开了像素比对就不算失败：纯图形文档（扫描件、图表页）本来就没文字层，
        // 这时像素比对是唯一能给出答案的路径，不能因为「没文字」就把整次比较判死。
        const QString message = QStringLiteral(
            "两份文件都没有文字层（可能是扫描件），无法比较文字内容；请先运行 OCR。");
        result->error = message;
        setError(error, message);
        return false;
    }

    // 每页的指纹：按内容对齐与按位置配对都用它算相似度。
    QVector<PageFingerprint> oldFingerprints;
    QVector<PageFingerprint> newFingerprints;
    oldFingerprints.reserve(oldPages.size());
    newFingerprints.reserve(newPages.size());
    for (const QVector<TextBlock>& page : oldPages) {
        oldFingerprints.append(fingerprintOfPage(page));
    }
    for (const QVector<TextBlock>& page : newPages) {
        newFingerprints.append(fingerprintOfPage(page));
    }

    // 按位置配对（第 i 页对第 i 页）作为基线。
    //
    // 为什么一定要留着这条基线：它是「不做对齐时一定会有的口径」，
    // 对齐要比它更好才值得用 —— 对齐不比它好就说明它没起作用，
    // 这时说「用了自动对齐」是在骗人。
    QVector<SequencePair> positionPairs;
    for (int index = 0; index < qMin(oldPages.size(), newPages.size()); ++index) {
        positionPairs.append({index, index,
                              pageSimilarity(oldFingerprints.at(index),
                                             newFingerprints.at(index))});
    }

    // 页面级对齐：默认按内容对齐（页指纹 + 序列对齐）。
    QVector<SequencePair> pagePairs = positionPairs;
    bool alignmentApplied = false;
    QString alignmentNote;

    const int maxAlignmentPages = options.maxAlignmentPages > 0
                                      ? options.maxAlignmentPages
                                      : kDefaultMaxAlignmentPages;
    const qint64 cells = static_cast<qint64>(oldPages.size())
        * static_cast<qint64>(newPages.size());
    const bool limitExceeded = oldPages.size() > maxAlignmentPages
        || newPages.size() > maxAlignmentPages || cells > kMaxPageAlignmentCells;

    if (!options.autoAlign) {
        alignmentNote = QStringLiteral("sequence alignment disabled by option");
    } else if (limitExceeded) {
        alignmentNote = QStringLiteral(
            "page count exceeds alignment limit (old=%1 new=%2 limit=%3); "
            "fell back to position pairing")
            .arg(oldPages.size())
            .arg(newPages.size())
            .arg(maxAlignmentPages);
    } else {
        const PageAlignment alignment = alignPageSequences(oldFingerprints, newFingerprints,
                                                           options.pageThreshold);
        int alignedCount = 0;
        double alignedSum = 0.0;
        for (const SequencePair& pair : alignment.pairs) {
            if (pair.similarity >= options.pageThreshold) {
                ++alignedCount;
                alignedSum += pair.similarity;
            }
        }
        int positionCount = 0;
        double positionSum = 0.0;
        for (const SequencePair& pair : positionPairs) {
            if (pair.similarity >= options.pageThreshold) {
                ++positionCount;
                positionSum += pair.similarity;
            }
        }
        const bool better = alignedCount > positionCount
            || (alignedCount == positionCount && alignedSum > positionSum + 1e-9);
        if (better) {
            pagePairs = alignment.pairs;
            alignmentApplied = true;
            alignmentNote =
                QStringLiteral("sequence alignment applied (%1 page pairs)").arg(alignedCount);
        } else {
            alignmentNote = QStringLiteral(
                "sequence alignment did not improve on position pairing; "
                "using position pairing");
        }
    }

    result->alignmentApplied = alignmentApplied;
    result->alignmentMode = alignmentApplied ? QStringLiteral("sequence")
                                              : QStringLiteral("position");
    result->alignmentNote = alignmentNote;

    QVector<bool> oldPageMatched(oldPages.size(), false);
    QVector<bool> newPageMatched(newPages.size(), false);
    // 一边独有的页码：按对齐结果算出来的，界面点「跳到那一页」用得上。
    QVector<int> oldOnly;
    QVector<int> newOnly;

    for (const SequencePair& pair : pagePairs) {
        oldPageMatched[pair.oldIndex] = true;
        newPageMatched[pair.newIndex] = true;

        if (pair.similarity < options.pageThreshold) {
            // 两页几乎没关系：说「同一页被改过」会让人以为只是改了几行，
            // 按「删页 + 加页」报更贴近事实。
            CompareDifference removed;
            removed.kind = DiffKind::PageRemoved;
            removed.oldPageIndex = pair.oldIndex;
            removed.newPageIndex = -1;
            removed.oldText = pageSnippet(oldPages.at(pair.oldIndex));
            removed.similarity = pair.similarity;
            result->differences.append(removed);
            ++result->removedPageCount;
            oldOnly.append(pair.oldIndex);

            CompareDifference added;
            added.kind = DiffKind::PageAdded;
            added.oldPageIndex = -1;
            added.newPageIndex = pair.newIndex;
            added.newText = pageSnippet(newPages.at(pair.newIndex));
            added.similarity = pair.similarity;
            result->differences.append(added);
            ++result->addedPageCount;
            newOnly.append(pair.newIndex);
            continue;
        }

        ++result->comparedPageCount;
        ++result->alignedPairCount;
        compareBlocksOfPage(oldPages.at(pair.oldIndex),
                            newPages.at(pair.newIndex),
                            pair.oldIndex,
                            pair.newIndex,
                            options,
                            result);
    }

    // 没有配上的页：一边有一边没有。
    for (int oldIndex = 0; oldIndex < oldPages.size(); ++oldIndex) {
        if (oldPageMatched.at(oldIndex)) {
            continue;
        }
        CompareDifference removed;
        removed.kind = DiffKind::PageRemoved;
        removed.oldPageIndex = oldIndex;
        removed.newPageIndex = -1;
        removed.oldText = pageSnippet(oldPages.at(oldIndex));
        result->differences.append(removed);
        ++result->removedPageCount;
        oldOnly.append(oldIndex);
    }
    for (int newIndex = 0; newIndex < newPages.size(); ++newIndex) {
        if (newPageMatched.at(newIndex)) {
            continue;
        }
        CompareDifference added;
        added.kind = DiffKind::PageAdded;
        added.oldPageIndex = -1;
        added.newPageIndex = newIndex;
        added.newText = pageSnippet(newPages.at(newIndex));
        result->differences.append(added);
        ++result->addedPageCount;
        newOnly.append(newIndex);
    }

    // 一边独有的页：口径与 addedPageCount / removedPageCount 一致，
    // 都是「按对齐结果算出来的」，不是「按页码位置算出来的」。
    result->oldOnlyPageIndices = oldOnly;
    result->newOnlyPageIndices = newOnly;
    result->oldOnlyPageCount = oldOnly.size();
    result->newOnlyPageCount = newOnly.size();

    // 像素级比对：按上面的页配对逐页渲染、逐像素比。
    // 放在文字层之后：它要用刚算出来的配对关系。
    if (options.comparePixels) {
        comparePagesByPixels(oldBytes, newBytes, options, pagePairs, result);
    }

    // 整体相似度：配对上的页与块按相似度加权，除以两边的块数。
    double blockSimilaritySum = 0.0;
    for (const CompareDifference& difference : result->differences) {
        if (difference.kind == DiffKind::Modified) {
            blockSimilaritySum += difference.similarity;
        }
    }
    const int unchanged = result->unchangedBlockCount;
    const int largestSide = qMax(result->oldBlockCount, result->newBlockCount);
    if (largestSide > 0) {
        result->overallSimilarity = qBound(
            0.0, (unchanged + blockSimilaritySum) / static_cast<double>(largestSide), 1.0);
    } else {
        result->overallSimilarity = 1.0;
    }

    // 排序：先按页码，再按块序号，最后按差异种类。
    std::stable_sort(result->differences.begin(), result->differences.end(),
                     [](const CompareDifference& left, const CompareDifference& right) {
                         const int leftPage = displayPageOf(left);
                         const int rightPage = displayPageOf(right);
                         if (leftPage != rightPage) {
                             return leftPage < rightPage;
                         }
                         const int leftBlock = displayBlockOf(left);
                         const int rightBlock = displayBlockOf(right);
                         if (leftBlock != rightBlock) {
                             return leftBlock < rightBlock;
                         }
                         return kindOrder(left.kind) < kindOrder(right.kind);
                     });

    result->succeeded = true;
    result->error.clear();
    setError(error, QString());
    return true;
}

bool compareDocumentFiles(const QString& oldPath,
                          const QString& newPath,
                          const DocumentCompareOptions& options,
                          DocumentCompareResult* result,
                          QString* error) {
    if (result == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    *result = DocumentCompareResult();

    const auto readFile = [](const QString& path, QByteArray* bytes, QString* reason) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *reason = QStringLiteral("读取文件失败：%1").arg(path);
            return false;
        }
        *bytes = file.readAll();
        file.close();
        if (bytes->isEmpty()) {
            *reason = QStringLiteral("文件是空的：%1").arg(path);
            return false;
        }
        return true;
    };

    QByteArray oldBytes;
    QByteArray newBytes;
    QString reason;
    if (!readFile(oldPath, &oldBytes, &reason)) {
        result->error = reason;
        setError(error, reason);
        return false;
    }
    if (!readFile(newPath, &newBytes, &reason)) {
        result->error = reason;
        setError(error, reason);
        return false;
    }

    return compareDocuments(oldBytes, newBytes, options, result, error);
}

}  // namespace aipdf::pdf
