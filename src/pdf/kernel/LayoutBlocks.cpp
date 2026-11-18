#include "pdf/kernel/LayoutBlocks.h"

#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/TableStructure.h"

#include <QHash>
#include <QImage>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

void setReason(QString* reason, const QString& message) {
    if (reason != nullptr) {
        *reason = message;
    }
}

// FNV-1a：给块内容算一个指纹。
//
// 为什么用 FNV-1a 而不是更强的哈希：这里要的是「同样的内容算出同样的值」，
// 而且指纹会被塞进结果结构体长期保存，8 字节够了。
quint64 fnv1aText(const QString& text) {
    quint64 hash = 1469598103934665603ULL;
    for (const QChar character : text) {
        hash ^= static_cast<quint64>(character.unicode());
        hash *= 1099511628211ULL;
    }
    return hash;
}

// 归一化：连续空白压成一个空格，首尾空白去掉。
QString normalizeText(const QString& text) {
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

// 一行里两段文字的横向间隔超过这个点数就当成两块。
constexpr double kMinimumBlockGapPoints = 12.0;

// 两个字符的竖直区间重叠不到较矮者的这个比例就算不同行。
constexpr double kLineOverlapRatio = 0.4;

// 相邻两行之间的间距超过行距中位数的这个比例就算换段。
constexpr double kParagraphGapRatio = 0.5;

// 两行要算同一段，横向得至少重叠这么多（按较窄的那一行算）。
constexpr double kParagraphOverlapRatio = 0.5;

// 同一段里下一行的左边界最多能往右偏这么多点（相对上一行的宽度）。
constexpr double kParagraphIndentTolerancePoints = 12.0;
constexpr double kParagraphIndentToleranceRatio = 0.2;

// 「满行」判定：行的右边界离它所在列的最右边界差这么多点以内就算满行。
constexpr double kFullLineTolerancePoints = 12.0;

// 同一列的行：左边界相差不超过这么多点算同一列。
constexpr double kColumnLeftTolerancePoints = 8.0;

// 句子结束符：上一行以这些字符结尾时，通常意味着段落到此结束。
bool endsSentence(const QString& text) {
    if (text.isEmpty()) {
        return false;
    }
    const QChar last = text.at(text.size() - 1);
    return last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?')
        || last == QChar(0x3002)      // 。
        || last == QChar(0xFF01)      // ！
        || last == QChar(0xFF1F)      // ？
        || last == QChar(0xFF1B)      // ；
        || last == QChar(0x2026);     // …
}

double medianOf(QVector<double> values) {
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values.at(middle);
    }
    return (values.at(middle - 1) + values.at(middle)) * 0.5;
}

// 一个字符。
struct TextItem {
    QString text;
    QRectF box;
};

// 一个视觉行：一行里的文字块（横向间隔过大处切开）与整行的信息。
struct TextRow {
    // 行里的文字块（按 x 从左到右）。
    struct Segment {
        QString text;
        QRectF bounds;
    };
    QVector<Segment> segments;
    QRectF bounds;
    QString text;
};

