#include "pdf/ops/LayoutDiff.h"

#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/ops/DocumentComparer.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

// 文本改写配对的最低相似度：比这低就是「两段不相干的文字」，
// 不该配成一对（配了也只会报「改写」，不如老实报删掉 + 新增）。
constexpr double kMinimumTextSimilarity = 0.3;

// 表格行列对齐的最低相似度。
constexpr double kTableAlignmentSimilarity = 0.5;

// 表格行列「精确对齐」的相似度门槛：达到这个值的先配上。
constexpr double kTableAlignmentExactSimilarity = 0.98;

// 归一化的块键：种类 + 内容指纹。
//
// 为什么把种类混进去：图片的像素指纹与文字的文本哈希是两套数值空间，
// 不区分的话理论上会撞上，撞上就会把一段文字配到一张图上。
quint64 blockKey(const LayoutBlock& block) {
    quint64 kind = 0;
    switch (block.kind) {
        case LayoutBlockKind::Text:
            kind = 1;
            break;
        case LayoutBlockKind::Image:
            kind = 2;
            break;
        case LayoutBlockKind::Table:
            kind = 3;
            break;
        case LayoutBlockKind::Graphic:
            kind = 4;
            break;
    }
    return block.contentHash ^ (kind * 0x9E3779B97F4A7C15ULL);
}

// 把文本切成词元（小写化后按非字母数字切开）。
//
// 为什么按词切：中文按字切的话，两页中文的常用字都一样，
// 重合度会虚高到「看起来是同一页」。
QSet<QString> tokenize(const QString& text) {
    QSet<QString> tokens;
    QString current;
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            current.append(character.toLower());
        } else {
            if (!current.isEmpty()) {
                tokens.insert(current);
                current.clear();
            }
        }
    }
    if (!current.isEmpty()) {
        tokens.insert(current);
    }
    return tokens;
}

