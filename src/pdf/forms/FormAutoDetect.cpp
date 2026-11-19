#include "pdf/forms/FormAutoDetect.h"

#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/TextPage.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {
namespace {

// ============================================================ 判定用的常量 ========

// 判定"这条线是水平的"的容差（点）。
// 为什么要容差：PDF 里的坐标是浮点数，同一条水平线的两端点常常差零点几个点。
constexpr double kAxisTolerance = 1.5;

// 一条线至少要这么长才算线框。
// 太短的线段多半是勾号、边框转角的残留、装饰线，报成字段全是误报。
constexpr double kMinSegmentLength = 12.0;

// 合并共线线段时的位置容差与允许缺口。
constexpr double kMergePositionTolerance = 1.5;
constexpr double kMergeGap = 2.0;

// 格子的最小尺寸与行带的最大高度。
constexpr double kMinCellWidth = 18.0;
constexpr double kMinCellHeight = 6.0;
constexpr double kMaxCellHeight = 90.0;

// 标签与候选区之间的最大水平距离（点）。
constexpr double kMaxLabelGap = 72.0;
// 上方标签与候选区之间的最大垂直距离（点）。
constexpr double kMaxAboveGap = 16.0;
// 标签与候选区的最小纵向重叠（占较小一方高度的比例）。
constexpr double kMinVerticalOverlap = 0.4;
// 上方标签与候选区的最小横向重叠（占标签宽度的比例）。
constexpr double kMinAboveOverlap = 0.6;
// 下划线输入区的高度（点）：线以上的这一条就是用户要填的地方。
constexpr double kUnderlineHeight = 14.0;
// 标签最多几个字：再长就是正文段落，不是标签。
constexpr int kMaxLabelChars = 12;
// 页眉页脚带（占页高的比例）。
constexpr double kHeaderFooterBand = 0.06;
// 整页大小的框（页面边框、装饰框）不报：面积占比超过这个值就当装饰。
constexpr double kMaxFrameAreaRatio = 0.5;
// 勾选框的边长上限（点）：比这个大就不是勾选框了。
constexpr double kMaxCheckBoxSize = 26.0;

// ============================================================ 中间结构 ============

// 一条轴对齐的线段。
struct LineSegment {
    bool horizontal = true;
    double pos = 0.0;  // 水平线的 y / 垂直线的 x
    double lo = 0.0;   // 另一轴上的起点
    double hi = 0.0;   // 另一轴上的终点
};

// 合并共线线段之后的一条线。
struct MergedLine {
    bool horizontal = true;
    double pos = 0.0;
    double lo = 0.0;
    double hi = 0.0;
};

// 一行文字。
struct TextLine {
    QString text;
    QRectF box;
};

// 一个候选区域（还没配标签）。
struct Region {
    QRectF rect;
    bool isCell = false;       // 由框线围出来的格子
    int rowIndex = -1;         // 所在行带
    int columnIndex = -1;      // 所在列带
    int topLineIndex = -1;     // 上边用的横线序号
    int bottomLineIndex = -1;  // 下边用的横线序号
};

// 找到的标签。
struct LabelMatch {
    bool found = false;
    QString text;
    QRectF box;
    bool fromAbove = false;
};

// ============================================================ 线框提取 ============

// 从页面图形里挑出轴对齐的线段。
//
// 为什么要连 Close 段一起看：PDF 的矩形（re）只给三条 Line 边，
// 第四条边是闭合段给的——少了它就只剩三条边，框不成"框"。
void collectSegments(const PageGraphics& graphics, QVector<LineSegment>* horizontal,
                     QVector<LineSegment>* vertical) {
    for (const GraphicsPath& path : graphics.paths) {
        for (const GraphicsSegment& segment : path.segments) {
            if (segment.verb != GraphicsVerb::Line && segment.verb != GraphicsVerb::Close) continue;
            const double dx = segment.p1.x() - segment.p0.x();
            const double dy = segment.p1.y() - segment.p0.y();
            if (qAbs(dy) <= kAxisTolerance && qAbs(dx) >= kMinSegmentLength) {
                horizontal->append({true, (segment.p0.y() + segment.p1.y()) * 0.5,
                                    qMin(segment.p0.x(), segment.p1.x()),
                                    qMax(segment.p0.x(), segment.p1.x())});
            } else if (qAbs(dx) <= kAxisTolerance && qAbs(dy) >= kMinSegmentLength) {
                vertical->append({false, (segment.p0.x() + segment.p1.x()) * 0.5,
                                  qMin(segment.p0.y(), segment.p1.y()),
                                  qMax(segment.p0.y(), segment.p1.y())});
            }
        }
    }
}

// 把共线且挨着的线段并成一条线。
//
// 为什么要合并：表格的边框常常是分成好几段画的（每段一个单元格宽），
// 不合并就没法把它当成一条完整的边。
// 为什么要"挨着"才算：同一行上的两个填空区（"姓名：____  日期：____"）
// 中间隔着几十个点，合并成一条会把两个字段吃成一个。
QVector<MergedLine> mergeLines(QVector<LineSegment> segments) {
    std::sort(segments.begin(), segments.end(), [](const LineSegment& a, const LineSegment& b) {
        if (a.pos != b.pos) return a.pos < b.pos;
        return a.lo < b.lo;
    });
    QVector<MergedLine> merged;
    for (const LineSegment& segment : segments) {
        bool attached = false;
        for (MergedLine& line : merged) {
            if (qAbs(line.pos - segment.pos) > kMergePositionTolerance) continue;
            if (segment.lo > line.hi + kMergeGap || line.lo > segment.hi + kMergeGap) continue;
            line.lo = qMin(line.lo, segment.lo);
            line.hi = qMax(line.hi, segment.hi);
            attached = true;
            break;
        }
        if (!attached) merged.append({segment.horizontal, segment.pos, segment.lo, segment.hi});
    }
    return merged;
}

// 表格里的格子：相邻两条横线围出行带，相邻两条竖线围出列带，
// 行带 × 列带就是格子。
//
// 为什么要竖线"覆盖整条行带"才算：只画了一半的竖线（常见于装饰线、
// 半个表格）会把整页切成列，报出来的全是假字段。
QVector<Region> buildCells(const QVector<MergedLine>& horizontal,
                            const QVector<MergedLine>& vertical) {
    QVector<Region> cells;
    for (int row = 0; row + 1 < horizontal.size(); ++row) {
        // PDF 用户空间里 y 向上：横线按 y 从小到大排，
        // 所以 horizontal[row] 是这条带子的**下**边，horizontal[row + 1] 是上边。
        const MergedLine& lower = horizontal.at(row);
        const MergedLine& upper = horizontal.at(row + 1);
        const double bandHeight = upper.pos - lower.pos;
        if (bandHeight < kMinCellHeight || bandHeight > kMaxCellHeight) continue;
        for (int column = 0; column + 1 < vertical.size(); ++column) {
            const MergedLine& left = vertical.at(column);
            const MergedLine& right = vertical.at(column + 1);
            const double width = right.pos - left.pos;
            if (width < kMinCellWidth) continue;
            if (left.lo > lower.pos + kAxisTolerance || left.hi < upper.pos - kAxisTolerance) continue;
            if (right.lo > lower.pos + kAxisTolerance || right.hi < upper.pos - kAxisTolerance) continue;
            Region region;
            region.rect = QRectF(left.pos, lower.pos, width, bandHeight);
            region.isCell = true;
            region.rowIndex = row;
            region.columnIndex = column;
            region.bottomLineIndex = row;
            region.topLineIndex = row + 1;
            cells.append(region);
        }
    }
    return cells;
}

// 下划线：没有被任何格子用到的横线。线以上的那一条就是填空区。
QVector<Region> buildUnderlines(const QVector<MergedLine>& horizontal,
                                const QVector<Region>& cells) {
    QVector<bool> used(horizontal.size(), false);
    for (const Region& cell : cells) {
        if (cell.bottomLineIndex >= 0 && cell.bottomLineIndex < used.size()) used[cell.bottomLineIndex] = true;
        if (cell.topLineIndex >= 0 && cell.topLineIndex < used.size()) used[cell.topLineIndex] = true;
    }
    QVector<Region> underlines;
    for (int index = 0; index < horizontal.size(); ++index) {
        if (used.at(index)) continue;
        const MergedLine& line = horizontal.at(index);
        if (line.hi - line.lo < kMinCellWidth) continue;
        Region region;
        region.rect = QRectF(line.lo, line.pos, line.hi - line.lo, kUnderlineHeight);
        region.isCell = false;
        underlines.append(region);
    }
    return underlines;
}

// ============================================================ 文字层 ==============

// 这个字符是不是中日韩文字（汉字、全角标点）。
//
// 为什么需要它：CJK 文本里没有词间空格，按几何间距补空格会把
// "姓名："变成"姓名 ："——标签就配不上原来的字段名了。
bool containsCjk(const QString& text) {
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if ((code >= 0x4E00 && code <= 0x9FFF) ||   // 汉字
            (code >= 0x3000 && code <= 0x303F) ||   // CJK 标点
            (code >= 0xFF00 && code <= 0xFFEF)) {   // 全角字符
            return true;
        }
    }
    return false;
}

