#include "app/editor/PageChunkBuilder.h"

#include "app/editor/PageTransform.h"
#include "app/editor/TextLayout.h"
#include "pdf/kernel/TextPage.h"

#include <algorithm>

namespace aipdf::app {

QVector<doc::ChunkLine> buildPageLines(const pdf::TextPage& text) {
    QVector<doc::ChunkLine> lines;

    // 用页面自己的 /Rotate 建变换：分块是文档的属性，
    // 不该跟着用户的临时旋转变。
    PageTransform transform;
    transform.setPage(text.contentBox.left,
                      text.contentBox.bottom,
                      text.contentBox.width(),
                      text.contentBox.height());
    transform.setRotationDegrees(text.rotationDegrees);

    QVector<LayoutChar> chars;
    chars.reserve(text.chars.size());
    for (const pdf::TextChar& character : text.chars) {
        if (character.text.isEmpty()) {
            continue;
        }
        const QChar first = character.text.at(0);
        if (character.text.size() == 1 && first.unicode() < 0x20) {
            // 换行之类的控制字符没有字形，盒子是 0 宽 0 高，
            // 混进行聚类会凭空多出一行。
            continue;
        }
        LayoutChar item;
        item.text = character.text;
        item.box = transform.toDisplay(character.box);
        item.sourceIndex = character.index;
        chars.append(item);
    }
    if (chars.isEmpty()) {
        return lines;
    }

    TextLayout layout;
    layout.setChars(chars);
    const QVector<int>& order = layout.readingOrder();

    for (const TextLayout::Line& line : layout.lines()) {
        doc::ChunkLine result;
        result.text = layout.textForRange(line.start, line.end);
        QRectF box;
        QVector<double> heights;
        for (int position = line.start; position < line.end; ++position) {
            if (position < 0 || position >= order.size()) {
                continue;
            }
            const int source = order.at(position);
            if (source < 0 || source >= text.chars.size()) {
                continue;
            }
            const QRectF& charBox = text.chars.at(source).box;
            box = box.isNull() ? charBox : box.united(charBox);
            heights.append(charBox.height());
        }
        if (!heights.isEmpty()) {
            std::sort(heights.begin(), heights.end());
            result.charHeight = heights.at(heights.size() / 2);
        }
        result.box = box;
        if (!result.text.trimmed().isEmpty()) {
            lines.append(result);
        }
    }
    return lines;
}

}  // namespace aipdf::app