// 把一页的字符聚成行，行内再按横向间隔切块。
//
// 规则与 DocumentComparer 的 groupIntoBlocks 一致：
// 先按坐标重排（PDFium 给的是内容流顺序，不保证视觉顺序），
// 再按竖直重叠分行，行内按横向间隔切块。
QVector<TextRow> groupIntoRows(const TextPage& textPage) {
    QVector<TextItem> items;
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

    std::sort(items.begin(), items.end(), [](const TextItem& left, const TextItem& right) {
        const double leftCenter = left.box.center().y();
        const double rightCenter = right.box.center().y();
        if (std::abs(leftCenter - rightCenter) > 1e-6) {
            return leftCenter > rightCenter;  // y 大的在上面（PDF user space 向上为正）
        }
        return left.box.left() < right.box.left();
    });

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
        const double requiredOverlap = kLineOverlapRatio * (charHeight > 0.0 ? charHeight : 1.0);
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

    QVector<TextRow> rows;
    rows.reserve(lines.size());
    for (const Line& line : lines) {
        QVector<int> ordered = line.indices;
        std::sort(ordered.begin(), ordered.end(), [&items](int left, int right) {
            return items.at(left).box.left() < items.at(right).box.left();
        });

        QVector<double> heights;
        heights.reserve(ordered.size());
        for (int index : ordered) {
            const double height = items.at(index).box.height();
            if (height > 0.0) {
                heights.append(height);
            }
        }
        const double medianHeight = medianOf(heights);
        const double gapThreshold = qMax(kMinimumBlockGapPoints, 1.5 * medianHeight);

        TextRow row;
        QString currentText;
        QRectF currentBounds;
        int currentCount = 0;
        double previousRight = 0.0;
        bool hasPrevious = false;

        const auto flush = [&]() {
            if (currentCount == 0) {
                return;
            }
            TextRow::Segment segment;
            segment.text = currentText.trimmed();
            segment.bounds = currentBounds;
            if (!segment.text.isEmpty()) {
                row.segments.append(segment);
            }
            currentText.clear();
            currentBounds = QRectF();
            currentCount = 0;
        };

        for (int index : ordered) {
            const TextItem& item = items.at(index);
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

        if (row.segments.isEmpty()) {
            continue;
        }
        row.bounds = row.segments.first().bounds;
        for (const TextRow::Segment& segment : row.segments) {
            row.bounds = row.bounds.united(segment.bounds);
            if (!row.text.isEmpty()) {
                row.text.append(QLatin1Char(' '));
            }
            row.text.append(segment.text);
        }
        rows.append(row);
    }
    return rows;
}

// 把行合并成段落。
//
// 合并规则（相邻两行算同一段要同时满足）：
//   1. 竖直间距 ≤ 行距中位数的一半——段落之间会多出一段空白；
//   2. 横向重叠 ≥ 较窄那一行的一半——双栏排版的两栏不能并成一段；
//   3. 上一行不以句子结束符结尾——句子没说完，多半还在同一段；
//   4. 上一行是「满行」（右边界到得了它所在列的最右边界）——
//      短行多半是标题、表格单元或列表项，各自成段；
//   5. 下一行的左边界与上一行相差不多——缩进变了通常就是新的一段。
//
// 为什么要有这些条件：只按间距合并的话，一份每行都很短的名录
// 会被并成一大段，块级归因就退化成「整页是一块」，什么也说不清。
QVector<TextRow> mergeRowsIntoParagraphs(const QVector<TextRow>& rows) {
    if (rows.size() <= 1) {
        return rows;
    }

    // 行距中位数：相邻两行的中心距。
    QVector<double> pitches;
    pitches.reserve(rows.size());
    for (int index = 0; index + 1 < rows.size(); ++index) {
        const double pitch = rows.at(index).bounds.center().y() - rows.at(index + 1).bounds.center().y();
        if (pitch > 0.0) {
            pitches.append(pitch);
        }
    }
    const double medianPitch = medianOf(pitches);

    // 每行所在列的最右边界：左边界相近的行算同一列。
    QVector<double> columnRight(rows.size(), 0.0);
    for (int index = 0; index < rows.size(); ++index) {
        double right = rows.at(index).bounds.right();
        for (int other = 0; other < rows.size(); ++other) {
            if (std::abs(rows.at(other).bounds.left() - rows.at(index).bounds.left())
                <= kColumnLeftTolerancePoints) {
                right = qMax(right, rows.at(other).bounds.right());
            }
        }
        columnRight[index] = right;
    }

    QVector<TextRow> paragraphs;
    TextRow current = rows.first();
    for (int index = 1; index < rows.size(); ++index) {
        const TextRow& previous = rows.at(index - 1);
        const TextRow& next = rows.at(index);

        // PDF user space 里上一行在上：上一行的下沿（top()）减去下一行的上沿（bottom()）。
        const double gap = previous.bounds.top() - next.bounds.bottom();
        const double overlap = qMin(previous.bounds.right(), next.bounds.right())
            - qMax(previous.bounds.left(), next.bounds.left());
        const double narrower = qMin(previous.bounds.width(), next.bounds.width());
        const bool overlaps = narrower > 0.0 && overlap >= narrower * kParagraphOverlapRatio;
        const bool tight = medianPitch > 0.0 && gap <= medianPitch * kParagraphGapRatio;
        const bool continues = !endsSentence(previous.text);
        const bool full = previous.bounds.right()
            >= columnRight.at(index - 1) - kFullLineTolerancePoints;
        const double indentTolerance =
            qMax(kParagraphIndentTolerancePoints, previous.bounds.width() * kParagraphIndentToleranceRatio);
        const bool aligned = std::abs(next.bounds.left() - previous.bounds.left()) <= indentTolerance;

        if (overlaps && tight && continues && full && aligned) {
            for (const TextRow::Segment& segment : next.segments) {
                current.segments.append(segment);
                current.bounds = current.bounds.united(segment.bounds);
                current.text.append(QLatin1Char(' '));
                current.text.append(segment.text);
            }
            continue;
        }
        paragraphs.append(current);
        current = next;
    }
    paragraphs.append(current);
    return paragraphs;
}

// 文字块里有多少落在表格区域内（按面积）。
double overlapRatioWith(const QRectF& rect, const QRectF& region) {
    if (rect.isEmpty()) {
        return 0.0;
    }
    const QRectF intersection = rect.intersected(region);
    const double area = rect.width() * rect.height();
    if (area <= 0.0) {
        return 0.0;
    }
    return (intersection.width() * intersection.height()) / area;
}

// 表格块的内容指纹：行列数 + 各单元格的文字。
quint64 tableContentHash(const LayoutBlock& block);

// 表格块：把文字对象按行聚成「带」，每带跑一次 TableStructure::detect。
//
// 为什么要按带切：整页一起检测时，表格与正文会被混进同一张网格里，
// 行数列数全乱。正文每行通常只有一个文本对象（不成带），
// 只有真正的表格才会连着几行都有多个对象。
QVector<LayoutBlock> detectTableBlocks(const QVector<PageObjectInfo>& objects,
                                        int pageIndex,
                                        const LayoutBlockOptions& options) {
    struct Candidate {
        int objectId = -1;
        QRectF bounds;
        QString text;
        double centerY = 0.0;
        double height = 0.0;
    };
    QVector<Candidate> candidates;
    QVector<double> heights;
    for (const PageObjectInfo& object : objects) {
        if (object.type != PageObjectType::Text) {
            continue;
        }
        if (object.index < 0 || object.bounds.isEmpty() || object.text.trimmed().isEmpty()) {
            continue;
        }
        Candidate candidate;
        candidate.objectId = object.index;
        candidate.bounds = object.bounds;
        candidate.text = object.text;
        candidate.centerY = object.bounds.center().y();
        candidate.height = object.bounds.height();
        candidates.append(candidate);
        heights.append(candidate.height);
    }
    if (candidates.size() < 4) {
        return {};
    }

    const double rowTolerance = qMax(2.0, medianOf(heights) * 0.65);
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (std::abs(left.centerY - right.centerY) > 1e-6) {
            return left.centerY > right.centerY;
        }
        return left.bounds.left() < right.bounds.left();
    });

    // 按 y 聚成行。
    QVector<QVector<Candidate>> rows;
    for (const Candidate& candidate : candidates) {
        int target = -1;
        for (int index = 0; index < rows.size(); ++index) {
            double center = 0.0;
            for (const Candidate& item : rows.at(index)) {
                center += item.centerY;
            }
            center /= static_cast<double>(rows.at(index).size());
            if (std::abs(center - candidate.centerY) <= rowTolerance) {
                target = index;
                break;
            }
        }
        if (target < 0) {
            rows.append({candidate});
        } else {
            rows[target].append(candidate);
        }
    }

    // 连续的多对象行组成一个「带」，带与带之间被单对象行隔开。
    QVector<QVector<Candidate>> band;
    QVector<QVector<QVector<Candidate>>> bands;
    for (const QVector<Candidate>& row : rows) {
        if (row.size() >= 2) {
            band.append(row);
        } else if (!band.isEmpty()) {
            bands.append(band);
            band.clear();
        }
    }
    if (!band.isEmpty()) {
        bands.append(band);
    }

    QVector<LayoutBlock> tables;
    for (const QVector<QVector<Candidate>>& currentBand : bands) {
        QVector<TableTextObject> tableObjects;
        for (const QVector<Candidate>& row : currentBand) {
            for (const Candidate& candidate : row) {
                tableObjects.append({candidate.objectId, candidate.bounds, candidate.text});
            }
        }
        const TableStructure table = TableStructure::detect(tableObjects);
        if (!table.isValid()) {
            continue;
        }

        // 单元格文本不能太长：双栏正文按行列聚起来也像一张表，
        // 但它的「单元格」是一整行几百个字的正文。
        QVector<double> lengths;
        QHash<int, QString> mergedText;
        QHash<int, QRectF> mergedBounds;
        for (const TableCell& cell : table.cells) {
            const int key = cell.row * 1000 + cell.column;
            mergedText[key] = mergedText.value(key) + QLatin1Char(' ')
                + cell.text.trimmed();
            if (!mergedBounds.contains(key)) {
                mergedBounds.insert(key, cell.bounds);
            } else {
                mergedBounds.insert(key, mergedBounds.value(key).united(cell.bounds));
            }
            lengths.append(cell.text.trimmed().size());
        }
        if (lengths.isEmpty() || medianOf(lengths) > options.maximumCellTextLength) {
            continue;
        }

        LayoutBlock block;
        block.pageIndex = pageIndex;
        block.kind = LayoutBlockKind::Table;
        block.rowCount = table.rowCount;
        block.columnCount = table.columnCount;
        for (const TableCell& cell : table.cells) {
            LayoutCell layoutCell;
            layoutCell.row = cell.row;
            layoutCell.column = cell.column;
            layoutCell.bounds = cell.bounds;
            layoutCell.text = cell.text.trimmed();
            block.cells.append(layoutCell);
        }
        // 同一个 (行,列) 可能有多个文本对象（一个单元格里塞了两段），
        // 合并成一个单元格，单元格比较才对得上。
        QVector<LayoutCell> merged;
        for (int row = 0; row < table.rowCount; ++row) {
            for (int column = 0; column < table.columnCount; ++column) {
                const int key = row * 1000 + column;
                if (!mergedText.contains(key)) {
                    continue;
                }
                LayoutCell cell;
                cell.row = row;
                cell.column = column;
                cell.bounds = mergedBounds.value(key);
                cell.text = mergedText.value(key).trimmed();
                merged.append(cell);
                block.bounds = block.bounds.isNull() ? cell.bounds : block.bounds.united(cell.bounds);
            }
        }
        block.cells = merged;
        QStringList texts;
        for (const LayoutCell& cell : merged) {
            texts.append(cell.text);
        }
        block.text = normalizeText(texts.join(QLatin1Char(' ')));
        block.contentHash = tableContentHash(block);
        tables.append(block);
    }
    return tables;
}

