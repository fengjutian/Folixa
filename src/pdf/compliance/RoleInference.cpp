#include "pdf/compliance/RoleInference.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {
namespace {

// 标题比正文大的比例：小于这个比例不算标题（只是稍微大一点）。
constexpr double kHeadingSizeRatio = 1.18;

// 合成一个段落时，两行的左边缘允许差这么多（点）。
constexpr double kParagraphLeftTolerance = 4.0;

// 行的编号前缀（「第 3 章」「3.2.1 标题」「一、概述」）。
//
// 捕获组：1 = 编号本身（用来数层级），3 = 标题文字。
QRegularExpression makeNumberedHeadingPattern() {
    QRegularExpression pattern;
    pattern.setPattern(QStringLiteral(
        "^(?:第\\s*([0-9一二三四五六七八九十百]+)\\s*[章节節篇部分]|[0-9]+(?:\\.[0-9]+)*[.、．]?\\s*|"
        "([一二三四五六七八九十]+)[、．.])\\s*(.*)$"));
    return pattern;
}

// 列表项的标记前缀。
struct ListMarker {
    QString prefix;  // 匹配到的前缀
    int level = 1;   // 层级
};

// 认出列表项的行首标记。
//
// 为什么只认这些：它们在视觉上是明确的「项目符号」或「编号」，
// 看到就能确定是列表项；而「1.」既可能是编号列表，也可能是章节号——
// 那种要靠「后面几行的编号连不连得上」才能判断。
bool detectListMarker(const QString& text, ListMarker* marker) {
    if (marker == nullptr) return false;
    static const QStringList bullets = {
        QStringLiteral("•"), QStringLiteral("·"), QStringLiteral("●"), QStringLiteral("○"),
        QStringLiteral("▪"), QStringLiteral("‣"), QStringLiteral("–"), QStringLiteral("—"),
    };
    const QString trimmed = text.trimmed();
    for (const QString& bullet : bullets) {
        if (trimmed.startsWith(bullet)) {
            marker->prefix = bullet;
            marker->level = 1;
            return true;
        }
    }
    // "- " / "* "：后面必须跟空格，否则是减号或乘号。
    for (const QString& symbol : {QStringLiteral("-"), QStringLiteral("*"), QStringLiteral("+")}) {
        if (trimmed.startsWith(symbol + QLatin1Char(' '))) {
            marker->prefix = symbol;
            marker->level = 1;
            return true;
        }
    }
    // "1)" / "(1)" / "①"
    static const QRegularExpression parenthesized(
        QStringLiteral("^(\\(?[0-9]{1,2}[)\\]]|[①-⑳])\\s*"));
    const QRegularExpressionMatch match = parenthesized.match(trimmed);
    if (match.hasMatch()) {
        marker->prefix = match.captured(1);
        marker->level = 1;
        return true;
    }
    return false;
}

// 一段文字是不是短句（标题通常是短句，不以句号结尾）。
bool looksLikeShortHeading(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    if (trimmed.size() > 60) return false;
    if (trimmed.endsWith(QChar(0x3002)) || trimmed.endsWith(QLatin1Char('.')) ||
        trimmed.endsWith(QChar(0xFF1B)) || trimmed.endsWith(QLatin1Char(';'))) {
        return false;
    }
    return true;
}

// 两行的左边缘是不是差不多（同一段落的特征）。
bool sameLeftEdge(const QRectF& left, const QRectF& right) {
    if (left.isNull() || right.isNull()) return true;
    return std::abs(left.left() - right.left()) <= kParagraphLeftTolerance;
}

// 一块的中文说明（给 issue 用）。
QString describeBlock(const SemanticBlock& block) {
    QString text = block.text.trimmed();
    if (text.size() > 20) text = text.left(20) + QStringLiteral("…");
    if (text.isEmpty()) text = QStringLiteral("（无文字）");
    if (block.pageIndex >= 0) {
        return QStringLiteral("第 %1 页的「%2」").arg(block.pageIndex + 1).arg(text);
    }
    return QStringLiteral("「%1」").arg(text);
}

// 一行是不是「多列」的行（至少两列）。
bool isColumnarRow(const SemanticBlock& block) {
    return block.columnStarts.size() >= 2;
}

// 两行的列位置对不对得上（差在几个点之内算同一列）。
bool columnsAlign(const QVector<double>& left, const QVector<double>& right) {
    if (left.size() != right.size()) return false;
    for (int i = 0; i < left.size(); ++i) {
        if (std::abs(left.at(i) - right.at(i)) > 12.0) return false;
    }
    return true;
}

}  // namespace

