#include "pdf/compliance/AltTextGenerator.h"

#include <QSet>

#include <cmath>

namespace aipdf::pdf {
namespace {

// 图的说明文字行首会是什么。
const QStringList kCaptionPrefixes = {
    QStringLiteral("图"), QStringLiteral("圖"), QStringLiteral("Figure"), QStringLiteral("figure"),
    QStringLiteral("Fig"), QStringLiteral("fig"), QStringLiteral("图片"), QStringLiteral("照片"),
};

// 一段文字像不像图的说明。
bool looksLikeCaption(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    for (const QString& prefix : kCaptionPrefixes) {
        if (trimmed.startsWith(prefix)) return true;
    }
    return false;
}

// 说明文字与图挨得够近吗（说明文字的行框与图在横向要重叠，
// 纵向要贴着图的上边或下边）。
bool captionMatchesImage(const QRectF& caption, const QRectF& image) {
    if (caption.isNull() || image.isNull()) return false;
    const double horizontalOverlap =
        std::min(caption.right(), image.right()) - std::max(caption.left(), image.left());
    if (horizontalOverlap <= 0.0) return false;
    const double above = image.top() - caption.bottom();
    const double below = caption.top() - image.bottom();
    const double distance = std::min(std::abs(above), std::abs(below));
    // 说明文字一般离图很近；给一个宽松的上限，别把半页之外的文字拉过来。
    const double limit = std::max(48.0, image.height());
    return distance <= limit;
}

// 一块的中文说明（给 issue 用）。
QString describe(const SemanticBlock& block) {
    QString text = block.text.trimmed();
    if (text.size() > 20) text = text.left(20) + QStringLiteral("…");
    if (block.image) text = QStringLiteral("图像");
    if (block.formField) {
        text = block.fieldName.isEmpty() ? QStringLiteral("表单域") : block.fieldName;
    }
    if (block.pageIndex >= 0) {
        return QStringLiteral("第 %1 页的「%2」").arg(block.pageIndex + 1).arg(text);
    }
    return QStringLiteral("「%1」").arg(text);
}

}  // namespace

AltTextResult generateAltTexts(const QVector<SemanticBlock>& blocks) {
    AltTextResult result;
    result.alternatives.reserve(blocks.size());

    // 表头文字：按表格分组收集。
    QHash<int, QStringList> headerCellsOfTable;
    QHash<int, QSet<int>> tableBlocks;
    for (int i = 0; i < blocks.size(); ++i) {
        const SemanticBlock& block = blocks.at(i);
        if (!block.tableCell) continue;
        // 表格用「第一个块的页序号 + 行号」当身份：同一页里可能有几张表。
        const int tableKey = block.pageIndex * 100000 + block.tableRow;
        tableBlocks[tableKey].insert(i);
        if (block.tableHeader) {
            headerCellsOfTable[tableKey].append(block.text.trimmed());
        }
    }

    for (int i = 0; i < blocks.size(); ++i) {
        const SemanticBlock& block = blocks.at(i);
        QString alternative;

        if (block.formField) {
            // 表单域：提示文字优先，其次字段名。
            if (!block.fieldTooltip.isEmpty()) {
                alternative = block.fieldTooltip.trimmed();
            } else if (!block.fieldName.isEmpty()) {
                alternative = block.fieldName.trimmed();
            } else {
                result.issues.append({describe(block),
                                      QStringLiteral("这个表单域既没有提示文字（/TU）也没有字段名（/T），"
                                                     "没有可用的替代文本")});
            }
        } else if (block.image) {
            // 图：找紧挨着的说明文字行。
            QString caption;
            for (int j = 0; j < blocks.size(); ++j) {
                if (j == i) continue;
                const SemanticBlock& other = blocks.at(j);
                if (other.pageIndex != block.pageIndex) continue;
                if (other.image || other.formField || other.tableCell) continue;
                if (!looksLikeCaption(other.text)) continue;
                if (!captionMatchesImage(other.rect, block.rect)) continue;
                caption = other.text.trimmed();
                break;
            }
            if (!caption.isEmpty()) {
                alternative = caption;
            } else {
                result.issues.append({describe(block),
                                      QStringLiteral("这张图旁边没有能当作说明的文字，"
                                                     "没有可用的替代文本")});
            }
        } else if (block.tableHeader) {
            // 表头格：本身不需要 /Alt（表由 /Table 元素带替代文本）。
            alternative.clear();
        } else {
            alternative.clear();
        }

        result.alternatives.append(alternative);
    }

    // 表格整体的替代文本：表头各格的文字。
    for (auto it = headerCellsOfTable.constBegin(); it != headerCellsOfTable.constEnd(); ++it) {
        QStringList cells = it.value();
        cells.removeAll(QString());
        if (cells.isEmpty()) continue;
        // 表格的替代文本挂在 /Table 元素上：键是这张表第一个块的序号。
        const QSet<int>& members = tableBlocks.value(it.key());
        if (members.isEmpty()) continue;
        int first = members.values().first();
        for (int candidate : members) first = std::min(first, candidate);
        result.tableAlternatives.insert(first, cells.join(QStringLiteral("、")));
    }
    return result;
}

}  // namespace aipdf::pdf
