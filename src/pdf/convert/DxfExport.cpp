#include "pdf/convert/DxfExport.h"

#include "pdf/convert/ExportFormats.h"  // extractCharFontSizes（内容流里的真实字号）
#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/TextPage.h"

#include <QHash>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace aipdf::pdf {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ============================================================== 小工具 ======

// 不带科学计数法的定点数：PDF 与 DXF 里写科学计数法会被一部分读档器嫌弃。
QString fixedNumber(double value, int decimals = 6) {
    return QString::number(value, 'f', decimals);
}

// DXF 文本里不能有换行；控制字符一律换成空格。
//
// 非 ASCII 字符写成 AutoCAD 的 \U+XXXX 转义：R2000 的 DXF 是 ANSI 编码的，
// 直接塞 UTF-8 字节会被读档器按本地代码页解成乱码。
QString dxfSafeText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QLatin1Char('\t')) {
            out += QLatin1Char(' ');
        } else if (c.unicode() < 0x20) {
            continue;
        } else if (c.unicode() < 0x80) {
            out += c;
        } else {
            out += QStringLiteral("\\U+%1").arg(c.unicode(), 4, 16, QLatin1Char('0')).toUpper();
        }
    }
    return out;
}

// 把 PDF 用户空间的点映射到「显示方向」的坐标。
//
// 与 ExportFormats.cpp 里那份同名函数同义（那边在匿名命名空间里，跨编译单元
// 拿不到，只能各留一份；两边改动要同步）。
//
// PDF 的 /Rotate 是**顺时针**转页面：转 90° 之后，原来的左下角跑到显示区的
// 左上角。页面盒子会正好铺满显示区，所以映射是仿射的。
//
// 为什么用 MediaBox 而不是 QRectF：QRectF 的 top/bottom 是「左上原点」的上下
// 边缘（bottom 是**最大** y），PDF 的 bottom 是「左下原点」的下边缘（最小 y）。
// 两者混用会让整页的 y 平移一个页高。
QPointF mapToDisplay(const QPointF& point, const MediaBox& box, int rotation, double stackOffset) {
    const double width = box.width();
    const double height = box.height();
    const double x = point.x() - box.left;
    const double y = point.y() - box.bottom;
    switch (rotation) {
    case 90: return QPointF(y, width - x) + QPointF(0.0, stackOffset);
    case 180: return QPointF(width - x, height - y) + QPointF(0.0, stackOffset);
    case 270: return QPointF(height - y, x) + QPointF(0.0, stackOffset);
    default: return QPointF(x, y) + QPointF(0.0, stackOffset);
    }
}

// 旋转后的显示尺寸：转 90°/270° 时宽高互换。
QSizeF displaySize(const QSizeF& pageSize, int rotation) {
    if (rotation == 90 || rotation == 270) return QSizeF(pageSize.height(), pageSize.width());
    return pageSize;
}

// ========================================================== 颜色与线宽 ======

// 最接近的 AutoCAD 索引色（ACI）。
//
// 为什么还要索引色：真彩色（组码 420）是 R2004 才有的，老读档器只认 62。
// 两个都写，新读档器用真彩色，老读档器退回近似色。
int nearestColorIndex(double red, double green, double blue) {
    static const int kRed[7] = {255, 255, 0, 0, 0, 255, 255};
    static const int kGreen[7] = {0, 255, 255, 255, 0, 0, 255};
    static const int kBlue[7] = {0, 0, 0, 255, 255, 255, 0};
    // 近黑（纯黑）走索引色 7：AutoCAD 里 7 是「前景色」，白底显示为黑、
    // 黑底显示为白；按最近色去算，纯黑会落到红色上去。
    if (red * 255.0 < 8.0 && green * 255.0 < 8.0 && blue * 255.0 < 8.0) return 7;
    int best = 7;
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index < 7; ++index) {
        const double dr = red * 255.0 - kRed[index];
        const double dg = green * 255.0 - kGreen[index];
        const double db = blue * 255.0 - kBlue[index];
        const double distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index + 1;
        }
    }
    return best;
}

// 点 → DXF 线宽（组码 370，单位 1/100 毫米）并吸附到标准线宽档位。
//
// 为什么换算成毫米：DXF 的线宽是长度量，AutoCAD 用 1/100 毫米；
// 直接把点写进去，CAD 里会以为线宽是原来的 25.4/72 倍那么粗。
int lineWeightHundredthsMm(double lineWidthPoints) {
    if (lineWidthPoints <= 0.0) return 0;
    static const int kStandard[] = {5,   9,   13,  15,  20,  25,  30,  35,  40,  50,  53,
                                     60,  70,  80,  90,  100, 106, 120, 140, 158, 200, 211};
    const double millimeters = lineWidthPoints * 25.4 / 72.0 * 100.0;
    int best = kStandard[0];
    double bestDistance = std::numeric_limits<double>::max();
    for (const int value : kStandard) {
        const double distance = std::abs(millimeters - value);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = value;
        }
    }
    return best;
}