// 把字符框重建成行。
//
// 为什么不能直接拼 TextPage::plainText()：那是按内容流顺序拼的，
// 一页里的文字常常不是阅读顺序，而且词与词之间的空格在很多 PDF 里
// 是靠定位实现的——直接拼会粘成一团。
QVector<TextLine> buildTextLines(const TextPage& page) {
    struct Item {
        QString text;
        QRectF box;
    };
    QVector<Item> items;
    for (const TextChar& character : page.chars) {
        if (character.text.isEmpty()) continue;
        // PDFium 在换行处会插入控制字符（\r）：不参与几何。
        if (character.text.at(0).unicode() < 0x20) continue;
        if (character.box.width() <= 0.0 || character.box.height() <= 0.0) continue;
        items.append({character.text, character.box});
    }
    if (items.isEmpty()) return {};

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.box.top() != b.box.top()) return a.box.top() < b.box.top();
        return a.box.left() < b.box.left();
    });

    QVector<TextLine> lines;
    QVector<Item> current;
    double referenceTop = 0.0;

    auto flush = [&lines, &current]() {
        if (current.isEmpty()) return;
        std::sort(current.begin(), current.end(), [](const Item& a, const Item& b) {
            return a.box.left() < b.box.left();
        });
        QString text;
        QRectF box = current.first().box;
        double previousRight = current.first().box.right();
        for (const Item& item : current) {
            // 空隙大到一定程度才补空格：否则整页文字会粘成一团。
            // 两个都是中日韩字符时不补：CJK 文本本来就没有词间空格。
            const bool cjkPair = !text.isEmpty() && containsCjk(text.at(text.size() - 1)) &&
                                 containsCjk(item.text);
            if (!text.isEmpty() && !cjkPair &&
                item.box.left() - previousRight > 0.25 * item.box.height()) {
                text += QLatin1Char(' ');
            }
            text += item.text;
            box = box.united(item.box);
            previousRight = qMax(previousRight, item.box.right());
        }
        current.clear();
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) return;
        TextLine line;
        line.text = trimmed;
        line.box = box;
        lines.append(line);
    };

    for (const Item& item : items) {
        if (current.isEmpty()) {
            current.append(item);
            referenceTop = item.box.top();
            continue;
        }
        // 纵向差得太多就是换了一行。
        if (qAbs(item.box.top() - referenceTop) > 0.5 * qMax(item.box.height(), 1.0)) {
            flush();
            referenceTop = item.box.top();
        }
        current.append(item);
    }
    flush();
    return lines;
}

