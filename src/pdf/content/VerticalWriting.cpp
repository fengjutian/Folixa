#include "pdf/content/VerticalWriting.h"

#include "pdf/content/TrueTypeSubset.h"

#include <QTextBoundaryFinder>

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {
namespace {

// 竖排原点 x 的默认值：半个 em（500/1000 em），字身框居中于列心。
constexpr double kDefaultOriginX = 500.0;

// 没有竖排度量时 /DW2 的规范默认值：w1y = 880，vy = -1000（向下）。
constexpr double kFallbackOriginY = 880.0;
constexpr double kFallbackDisplacement = -1000.0;

// 数字转字节：最多三位小数，去掉尾随的 0（"300" 而不是 "300.000"）。
QByteArray number(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) ||
                                                text.endsWith(QLatin1Char('.')))) {
        text.chop(1);
    }
    return text.toUtf8();
}

QByteArray number(int value) {
    return QByteArray::number(value);
}

// 切 grapheme（与 pdf::TextReflow 用的是同一套边界规则）。
QVector<QPair<QString, qsizetype>> graphemes(const QString& text) {
    QVector<QPair<QString, qsizetype>> result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    finder.toStart();
    for (qsizetype end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) {
        result.append(qMakePair(text.mid(start, end - start), end - start));
        start = end;
    }
    return result;
}

bool sameMetrics(const VerticalCidMetrics& left, const VerticalCidMetrics& right) {
    return std::abs(left.originY - right.originY) < 1e-6 &&
           std::abs(left.originX - right.originX) < 1e-6 &&
           std::abs(left.displacement - right.displacement) < 1e-6;
}

}  // namespace

VerticalMetrics readVerticalMetrics(const QByteArray& fontBytes) {
    VerticalMetrics metrics;
    const TrueTypeVerticalMetrics raw = readTrueTypeVerticalMetrics(fontBytes);
    if (!raw.hasVerticalMetrics) {
        // 字体没有竖排度量：如实说没有，不编数。
        return metrics;
    }
    const double scale =
        1000.0 / static_cast<double>(raw.unitsPerEm > 0 ? raw.unitsPerEm : 1000);
    metrics.ascent = raw.ascent * scale;
    metrics.descent = raw.descent * scale;
    // 默认宽度取最后一条竖排度量：vmtx 里没单独记录的字形都用它。
    const int lastMetric = raw.numberOfVMetrics > 0 ? raw.numberOfVMetrics - 1 : 0;
    metrics.defaultWidth = raw.advanceHeightForGlyph(lastMetric) * scale;
    metrics.hasVerticalMetrics = true;
    return metrics;
}

VerticalLayout layoutVerticalText(const QString& text, const VerticalLayoutOptions& options) {
    VerticalLayout layout;
    if (options.right <= options.left || options.top <= options.bottom ||
        options.fontSize <= 0.0) {
        layout.error = QStringLiteral("竖排失败：排版区域为空或字号不大于 0");
        return layout;
    }

    const bool hasMetrics = options.metrics.hasVerticalMetrics;
    layout.verticalMetricsDegraded = !hasMetrics;

    // 字身框高度（点）：有竖排度量用字体的竖排默认宽度，
    // 没有就按降级路径用竖排默认宽度（1 em），**不拿横排宽度顶替**。
    const double advanceEm = hasMetrics ? options.metrics.defaultWidth : options.defaultWidth;
    if (advanceEm <= 0.0) {
        layout.error = QStringLiteral("竖排失败：竖排默认宽度不合法");
        return layout;
    }
    const double emHeight = advanceEm / 1000.0 * options.fontSize;
    const double glyphWidth = options.glyphWidth > 0.0 ? options.glyphWidth : emHeight;
    const double advance = emHeight + options.glyphGap;
    const double columnStep = glyphWidth + options.columnGap;

    // 最右列的字身框中心：从右边界往里让半个字身框。
    double columnX = options.right - glyphWidth / 2.0;
    // 当前字身框的顶边（PDF 空间，y 大的那边）。列内自上而下 = y 递减。
    double y = options.top;
    int columnIndex = 0;
    int rowIndex = 0;
    qsizetype consumed = 0;

    for (const auto& item : graphemes(text)) {
        const QString& grapheme = item.first;
        const qsizetype length = item.second;

        // 列里放不下了：换到左边一列。
        if (y - advance < options.bottom) {
            ++columnIndex;
            columnX -= columnStep;
            if (columnX - glyphWidth / 2.0 < options.left) {
                layout.overflow = true;
                break;
            }
            y = options.top;
            rowIndex = 0;
        }

        VerticalGlyph glyph;
        glyph.text = grapheme;
        glyph.columnIndex = columnIndex;
        glyph.rowIndex = rowIndex;
        glyph.origin = QPointF(columnX, y);
        glyph.advance = advance;
        glyph.width = glyphWidth;
        // 空白也占一个字身框（竖排里空格是全角宽的），
        // 但它没有字符码时不会被写进内容流。

        if (layout.columns.size() <= columnIndex) {
            VerticalColumn column;
            column.origin = glyph.origin;
            column.firstGlyph = layout.glyphs.size();
            column.glyphCount = 0;
            layout.columns.append(column);
        }
        layout.columns[columnIndex].glyphCount += 1;
        layout.glyphs.append(glyph);

        y -= advance;
        ++rowIndex;
        consumed += length;
    }

    layout.consumedLength = consumed;
    layout.remainingText = text.mid(consumed);
    layout.overflow = layout.overflow || !layout.remainingText.isEmpty();
    return layout;
}