int channelToByte(double value) {
    return qBound(0, static_cast<int>(value * 255.0 + 0.5), 255);
}

// ========================================================== 文本层 → 行 ======

// 一行文字（DXF 侧只需要「文字 + 插入点 + 字号」）。
struct DxfTextLine {
    QString text;
    double left = 0.0;
    double right = 0.0;
    double baseline = 0.0;
    double top = 0.0;
    double fontSize = 0.0;
    // 这个行是不是已经被某个尺寸标注用掉了（用掉就不能再当普通文字导一次）。
    bool consumedByDimension = false;
};

bool isUpperOrDigit(const QString& text) {
    for (const QChar c : text) {
        if (c.isUpper() || c.isDigit()) return true;
    }
    return false;
}

bool isCjk(char32_t code) {
    return (code >= 0x2E80 && code <= 0x9FFF) || (code >= 0x3000 && code <= 0x303F) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0xFF00 && code <= 0xFF60);
}

// 把一页的字符框重建成若干行。
//
// 归行口径与 ExportFormats.cpp 那份一致（按**基线**比，不按框中心，
// 否则空格字符会飘到别的行里去），但没搬词间距启发式：DXF 侧只要
// 「一行一个 TEXT 实体」，字与字之间补不补空格对 CAD 没有意义。
QVector<DxfTextLine> buildTextLines(const TextPage& text, const QVector<double>& realSizes) {
    struct Group {
        QVector<int> members;
        double baseline = 0.0;
        double height = 0.0;
    };
    QVector<Group> groups;

    for (int i = 0; i < text.chars.size(); ++i) {
        const TextChar& character = text.chars.at(i);
        if (character.text.isEmpty()) continue;
        const QChar first = character.text.at(0);
        if (first == QLatin1Char('\n') || first == QLatin1Char('\r') || first == QLatin1Char('\t') ||
            first == QLatin1Char('\f')) {
            continue;
        }
        const QRectF box = character.box;
        const double low = qMin(box.top(), box.bottom());
        const double high = qMax(box.top(), box.bottom());
        const double height = qMax(high - low, 0.0);

        int target = -1;
        for (int g = 0; g < groups.size(); ++g) {
            const Group& group = groups.at(g);
            const double tolerance = qMax(2.0, 0.45 * qMax(group.height, height));
            if (qAbs(group.baseline - low) <= tolerance) {
                target = g;
                break;
            }
        }
        if (target < 0) {
            Group group;
            group.baseline = low;
            group.height = height;
            groups.append(group);
            target = groups.size() - 1;
        }
        Group& group = groups[target];
        const double count = group.members.size();
        group.baseline = (group.baseline * count + low) / (count + 1.0);
        group.height = qMax(group.height, height);
        group.members.append(i);
    }

    QVector<DxfTextLine> lines;
    for (const Group& group : groups) {
        QVector<int> members = group.members;
        std::sort(members.begin(), members.end(), [&text](int a, int b) {
            return text.chars.at(a).box.left() < text.chars.at(b).box.left();
        });

        double baseline = 0.0;
        double top = 0.0;
        double left = std::numeric_limits<double>::max();
        double right = -std::numeric_limits<double>::max();
        bool upper = false;
        bool cjk = false;
        for (const int index : members) {
            const TextChar& character = text.chars.at(index);
            const QRectF box = character.box;
            baseline = qMax(baseline, qMin(box.top(), box.bottom()));
            top = qMax(top, qMax(box.top(), box.bottom()));
            left = qMin(left, box.left());
            right = qMax(right, box.right());
            upper = upper || isUpperOrDigit(character.text);
            cjk = cjk || isCjk(character.text.at(0).unicode());
        }

        const double height = qMax(top - baseline, 0.0);
        double ratio = 0.52;
        if (cjk) ratio = 0.88;
        else if (upper) ratio = 0.72;
        const double estimatedSize = height > 0.5 ? height / ratio : 0.0;

        // 真实字号：这一行里出现最多的那个（按字符匹配来的）。
        double realSize = 0.0;
        {
            QHash<double, int> counts;
            for (const int index : members) {
                if (index < 0 || index >= realSizes.size()) continue;
                const double size = realSizes.at(index);
                if (size <= 0.0) continue;
                counts[qRound(size * 100.0) / 100.0] += 1;
            }
            int bestCount = 0;
            for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
                if (it.value() > bestCount) {
                    bestCount = it.value();
                    realSize = it.key();
                }
            }
        }

        QString content;
        for (const int index : members) {
            content += text.chars.at(index).text;
        }

        DxfTextLine line;
        line.text = content.trimmed();
        line.left = left;
        line.right = right;
        line.baseline = baseline;
        line.top = top;
        line.fontSize = realSize > 0.0 ? realSize : (estimatedSize > 0.0 ? estimatedSize : 10.0);
        if (!line.text.isEmpty()) lines.append(line);
    }

    std::sort(lines.begin(), lines.end(), [](const DxfTextLine& a, const DxfTextLine& b) {
        if (qAbs(a.top - b.top) > 0.01) return a.top > b.top;
        return a.left < b.left;
    });
    return lines;
}