// 表格块的内容指纹：行列数 + 各单元格的文字。
quint64 tableContentHash(const LayoutBlock& block) {
    quint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](quint64 value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(static_cast<quint64>(block.rowCount));
    mix(static_cast<quint64>(block.columnCount));
    for (const LayoutCell& cell : block.cells) {
        mix(fnv1aText(cell.text));
    }
    return hash;
}

}  // namespace

QString layoutBlockKindName(LayoutBlockKind kind) {
    switch (kind) {
        case LayoutBlockKind::Text:
            return QStringLiteral("text");
        case LayoutBlockKind::Image:
            return QStringLiteral("image");
        case LayoutBlockKind::Table:
            return QStringLiteral("table");
        case LayoutBlockKind::Graphic:
            return QStringLiteral("graphic");
    }
    return QStringLiteral("unknown");
}

QRectF normalizeRect(const QRectF& rect, const MediaBox& box) {
    if (box.width() <= 0.0 || box.height() <= 0.0) {
        return QRectF();
    }
    return QRectF((rect.left() - box.left) / box.width(),
                  (rect.top() - box.bottom) / box.height(),
                  rect.width() / box.width(),
                  rect.height() / box.height());
}

double normalizedRectIou(const QRectF& left, const QRectF& right) {
    if (left.isEmpty() || right.isEmpty()) {
        return 0.0;
    }
    const QRectF intersection = left.intersected(right);
    const double intersectionArea = qMax(0.0, intersection.width()) * qMax(0.0, intersection.height());
    const double leftArea = left.width() * left.height();
    const double rightArea = right.width() * right.height();
    const double unionArea = leftArea + rightArea - intersectionArea;
    if (unionArea <= 0.0) {
        return 0.0;
    }
    return intersectionArea / unionArea;
}