// ============================================================ 标签配对 ============

bool overlapsVertically(const QRectF& a, const QRectF& b, double ratio) {
    const double top = qMax(a.top(), b.top());
    const double bottom = qMin(a.bottom(), b.bottom());
    if (bottom <= top) return false;
    const double smallest = qMin(a.height(), b.height());
    if (smallest <= 0.0) return false;
    return (bottom - top) >= ratio * smallest;
}

// 一个矩形里的文字（表格的表头、标签格）。
QString textInside(const QRectF& rect, const QVector<TextLine>& lines) {
    QStringList parts;
    for (const TextLine& line : lines) {
        const QRectF intersection = line.box.intersected(rect);
        if (intersection.isEmpty()) continue;
        if (intersection.width() < 0.6 * line.box.width()) continue;
        parts.append(line.text);
    }
    return parts.join(QLatin1Char(' ')).trimmed();
}

// 给一个候选区域找标签。
//
// 三种找法，按可信度从高到低：
//   1) 表格里左边那格里的文字（"姓名 | [空]"这种，标签就在隔壁格）；
//   2) 左边同一行的文字（"姓名：____"这种下划线写法）；
//   3) 上方的文字（框形的字段，标签写在框上面）。
// 第 3 种只对格子开放：下划线上方的文字是同一行的别的内容，不是它的标签。
LabelMatch findLabel(const Region& region, const QVector<TextLine>& lines,
                     const QVector<Region>& cells) {
    LabelMatch match;

    if (region.isCell) {
        for (const Region& cell : cells) {
            if (!cell.isCell) continue;
            if (cell.rowIndex != region.rowIndex) continue;
            if (cell.columnIndex != region.columnIndex - 1) continue;
            const QString text = textInside(cell.rect, lines);
            if (text.isEmpty()) continue;
            match.found = true;
            match.text = text;
            match.box = cell.rect;
            return match;
        }
    }

    const TextLine* best = nullptr;
    double bestGap = 0.0;
    for (const TextLine& line : lines) {
        if (line.box.right() > region.rect.left() + 1.0) continue;
        const double gap = region.rect.left() - line.box.right();
        if (gap > kMaxLabelGap) continue;
        if (!overlapsVertically(line.box, region.rect, kMinVerticalOverlap)) continue;
        if (best == nullptr || gap < bestGap) {
            best = &line;
            bestGap = gap;
        }
    }
    if (best != nullptr) {
        match.found = true;
        match.text = best->text;
        match.box = best->box;
        return match;
    }

    if (region.isCell) {
        for (const TextLine& line : lines) {
            // PDF 用户空间里 y 向上：区域的上边是 rect.bottom()。
            if (line.box.top() < region.rect.bottom() - 1.0) continue;
            const double gap = line.box.top() - region.rect.bottom();
            if (gap > kMaxAboveGap) continue;
            const double overlap =
                qMin(line.box.right(), region.rect.right()) - qMax(line.box.left(), region.rect.left());
            if (overlap < kMinAboveOverlap * line.box.width()) continue;
            match.found = true;
            match.text = line.text;
            match.box = line.box;
            match.fromAbove = true;
            return match;
        }
    }
    return match;
}