// 像不像一个测量值（尺寸标注上的数字）。
//
// 为什么要这个判断：内容流里的箭头 + 线也可能只是普通装饰线，
// 只有配上数字才像是一条尺寸标注。
bool looksLikeMeasurement(const QString& text) {
    if (text.isEmpty()) return false;
    int digits = 0;
    int total = 0;
    for (const QChar c : text) {
        if (c.isDigit()) {
            ++digits;
            ++total;
        } else if (c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('/')) {
            ++total;
        } else if (c.isSpace()) {
            continue;
        } else {
            ++total;
        }
    }
    return digits > 0 && total > 0 && static_cast<double>(digits) / total >= 0.5;
}

// ============================================================== 实体 ========

enum class DxfEntityKind { Line, Polyline, Spline, Text };

struct DxfEntity {
    DxfEntityKind kind = DxfEntityKind::Line;
    QString layer;
    QVector<QPointF> points;        // Polyline 顶点
    QVector<QPointF> controlPoints;  // Spline 控制点（三次贝塞尔 4 个）
    bool closed = false;
    QPointF start;
    QPointF end;
    QString text;
    QPointF insertion;
    double height = 0.0;
    double rotationDegrees = 0.0;
    bool hasColor = false;
    int colorIndex = 7;
    bool hasTrueColor = false;
    int red = 0;
    int green = 0;
    int blue = 0;
    double lineWidth = 0.0;
};

// 一个页面上的一个子路径（带它所属路径的填充/描边属性）。
struct DxfShape {
    QVector<GraphicsSegment> segments;
    bool closed = false;
    bool filled = false;
    bool stroked = false;
    GraphicsRgb color;
    double lineWidth = 0.0;
    // 被判定为尺寸标注的一部分（尺寸线或箭头）。
    bool isDimension = false;
};

// 把一条路径拆成若干子路径。
//
// 为什么要拆：一次绘制操作（一次 S 或一次 f）可能包含好几条子路径，
// DXF 里每条子路径都得是独立的实体。
QVector<DxfShape> splitSubpaths(const GraphicsPath& path) {
    QVector<DxfShape> shapes;
    DxfShape current;
    for (const GraphicsSegment& segment : path.segments) {
        switch (segment.verb) {
        case GraphicsVerb::Move:
            if (!current.segments.isEmpty()) shapes.append(current);
            current = DxfShape();
            current.segments.append(segment);
            break;
        case GraphicsVerb::Line:
        case GraphicsVerb::Curve:
            current.segments.append(segment);
            break;
        case GraphicsVerb::Close:
            current.closed = true;
            current.segments.append(segment);
            break;
        }
    }
    if (!current.segments.isEmpty()) shapes.append(current);

    for (DxfShape& shape : shapes) {
        shape.filled = path.filled;
        shape.stroked = path.stroked;
        shape.lineWidth = path.lineWidth;
        shape.color = path.stroked ? path.strokeColor : path.fillColor;
    }
    return shapes;
}

// 一个子路径上的顶点（Move 的落点 + 每条 Line 的落点）。
QVector<QPointF> shapeVertices(const DxfShape& shape) {
    QVector<QPointF> vertices;
    for (const GraphicsSegment& segment : shape.segments) {
        if (segment.verb == GraphicsVerb::Move || segment.verb == GraphicsVerb::Line) {
            vertices.append(segment.p1);
        }
    }
    return vertices;
}

int lineSegmentCount(const DxfShape& shape) {
    int count = 0;
    for (const GraphicsSegment& segment : shape.segments) {
        if (segment.verb == GraphicsVerb::Line) ++count;
    }
    return count;
}

// 是不是「箭头」：闭合、填充、恰好三个顶点。
//
// 为什么按**顶点**数而不是边数：PDF 的三角形是「两条直线 + 一次闭合」，
// 闭合的那条边是 h 算子，不是 l 算子，按边数数会少一条。
bool isArrowHead(const DxfShape& shape) {
    return shape.closed && shape.filled && shapeVertices(shape).size() == 3;
}

// 是不是「直线」：一条线段、没闭合。
bool isPlainLine(const DxfShape& shape) {
    return !shape.closed && lineSegmentCount(shape) == 1;
}

// ========================================================== DXF 写出器 ======

// 写一份 AutoCAD R2000（AC1015）ASCII 的 DXF。
//
// 为什么升到 R2000：SPLINE（真曲线）、LWPOLYLINE、真彩色（420）与线宽（370）
// 都是 R13 之后的特性，挂在 AC1009（R12）的文件里，AutoCAD 打开时会报
// 「无效的实体类型」。R2000 是 CAD 侧最通用的交换版本。
class DxfWriter {
public:
    explicit DxfWriter(double scale) : scale_(scale > 0.0 ? scale : 1.0) {}