bool buildPagePixelMap(const PdfPage& page,
                       const QImage& rendered,
                       PagePixelMap* map,
                       QString* reason) {
    if (map == nullptr) {
        setReason(reason, QStringLiteral("输出参数为空"));
        return false;
    }
    if (rendered.isNull()) {
        setReason(reason, QStringLiteral("页面渲染失败"));
        return false;
    }
    const MediaBox box = page.contentBox();
    if (box.width() <= 0.0 || box.height() <= 0.0) {
        setReason(reason, QStringLiteral("页面内容区是空的"));
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
    return true;
}

QPointF pixelToUserSpace(const PagePixelMap& map, double px, double py) {
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

QRectF pixelRectToUserSpace(const PagePixelMap& map, const QRect& pixelRect) {
    if (pixelRect.isNull()) {
        return {};
    }
    const QPointF topLeft = pixelToUserSpace(map, pixelRect.left(), pixelRect.top());
    const QPointF bottomRight =
        pixelToUserSpace(map, pixelRect.right() + 1.0, pixelRect.bottom() + 1.0);
    return QRectF(topLeft, bottomRight).normalized();
}

QRect userRectToPixelRect(const PagePixelMap& map, const QRectF& userRect) {
    if (userRect.isEmpty()) {
        return {};
    }
    const QPointF corners[4] = {{userRect.left(), userRect.bottom()},
                                {userRect.right(), userRect.bottom()},
                                {userRect.right(), userRect.top()},
                                {userRect.left(), userRect.top()}};
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    for (int index = 0; index < 4; ++index) {
        const double x = corners[index].x();
        const double y = corners[index].y();
        double px = 0.0;
        double py = 0.0;
        switch (map.rotation) {
            case 90:
                px = (y - map.box.bottom) * map.pixelsPerUnitY;
                py = (x - map.box.left) * map.pixelsPerUnitX;
                break;
            case 180:
                px = (map.box.right - x) * map.pixelsPerUnitX;
                py = (y - map.box.bottom) * map.pixelsPerUnitY;
                break;
            case 270:
                px = (map.box.top - y) * map.pixelsPerUnitY;
                py = (map.box.right - x) * map.pixelsPerUnitX;
                break;
            case 0:
            default:
                px = (x - map.box.left) * map.pixelsPerUnitX;
                py = (map.box.top - y) * map.pixelsPerUnitY;
                break;
        }
        if (index == 0) {
            minX = maxX = px;
            minY = maxY = py;
        } else {
            minX = qMin(minX, px);
            maxX = qMax(maxX, px);
            minY = qMin(minY, py);
            maxY = qMax(maxY, py);
        }
    }
    return QRect(QPoint(qFloor(minX), qFloor(minY)), QPoint(qCeil(maxX), qCeil(maxY)));
}

quint64 pixelFingerprint(const QImage& pageImage,
                         const PagePixelMap& map,
                         const QRectF& userRect,
                         int gridSize) {
    if (pageImage.isNull() || gridSize <= 0 || userRect.isEmpty()) {
        return 0;
    }
    const QRect pixelRect =
        userRectToPixelRect(map, userRect).intersected(pageImage.rect());
    if (pixelRect.width() <= 0 || pixelRect.height() <= 0) {
        return 0;
    }
    const QImage image = pageImage.format() == QImage::Format_ARGB32
                             ? pageImage
                             : pageImage.convertToFormat(QImage::Format_ARGB32);

    // 每个格子最多取 4×4 个采样点：一整页的图片有上百万像素，
    // 全取会慢到看不出差别，而指纹只需要能区分「换了图」就够了。
    QVector<int> levels;
    levels.reserve(gridSize * gridSize);
    for (int row = 0; row < gridSize; ++row) {
        const int y0 = pixelRect.top() + (pixelRect.height() * row) / gridSize;
        const int y1 = pixelRect.top() + (pixelRect.height() * (row + 1)) / gridSize;
        for (int column = 0; column < gridSize; ++column) {
            const int x0 = pixelRect.left() + (pixelRect.width() * column) / gridSize;
            const int x1 = pixelRect.left() + (pixelRect.width() * (column + 1)) / gridSize;
            long long sum = 0;
            int count = 0;
            const int stepY = qMax(1, (y1 - y0) / 4);
            const int stepX = qMax(1, (x1 - x0) / 4);
            for (int y = y0; y < y1; y += stepY) {
                for (int x = x0; x < x1; x += stepX) {
                    sum += qGray(image.pixel(x, y));
                    ++count;
                }
            }
            levels.append(count > 0 ? static_cast<int>(sum / count) : 0);
        }
    }

    quint64 hash = 1469598103934665603ULL;
    for (const int level : levels) {
        hash ^= static_cast<quint64>(level);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool extractLayoutBlocks(const PdfPage& page,
                         int pageIndex,
                         const LayoutBlockOptions& options,
                         QVector<LayoutBlock>* blocks,
                         QString* reason) {
    if (blocks == nullptr) {
        setReason(reason, QStringLiteral("输出参数为空"));
        return false;
    }
    blocks->clear();

    const MediaBox box = page.contentBox();
    if (box.width() <= 0.0 || box.height() <= 0.0) {
        setReason(reason, QStringLiteral("页面内容区是空的"));
        return false;
    }
    const QRectF contentRect(box.left, box.bottom, box.width(), box.height());

    const QVector<PageObjectInfo> objects = page.objectsInRect(contentRect, 0.0);

    QVector<LayoutBlock> result;

    // 表格块（要排在文字之前：表格区域里的文字不再单独成块）。
    QVector<LayoutBlock> tables;
    if (options.detectTables) {
        tables = detectTableBlocks(objects, pageIndex, options);
    }
    result.append(tables);

    // 文字块：字符 → 行 → 段落。
    const std::optional<TextPage> textPage = page.extractText();
    if (textPage.has_value()) {
        const QVector<TextRow> rows = groupIntoRows(*textPage);
        QVector<TextRow> kept;
        kept.reserve(rows.size());
        for (const TextRow& row : rows) {
            bool insideTable = false;
            for (const LayoutBlock& table : tables) {
                if (overlapRatioWith(row.bounds, table.bounds) >= 0.5) {
                    insideTable = true;
                    break;
                }
            }
            if (!insideTable) {
                kept.append(row);
            }
        }
        const QVector<TextRow> paragraphs = mergeRowsIntoParagraphs(kept);
        for (const TextRow& paragraph : paragraphs) {
            LayoutBlock block;
            block.pageIndex = pageIndex;
            block.kind = LayoutBlockKind::Text;
            block.bounds = paragraph.bounds;
            block.text = normalizeText(paragraph.text);
            block.contentHash = fnv1aText(block.text);
            result.append(block);
        }
    }

    // 图片块与图形块。
    const double contentArea = box.width() * box.height();
    const double minimumGraphicArea = contentArea * options.minimumGraphicAreaRatio;
    for (const PageObjectInfo& object : objects) {
        if (object.bounds.isEmpty() || !object.bounds.intersects(contentRect)) {
            continue;
        }
        if (object.type == PageObjectType::Image && options.detectImages) {
            LayoutBlock block;
            block.pageIndex = pageIndex;
            block.kind = LayoutBlockKind::Image;
            block.bounds = object.bounds;
            result.append(block);
        } else if ((object.type == PageObjectType::Path || object.type == PageObjectType::Shading)
                   && options.detectGraphics) {
            const double area = object.bounds.width() * object.bounds.height();
            if (area < minimumGraphicArea) {
                continue;
            }
            LayoutBlock block;
            block.pageIndex = pageIndex;
            block.kind = LayoutBlockKind::Graphic;
            block.bounds = object.bounds;
            result.append(block);
        }
    }

    // 像素指纹：图片与图形块要靠它才能认出「图被换掉了」。
    bool needFingerprint = false;
    for (const LayoutBlock& block : result) {
        if (block.kind == LayoutBlockKind::Image || block.kind == LayoutBlockKind::Graphic) {
            needFingerprint = true;
            break;
        }
    }
    if (needFingerprint && options.computeFingerprint) {
        const QImage rendered = page.render(1.0);
        PagePixelMap map;
        QString mapReason;
        if (buildPagePixelMap(page, rendered, &map, &mapReason)) {
            if (options.fingerprintComputed != nullptr) {
                *options.fingerprintComputed = true;
            }
            for (LayoutBlock& block : result) {
                if (block.kind != LayoutBlockKind::Image && block.kind != LayoutBlockKind::Graphic) {
                    continue;
                }
                const quint64 fingerprint =
                    pixelFingerprint(rendered, map, block.bounds, options.pixelGridSize);
                if (fingerprint != 0) {
                    block.contentHash = fingerprint;
                    block.pixelFingerprintValid = true;
                }
            }
        }
    }

    // 没有像素指纹的图片 / 图形块：退化成「只看几何位置」的指纹。
    //
    // 为什么不能就这么算了：两份文档里同一张图的对象号不一样，
    // 但「同一个位置上的图」在两份文档里应该能配上。
    for (LayoutBlock& block : result) {
        if (block.kind != LayoutBlockKind::Image && block.kind != LayoutBlockKind::Graphic) {
            continue;
        }
        if (block.pixelFingerprintValid) {
            continue;
        }
        quint64 hash = 1469598103934665603ULL;
        const auto mix = [&hash](quint64 value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(static_cast<quint64>(block.kind == LayoutBlockKind::Image ? 1 : 2));
        mix(qRound(block.bounds.left() * 16.0));
        mix(qRound(block.bounds.top() * 16.0));
        mix(qRound(block.bounds.width() * 16.0));
        mix(qRound(block.bounds.height() * 16.0));
        block.contentHash = hash;
    }

    // 归一化矩形 + 阅读顺序编号。
    for (LayoutBlock& block : result) {
        block.normalizedBounds = normalizeRect(block.bounds, box);
    }
    std::stable_sort(result.begin(), result.end(), [](const LayoutBlock& left, const LayoutBlock& right) {
        const double leftCenter = left.bounds.center().y();
        const double rightCenter = right.bounds.center().y();
        if (std::abs(leftCenter - rightCenter) > 1e-6) {
            return leftCenter > rightCenter;  // y 大的在上面
        }
        return left.bounds.left() < right.bounds.left();
    });
    for (int index = 0; index < result.size(); ++index) {
        result[index].blockIndex = index;
    }

    // 页内块数上限：匹配是 O(n×m) 的，不设上限的话畸形文档能把内存吃光。
    if (options.maxBlocksPerPage > 0 && result.size() > options.maxBlocksPerPage) {
        result.resize(options.maxBlocksPerPage);
    }

    *blocks = result;
    setReason(reason, QString());
    return true;
}

}  // namespace aipdf::pdf