// 这段文字像不像一个字段标签。
bool looksLikeLabel(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    if (trimmed.size() > kMaxLabelChars) return false;
    // 句号、逗号结尾的是正文句子，不是标签。
    static const QStringList endings = {
        QStringLiteral("。"), QStringLiteral("，"), QStringLiteral("、"), QStringLiteral("；"),
        QStringLiteral("！"), QStringLiteral("？"), QStringLiteral("."),  QStringLiteral(","),
        QStringLiteral(";"),  QStringLiteral("!"),  QStringLiteral("?")};
    for (const QString& ending : endings) {
        if (trimmed.endsWith(ending)) return false;
    }
    return true;
}

// 去掉标签结尾的冒号与空白，剩下的就是字段名。
QString fieldNameFromLabel(const QString& label) {
    QString name = label.trimmed();
    while (!name.isEmpty()) {
        const QChar last = name.at(name.size() - 1);
        // 全角冒号（U+FF1A）与半角冒号都要去掉。
        if (last == QLatin1Char(':') || last == QChar(0xFF1A) || last.isSpace()) {
            name.chop(1);
            continue;
        }
        break;
    }
    return name;
}

// 页眉页脚带里的文字不作为标签。
//
// 为什么按位置排除：页眉页脚是页面上的固定装饰（公司名、页码、密级），
// 它们经常紧挨着一条横线，靠"有标签"这条规则拦不住。
//
// 页面尺寸拿不到时一律放行：这时"页眉页脚带"根本无从谈起，
// 按 0 算会把整页都当成页眉，一个字段都报不出来。
bool inHeaderFooterBand(const QRectF& box, double pageHeight) {
    if (pageHeight <= 0.0) return false;
    const double band = pageHeight * kHeaderFooterBand;
    return box.bottom() >= pageHeight - band || box.top() <= band;
}