    void addEntity(const DxfEntity& entity) { entities_.append(entity); }

    void ensureLayer(const QString& name, int colorIndex) {
        for (const CadDxfLayer& layer : layers_) {
            if (layer.name == name) return;
        }
        CadDxfLayer layer;
        layer.name = name;
        layer.colorIndex = colorIndex;
        layers_.append(layer);
    }

    QVector<CadDxfLayer> layers() const { return layers_; }

    QByteArray finish(const QSizeF& extents) {
        QByteArray out;
        writeHeader(out, extents);
        writeTables(out);
        writeBlocks(out);
        writeEntities(out);
        writeObjects(out);
        out += "  0\nEOF\n";
        return out;
    }

private:
    QString nextHandle() {
        return QString::number(handleCounter_++, 16).toUpper();
    }

    void group(QByteArray& out, int code, const QString& value) {
        out += QStringLiteral("%1\n%2\n").arg(code, 3).arg(value).toUtf8();
    }

    void group(QByteArray& out, int code, double value) {
        group(out, code, fixedNumber(value, 6));
    }

    void point(QByteArray& out, int codeX, int codeY, const QPointF& value) {
        group(out, codeX, value.x() * scale_);
        group(out, codeY, value.y() * scale_);
    }

    void writeHeader(QByteArray& out, const QSizeF& extents) {
        group(out, 0, QStringLiteral("SECTION"));
        group(out, 2, QStringLiteral("HEADER"));
        group(out, 9, QStringLiteral("$ACADVER"));
        group(out, 1, QStringLiteral("AC1015"));
        group(out, 9, QStringLiteral("$HANDSEED"));
        group(out, 5, QString::number(handleCounter_, 16).toUpper());
        group(out, 9, QStringLiteral("$INSUNITS"));
        // 0 = 未指定：DXF 的单位枚举里没有「点」，写别的会让人误读。
        group(out, 70, QStringLiteral("0"));
        group(out, 9, QStringLiteral("$EXTMIN"));
        group(out, 10, 0.0);
        group(out, 20, 0.0);
        group(out, 30, 0.0);
        group(out, 9, QStringLiteral("$EXTMAX"));
        group(out, 10, extents.width());
        group(out, 20, extents.height());
        group(out, 30, 0.0);
        group(out, 0, QStringLiteral("ENDSEC"));
    }

    void writeTableRecord(QByteArray& out, const QString& type, const QString& recordType,
                          const QString& name, int flags) {
        group(out, 0, type);
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTableRecord"));
        group(out, 100, recordType);
        group(out, 2, name);
        group(out, 70, QString::number(flags));
    }