RoleInferenceResult inferSemanticRoles(const QVector<SemanticBlock>& blocks) {
    RoleInferenceResult result;

    // ======================================================================
    // 阶段 1：表格检测与拆格
    //
    // 连续几行都是多列行、而且列位置在几行之间对得上 → 一张表。
    // 确认是表之后，把这些「行块」换成「格块」（一格一个块），
    // 行号、列号、表头与否都记在块上——后面建树时按这些信息建 TR / TH / TD。
    // ======================================================================
    QVector<QVector<int>> tableGroups;
    {
        int i = 0;
        while (i < blocks.size()) {
            if (!isColumnarRow(blocks.at(i))) {
                ++i;
                continue;
            }
            int end = i;
            while (end + 1 < blocks.size() && isColumnarRow(blocks.at(end + 1)) &&
                   columnsAlign(blocks.at(i).columnStarts, blocks.at(end + 1).columnStarts)) {
                ++end;
            }
            if (end > i) {
                QVector<int> group;
                for (int k = i; k <= end; ++k) group.append(k);
                tableGroups.append(group);
            }
            i = end + 1;
        }

        QHash<int, int> groupOfBlock;
        QHash<int, int> rowInGroup;
        for (int g = 0; g < tableGroups.size(); ++g) {
            const QVector<int>& group = tableGroups.at(g);
            for (int k = 0; k < group.size(); ++k) {
                groupOfBlock.insert(group.at(k), g);
                rowInGroup.insert(group.at(k), k);
            }
        }

        for (int i = 0; i < blocks.size(); ++i) {
            const auto groupIt = groupOfBlock.constFind(i);
            if (groupIt == groupOfBlock.constEnd()) {
                SemanticBlock block = blocks.at(i);
                if (isColumnarRow(block)) {
                    // 只有一行多列：没有第二行能对照，判不了是不是表格。
                    // 如实标出来，按段落处理。
                    block.reliable = false;
                    block.unreliableReason = QStringLiteral(
                        "这一行看起来分成了几列，但没有第二行能对照，"
                        "不能确定它是不是表格，按段落处理");
                    result.issues.append({describeBlock(block), block.unreliableReason});
                }
                result.blocks.append(block);
                continue;
            }
            const int group = groupIt.value();
            const int row = rowInGroup.value(i);
            const QVector<int>& groupBlocks = tableGroups.at(group);
            const SemanticBlock& line = blocks.at(i);
            const int columnCount = line.columnStarts.size();
            for (int column = 0; column < columnCount; ++column) {
                SemanticBlock cell = line;
                cell.text = column < line.columnTexts.size() ? line.columnTexts.at(column)
                                                             : QString();
                cell.runIndices = column < line.columnRunIndices.size()
                                      ? line.columnRunIndices.at(column)
                                      : QVector<int>();
                cell.columnStarts.clear();
                cell.columnTexts.clear();
                cell.columnRunIndices.clear();
                cell.tableCell = true;
                cell.tableId = group;
                cell.tableRow = row;
                cell.tableColumn = column;
                cell.tableRowCount = groupBlocks.size();
                cell.tableColumnCount = columnCount;
                cell.tableHeader = (row == 0);
                cell.role = (row == 0) ? StructRole::TH : StructRole::TD;
                // 格的矩形：从这一列的起点到下一列的起点。
                const double left = line.columnStarts.at(column);
                const double right = (column + 1 < columnCount) ? line.columnStarts.at(column + 1)
                                                                : line.rect.right();
                cell.rect = QRectF(left, line.rect.top(), std::max(right - left, 1.0),
                                    line.rect.height());
                result.blocks.append(cell);
            }
        }
    }

    // ======================================================================
    // 阶段 2：逐块分类
    // ======================================================================
    QHash<int, double> bodySizeOfPage;
    {
        QVector<int> pageOrder;
        QHash<int, QVector<int>> blocksOfPage;
        for (int i = 0; i < result.blocks.size(); ++i) {
            const int page = result.blocks.at(i).pageIndex;
            if (!blocksOfPage.contains(page)) pageOrder.append(page);
            blocksOfPage[page].append(i);
        }
        for (int page : pageOrder) {
            QMap<double, int> histogram;
            for (int index : blocksOfPage.value(page)) {
                const SemanticBlock& block = result.blocks.at(index);
                if (block.fontSize <= 0.0) continue;
                histogram[block.fontSize] += 1;
            }
            double best = 10.0;
            int bestCount = 0;
            for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
                if (it.value() > bestCount) {
                    bestCount = it.value();
                    best = it.key();
                }
            }
            bodySizeOfPage.insert(page, bestCount > 0 ? best : 10.0);
        }
    }

    // 列表组：连续的列表项归成一组。
    QVector<QPair<int, int>> listGroups;  // (第一个块下标, 最后一个块下标)
    {
        int i = 0;
        while (i < result.blocks.size()) {
            ListMarker marker;
            if (!detectListMarker(result.blocks.at(i).text, &marker)) {
                ++i;
                continue;
            }
            int end = i;
            while (end + 1 < result.blocks.size()) {
                const SemanticBlock& next = result.blocks.at(end + 1);
                ListMarker nextMarker;
                if (!detectListMarker(next.text, &nextMarker)) break;
                if (next.text.trimmed().isEmpty()) break;
                ++end;
            }
            listGroups.append({i, end});
            i = end + 1;
        }
    }

    // 逐块归类（标题 / 列表项 / 段落），并把连续的行合成段落。
    QSet<int> consumed;  // 已经被并进别的块的块下标
    for (int i = 0; i < result.blocks.size(); ++i) {
        if (consumed.contains(i)) continue;
        SemanticBlock& block = result.blocks[i];

        if (block.tableCell) continue;  // 表格单元：类型在阶段 1 定好了
        if (block.image) {
            block.role = StructRole::Figure;
            continue;
        }
        if (block.formField) {
            block.role = StructRole::Form;
            continue;
        }

        // 列表项。
        ListMarker marker;
        if (detectListMarker(block.text, &marker)) {
            block.role = StructRole::LI;
            block.listLevel = marker.level;
            continue;
        }

        // 标题：编号、字号、加粗。
        {
            const QRegularExpression numbered = makeNumberedHeadingPattern();
            const QRegularExpressionMatch match = numbered.match(block.text.trimmed());
            int level = 0;
            if (match.hasMatch() &&
                looksLikeShortHeading(match.hasCaptured(3) ? match.captured(3) : block.text)) {
                const QString number =
                    match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
                // 编号的层数：数字里的点分段数（「3.2.1」是三层）。
                level = (!number.isEmpty() && number.at(0).isDigit())
                            ? static_cast<int>(number.count(QLatin1Char('.'))) + 1
                            : 1;
            } else {
                const double bodySize = bodySizeOfPage.value(block.pageIndex, 10.0);
                if (block.fontSize >= bodySize * kHeadingSizeRatio &&
                    looksLikeShortHeading(block.text)) {
                    // 没有编号：按字号相对大小给层级。
                    level = block.fontSize >= bodySize * 1.6
                                ? 1
                                : (block.fontSize >= bodySize * 1.35 ? 2 : 3);
                } else if (block.bold && block.fontSize >= bodySize * 1.05 &&
                           looksLikeShortHeading(block.text)) {
                    level = 2;
                }
            }
            if (level > 0) {
                block.role = static_cast<StructRole>(static_cast<int>(StructRole::H1) + level - 1);
                continue;
            }
        }

        // 段落：把连续的行合成一个。
        block.role = StructRole::P;
        int end = i;
        while (end + 1 < result.blocks.size()) {
            const SemanticBlock& next = result.blocks.at(end + 1);
            // 跨页不合并：段落元素挂在**一页**的 Part 之下，
            // 把两页的行并成一个元素，写回时那些行就无处安放
            // （实测：多页文档自动打标签后会报「第 N 页有 M 处文字不归属任何语义元素」）。
            if (next.pageIndex != block.pageIndex) break;
            if (next.tableCell || next.image || next.formField) break;
            if (!sameLeftEdge(block.rect, next.rect)) break;
            ListMarker nextMarker;
            if (detectListMarker(next.text, &nextMarker)) break;
            // 已经是标题的块不并进段落。
            if (next.role == StructRole::H1 || next.role == StructRole::H2 ||
                next.role == StructRole::H3 || next.role == StructRole::H4 ||
                next.role == StructRole::H5 || next.role == StructRole::H6) {
                break;
            }
            ++end;
        }
        if (end > i) {
            SemanticBlock merged = block;
            merged.text = block.text.trimmed();
            merged.runIndices = block.runIndices;
            merged.rect = block.rect;
            for (int row = i + 1; row <= end; ++row) {
                const SemanticBlock& next = result.blocks.at(row);
                const QString piece = next.text.trimmed();
                if (!piece.isEmpty()) {
                    merged.text =
                        merged.text.isEmpty() ? piece : merged.text + QLatin1Char(' ') + piece;
                }
                merged.runIndices.append(next.runIndices);
                merged.rect = merged.rect.united(next.rect);
                consumed.insert(row);
            }
            result.blocks[i] = merged;
        }
    }

    // ======================================================================
    // 阶段 3：建树 Document → Part(每页) → Sect → 内容
    // ======================================================================
    SemanticTree& tree = result.tree;
    SemanticNode document;
    document.role = StructRole::Document;
    tree.rootIndex = appendSemanticNode(&tree, document);

    QVector<int> pageOrder;
    QHash<int, QVector<int>> blocksOfPage;
    for (int i = 0; i < result.blocks.size(); ++i) {
        const int page = result.blocks.at(i).pageIndex;
        if (!blocksOfPage.contains(page)) pageOrder.append(page);
        blocksOfPage[page].append(i);
    }

    // 表格组 → 该组在树里的节点（建过一次就不再来）。
    QHash<int, int> tableNodeOfGroup;
    // 列表组 → 该组在树里的节点。
    QHash<int, int> listNodeOfGroup;
    QHash<int, int> groupOfBlock;
    QHash<int, int> groupOfListItem;
    for (int g = 0; g < tableGroups.size(); ++g) {
        for (int k = 0; k < tableGroups.at(g).size(); ++k) {
            groupOfBlock.insert(tableGroups.at(g).at(k), g);
        }
    }
    for (int g = 0; g < listGroups.size(); ++g) {
        for (int k = listGroups.at(g).first; k <= listGroups.at(g).second; ++k) {
            groupOfListItem.insert(k, g);
        }
    }

    for (int page : pageOrder) {
        const QVector<int>& indices = blocksOfPage.value(page);

        SemanticNode part;
        part.role = StructRole::Part;
        part.pageIndex = page;
        const int partIndex = appendSemanticNode(&tree, part);
        appendSemanticChild(&tree, tree.rootIndex, partIndex);

        SemanticNode section;
        section.role = StructRole::Sect;
        section.pageIndex = page;
        const int sectionIndex = appendSemanticNode(&tree, section);
        appendSemanticChild(&tree, partIndex, sectionIndex);

        for (int index : indices) {
            if (consumed.contains(index)) continue;
            const SemanticBlock& block = result.blocks.at(index);

            // 表格单元：按表格组建 Table → TR → TH/TD。
            if (block.tableCell) {
                const int group = block.tableId;
                if (tableNodeOfGroup.contains(group)) continue;
                const int tableIndex = appendSemanticNode(&tree, SemanticNode());
                tree.nodes[tableIndex].role = StructRole::Table;
                tree.nodes[tableIndex].pageIndex = page;
                appendSemanticChild(&tree, sectionIndex, tableIndex);
                tableNodeOfGroup.insert(group, tableIndex);

                // 逐行建 TR，格建 TH / TD。
                int currentRow = -1;
                int rowIndex = -1;
                for (int candidate : indices) {
                    const SemanticBlock& cell = result.blocks.at(candidate);
                    if (!cell.tableCell || cell.tableId != group) continue;
                    if (cell.tableRow != currentRow) {
                        currentRow = cell.tableRow;
                        SemanticNode rowNode;
                        rowNode.role = StructRole::TR;
                        rowNode.pageIndex = page;
                        rowNode.tableRow = currentRow;
                        rowNode.tableRowCount = cell.tableRowCount;
                        rowNode.tableColumnCount = cell.tableColumnCount;
                        rowIndex = appendSemanticNode(&tree, rowNode);
                        appendSemanticChild(&tree, tableIndex, rowIndex);
                    }
                    SemanticNode cellNode;
                    cellNode.role = cell.tableHeader ? StructRole::TH : StructRole::TD;
                    cellNode.pageIndex = page;
                    cellNode.text = cell.text;
                    cellNode.blockIndices.append(candidate);
                    cellNode.tableRow = cell.tableRow;
                    cellNode.tableColumn = cell.tableColumn;
                    cellNode.tableRowCount = cell.tableRowCount;
                    cellNode.tableColumnCount = cell.tableColumnCount;
                    cellNode.tableHeader = cell.tableHeader;
                    const int cellIndex = appendSemanticNode(&tree, cellNode);
                    appendSemanticChild(&tree, rowIndex, cellIndex);
                }
                continue;
            }

            // 列表项：整组建一个 L，每个列表项建一个 LI。
            if (block.role == StructRole::LI) {
                const int group = groupOfListItem.value(index, -1);
                if (group < 0) continue;
                if (listNodeOfGroup.contains(group)) continue;
                listNodeOfGroup.insert(group, -1);
                SemanticNode listNode;
                listNode.role = StructRole::L;
                listNode.pageIndex = page;
                const int listIndex = appendSemanticNode(&tree, listNode);
                appendSemanticChild(&tree, sectionIndex, listIndex);
                listNodeOfGroup.insert(group, listIndex);

                const QPair<int, int>& range = listGroups.at(group);
                for (int candidate = range.first; candidate <= range.second; ++candidate) {
                    if (consumed.contains(candidate)) continue;
                    const SemanticBlock& item = result.blocks.at(candidate);
                    SemanticNode itemNode;
                    itemNode.role = StructRole::LI;
                    itemNode.pageIndex = page;
                    itemNode.text = item.text;
                    itemNode.blockIndices.append(candidate);
                    const int itemIndex = appendSemanticNode(&tree, itemNode);
                    appendSemanticChild(&tree, listIndex, itemIndex);
                }
                continue;
            }

            // 其余内容：标题 / 段落 / 图 / 域。
            SemanticNode node;
            node.role = block.role;
            node.pageIndex = page;
            node.text = block.text;
            node.blockIndices.append(index);
            node.reliable = block.reliable;
            node.unreliableReason = block.unreliableReason;
            if (block.formField) {
                // 表单域的 /T 写字段名：读屏软件靠它念出「这是什么字段」。
                node.title = block.fieldName;
            }
            const int nodeIndex = appendSemanticNode(&tree, node);
            appendSemanticChild(&tree, sectionIndex, nodeIndex);
        }
    }

    // 汇总：不可靠的地方写进树里的 issues。
    for (const RoleInferenceIssue& issue : result.issues) {
        tree.issues.append(QStringLiteral("%1：%2").arg(issue.where, issue.reason));
    }
    return result;
}

}  // namespace aipdf::pdf