// 置信度：证据越硬越高，用户先看高置信度的候选。
double confidenceOf(const LabelMatch& match, bool isCell) {
    double confidence = 0.55;
    const QString text = match.text.trimmed();
    if (text.endsWith(QLatin1Char(':')) || text.endsWith(QStringLiteral("："))) confidence += 0.2;
    if (isCell) confidence += 0.15;
    if (text.size() <= 6) confidence += 0.05;
    if (match.fromAbove) confidence -= 0.1;
    return qBound(0.05, confidence, 0.98);
}

// ============================================================ 单页识别 ============

QVector<DetectedFormField> detectPageCandidates(const QByteArray& pdfBytes, int pageIndex,
                                                 const PdfPage& page, QString* warning) {
    QVector<DetectedFormField> result;

    const PageGraphics graphics = extractPageGraphics(pdfBytes, pageIndex);
    if (!graphics.ok) {
        if (warning != nullptr && !graphics.error.isEmpty()) {
            *warning = QStringLiteral("第 %1 页的图形取不出来：%2").arg(pageIndex + 1).arg(graphics.error);
        }
        return result;
    }

    // 页面尺寸：优先用图形那一层取的盒子（与内容流同一个盒子），
    // 取不到再退回 PDFium 的 MediaBox。
    double pageWidth = graphics.sizePt.width();
    double pageHeight = graphics.sizePt.height();
    if (pageWidth <= 0.0 || pageHeight <= 0.0) {
        const MediaBox media = page.mediaBox();
        pageWidth = media.width();
        pageHeight = media.height();
    }

    QVector<LineSegment> horizontalSegments;
    QVector<LineSegment> verticalSegments;
    collectSegments(graphics, &horizontalSegments, &verticalSegments);
    if (horizontalSegments.isEmpty() && verticalSegments.isEmpty()) {
        // 这一页没有线：不是表单页（正文页、扫描件都走这里）。
        return result;
    }

    const QVector<MergedLine> horizontal = mergeLines(horizontalSegments);
    const QVector<MergedLine> vertical = mergeLines(verticalSegments);

    QVector<Region> cells = buildCells(horizontal, vertical);
    QVector<Region> underlines = buildUnderlines(horizontal, cells);
    QVector<Region> regions = cells;
    regions.append(underlines);

    QVector<TextLine> lines;
    const std::optional<TextPage> text = page.extractText();
    if (text.has_value()) {
        lines = buildTextLines(*text);
    }

    // 页面尺寸拿不到时，"整页框"与"页眉页脚"这两条按位置判断的规则都用不了：
    // 这时只按标签与形状判断，不能因为尺寸缺失就把整页的候选都毙掉。
    const bool sizeKnown = pageWidth > 0.0 && pageHeight > 0.0;
    const double pageArea = sizeKnown ? pageWidth * pageHeight : 0.0;
    QSet<QString> usedNames;

    for (const Region& region : regions) {
        // 整页大小的框是页面边框，不是字段。
        if (sizeKnown && region.rect.width() * region.rect.height() > kMaxFrameAreaRatio * pageArea) {
            continue;
        }
        // 框里已经有字的格子不是待填区（那是标签格或已填好的内容）。
        if (!textInside(region.rect, lines).isEmpty()) continue;

        const LabelMatch match = findLabel(region, lines, cells);
        if (!match.found) continue;
        if (!looksLikeLabel(match.text)) continue;
        if (inHeaderFooterBand(match.box, pageHeight)) continue;

        QString name = fieldNameFromLabel(match.text);
        if (name.isEmpty()) continue;
        // 同名字段不能重复建域：第二个加个后缀。
        if (usedNames.contains(name)) {
            int suffix = 2;
            while (usedNames.contains(QStringLiteral("%1_%2").arg(name).arg(suffix))) ++suffix;
            name = QStringLiteral("%1_%2").arg(name).arg(suffix);
        }
        usedNames.insert(name);

        DetectedFormField field;
        field.pageIndex = pageIndex;
        field.name = name;
        field.label = match.text.trimmed();
        field.rect = region.rect;
        field.kind = inferFieldKind(field.label, region.rect);
        field.confidence = confidenceOf(match, region.isCell);
        result.append(field);
    }
    return result;
}