    void writeTables(QByteArray& out) {
        group(out, 0, QStringLiteral("SECTION"));
        group(out, 2, QStringLiteral("TABLES"));

        // VPORT
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("VPORT"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QStringLiteral("1"));
        group(out, 0, QStringLiteral("VPORT"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTableRecord"));
        group(out, 100, QStringLiteral("AcDbViewportTableRecord"));
        group(out, 2, QStringLiteral("*ACTIVE"));
        group(out, 70, QStringLiteral("0"));
        group(out, 0, QStringLiteral("ENDTAB"));

        // LTYPE：CONTINUOUS + ByLayer + ByBlock
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("LTYPE"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QStringLiteral("3"));
        for (const QString& name : {QStringLiteral("ByBlock"), QStringLiteral("ByLayer"),
                                    QStringLiteral("CONTINUOUS")}) {
            group(out, 0, QStringLiteral("LTYPE"));
            group(out, 5, nextHandle());
            group(out, 100, QStringLiteral("AcDbSymbolTableRecord"));
            group(out, 100, QStringLiteral("AcDbLinetypeTableRecord"));
            group(out, 2, name);
            group(out, 70, QStringLiteral("0"));
            group(out, 3, QStringLiteral("CONTINUOUS"));
            group(out, 72, QStringLiteral("65"));
            group(out, 73, QStringLiteral("0"));
            group(out, 40, QStringLiteral("0.0"));
        }
        group(out, 0, QStringLiteral("ENDTAB"));

        // LAYER：实体真正落到的图层。
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("LAYER"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QString::number(static_cast<int>(layers_.size())));
        {
            CadDxfLayer zero;
            zero.name = QStringLiteral("0");
            zero.colorIndex = 7;
            writeLayerRecord(out, zero);
        }
        for (const CadDxfLayer& layer : layers_) writeLayerRecord(out, layer);
        group(out, 0, QStringLiteral("ENDTAB"));

        // STYLE：文字实体要用它。
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("STYLE"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QStringLiteral("1"));
        writeTableRecord(out, QStringLiteral("STYLE"),
                         QStringLiteral("AcDbTextStyleTableRecord"), QStringLiteral("Standard"), 0);
        group(out, 0, QStringLiteral("ENDTAB"));

        // VIEW / UCS：空表也要在（AutoCAD 期望这几个表存在）。
        for (const QString& tableName : {QStringLiteral("VIEW"), QStringLiteral("UCS")}) {
            group(out, 0, QStringLiteral("TABLE"));
            group(out, 2, tableName);
            group(out, 5, nextHandle());
            group(out, 100, QStringLiteral("AcDbSymbolTable"));
            group(out, 70, QStringLiteral("0"));
            group(out, 0, QStringLiteral("ENDTAB"));
        }

        // APPID
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("APPID"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QStringLiteral("1"));
        writeTableRecord(out, QStringLiteral("APPID"),
                         QStringLiteral("AcDbRegAppTableRecord"), QStringLiteral("ACAD"), 0);
        group(out, 0, QStringLiteral("ENDTAB"));

        // DIMSTYLE
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("DIMSTYLE"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QStringLiteral("1"));
        writeTableRecord(out, QStringLiteral("DIMSTYLE"),
                         QStringLiteral("AcDbDimStyleTableRecord"), QStringLiteral("Standard"), 0);
        group(out, 0, QStringLiteral("ENDTAB"));

        // BLOCK_RECORD：*Model_Space / *Paper_Space 必须有，
        // 否则 CAD 打开时会认为文件缺了模型空间而报修复。
        group(out, 0, QStringLiteral("TABLE"));
        group(out, 2, QStringLiteral("BLOCK_RECORD"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTable"));
        group(out, 70, QStringLiteral("2"));
        writeTableRecord(out, QStringLiteral("BLOCK_RECORD"),
                         QStringLiteral("AcDbBlockTableRecord"), QStringLiteral("*Model_Space"), 0);
        writeTableRecord(out, QStringLiteral("BLOCK_RECORD"),
                         QStringLiteral("AcDbBlockTableRecord"), QStringLiteral("*Paper_Space"), 0);
        group(out, 0, QStringLiteral("ENDTAB"));

        group(out, 0, QStringLiteral("ENDSEC"));
    }

    void writeLayerRecord(QByteArray& out, const CadDxfLayer& layer) {
        group(out, 0, QStringLiteral("LAYER"));
        group(out, 5, nextHandle());
        group(out, 100, QStringLiteral("AcDbSymbolTableRecord"));
        group(out, 100, QStringLiteral("AcDbLayerTableRecord"));
        group(out, 2, layer.name);
        group(out, 70, QStringLiteral("0"));
        group(out, 62, QString::number(layer.colorIndex));
        if (layer.hasTrueColor) {
            group(out, 420, QString::number((layer.red << 16) | (layer.green << 8) | layer.blue));
        }
        group(out, 6, QStringLiteral("CONTINUOUS"));
    }

    void writeBlocks(QByteArray& out) {
        group(out, 0, QStringLiteral("SECTION"));
        group(out, 2, QStringLiteral("BLOCKS"));
        for (const QString& name : {QStringLiteral("*Model_Space"), QStringLiteral("*Paper_Space")}) {
            group(out, 0, QStringLiteral("BLOCK"));
            group(out, 5, nextHandle());
            group(out, 100, QStringLiteral("AcDbEntity"));
            group(out, 8, QStringLiteral("0"));
            group(out, 100, QStringLiteral("AcDbBlockBegin"));
            group(out, 2, name);
            group(out, 70, QStringLiteral("0"));
            group(out, 10, QStringLiteral("0.0"));
            group(out, 20, QStringLiteral("0.0"));
            group(out, 30, QStringLiteral("0.0"));
            group(out, 3, name);
            group(out, 1, QString());
            group(out, 0, QStringLiteral("ENDBLK"));
            group(out, 5, nextHandle());
            group(out, 100, QStringLiteral("AcDbEntity"));
            group(out, 8, QStringLiteral("0"));
            group(out, 100, QStringLiteral("AcDbBlockEnd"));
        }
        group(out, 0, QStringLiteral("ENDSEC"));
    }

    void writeCommon(QByteArray& out, const DxfEntity& entity, const QString& subclass) {
        // R2000 的实体要带子类标记（100 组码）：读档器靠它决定实体的字段布局，
        // 少了会直接判成结构错误。
        group(out, 100, QStringLiteral("AcDbEntity"));
        group(out, 100, subclass);
        group(out, 8, entity.layer);
        if (entity.hasColor) {
            group(out, 62, QString::number(entity.colorIndex));
        }
        if (entity.hasTrueColor) {
            group(out, 420, QString::number((entity.red << 16) | (entity.green << 8) | entity.blue));
        }
        const int weight = lineWeightHundredthsMm(entity.lineWidth);
        if (weight > 0) group(out, 370, QString::number(weight));
    }

    void writeEntities(QByteArray& out) {
        group(out, 0, QStringLiteral("SECTION"));
        group(out, 2, QStringLiteral("ENTITIES"));
        for (const DxfEntity& entity : entities_) {
            switch (entity.kind) {
            case DxfEntityKind::Line: {
                group(out, 0, QStringLiteral("LINE"));
                group(out, 5, nextHandle());
                writeCommon(out, entity, QStringLiteral("AcDbLine"));
                point(out, 10, 20, entity.start);
                group(out, 30, QStringLiteral("0.0"));
                point(out, 11, 21, entity.end);
                group(out, 31, QStringLiteral("0.0"));
                break;
            }
            case DxfEntityKind::Polyline: {
                group(out, 0, QStringLiteral("LWPOLYLINE"));
                group(out, 5, nextHandle());
                writeCommon(out, entity, QStringLiteral("AcDbPolyline"));
                group(out, 90, QString::number(static_cast<int>(entity.points.size())));
                group(out, 70, entity.closed ? QStringLiteral("1") : QStringLiteral("0"));
                for (const QPointF& vertex : entity.points) {
                    point(out, 10, 20, vertex);
                }
                break;
            }
            case DxfEntityKind::Spline: {
                // 三次贝塞尔 → 一条三次样条：控制点就是 p0..p3，
                // 节点向量取夹紧的 0,0,0,0,1,1,1,1。
                group(out, 0, QStringLiteral("SPLINE"));
                group(out, 5, nextHandle());
                writeCommon(out, entity, QStringLiteral("AcDbSpline"));
                group(out, 70, QStringLiteral("8"));  // 8 = 平面曲线
                group(out, 71, QStringLiteral("3"));  // 次数
                group(out, 72, QStringLiteral("8"));  // 节点数
                group(out, 73, QString::number(static_cast<int>(entity.controlPoints.size())));
                group(out, 74, QStringLiteral("0"));  // 拟合点数
                group(out, 42, QStringLiteral("0.0"));
                group(out, 43, QStringLiteral("0.0"));
                group(out, 44, QStringLiteral("0.0"));
                for (int knot = 0; knot < 4; ++knot) group(out, 40, QStringLiteral("0.0"));
                for (int knot = 0; knot < 4; ++knot) group(out, 40, QStringLiteral("1.0"));
                for (const QPointF& control : entity.controlPoints) {
                    point(out, 10, 20, control);
                    group(out, 30, QStringLiteral("0.0"));
                }
                break;
            }
            case DxfEntityKind::Text: {
                group(out, 0, QStringLiteral("TEXT"));
                group(out, 5, nextHandle());
                writeCommon(out, entity, QStringLiteral("AcDbText"));
                group(out, 7, QStringLiteral("Standard"));
                point(out, 10, 20, entity.insertion);
                group(out, 30, QStringLiteral("0.0"));
                group(out, 40, fixedNumber(entity.height * scale_, 6));
                group(out, 1, dxfSafeText(entity.text));
                if (qAbs(entity.rotationDegrees) > 0.001) {
                    group(out, 50, fixedNumber(entity.rotationDegrees, 4));
                }
                break;
            }
            }
        }
        group(out, 0, QStringLiteral("ENDSEC"));
    }

    void writeObjects(QByteArray& out) {
        // 根字典：CAD 侧要靠它找命名对象字典，缺了会提示修复。
        group(out, 0, QStringLiteral("SECTION"));
        group(out, 2, QStringLiteral("OBJECTS"));
        group(out, 0, QStringLiteral("DICTIONARY"));
        group(out, 5, nextHandle());
        group(out, 330, QStringLiteral("0"));
        group(out, 100, QStringLiteral("AcDbDictionary"));
        group(out, 281, QStringLiteral("1"));
        group(out, 0, QStringLiteral("ENDSEC"));
    }

    double scale_ = 1.0;
    quint32 handleCounter_ = 0x100;
    QVector<DxfEntity> entities_;
    QVector<CadDxfLayer> layers_;
};

}  // namespace

// ============================================================== 导出 ========

CadDxfResult exportPdfToCadDxf(const QByteArray& pdfBytes, const CadDxfOptions& options) {
    CadDxfResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("输入数据为空");
        return result;
    }
    const PdfOpenResult opened = openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        result.error = QStringLiteral("输入数据不是有效的 PDF 文件");
        return result;
    }
    const int pageCount = opened.document.pageCount();
    if (pageCount <= 0) {
        result.error = QStringLiteral("PDF 里没有页面");
        return result;
    }

    DxfWriter writer(options.scale);
    writer.ensureLayer(QString::fromLatin1(kDxfLayerPath), 7);
    writer.ensureLayer(QString::fromLatin1(kDxfLayerText), 3);
    writer.ensureLayer(QString::fromLatin1(kDxfLayerDim), 1);

    double stackOffset = 0.0;
    double maxWidth = 0.0;
    double maxHeight = 0.0;

    for (int index = 0; index < pageCount; ++index) {
        const std::optional<PdfPage> page = opened.document.page(index);
        if (!page.has_value()) continue;

        MediaBox mediaBox = page->contentBox();
        if (mediaBox.width() <= 0.0 || mediaBox.height() <= 0.0) {
            mediaBox = MediaBox{0.0, 0.0, page->widthPoints(), page->heightPoints()};
        }
        const int rotation = page->rotationDegrees();
        const QSizeF shown = displaySize(QSizeF(mediaBox.width(), mediaBox.height()), rotation);

        // 图形底座：只有 qpdf 能读全内容流，曲线控制点、颜色与线宽都在那里。
        const PageGraphics graphics = extractPageGraphics(pdfBytes, index);
        if (!graphics.ok) {
            result.error =
                graphics.error.isEmpty() ? QStringLiteral("无法提取页面图形") : graphics.error;
            return result;
        }
        result.imageCount += static_cast<int>(graphics.images.size());

        // 一页上的所有子路径（带颜色与线宽）。
        QVector<DxfShape> shapes;
        for (const GraphicsPath& path : graphics.paths) {
            shapes += splitSubpaths(path);
        }

        // 尺寸标注识别：箭头贴着直线端点 → 这条线是尺寸线。
        QVector<int> dimensionLines;
        if (options.includeDimensions) {
            for (int i = 0; i < shapes.size(); ++i) {
                if (!isPlainLine(shapes.at(i))) continue;
                const QVector<QPointF> lineVertices = shapeVertices(shapes.at(i));
                if (lineVertices.size() != 2) continue;
                bool matched = false;
                for (int j = 0; j < shapes.size() && !matched; ++j) {
                    if (i == j || !isArrowHead(shapes.at(j))) continue;
                    for (const QPointF& tip : shapeVertices(shapes.at(j))) {
                        for (const QPointF& end : lineVertices) {
                            if (QLineF(tip, end).length() <= 2.0) {
                                matched = true;
                                break;
                            }
                        }
                        if (matched) break;
                    }
                }
                if (matched) {
                    dimensionLines.append(i);
                    shapes[i].isDimension = true;
                }
            }
            // 贴着尺寸线的箭头也算标注的一部分。
            for (int j = 0; j < shapes.size(); ++j) {
                if (!isArrowHead(shapes.at(j))) continue;
                for (const int lineIndex : dimensionLines) {
                    const QVector<QPointF> lineVertices = shapeVertices(shapes.at(lineIndex));
                    for (const QPointF& tip : shapeVertices(shapes.at(j))) {
                        bool touched = false;
                        for (const QPointF& end : lineVertices) {
                            if (QLineF(tip, end).length() <= 2.0) {
                                touched = true;
                                break;
                            }
                        }
                        if (touched) {
                            shapes[j].isDimension = true;
                            break;
                        }
                    }
                }
            }
        }

        // 文字层（可选）与尺寸标注的数值。
        std::optional<TextPage> textPage;
        QVector<double> realSizes;
        if (options.includeText || options.includeDimensions) {
            textPage = page->extractText();
            if (textPage.has_value()) {
                realSizes = extractCharFontSizes(pdfBytes, *textPage, index);
            }
        }
        QVector<DxfTextLine> textLines;
        if (textPage.has_value()) textLines = buildTextLines(*textPage, realSizes);

        // 把数值配给尺寸线：离尺寸线中点最近、且像测量值的那一行。
        for (const int lineIndex : dimensionLines) {
            const QVector<QPointF> vertices = shapeVertices(shapes.at(lineIndex));
            if (vertices.size() != 2) continue;
            const QPointF midpoint = (vertices.at(0) + vertices.at(1)) / 2.0;
            int bestIndex = -1;
            double bestDistance = 0.0;
            for (int t = 0; t < textLines.size(); ++t) {
                const DxfTextLine& candidate = textLines.at(t);
                if (candidate.consumedByDimension) continue;
                if (!looksLikeMeasurement(candidate.text)) continue;
                const QPointF center((candidate.left + candidate.right) / 2.0, candidate.baseline);
                const double distance = QLineF(midpoint, center).length();
                if (distance > 40.0) continue;
                if (bestIndex < 0 || distance < bestDistance) {
                    bestIndex = t;
                    bestDistance = distance;
                }
            }
            if (bestIndex >= 0) {
                textLines[bestIndex].consumedByDimension = true;
                const DxfTextLine& measurement = textLines.at(bestIndex);
                DxfEntity entity;
                entity.kind = DxfEntityKind::Text;
                entity.layer = QString::fromLatin1(kDxfLayerDim);
                entity.text = measurement.text;
                entity.insertion = mapToDisplay(QPointF(measurement.left, measurement.baseline), mediaBox,
                                                rotation, stackOffset);
                entity.height = measurement.fontSize;
                // 数值跟着尺寸线走：水平尺寸不转，竖直尺寸转 90°。
                const double angle =
                    std::atan2(vertices.at(1).y() - vertices.at(0).y(),
                               vertices.at(1).x() - vertices.at(0).x()) *
                    180.0 / kPi;
                double rotationDegrees = angle;
                while (rotationDegrees <= -90.0) rotationDegrees += 180.0;
                while (rotationDegrees > 90.0) rotationDegrees -= 180.0;
                entity.rotationDegrees = rotationDegrees;
                writer.addEntity(entity);
                ++result.textCount;
            }
        }

        // 路径实体：直线段 → LINE / LWPOLYLINE，曲线段 → SPLINE。
        for (const DxfShape& shape : shapes) {
            const QString layer =
                QString::fromLatin1(shape.isDimension ? kDxfLayerDim : kDxfLayerPath);
            QVector<QPointF> pending;  // 连续的直线段
            auto flushPending = [&](bool closed) {
                if (pending.size() < 2) {
                    pending.clear();
                    return;
                }
                DxfEntity entity;
                entity.layer = layer;
                entity.hasColor = true;
                entity.colorIndex =
                    nearestColorIndex(shape.color.r, shape.color.g, shape.color.b);
                entity.hasTrueColor = true;
                entity.red = channelToByte(shape.color.r);
                entity.green = channelToByte(shape.color.g);
                entity.blue = channelToByte(shape.color.b);
                entity.lineWidth = shape.lineWidth;
                if (pending.size() == 2) {
                    entity.kind = DxfEntityKind::Line;
                    entity.start = pending.at(0);
                    entity.end = pending.at(1);
                    writer.addEntity(entity);
                    ++result.lineCount;
                } else {
                    entity.kind = DxfEntityKind::Polyline;
                    entity.points = pending;
                    entity.closed = closed;
                    // 闭合路径的起点会被再写一遍（PDF 的 h 操作回到起点）：
                    // 留着就是多一个零长线段，CAD 里会显示成毛刺。
                    if (closed && entity.points.size() > 2) {
                        const QPointF first = entity.points.first();
                        const QPointF last = entity.points.last();
                        if (QLineF(first, last).length() < 1e-6) entity.points.removeLast();
                    }
                    writer.addEntity(entity);
                    ++result.polylineCount;
                }
                pending.clear();
            };

            for (const GraphicsSegment& segment : shape.segments) {
                switch (segment.verb) {
                case GraphicsVerb::Move:
                    flushPending(false);
                    pending.append(mapToDisplay(segment.p1, mediaBox, rotation, stackOffset));
                    break;
                case GraphicsVerb::Line:
                    if (pending.isEmpty()) {
                        pending.append(mapToDisplay(segment.p0, mediaBox, rotation, stackOffset));
                    }
                    pending.append(mapToDisplay(segment.p1, mediaBox, rotation, stackOffset));
                    break;
                case GraphicsVerb::Curve: {
                    flushPending(false);
                    DxfEntity entity;
                    entity.kind = DxfEntityKind::Spline;
                    entity.layer = layer;
                    entity.hasColor = true;
                    entity.colorIndex =
                        nearestColorIndex(shape.color.r, shape.color.g, shape.color.b);
                    entity.hasTrueColor = true;
                    entity.red = channelToByte(shape.color.r);
                    entity.green = channelToByte(shape.color.g);
                    entity.blue = channelToByte(shape.color.b);
                    entity.lineWidth = shape.lineWidth;
                    // 控制点逐个过一遍映射：映射是仿射的，贝塞尔曲线映射后
                    // 还是同一条三次贝塞尔（控制点也跟着转）。
                    entity.controlPoints = {
                        mapToDisplay(segment.p0, mediaBox, rotation, stackOffset),
                        mapToDisplay(segment.p1, mediaBox, rotation, stackOffset),
                        mapToDisplay(segment.p2, mediaBox, rotation, stackOffset),
                        mapToDisplay(segment.p3, mediaBox, rotation, stackOffset)};
                    writer.addEntity(entity);
                    ++result.splineCount;
                    pending.append(mapToDisplay(segment.p3, mediaBox, rotation, stackOffset));
                    break;
                }
                case GraphicsVerb::Close:
                    flushPending(true);
                    break;
                }
            }
            flushPending(shape.closed);
        }

        // 普通文字（可选）。
        if (options.includeText) {
            for (const DxfTextLine& line : textLines) {
                if (line.consumedByDimension) continue;
                if (line.text.trimmed().isEmpty()) continue;
                DxfEntity entity;
                entity.kind = DxfEntityKind::Text;
                entity.layer = QString::fromLatin1(kDxfLayerText);
                entity.text = line.text;
                entity.insertion =
                    mapToDisplay(QPointF(line.left, line.baseline), mediaBox, rotation, stackOffset);
                entity.height = line.fontSize;
                writer.addEntity(entity);
                ++result.textCount;
            }
        }

        // 尺寸标注计数：一条尺寸线算一个标注。
        result.dimensionCount += static_cast<int>(dimensionLines.size());

        // 多页竖着摞。
        stackOffset += shown.height();
        maxWidth = qMax(maxWidth, shown.width());
        maxHeight = qMax(maxHeight, stackOffset);
    }

    result.dxf = writer.finish(QSizeF(maxWidth, maxHeight));
    result.layers = writer.layers();
    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