// 两个集合的 Jaccard 相似度。
template <typename T>
double jaccard(const QSet<T>& left, const QSet<T>& right) {
    if (left.isEmpty() && right.isEmpty()) {
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

// 一页的「指纹」。
struct PageSignature {
    QSet<quint64> blockKeys;
    QSet<QString> tokens;
};

PageSignature signatureOf(const QVector<LayoutBlock>& blocks) {
    PageSignature signature;
    for (const LayoutBlock& block : blocks) {
        signature.blockKeys.insert(blockKey(block));
        if (!block.text.isEmpty()) {
            for (const QString& token : tokenize(block.text)) {
                signature.tokens.insert(token);
            }
        }
    }
    return signature;
}

// 两页的相似度：块指纹重合与词元重合各占一半。
//
// 为什么不能只看块指纹：表格少了一列时，表的指纹整个变了，
// 只看指纹会把「同一张表改过了」判成「两页毫无关系」。
double pageSimilarity(const PageSignature& left, const PageSignature& right) {
    return 0.5 * jaccard(left.blockKeys, right.blockKeys)
        + 0.5 * jaccard(left.tokens, right.tokens);
}

// 两个块的复合打分：文本相似度为主，位置 IoU 为辅。
//
// 为什么 IoU 只能当辅助分：表格与双栏的切分会让块框飘，
// 只看 IoU 会把「同一段话换了个位置」判成毫不相干。
double compositeScore(const LayoutBlock& oldBlock,
                       const LayoutBlock& newBlock,
                       double* textSimilarityOut,
                       double* iouOut) {
    const double iou =
        normalizedRectIou(oldBlock.normalizedBounds, newBlock.normalizedBounds);
    const double similarity = textSimilarity(oldBlock.text, newBlock.text);
    if (textSimilarityOut != nullptr) {
        *textSimilarityOut = similarity;
    }
    if (iouOut != nullptr) {
        *iouOut = iou;
    }
    if (oldBlock.text.isEmpty() && newBlock.text.isEmpty()) {
        // 图片与图形没有文字可比，只剩位置能比。
        return iou;
    }
    return 0.7 * similarity + 0.3 * iou;
}

// 块是不是「挪动过」。
//
// 判定用归一化后的位移，不用绝对坐标：两份文档的页面尺寸可以不同。
//
// 位移门槛同时看页面尺寸与块自身大小：
// 只按页面尺寸判，一段短文字在宽页上挪一点点也会被报成「移动」；
// 只按块自身大小判，一整段话挪动一点点又看不出来。
//
// 块自身大小这一项取 0.3：两份文档页面尺寸不同时，同一段文字的
// 归一化位置也会有一点点差别（块没跟着页面等比缩放），
// 门槛太紧会把「同一段话放在同样的相对位置」误报成「挪动了」。
bool isMoved(const LayoutBlock& oldBlock,
             const LayoutBlock& newBlock,
             const LayoutDiffOptions& options) {
    if (oldBlock.pageIndex != newBlock.pageIndex) {
        // 跨页了：一定算挪动。
        return true;
    }
    const double dx = qAbs(oldBlock.normalizedBounds.center().x()
                           - newBlock.normalizedBounds.center().x());
    const double dy = qAbs(oldBlock.normalizedBounds.center().y()
                           - newBlock.normalizedBounds.center().y());
    const double widthThreshold =
        qMax(options.moveDisplacement,
             0.3 * qMax(oldBlock.normalizedBounds.width(), newBlock.normalizedBounds.width()));
    const double heightThreshold =
        qMax(options.moveDisplacement,
             0.3 * qMax(oldBlock.normalizedBounds.height(), newBlock.normalizedBounds.height()));
    return dx > widthThreshold || dy > heightThreshold;
}

// 一组配对候选。
struct Candidate {
    int oldIndex = -1;
    int newIndex = -1;
    double score = 0.0;
    double similarity = 0.0;
    double iou = 0.0;
};

// 候选排序：分数高的先配；同分按（原文页码、原文块号、新文页码、新文块号）定序。
//
// 为什么同分也要定序：同样的输入必须给出同样的配对结果，
// 否则「今天说第 3 块挪了、明天说第 5 块挪了」，用户没法复核。
bool candidateLess(const Candidate& left, const Candidate& right) {
    if (left.score != right.score) {
        return left.score > right.score;
    }
    if (left.oldIndex != right.oldIndex) {
        return left.oldIndex < right.oldIndex;
    }
    return left.newIndex < right.newIndex;
}

// 页与页的配对。
struct PagePair {
    int oldPage = -1;
    int newPage = -1;
};

// 内容配页：先按指纹集合完全相同配，再按相似度贪心配，
// 最后按页码位置兜底（两边都还有剩的页时，第 i 页对第 i 页）。
QVector<PagePair> pairPages(const QVector<PageSignature>& oldSignatures,
                             const QVector<PageSignature>& newSignatures,
                             double threshold) {
    QVector<PagePair> pairs;
    const int oldCount = oldSignatures.size();
    const int newCount = newSignatures.size();
    if (oldCount == 0 || newCount == 0) {
        return pairs;
    }

    QVector<bool> oldUsed(oldCount, false);
    QVector<bool> newUsed(newCount, false);

    // 第一步：指纹集合完全相同的页配上（不看先后顺序，所以支持页移动）。
    QHash<QString, QVector<int>> newBySignature;
    for (int newIndex = 0; newIndex < newCount; ++newIndex) {
        QVector<quint64> keys = newSignatures.at(newIndex).blockKeys.values();
        std::sort(keys.begin(), keys.end());
        QString signature;
        for (const quint64 key : keys) {
            signature.append(QString::number(key) + QLatin1Char(','));
        }
        newBySignature[signature].append(newIndex);
    }
    QHash<QString, int> usedBySignature;
    for (int oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
        QVector<quint64> keys = oldSignatures.at(oldIndex).blockKeys.values();
        std::sort(keys.begin(), keys.end());
        QString signature;
        for (const quint64 key : keys) {
            signature.append(QString::number(key) + QLatin1Char(','));
        }
        const QVector<int> candidates = newBySignature.value(signature);
        const int used = usedBySignature.value(signature, 0);
        if (used >= candidates.size()) {
            continue;
        }
        usedBySignature.insert(signature, used + 1);
        oldUsed[oldIndex] = true;
        newUsed[candidates.at(used)] = true;
        pairs.append({oldIndex, candidates.at(used)});
    }

    // 第二步：剩下的页按相似度贪心配。
    struct PageCandidate {
        int oldIndex = -1;
        int newIndex = -1;
        double similarity = 0.0;
    };
    QVector<PageCandidate> candidates;
    for (int oldIndex = 0; oldIndex < oldCount; ++oldIndex) {
        if (oldUsed.at(oldIndex)) {
            continue;
        }
        for (int newIndex = 0; newIndex < newCount; ++newIndex) {
            if (newUsed.at(newIndex)) {
                continue;
            }
            const double score = pageSimilarity(oldSignatures.at(oldIndex),
                                                 newSignatures.at(newIndex));
            if (score >= threshold) {
                candidates.append({oldIndex, newIndex, score});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const PageCandidate& left,
                                                         const PageCandidate& right) {
        if (left.similarity != right.similarity) {
            return left.similarity > right.similarity;
        }
        if (left.oldIndex != right.oldIndex) {
            return left.oldIndex < right.oldIndex;
        }
        return left.newIndex < right.newIndex;
    });
    for (const PageCandidate& candidate : candidates) {
        if (oldUsed.at(candidate.oldIndex) || newUsed.at(candidate.newIndex)) {
            continue;
        }
        oldUsed[candidate.oldIndex] = true;
        newUsed[candidate.newIndex] = true;
        pairs.append({candidate.oldIndex, candidate.newIndex});
    }

    // 第三步：按页码位置兜底。
    //
    // 为什么要有这一步：整页被重写之后，内容配页会配不上，
    // 这时「第 i 页对第 i 页」至少给块级匹配一个机会。
    // 配上的页也未必真配得上——块级匹配还要过相似度门槛。
    for (int index = 0; index < qMin(oldCount, newCount); ++index) {
        if (oldUsed.at(index) || newUsed.at(index)) {
            continue;
        }
        oldUsed[index] = true;
        newUsed[index] = true;
        pairs.append({index, index});
    }

    std::sort(pairs.begin(), pairs.end(), [](const PagePair& left, const PagePair& right) {
        return left.oldPage < right.oldPage;
    });
    return pairs;
}

// 在两组块之间做贪心配对。
//
// 只配「同一页内」的块（oldPage / newPage 给定时），
// 或者跨页配对（oldPage / newPage 给 -1 时表示不限页）。
void matchBlocks(const QVector<LayoutBlock>& oldBlocks,
                 const QVector<LayoutBlock>& newBlocks,
                 const QVector<bool>& oldMatched,
                 const QVector<bool>& newMatched,
                 int oldPage,
                 int newPage,
                 const LayoutDiffOptions& options,
                 QVector<Candidate>* matched) {
    QVector<Candidate> candidates;
    for (int oldIndex = 0; oldIndex < oldBlocks.size(); ++oldIndex) {
        if (oldMatched.at(oldIndex)) {
            continue;
        }
        if (oldPage >= 0 && oldBlocks.at(oldIndex).pageIndex != oldPage) {
            continue;
        }
        for (int newIndex = 0; newIndex < newBlocks.size(); ++newIndex) {
            if (newMatched.at(newIndex)) {
                continue;
            }
            if (newPage >= 0 && newBlocks.at(newIndex).pageIndex != newPage) {
                continue;
            }
            const LayoutBlock& oldBlock = oldBlocks.at(oldIndex);
            const LayoutBlock& newBlock = newBlocks.at(newIndex);
            if (oldBlock.kind != newBlock.kind) {
                continue;
            }
            double similarity = 0.0;
            double iou = 0.0;
            const double score = compositeScore(oldBlock, newBlock, &similarity, &iou);
            if (oldBlock.text.isEmpty() && newBlock.text.isEmpty()) {
                // 图片与图形：只能靠位置配。
                if (iou < options.imageReplaceIou) {
                    continue;
                }
            } else if (similarity < kMinimumTextSimilarity) {
                continue;
            }
            if (score < options.matchThreshold) {
                continue;
            }
            candidates.append({oldIndex, newIndex, score, similarity, iou});
        }
    }
    std::sort(candidates.begin(), candidates.end(), candidateLess);

    // 贪心：一个块只能配一次，所以要自己记着谁已经被用掉了。
    QVector<bool> usedOld = oldMatched;
    QVector<bool> usedNew = newMatched;
    for (const Candidate& candidate : candidates) {
        if (usedOld.at(candidate.oldIndex) || usedNew.at(candidate.newIndex)) {
            continue;
        }
        usedOld[candidate.oldIndex] = true;
        usedNew[candidate.newIndex] = true;
        matched->append(candidate);
    }
}

// 表格的一行 / 一列的文字。
QVector<QString> rowTextsOf(const LayoutBlock& table) {
    QVector<QString> texts(qMax(0, table.rowCount));
    for (const LayoutCell& cell : table.cells) {
        if (cell.row < 0 || cell.row >= texts.size()) {
            continue;
        }
        if (!texts[cell.row].isEmpty()) {
            texts[cell.row].append(QLatin1Char(' '));
        }
        texts[cell.row].append(cell.text);
    }
    return texts;
}

QVector<QString> columnTextsOf(const LayoutBlock& table) {
    QVector<QString> texts(qMax(0, table.columnCount));
    for (const LayoutCell& cell : table.cells) {
        if (cell.column < 0 || cell.column >= texts.size()) {
            continue;
        }
        if (!texts[cell.column].isEmpty()) {
            texts[cell.column].append(QLatin1Char(' '));
        }
        texts[cell.column].append(cell.text);
    }
    return texts;
}

// 表格的行列对齐结果。
struct TableAlignment {
    // 旧行号 → 新行号；-1 表示这一行没有对应（被删了）。
    QVector<int> oldToNew;
    // 新行号 → 旧行号；-1 表示这一行是新加的。
    QVector<int> newToOld;
};

// 按内容对齐两组序列。
//
// 为什么不能按序号对齐：删掉一列之后，后面的列会整体左移，
// 按序号比会把每一格都报成「被改过」——用户看到的是满屏红，
// 真正的变化（少了一列）反而被淹掉。
//
// 为什么先精确后模糊：「B1 B2」与「C1 C2」的相似度有 0.6，
// 只按相似度贪心的话，被删掉的 B 列会错认成 C 列，
// 于是每一格都被报成「被改过」。先把完全一样的对上，
// 剩下的才去模糊匹配，被删掉的那列就自然对不上任何一列。
TableAlignment alignByContent(const QVector<QString>& oldTexts,
                               const QVector<QString>& newTexts) {
    TableAlignment alignment;
    alignment.oldToNew.fill(-1, oldTexts.size());
    alignment.newToOld.fill(-1, newTexts.size());
    QVector<bool> newUsed(newTexts.size(), false);

    // 第一轮：完全一样（或几乎一样）的先配上。
    for (int oldIndex = 0; oldIndex < oldTexts.size(); ++oldIndex) {
        for (int newIndex = 0; newIndex < newTexts.size(); ++newIndex) {
            if (newUsed.at(newIndex)) {
                continue;
            }
            if (textSimilarity(oldTexts.at(oldIndex), newTexts.at(newIndex))
                < kTableAlignmentExactSimilarity) {
                continue;
            }
            alignment.oldToNew[oldIndex] = newIndex;
            alignment.newToOld[newIndex] = oldIndex;
            newUsed[newIndex] = true;
            break;
        }
    }

    // 第二轮：剩下的按相似度贪心配。
    for (int oldIndex = 0; oldIndex < oldTexts.size(); ++oldIndex) {
        if (alignment.oldToNew.at(oldIndex) >= 0) {
            continue;
        }
        int best = -1;
        double bestScore = 0.0;
        for (int newIndex = 0; newIndex < newTexts.size(); ++newIndex) {
            if (newUsed.at(newIndex)) {
                continue;
            }
            const double score = textSimilarity(oldTexts.at(oldIndex), newTexts.at(newIndex));
            if (score > bestScore + 1e-9) {
                bestScore = score;
                best = newIndex;
            }
        }
        if (best >= 0 && bestScore >= kTableAlignmentSimilarity) {
            alignment.oldToNew[oldIndex] = best;
            alignment.newToOld[best] = oldIndex;
            newUsed[best] = true;
        }
    }
    return alignment;
}

QString cellTextOf(const LayoutBlock& table, int row, int column) {
    for (const LayoutCell& cell : table.cells) {
        if (cell.row == row && cell.column == column) {
            return cell.text;
        }
    }
    return QString();
}

LayoutDifference makeDifference(LayoutDiffKind kind,
                                const LayoutBlock& oldBlock,
                                const LayoutBlock& newBlock,
                                double similarity,
                                double iou) {
    LayoutDifference difference;
    difference.kind = kind;
    difference.oldPageIndex = oldBlock.pageIndex;
    difference.newPageIndex = newBlock.pageIndex;
    difference.oldBlockIndex = oldBlock.blockIndex;
    difference.newBlockIndex = newBlock.blockIndex;
    difference.blockKind = oldBlock.kind;
    difference.oldText = oldBlock.text;
    difference.newText = newBlock.text;
    difference.oldBounds = oldBlock.bounds;
    difference.newBounds = newBlock.bounds;
    difference.similarity = similarity;
    difference.iou = iou;

    // 「删除」与「新增」只有一边：另一边不能带页码，
    // 否则界面上会跳到一个根本不存在的块。
    if (kind == LayoutDiffKind::BlockDeleted) {
        difference.newPageIndex = -1;
        difference.newBlockIndex = -1;
        difference.newText.clear();
        difference.newBounds = QRectF();
    } else if (kind == LayoutDiffKind::BlockInserted) {
        difference.oldPageIndex = -1;
        difference.oldBlockIndex = -1;
        difference.oldText.clear();
        difference.oldBounds = QRectF();
    }
    return difference;
}

// 表格的行列与单元格比较。
//
// 返回 true 表示报出了差异（表结构变了或表格被挪动了）。
bool compareTables(const LayoutBlock& oldTable,
                   const LayoutBlock& newTable,
                   const LayoutDiffOptions& options,
                   QVector<LayoutDifference>* differences,
                   LayoutDiffResult* result) {
    bool structural = false;

    if (oldTable.rowCount != newTable.rowCount) {
        LayoutDifference difference = makeDifference(LayoutDiffKind::TableRowsChanged,
                                                     oldTable, newTable, 1.0, 0.0);
        difference.oldRowCount = oldTable.rowCount;
        difference.newRowCount = newTable.rowCount;
        difference.oldColumnCount = oldTable.columnCount;
        difference.newColumnCount = newTable.columnCount;
        differences->append(difference);
        ++result->tableRowChangeCount;
        structural = true;
    }
    if (oldTable.columnCount != newTable.columnCount) {
        LayoutDifference difference = makeDifference(LayoutDiffKind::TableColumnsChanged,
                                                     oldTable, newTable, 1.0, 0.0);
        difference.oldRowCount = oldTable.rowCount;
        difference.newRowCount = newTable.rowCount;
        difference.oldColumnCount = oldTable.columnCount;
        difference.newColumnCount = newTable.columnCount;
        differences->append(difference);
        ++result->tableColumnChangeCount;
        structural = true;
    }

    const TableAlignment rowAlignment = alignByContent(rowTextsOf(oldTable),
                                                       rowTextsOf(newTable));
    const TableAlignment columnAlignment = alignByContent(columnTextsOf(oldTable),
                                                          columnTextsOf(newTable));

    for (int oldRow = 0; oldRow < rowAlignment.oldToNew.size(); ++oldRow) {
        const int newRow = rowAlignment.oldToNew.at(oldRow);
        if (newRow < 0) {
            continue;
        }
        for (int oldColumn = 0; oldColumn < columnAlignment.oldToNew.size(); ++oldColumn) {
            const int newColumn = columnAlignment.oldToNew.at(oldColumn);
            if (newColumn < 0) {
                continue;
            }
            const QString oldText = cellTextOf(oldTable, oldRow, oldColumn);
            const QString newText = cellTextOf(newTable, newRow, newColumn);
            if (oldText == newText) {
                continue;
            }
            LayoutDifference difference = makeDifference(LayoutDiffKind::TableCellRewritten,
                                                         oldTable, newTable, 1.0, 0.0);
            difference.oldText = oldText;
            difference.newText = newText;
            difference.rowIndex = oldRow;
            difference.columnIndex = oldColumn;
            difference.oldRowCount = oldTable.rowCount;
            difference.newRowCount = newTable.rowCount;
            difference.oldColumnCount = oldTable.columnCount;
            difference.newColumnCount = newTable.columnCount;
            differences->append(difference);
            ++result->tableCellChangeCount;
            structural = true;
        }
    }

    if (!structural && isMoved(oldTable, newTable, options)) {
        differences->append(makeDifference(LayoutDiffKind::BlockMoved,
                                           oldTable, newTable, 1.0, 0.0));
        ++result->movedBlockCount;
        return true;
    }
    return structural;
}

// 配成一对的两个块该怎么报。
void classifyPair(const LayoutBlock& oldBlock,
                   const LayoutBlock& newBlock,
                   double similarity,
                   double iou,
                   const LayoutDiffOptions& options,
                   QVector<LayoutDifference>* differences,
                   LayoutDiffResult* result) {
    if (oldBlock.kind != newBlock.kind) {
        // 种类不同：只能是「删掉 + 新增」。
        differences->append(makeDifference(LayoutDiffKind::BlockDeleted,
                                           oldBlock, oldBlock, 0.0, 0.0));
        differences->append(makeDifference(LayoutDiffKind::BlockInserted,
                                           newBlock, newBlock, 0.0, 0.0));
        ++result->deletedBlockCount;
        ++result->insertedBlockCount;
        return;
    }

    if (oldBlock.kind == LayoutBlockKind::Image || oldBlock.kind == LayoutBlockKind::Graphic) {
        if (oldBlock.contentHash != newBlock.contentHash) {
            if (iou >= options.imageReplaceIou) {
                LayoutDifference difference =
                    makeDifference(LayoutDiffKind::ImageReplaced, oldBlock, newBlock, 1.0, iou);
                if (oldBlock.kind == LayoutBlockKind::Graphic) {
                    difference.note = QStringLiteral("graphic content changed");
                }
                differences->append(difference);
                ++result->imageReplacedCount;
                return;
            }
            // 位置也变了：说「换图」会让人以为只是换了张图，
            // 实际上这块是「删了一张图、又加了一张图」。
            differences->append(makeDifference(LayoutDiffKind::BlockDeleted,
                                               oldBlock, oldBlock, 0.0, 0.0));
            differences->append(makeDifference(LayoutDiffKind::BlockInserted,
                                               newBlock, newBlock, 0.0, 0.0));
            ++result->deletedBlockCount;
            ++result->insertedBlockCount;
            return;
        }
        if (isMoved(oldBlock, newBlock, options)) {
            differences->append(makeDifference(LayoutDiffKind::BlockMoved,
                                               oldBlock, newBlock, 1.0, iou));
            ++result->movedBlockCount;
            return;
        }
        ++result->unchangedBlockCount;
        return;
    }

    if (oldBlock.kind == LayoutBlockKind::Table) {
        if (!compareTables(oldBlock, newBlock, options, differences, result)) {
            ++result->unchangedBlockCount;
        }
        return;
    }

    if (similarity >= options.moveTextThreshold) {
        if (isMoved(oldBlock, newBlock, options)) {
            differences->append(makeDifference(LayoutDiffKind::BlockMoved,
                                               oldBlock, newBlock, similarity, iou));
            ++result->movedBlockCount;
            return;
        }
        ++result->unchangedBlockCount;
        return;
    }

    differences->append(makeDifference(LayoutDiffKind::TextRewritten,
                                       oldBlock, newBlock, similarity, iou));
    ++result->rewrittenBlockCount;
}

int layoutDisplayPage(const LayoutDifference& difference) {
    return difference.newPageIndex >= 0 ? difference.newPageIndex : difference.oldPageIndex;
}

int layoutDisplayBlock(const LayoutDifference& difference) {
    return difference.newBlockIndex >= 0 ? difference.newBlockIndex : difference.oldBlockIndex;
}

int layoutKindOrder(LayoutDiffKind kind) {
    switch (kind) {
        case LayoutDiffKind::BlockMoved:
            return 0;
        case LayoutDiffKind::ImageReplaced:
            return 1;
        case LayoutDiffKind::TextRewritten:
            return 2;
        case LayoutDiffKind::TableRowsChanged:
            return 3;
        case LayoutDiffKind::TableColumnsChanged:
            return 4;
        case LayoutDiffKind::TableCellRewritten:
            return 5;
        case LayoutDiffKind::BlockInserted:
            return 6;
        case LayoutDiffKind::BlockDeleted:
            return 7;
    }
    return 8;
}

}  // namespace

QString layoutDiffKindName(LayoutDiffKind kind) {
    switch (kind) {
        case LayoutDiffKind::BlockInserted:
            return QStringLiteral("block-inserted");
        case LayoutDiffKind::BlockDeleted:
            return QStringLiteral("block-deleted");
        case LayoutDiffKind::BlockMoved:
            return QStringLiteral("block-moved");
        case LayoutDiffKind::ImageReplaced:
            return QStringLiteral("image-replaced");
        case LayoutDiffKind::TextRewritten:
            return QStringLiteral("text-rewritten");
        case LayoutDiffKind::TableRowsChanged:
            return QStringLiteral("table-rows-changed");
        case LayoutDiffKind::TableColumnsChanged:
            return QStringLiteral("table-columns-changed");
        case LayoutDiffKind::TableCellRewritten:
            return QStringLiteral("table-cell-rewritten");
    }
    return QStringLiteral("unknown");
}

LayoutDiffResult diffLayoutBlocks(const QVector<QVector<LayoutBlock>>& oldPages,
                                   const QVector<QVector<LayoutBlock>>& newPages,
                                   const LayoutDiffOptions& options) {
    LayoutDiffResult result;
    QVector<LayoutDifference> differences;

    QVector<LayoutBlock> oldBlocks;
    QVector<LayoutBlock> newBlocks;
    for (const QVector<LayoutBlock>& page : oldPages) {
        oldBlocks.append(page);
    }
    for (const QVector<LayoutBlock>& page : newPages) {
        newBlocks.append(page);
    }
    result.oldBlockCount = oldBlocks.size();
    result.newBlockCount = newBlocks.size();

    QVector<bool> oldMatched(oldBlocks.size(), false);
    QVector<bool> newMatched(newBlocks.size(), false);

    // ---------------------------------------------------------- 阶段一 ----
    //
    // 内容指纹完全相同的块直接配上，**不看位置**。
    //
    // 为什么这是「移动」的来源：位置变了但内容一模一样，
    // 这正是「这一块被挪走了」的定义。按位置配对永远看不到它。
    {
        QHash<quint64, QVector<int>> oldByKey;
        QHash<quint64, QVector<int>> newByKey;
        for (int index = 0; index < oldBlocks.size(); ++index) {
            oldByKey[blockKey(oldBlocks.at(index))].append(index);
        }
        for (int index = 0; index < newBlocks.size(); ++index) {
            newByKey[blockKey(newBlocks.at(index))].append(index);
        }
        QList<quint64> keys = oldByKey.keys();
        std::sort(keys.begin(), keys.end());
        for (const quint64 key : keys) {
            const QVector<int> oldIndices = oldByKey.value(key);
            const QVector<int> newIndices = newByKey.value(key);
            const int count = qMin(oldIndices.size(), newIndices.size());
            for (int index = 0; index < count; ++index) {
                const int oldIndex = oldIndices.at(index);
                const int newIndex = newIndices.at(index);
                if (oldMatched.at(oldIndex) || newMatched.at(newIndex)) {
                    continue;
                }
                oldMatched[oldIndex] = true;
                newMatched[newIndex] = true;
                ++result.matchedBlockCount;
                const LayoutBlock& oldBlock = oldBlocks.at(oldIndex);
                const LayoutBlock& newBlock = newBlocks.at(newIndex);
                const double iou =
                    normalizedRectIou(oldBlock.normalizedBounds, newBlock.normalizedBounds);
                classifyPair(oldBlock, newBlock, 1.0, iou, options, &differences, &result);
            }
        }
    }

    // ---------------------------------------------------------- 阶段二 ----
    //
    // 剩下的块按「归一化 IoU + 文本相似度」复合打分贪心配对。
    // 先在配成的页对内部配，再在没配上页的页之间配（跨页归因）。
    QVector<PageSignature> oldSignatures;
    QVector<PageSignature> newSignatures;
    oldSignatures.reserve(oldPages.size());
    newSignatures.reserve(newPages.size());
    for (const QVector<LayoutBlock>& page : oldPages) {
        oldSignatures.append(signatureOf(page));
    }
    for (const QVector<LayoutBlock>& page : newPages) {
        newSignatures.append(signatureOf(page));
    }

    const QVector<PagePair> pagePairs =
        pairPages(oldSignatures, newSignatures, options.pagePairThreshold);

    for (const PagePair& pair : pagePairs) {
        QVector<Candidate> matched;
        matchBlocks(oldBlocks, newBlocks, oldMatched, newMatched,
                    pair.oldPage, pair.newPage, options, &matched);
        for (const Candidate& candidate : matched) {
            oldMatched[candidate.oldIndex] = true;
            newMatched[candidate.newIndex] = true;
            ++result.matchedBlockCount;
            classifyPair(oldBlocks.at(candidate.oldIndex), newBlocks.at(candidate.newIndex),
                         candidate.similarity, candidate.iou, options, &differences, &result);
        }
    }

    // 没配上页的页之间再配一次：块被挪到「新增的一页」上时，
    // 页级配对根本配不上，不补这一步就只会报「删页 + 加页」。
    {
        QVector<int> oldOnly;
        QVector<int> newOnly;
        for (int index = 0; index < oldPages.size(); ++index) {
            bool paired = false;
            for (const PagePair& pair : pagePairs) {
                if (pair.oldPage == index) {
                    paired = true;
                    break;
                }
            }
            if (!paired) {
                oldOnly.append(index);
            }
        }
        for (int index = 0; index < newPages.size(); ++index) {
            bool paired = false;
            for (const PagePair& pair : pagePairs) {
                if (pair.newPage == index) {
                    paired = true;
                    break;
                }
            }
            if (!paired) {
                newOnly.append(index);
            }
        }
        int attempts = 0;
        for (const int oldPage : oldOnly) {
            for (const int newPage : newOnly) {
                if (attempts >= options.maxCrossPageCandidates) {
                    break;
                }
                ++attempts;
                QVector<Candidate> matched;
                matchBlocks(oldBlocks, newBlocks, oldMatched, newMatched,
                            oldPage, newPage, options, &matched);
                for (const Candidate& candidate : matched) {
                    oldMatched[candidate.oldIndex] = true;
                    newMatched[candidate.newIndex] = true;
                    ++result.matchedBlockCount;
                    classifyPair(oldBlocks.at(candidate.oldIndex),
                                 newBlocks.at(candidate.newIndex),
                                 candidate.similarity, candidate.iou, options,
                                 &differences, &result);
                }
            }
        }
    }

    // ---------------------------------------------------------- 阶段三 ----
    //
    // 剩下没配上的：一边的块被删了 / 新增了。
    for (int index = 0; index < oldBlocks.size(); ++index) {
        if (oldMatched.at(index)) {
            continue;
        }
        const LayoutBlock& block = oldBlocks.at(index);
        differences.append(makeDifference(LayoutDiffKind::BlockDeleted, block, block, 0.0, 0.0));
        ++result.deletedBlockCount;
    }
    for (int index = 0; index < newBlocks.size(); ++index) {
        if (newMatched.at(index)) {
            continue;
        }
        const LayoutBlock& block = newBlocks.at(index);
        differences.append(makeDifference(LayoutDiffKind::BlockInserted, block, block, 0.0, 0.0));
        ++result.insertedBlockCount;
    }

    // 排序：先按页码，再按块序号，最后按差异种类。
    std::stable_sort(differences.begin(), differences.end(),
                     [](const LayoutDifference& left, const LayoutDifference& right) {
                         const int leftPage = layoutDisplayPage(left);
                         const int rightPage = layoutDisplayPage(right);
                         if (leftPage != rightPage) {
                             return leftPage < rightPage;
                         }
                         const int leftBlock = layoutDisplayBlock(left);
                         const int rightBlock = layoutDisplayBlock(right);
                         if (leftBlock != rightBlock) {
                             return leftBlock < rightBlock;
                         }
                         return layoutKindOrder(left.kind) < layoutKindOrder(right.kind);
                     });

    result.differences = differences;
    result.analyzed = true;
    return result;
}

bool compareDocumentLayout(const QByteArray& oldBytes,
                           const QByteArray& newBytes,
                           const LayoutDiffOptions& options,
                           LayoutDiffResult* result,
                           QString* error) {
    if (result == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("输出参数为空");
        }
        return false;
    }
    *result = LayoutDiffResult();

    if (oldBytes.isEmpty() || newBytes.isEmpty()) {
        result->error = QStringLiteral("没有可比较的内容");
        if (error != nullptr) {
            *error = result->error;
        }
        return false;
    }

    const PdfOpenResult oldOpened = openDocumentFromBytes(oldBytes);
    const PdfOpenResult newOpened = openDocumentFromBytes(newBytes);
    if (!oldOpened.document.isValid() || !newOpened.document.isValid()) {
        result->error = QStringLiteral("打不开文档，无法分析版面结构");
        if (error != nullptr) {
            *error = result->error;
        }
        return false;
    }

    QVector<QVector<LayoutBlock>> oldPages;
    QVector<QVector<LayoutBlock>> newPages;

    int fingerprintPages = 0;
    const auto extract = [&](const PdfDocument& document,
                             QVector<QVector<LayoutBlock>>* pages,
                             bool* fingerprintLimitReached) -> bool {
        const int pageCount = document.pageCount();
        pages->resize(pageCount);
        for (int index = 0; index < pageCount; ++index) {
            const std::optional<PdfPage> page = document.page(index);
            if (!page.has_value()) {
                continue;
            }
            LayoutBlockOptions blockOptions;
            blockOptions.maxBlocksPerPage = options.maxBlocksPerPage;
            blockOptions.maximumCellTextLength = options.maximumCellTextLength;
            // 渲染预算按「真正渲染过的页数」算：没有图片的页根本不渲染，
            // 不能把预算浪费在它们身上。
            blockOptions.computeFingerprint = fingerprintPages < options.maxFingerprintPages;
            bool fingerprintComputed = false;
            blockOptions.fingerprintComputed = &fingerprintComputed;
            QVector<LayoutBlock> blocks;
            QString reason;
            if (!extractLayoutBlocks(*page, index, blockOptions, &blocks, &reason)) {
                continue;
            }
            if (fingerprintComputed) {
                ++fingerprintPages;
            } else if (!blockOptions.computeFingerprint) {
                *fingerprintLimitReached = true;
            }
            (*pages)[index] = blocks;
        }
        return true;
    };

    bool fingerprintLimitReached = false;
    extract(oldOpened.document, &oldPages, &fingerprintLimitReached);
    extract(newOpened.document, &newPages, &fingerprintLimitReached);

    // 两边都没有可分析的版面内容：多半是扫描件。
    //
    // 不能报「没有差异」——那是把「比不了」说成「一样」，
    // 用户会以为两份合同真的逐字相同。这里明确说「比不了」。
    int oldTotal = 0;
    int newTotal = 0;
    for (const QVector<LayoutBlock>& page : oldPages) {
        oldTotal += page.size();
    }
    for (const QVector<LayoutBlock>& page : newPages) {
        newTotal += page.size();
    }
    if (oldTotal == 0 && newTotal == 0) {
        result->analyzed = false;
        result->error = QStringLiteral(
            "两份文件都没有可分析的版面内容（可能是扫描件），无法比较版面结构；请先运行 OCR。");
        if (error != nullptr) {
            *error = result->error;
        }
        return false;
    }

    *result = diffLayoutBlocks(oldPages, newPages, options);
    if (fingerprintLimitReached) {
        result->note = QStringLiteral(
            "pixel fingerprint skipped for pages beyond the limit (%1 pages)")
            .arg(options.maxFingerprintPages);
    }
    if (error != nullptr) {
        *error = QString();
    }
    return true;
}

}  // namespace aipdf::pdf