// 两个矩形是否重叠（用于"线框暗示的位置是不是已经有域了"）。
bool rectsOverlap(const QRectF& a, const QRectF& b) {
    const QRectF shrunk = b.adjusted(1.0, 1.0, -1.0, -1.0);
    if (shrunk.width() <= 0.0 || shrunk.height() <= 0.0) return b.intersects(a);
    return shrunk.intersects(a);
}

}  // namespace

// ============================================================ 公开接口 ============

DetectedFieldKind inferFieldKind(const QString& label, const QRectF& rect) {
    const QString text = label.trimmed();
    const double width = rect.width();
    const double height = rect.height();

    // 形状先看：小而方的框是勾选框。
    // 为什么先看形状：勾选框的标签常常是"是否同意"这种，
    // 但也常常只有一个"同意"或者什么都没有——形状比文字更稳。
    if (width >= 5.0 && height >= 5.0 && width <= kMaxCheckBoxSize && height <= kMaxCheckBoxSize &&
        qAbs(width - height) <= 4.0) {
        return DetectedFieldKind::CheckBox;
    }

    static const QStringList dateWords = {
        QStringLiteral("日期"), QStringLiteral("时间"),   QStringLiteral("生日"),
        QStringLiteral("出生"), QStringLiteral("到期"),   QStringLiteral("年月日"),
        QStringLiteral("签署日"), QStringLiteral("填表日")};
    static const QStringList numberWords = {
        QStringLiteral("金额"),   QStringLiteral("价格"),   QStringLiteral("价钱"),
        QStringLiteral("数量"),   QStringLiteral("总价"),   QStringLiteral("合计"),
        QStringLiteral("税额"),   QStringLiteral("税率"),   QStringLiteral("比例"),
        QStringLiteral("电话"),   QStringLiteral("手机"),   QStringLiteral("号码"),
        QStringLiteral("编号"),   QStringLiteral("邮编"),   QStringLiteral("证号"),
        QStringLiteral("身份证"), QStringLiteral("税号"),   QStringLiteral("账号"),
        QStringLiteral("卡号"),   QStringLiteral("年龄"),   QStringLiteral("人数"),
        QStringLiteral("工资"),   QStringLiteral("余额"),   QStringLiteral("单价")};
    static const QStringList checkWords = {
        QStringLiteral("是否"), QStringLiteral("同意"), QStringLiteral("勾选"),
        QStringLiteral("已阅"), QStringLiteral("确认")};

    for (const QString& word : dateWords) {
        if (text.contains(word)) return DetectedFieldKind::Date;
    }
    for (const QString& word : numberWords) {
        if (text.contains(word)) return DetectedFieldKind::Number;
    }
    for (const QString& word : checkWords) {
        if (text.contains(word)) return DetectedFieldKind::CheckBox;
    }
    return DetectedFieldKind::Text;
}

QString detectedKindName(DetectedFieldKind kind) {
    switch (kind) {
        case DetectedFieldKind::Number:
            return QStringLiteral("number");
        case DetectedFieldKind::Date:
            return QStringLiteral("date");
        case DetectedFieldKind::CheckBox:
            return QStringLiteral("checkbox");
        default:
            return QStringLiteral("text");
    }
}