QByteArray buildVerticalContentStream(const VerticalLayout& layout,
                                       const QString& fontResourceName, double fontSize) {
    if (!layout.error.isEmpty()) {
        return {};
    }
    const QByteArray resource = fontResourceName.toLatin1();
    if (resource.isEmpty()) {
        return {};
    }

    QByteArray out;
    int currentColumn = -1;
    for (const VerticalGlyph& glyph : layout.glyphs) {
        if (glyph.columnIndex != currentColumn) {
            if (currentColumn >= 0) {
                out += "ET\n";
            }
            // 每一列单独一个 BT/ET：列的起点用 Tm 摆到位，
            // 列内字与字之间的推进交给字体的竖排量（/W2、/DW2）。
            out += "BT\n";
            out += "/" + resource + " " + number(fontSize) + " Tf\n";
            out += "1 0 0 1 " + number(glyph.origin.x()) + " " + number(glyph.origin.y()) +
                   " Tm\n";
            currentColumn = glyph.columnIndex;
        }
        if (glyph.code.isEmpty()) {
            // 没有字符码就不写：写了会画出错字。
            continue;
        }
        out += "<" + glyph.code + "> Tj\n";
    }
    if (currentColumn >= 0) {
        out += "ET\n";
    }
    return out;
}

QByteArray buildVerticalWidthArray(const QVector<VerticalCidMetrics>& cids) {
    QVector<VerticalCidMetrics> sorted;
    sorted.reserve(cids.size());
    for (const VerticalCidMetrics& entry : cids) {
        if (entry.cid < 0) {
            continue;
        }
        sorted.append(entry);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const VerticalCidMetrics& left, const VerticalCidMetrics& right) {
                  return left.cid < right.cid;
              });

    QByteArray out = "[";
    int index = 0;
    bool first = true;
    while (index < sorted.size()) {
        const VerticalCidMetrics& head = sorted.at(index);
        // 三元组的顺序：w1y（竖排位移）、v_x、v_y。
        QVector<double> values{head.displacement, head.originX, head.originY};
        int next = index + 1;
        // 连续的、度量相同的 CID 并成一条：c [w1y v_x v_y w1y v_x v_y ...]。
        while (next < sorted.size() && sorted.at(next).cid == sorted.at(next - 1).cid + 1 &&
               sameMetrics(sorted.at(next), sorted.at(next - 1))) {
            values.append(sorted.at(next).displacement);
            values.append(sorted.at(next).originX);
            values.append(sorted.at(next).originY);
            ++next;
        }
        if (!first) {
            out += " ";
        }
        first = false;
        out += number(head.cid) + " [";
        for (int valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            if (valueIndex > 0) {
                out += " ";
            }
            out += number(values.at(valueIndex));
        }
        out += "]";
        index = next;
    }
    out += "]";
    return out;
}

QByteArray buildVerticalFontEntries(const VerticalMetrics& metrics,
                                    const QVector<VerticalCidMetrics>& cids) {
    QByteArray out = "/WMode 1";
    if (!metrics.hasVerticalMetrics) {
        // 没有竖排度量：/DW2 用规范默认值，并且**不给 /W2**（不编数）。
        out += " /DW2 [" + number(kFallbackOriginY) + " " + number(kFallbackDisplacement) + "]";
        return out;
    }
    const double originY = metrics.ascent;
    const double displacement = -metrics.defaultWidth;
    out += " /DW2 [" + number(originY) + " " + number(displacement) + "]";
    if (!cids.isEmpty()) {
        out += " /W2 " + buildVerticalWidthArray(cids);
    }
    return out;
}

QByteArray buildVerticalFontEntriesForFont(const QByteArray& fontBytes,
                                           const QVector<int>& cids) {
    const TrueTypeVerticalMetrics raw = readTrueTypeVerticalMetrics(fontBytes);
    if (!raw.hasVerticalMetrics) {
        // 字体没有竖排度量：如实降级，只给 /WMode 1 与规范默认的 /DW2。
        return buildVerticalFontEntries(VerticalMetrics(), QVector<VerticalCidMetrics>());
    }
    const double scale =
        1000.0 / static_cast<double>(raw.unitsPerEm > 0 ? raw.unitsPerEm : 1000);
    const VerticalMetrics metrics = readVerticalMetrics(fontBytes);

    QVector<VerticalCidMetrics> entries;
    entries.reserve(cids.size());
    for (int cid : cids) {
        if (cid < 0 || cid >= raw.glyphCount) {
            continue;
        }
        VerticalCidMetrics entry;
        entry.cid = cid;
        entry.originY = metrics.ascent;
        entry.originX = kDefaultOriginX;
        entry.displacement = -raw.advanceHeightForGlyph(cid) * scale;
        entries.append(entry);
    }
    return buildVerticalFontEntries(metrics, entries);
}

}  // namespace aipdf::pdf
