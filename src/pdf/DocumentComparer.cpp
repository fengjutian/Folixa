#include "pdf/DocumentComparer.h"

#include "pdf/PdfiumAdapter.h"
#include "pdf/TextPage.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

// 页面级对齐最多算多少个格子（页数 × 页数）。
//
// 对齐是 O(n×m) 的动态规划：两份各 500 页就是 25 万个格子，
// 再多就会让「比较」变成一件要等很久的事。超限时退化成按下标配对，
// 多出来的页照报「新增/删除」，只是配对不再那么聪明。
constexpr qint64 kMaxPageAlignmentCells = 200000;

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

// 一页的「指纹」：这一页所有块文本的哈希集合。
QSet<quint64> signatureOf(const QVector<TextBlock>& blocks) {
    QSet<quint64> signature;
    signature.reserve(blocks.size());
    for (const TextBlock& block : blocks) {
        signature.insert(hashText(normalizedForCompare(block.text)));
    }
    return signature;
}

// 两个指纹的 Jaccard 相似度（共同 / 并集）。
double jaccard(const QSet<quint64>& left, const QSet<quint64>& right) {
    if (left.isEmpty() && right.isEmpty()) {
        // 两页都没有文字：内容一样（都可能是扫描件），不能因为空集就说成毫不相干。
        return 1.0;
    }
    if (left.isEmpty() || right.isEmpty()) {
        return 0.0;
    }
    int common = 0;
    for (quint64 value : left) {
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

    if (!result->oldHasText && !result->newHasText) {
        // 两边都是扫描件：没有文字可比。
        // 这里必须报「比不了」，不能报「没有差异」——
        // 把「没法比」说成「一样」，用户就会以为两份合同真的逐字相同。
        const QString message = QStringLiteral(
            "两份文件都没有文字层（可能是扫描件），无法比较文字内容；请先运行 OCR。");
        result->error = message;
        setError(error, message);
        return false;
    }

    // 页面级对齐。
    QVector<SequencePair> pagePairs;
    const qint64 cells = static_cast<qint64>(oldPages.size())
        * static_cast<qint64>(newPages.size());
    if (cells > kMaxPageAlignmentCells) {
        // 规模太大：按下标配对，剩下的页照报新增/删除。
        const int common = qMin(oldPages.size(), newPages.size());
        for (int index = 0; index < common; ++index) {
            pagePairs.append({index, index, 0.0});
        }
    } else {
        QVector<QSet<quint64>> oldSignatures;
        QVector<QSet<quint64>> newSignatures;
        oldSignatures.reserve(oldPages.size());
        newSignatures.reserve(newPages.size());
        for (const QVector<TextBlock>& page : oldPages) {
            oldSignatures.append(signatureOf(page));
        }
        for (const QVector<TextBlock>& page : newPages) {
            newSignatures.append(signatureOf(page));
        }

        QVector<QVector<double>> pageSimilarity(oldPages.size(),
                                                QVector<double>(newPages.size(), 0.0));
        for (int oldIndex = 0; oldIndex < oldPages.size(); ++oldIndex) {
            for (int newIndex = 0; newIndex < newPages.size(); ++newIndex) {
                pageSimilarity[oldIndex][newIndex] =
                    jaccard(oldSignatures.at(oldIndex), newSignatures.at(newIndex));
            }
        }
        pagePairs = alignBySimilarity(pageSimilarity);
    }

    QVector<bool> oldPageMatched(oldPages.size(), false);
    QVector<bool> newPageMatched(newPages.size(), false);

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

            CompareDifference added;
            added.kind = DiffKind::PageAdded;
            added.oldPageIndex = -1;
            added.newPageIndex = pair.newIndex;
            added.newText = pageSnippet(newPages.at(pair.newIndex));
            added.similarity = pair.similarity;
            result->differences.append(added);
            ++result->addedPageCount;
            continue;
        }

        ++result->comparedPageCount;
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