FormDetectionResult detectFormFieldsOnPage(const QByteArray& pdfBytes, int pageIndex) {
    FormDetectionResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("文档是空的。");
        return result;
    }

    const PdfOpenResult opened = openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        result.error = opened.errorMessage.isEmpty() ? QStringLiteral("打不开这个文档。")
                                                     : opened.errorMessage;
        return result;
    }

    const int pageCount = opened.document.pageCount();
    result.pageCount = pageCount;
    if (pageIndex < 0 || pageIndex >= pageCount) {
        result.error = QStringLiteral("没有第 %1 页。").arg(pageIndex + 1);
        return result;
    }

    // 已有域：照旧枚举，行为不变。
    const QVector<FormField> existing = opened.document.formFields();
    QVector<ExistingFormField> pageExisting;
    for (const FormField& field : existing) {
        if (field.pageIndex != pageIndex) continue;
        ExistingFormField item;
        item.pageIndex = field.pageIndex;
        item.name = field.name;
        item.rect = field.rect;
        item.pdfiumType = field.type;
        pageExisting.append(item);
        result.existingFields.append(item);
    }

    const std::optional<PdfPage> page = opened.document.page(pageIndex);
    if (!page.has_value()) {
        result.warnings.append(QStringLiteral("第 %1 页打不开。").arg(pageIndex + 1));
        result.ok = true;
        return result;
    }

    QString warning;
    const QVector<DetectedFormField> pageCandidates =
        detectPageCandidates(pdfBytes, pageIndex, *page, &warning);
    if (!warning.isEmpty()) result.warnings.append(warning);

    for (const DetectedFormField& candidate : pageCandidates) {
        bool covered = false;
        for (const ExistingFormField& field : pageExisting) {
            if (rectsOverlap(candidate.rect, field.rect)) {
                covered = true;
                break;
            }
        }
        if (covered) continue;
        // 已有域的页与没有域的页分开列：一个是事实，一个是猜测。
        if (pageExisting.isEmpty()) {
            result.candidates.append(candidate);
        } else {
            result.supplementary.append(candidate);
        }
    }

    result.ok = true;
    return result;
}

FormDetectionResult detectFormFields(const QByteArray& pdfBytes) {
    FormDetectionResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("文档是空的。");
        return result;
    }

    const PdfOpenResult opened = openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        result.error = opened.errorMessage.isEmpty() ? QStringLiteral("打不开这个文档。")
                                                     : opened.errorMessage;
        return result;
    }

    const int pageCount = opened.document.pageCount();
    result.pageCount = pageCount;

    // 已有域：照旧枚举，行为不变。
    const QVector<FormField> existing = opened.document.formFields();
    for (const FormField& field : existing) {
        ExistingFormField item;
        item.pageIndex = field.pageIndex;
        item.name = field.name;
        item.rect = field.rect;
        item.pdfiumType = field.type;
        result.existingFields.append(item);
    }

    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        const std::optional<PdfPage> page = opened.document.page(pageIndex);
        if (!page.has_value()) {
            result.warnings.append(QStringLiteral("第 %1 页打不开。").arg(pageIndex + 1));
            continue;
        }

        QString warning;
        const QVector<DetectedFormField> pageCandidates =
            detectPageCandidates(pdfBytes, pageIndex, *page, &warning);
        if (!warning.isEmpty()) result.warnings.append(warning);

        bool pageHasFields = false;
        for (const ExistingFormField& field : result.existingFields) {
            if (field.pageIndex == pageIndex) {
                pageHasFields = true;
                break;
            }
        }

        for (const DetectedFormField& candidate : pageCandidates) {
            bool covered = false;
            for (const ExistingFormField& field : result.existingFields) {
                if (field.pageIndex != pageIndex) continue;
                if (rectsOverlap(candidate.rect, field.rect)) {
                    covered = true;
                    break;
                }
            }
            if (covered) continue;
            if (pageHasFields) {
                result.supplementary.append(candidate);
            } else {
                result.candidates.append(candidate);
            }
        }
    }

    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
