#include "pdf/ExportFormats.h"

#include "pdf/PdfiumAdapter.h"

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QImageWriter>
#include <QString>

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace aipdf::pdf {

namespace detail {

// 适配层那把**进程级** PDFium 锁（PdfiumAdapter.cpp 里也有同一份定义）。
//
// 为什么这里要重复写一遍，而不是只声明：inline 函数的实体要在每个用到它的
// 编译单元里都可见，只声明会链接不上（实测 undefined reference）。
//
// 为什么重复写不会变成「两把锁」：C++ 里 inline 函数的实体在程序中只有一份，
// 链接器会把两个编译单元的 COMDAT 副本折叠成一个，函数里的 static 局部变量
// （那把锁）也随之唯一——两边拿到的确实是同一把锁。
//
// 代价：两边必须保持逐字一致。改了适配层那一份，这里要跟着改，
// 否则会退化成两把锁，PDFium 的并发问题就又回来了（本仓库踩过随机堆损坏）。
inline std::recursive_mutex& pdfiumMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

}  // namespace detail

namespace {

// ============================================================ 小工具 =========

// 1 点 = 25.4/72 毫米。HTML 里页面尺寸用毫米写，看着才像一张纸。
constexpr double kMillimeterPerPoint = 25.4 / 72.0;

// PDFium 只需要初始化一次；重复调用是安全的（内部有 call_once）。
void ensurePdfiumInitialized() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        FPDF_LIBRARY_CONFIG config = {};
        config.version = 2;
        FPDF_InitLibraryWithConfig(&config);
    });
}

// 不带科学计数法的定点数：PDF 与 DXF 里写科学计数法会被一部分读档器嫌弃。
QString fixedNumber(double value, int decimals = 4) {
    return QString::number(value, 'f', decimals);
}

// ====================================================== 文本层 → 行 =========

// 一行文字：从 PDF 的字符框重建出来的。
struct TextLine {
    QString text;
    double left = 0.0;      // 行左边界（PDF 用户空间）
    double right = 0.0;
    double baseline = 0.0;  // 基线 y（PDF 用户空间，y 向上）
    double top = 0.0;       // 最高字形的上沿 y
    double fontSize = 0.0;  // 估算字号（点）
};

bool isCjkCodePoint(char32_t code) {
    return (code >= 0x2E80 && code <= 0x9FFF) || (code >= 0x3000 && code <= 0x303F) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0xFF00 && code <= 0xFF60) ||
           (code >= 0x20000 && code <= 0x2FA1F);
}

// 一段文字里有没有中日韩字符。
bool containsCjk(const QString& text) {
    for (const QChar c : text) {
        if (isCjkCodePoint(c.unicode())) return true;
    }
    return false;
}

// 有没有大写字母或数字（大写字母的高度 ≈ 0.72 em，据此反推行高）。
bool containsUpper(const QString& text) {
    for (const QChar c : text) {
        if (c.isUpper() || c.isDigit()) return true;
    }
    return false;
}

// 有没有上伸部字母（b d f h k l t…，高度同样 ≈ 0.72 em）。
bool containsAscender(const QString& text) {
    static const QString kAscenders = QStringLiteral("bdfhkltBDFHKLT");
    for (const QChar c : text) {
        if (kAscenders.contains(c)) return true;
    }
    return false;
}

// 把一页的字符框重建成若干行。
//
// 为什么不能直接用 TextPage::plainText()：那是按 PDFium 给的字符顺序拼的，
// 一页里的文字常常按内容流顺序而不是阅读顺序排列，而且词与词之间的空格
// 在很多 PDF 里是靠定位而不是空格字符实现的——直接拼会粘成一团。
QVector<TextLine> buildTextLines(const TextPage& text) {
    struct Group {
        QVector<int> members;
        double baseline = 0.0;  // 组内基线的平均值
        double height = 0.0;    // 组内最高的字
    };
    QVector<Group> groups;

    for (int i = 0; i < text.chars.size(); ++i) {
        const TextChar& character = text.chars.at(i);
        if (character.text.isEmpty()) continue;
        // PDFium 会在文字块之间插换行符；那是段落边界，不是页面上的内容。
        const QChar first = character.text.at(0);
        if (first == QLatin1Char('\n') || first == QLatin1Char('\r') || first == QLatin1Char('\t') ||
            first == QLatin1Char('\f')) {
            continue;
        }
        const QRectF box = character.box;
        const double low = qMin(box.top(), box.bottom());
        const double high = qMax(box.top(), box.bottom());
        const double height = qMax(high - low, 0.0);

        // 归行按**基线**（字符框的下沿）比，不按框的中心：空格字符的框高度是 0，
        // 用中心比它会飘到别的行里去。
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

    QVector<TextLine> lines;
    for (const Group& group : groups) {
        QVector<int> members = group.members;
        std::sort(members.begin(), members.end(), [&text](int a, int b) {
            const QRectF left = text.chars.at(a).box;
            const QRectF right = text.chars.at(b).box;
            if (left.left() != right.left()) return left.left() < right.left();
            return a < b;
        });

        // 先量出这一行的基线、字高与字号，再拼文字：词间距的阈值要用字号，
        // 用单个字符的高度当基准会把「u」「m」这种紧挨着的字母判成两个词。
        double baseline = -std::numeric_limits<double>::max();
        double top = -std::numeric_limits<double>::max();
        double left = std::numeric_limits<double>::max();
        double right = -std::numeric_limits<double>::max();
        bool hasUpper = false;
        bool hasAscender = false;
        bool hasCjk = false;
        for (const int index : members) {
            const TextChar& character = text.chars.at(index);
            const QRectF box = character.box;
            baseline = qMax(baseline, qMin(box.top(), box.bottom()));
            top = qMax(top, qMax(box.top(), box.bottom()));
            left = qMin(left, box.left());
            right = qMax(right, box.right());
            hasUpper = hasUpper || containsUpper(character.text);
            hasAscender = hasAscender || containsAscender(character.text);
            hasCjk = hasCjk || containsCjk(character.text);
        }

        // 行高反推行号：大写/上伸字母 ≈ 0.72 em，汉字 ≈ 0.88 em，
        // 其余（小写为主）≈ 0.52 em 的 x 高度。
        const double height = qMax(top - baseline, 0.0);
        double ratio = 0.52;
        if (hasCjk) ratio = 0.88;
        else if (hasUpper || hasAscender) ratio = 0.72;
        const double fontSize = height > 0.5 ? height / ratio : 0.0;

        QString content;
        for (int k = 0; k < members.size(); ++k) {
            const TextChar& character = text.chars.at(members.at(k));
            const QRectF box = character.box;
            if (k > 0) {
                const TextChar& previous = text.chars.at(members.at(k - 1));
                const double gap = box.left() - previous.box.right();
                const double reference = fontSize > 0.5 ? fontSize : qMax(height, 1.0);
                const bool hasSpace = previous.text.endsWith(QLatin1Char(' ')) ||
                                      character.text.startsWith(QLatin1Char(' '));
                const bool cjkPair = containsCjk(previous.text) && containsCjk(character.text);
                // 词之间没有空格字符时按几何间距补一个：很多 PDF 是靠定位分词的。
                if (!hasSpace && !cjkPair && gap > 0.2 * reference) {
                    content += QLatin1Char(' ');
                }
            }
            content += character.text;
        }

        TextLine line;
        line.text = content;
        line.left = left;
        line.right = right;
        line.baseline = baseline;
        line.top = top;
        line.fontSize = fontSize;
        lines.append(line);
    }

    // 阅读顺序：从上到下、从左到右（PDF 的 y 向上，所以 top 大的排前面）。
    std::sort(lines.begin(), lines.end(), [](const TextLine& a, const TextLine& b) {
        if (qAbs(a.top - b.top) > 0.01) return a.top > b.top;
        return a.left < b.left;
    });
    return lines;
}

// ============================================================ HTML =========

QString escapeHtml(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case '\'': out += QLatin1String("&#39;"); break;
        default: out += c; break;
        }
    }
    return out;
}

QString buildHtml(const QVector<HtmlPage>& pages, const HtmlExportOptions& options) {
    QString html;
    html += QLatin1String("<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n");
    html += QLatin1String("<meta charset=\"utf-8\">\n");
    // 注意：带中文的字符串必须用 QStringLiteral——QLatin1String 会把 UTF-8
    // 字节当成 Latin-1，导出的 HTML 里就会出现乱码。
    html += QStringLiteral("<title>PDF 导出</title>\n");
    if (options.includeStyles) {
        html += QLatin1String(
            "<style>\n"
            "body { margin: 0; padding: 16px; background: #eef0f2; }\n"
            ".page { position: relative; margin: 0 auto 16px; background: #fff; overflow: hidden;\n"
            "        box-shadow: 0 1px 4px rgba(0,0,0,.25); }\n"
            // white-space: pre-wrap 是关键：HTML 默认会把连续空格折叠掉，
            // 词之间的空格一折叠就粘成一块了。
            ".page .line { position: absolute; white-space: pre-wrap; line-height: 1; }\n"
            "</style>\n");
    }
    html += QLatin1String("</head>\n<body>\n");

    for (const HtmlPage& page : pages) {
        const double width = page.sizePt.width() > 0 ? page.sizePt.width() : 0.0;
        const double height = page.sizePt.height() > 0 ? page.sizePt.height() : 0.0;
        html += QStringLiteral("<div class=\"page\" style=\"width:%1mm;height:%2mm;\">\n")
                    .arg(fixedNumber(width * kMillimeterPerPoint, 3),
                         fixedNumber(height * kMillimeterPerPoint, 3));
        const QVector<TextLine> lines = buildTextLines(page.text);
        for (const TextLine& line : lines) {
            // PDF 的 y 向上、HTML 的 y 向下：行的位置要翻到页面顶端算。
            const double top = height - line.top;
            const double fontSize = line.fontSize > 0.5 ? line.fontSize : options.fontSizePt;
            html += QStringLiteral(
                        "<span class=\"line\" style=\"left:%1pt;top:%2pt;font-size:%3pt;\">%4</span>\n")
                        .arg(fixedNumber(line.left, 2), fixedNumber(top, 2), fixedNumber(fontSize, 2),
                             escapeHtml(line.text));
        }
        html += QLatin1String("</div>\n");
    }

    html += QLatin1String("</body>\n</html>\n");
    return html;
}

// ==================================================== 手写一个最小 PDF ======

// 只够写「每页一张图」这种简单文件：不做增量、不做对象流、不加密。
class PdfWriter {
public:
    // 分配一个对象号（从 1 开始），返回它。
    int allocate() {
        objects_.append(QByteArray());
        return objects_.size();
    }

    void set(int id, const QByteArray& body) {
        objects_[id - 1] = body;
    }

    // 流对象：dictEntries 里写好除 /Length 以外的所有键。
    void setStream(int id, const QByteArray& dictEntries, const QByteArray& data) {
        QByteArray body = "<< ";
        body += dictEntries;
        if (!dictEntries.isEmpty() && !dictEntries.endsWith(' ')) body += ' ';
        body += "/Length " + QByteArray::number(data.size()) + " >>\nstream\n";
        body += data;
        body += "\nendstream";
        objects_[id - 1] = body;
    }

    QByteArray finish(int rootId) const {
        QByteArray out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        QVector<int> offsets;
        offsets.reserve(objects_.size());
        for (int i = 0; i < objects_.size(); ++i) {
            offsets.append(out.size());
            out += QByteArray::number(i + 1) + " 0 obj\n" + objects_.at(i) + "\nendobj\n";
        }
        const int xref = out.size();
        out += "xref\n0 " + QByteArray::number(objects_.size() + 1) + "\n";
        out += "0000000000 65535 f \n";
        for (int offset : offsets) {
            out += QByteArray::number(offset, 10).rightJustified(10, '0') + " 00000 n \n";
        }
        out += "trailer\n<< /Size " + QByteArray::number(objects_.size() + 1) + " /Root " +
               QByteArray::number(rootId) + " 0 R >>\n";
        out += "startxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return out;
    }

private:
    QVector<QByteArray> objects_;
};

QByteArray encodeJpeg(const QImage& image, int quality) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return {};
    QImageWriter writer(&buffer, "JPEG");
    writer.setQuality(qBound(1, quality, 100));
    if (!writer.write(image)) return {};
    return bytes;
}

// =============================================== 不可见文字层（可搜索） ======

// 不可见文字用的 Type0 字体：Identity-H 编码 + 一份 ToUnicode CMap。
//
// 为什么不用标准 14 字体：WinAnsi 装不下中文，写进去的汉字会变成问号。
// 不可见文字（渲染模式 3）根本不画字形，所以字体只要「能把编码翻回 Unicode」
// 就行——ToUnicode 正是干这个的，也不用嵌字体文件。
class InvisibleTextFont {
public:
    // 返回该字符的 CID（两字节编码）。
    int codeFor(char32_t code) {
        const auto key = static_cast<quint32>(code);
        auto it = codes_.find(key);
        if (it != codes_.end()) return it.value();
        const int cid = next_++;
        codes_.insert(key, cid);
        return cid;
    }

    bool isEmpty() const { return codes_.isEmpty(); }

    // Type0 字体字典。
    QByteArray type0Dictionary(int descendantId, int toUnicodeId) const {
        return "<< /Type /Font /Subtype /Type0 /BaseFont /AipdfInvisible "
               "/Encoding /Identity-H /DescendantFonts [" +
               QByteArray::number(descendantId) + " 0 R] /ToUnicode " +
               QByteArray::number(toUnicodeId) + " 0 R >>";
    }

    QByteArray descendantDictionary(int descriptorId) const {
        return "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /AipdfInvisible "
               "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
               "/FontDescriptor " +
               QByteArray::number(descriptorId) + " 0 R /DW 1000 >>";
    }

    QByteArray descriptorDictionary() const {
        return "<< /Type /FontDescriptor /FontName /AipdfInvisible /Flags 32 "
               "/FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 900 /Descent -200 "
               "/CapHeight 700 /StemV 80 /MissingWidth 1000 >>";
    }

    // ToUnicode CMap：把 CID 翻回 Unicode。
    QByteArray toUnicodeCMap() const {
        QByteArray out =
            "/CIDInit /ProcSet findresource begin\n"
            "12 dict begin\n"
            "begincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /Adobe-Identity-UCS def\n"
            "/CMapType 2 def\n"
            "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        // 一个码点一条 bfchar；非 BMP 码点的目标值写两个 UTF-16 码元。
        // 以前是“每个代理项各占一个 CID 各写一条”，写出来是 <cid> <D83D>
        // 这种非法映射，复制出来就是乱码。
        QVector<QByteArray> entries;
        entries.reserve(codes_.size());
        for (auto it = codes_.constBegin(); it != codes_.constEnd(); ++it) {
            const quint32 code = it.key();
            QByteArray line = "<" + hexWord(static_cast<quint16>(it.value())) + "> <";
            if (code > 0xFFFF) {
                const quint32 value = code - 0x10000;
                line += hexWord(static_cast<quint16>(0xD800 + (value >> 10)));
                line += hexWord(static_cast<quint16>(0xDC00 + (value & 0x3FF)));
            } else {
                line += hexWord(static_cast<quint16>(code));
            }
            line += ">\n";
            entries.append(line);
        }
        // PDF 规范：一个 beginbfchar 段最多 100 条，超了部分阅读器会整段丢弃。
        for (int start = 0; start < entries.size(); start += 100) {
            const int count = qMin(100, entries.size() - start);
            out += QByteArray::number(count) + " beginbfchar\n";
            for (int i = 0; i < count; ++i) {
                out += entries.at(start + i);
            }
            out += "endbfchar\n";
        }
        out += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        return out;
    }

    // 字符串按 CID 编码成十六进制串（<0001 0002>）。
    QByteArray encodeString(const QString& text) {
        QByteArray out = "<";
        // 按真实码点迭代：QChar 永远是 16 位，`code > 0xFFFF` 那个分支
        // 在按 QChar 循环时永远不成立，代理对会被拆成两个字符分别编码。
        for (const uint code : text.toUcs4()) {
            out += hexWord(static_cast<quint16>(codeFor(static_cast<char32_t>(code))));
        }
        out += ">";
        return out;
    }

private:
    static QByteArray hexWord(quint16 value) {
        const quint32 word = value;
        QByteArray out;
        out += QByteArray::number(word, 16).rightJustified(4, '0').toUpper();
        return out;
    }

    QHash<quint32, int> codes_;
    int next_ = 1;
};

// 把 PDF 用户空间的点映射到「显示方向」的坐标。
//
// PDF 的 /Rotate 是**顺时针**转页面：转 90° 之后，原来的左下角跑到显示区的
// 左上角。页面盒子（裁剪盒）会正好铺满显示区，所以映射是仿射的。
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

// 文本矩阵：字形方向跟着页面旋转走，文字才不会被转歪。
void textMatrixForRotation(int rotation, double* a, double* b, double* c, double* d) {
    switch (rotation) {
    case 90: *a = 0; *b = -1; *c = 1; *d = 0; break;
    case 180: *a = -1; *b = 0; *c = 0; *d = -1; break;
    case 270: *a = 0; *b = 1; *c = -1; *d = 0; break;
    default: *a = 1; *b = 0; *c = 0; *d = 1; break;
    }
}

// ============================================================ DXF ==========

struct DxfEntity {
    enum class Kind { Line, Polyline, Text };
    Kind kind = Kind::Line;
    QVector<QPointF> points;  // 多段线的顶点
    bool closed = false;
    QPointF start;
    QPointF end;
    QString text;
    QPointF insertion;
    double height = 0.0;
};

// DXF 文本里不能有换行；控制字符一律换成空格。
QString dxfSafeText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QLatin1Char('\t')) {
            out += QLatin1Char(' ');
        } else if (c.unicode() < 0x20) {
            continue;
        } else {
            out += c;
        }
    }
    return out;
}

// 写一份 AutoCAD R12 ASCII 的 DXF。
//
// 结构是三段：HEADER（版本号与图形范围）、ENTITIES（实体）、EOF。
// 坐标原点在左下、y 向上，与 PDF 用户空间一致，**不需要翻转**。
QByteArray writeDxf(const QVector<DxfEntity>& entities, const QSizeF& extents, double scale) {
    QByteArray out;
    auto group = [&out](int code, const QString& value) {
        out += QStringLiteral("%1\n%2\n").arg(code, 3).arg(value).toUtf8();
    };
    auto point = [&](int codeX, int codeY, const QPointF& value) {
        group(codeX, fixedNumber(value.x() * scale, 6));
        group(codeY, fixedNumber(value.y() * scale, 6));
    };

    group(0, QStringLiteral("SECTION"));
    group(2, QStringLiteral("HEADER"));
    group(9, QStringLiteral("$ACADVER"));
    group(1, QStringLiteral("AC1009"));
    group(9, QStringLiteral("$EXTMIN"));
    group(10, QStringLiteral("0.0"));
    group(20, QStringLiteral("0.0"));
    group(30, QStringLiteral("0.0"));
    group(9, QStringLiteral("$EXTMAX"));
    group(10, fixedNumber(extents.width() * scale, 6));
    group(20, fixedNumber(extents.height() * scale, 6));
    group(30, QStringLiteral("0.0"));
    group(0, QStringLiteral("ENDSEC"));

    group(0, QStringLiteral("SECTION"));
    group(2, QStringLiteral("ENTITIES"));
    for (const DxfEntity& entity : entities) {
        switch (entity.kind) {
        case DxfEntity::Kind::Line: {
            group(0, QStringLiteral("LINE"));
            group(8, QStringLiteral("0"));
            point(10, 20, entity.start);
            group(30, QStringLiteral("0.0"));
            point(11, 21, entity.end);
            group(31, QStringLiteral("0.0"));
            break;
        }
        case DxfEntity::Kind::Polyline: {
            group(0, QStringLiteral("LWPOLYLINE"));
            group(8, QStringLiteral("0"));
            QVector<QPointF> points = entity.points;
            if (entity.closed && points.size() > 2) {
                const QPointF first = points.first();
                const QPointF last = points.last();
                if (qAbs(first.x() - last.x()) < 1e-6 && qAbs(first.y() - last.y()) < 1e-6) {
                    points.removeLast();
                }
            }
            // 顶点数要写在去掉重复点之后：PDFium 的闭合路径会把起点再写一遍。
            group(90, QString::number(points.size()));
            // 70 组：位 1 = 闭合。闭合的多边形要标上，否则 CAD 里是一段散线。
            group(70, entity.closed ? QStringLiteral("1") : QStringLiteral("0"));
            for (const QPointF& vertex : points) {
                point(10, 20, vertex);
            }
            break;
        }
        case DxfEntity::Kind::Text: {
            group(0, QStringLiteral("TEXT"));
            group(8, QStringLiteral("0"));
            point(10, 20, entity.insertion);
            group(30, QStringLiteral("0.0"));
            group(40, fixedNumber(entity.height * scale, 6));
            group(1, dxfSafeText(entity.text));
            break;
        }
        }
    }
    group(0, QStringLiteral("ENDSEC"));
    group(0, QStringLiteral("EOF"));
    return out;
}

// ============================================== PDFium：页面的矢量路径 =======

struct PathSubpath {
    QVector<QPointF> points;
    bool closed = false;
};

// 自己开一份 PDF 文档，只为了拿路径对象。
//
// 为什么不走 PdfiumAdapter：适配层对外只给页面对象的**包围盒**，没有路径
// 的几何；而 DXF 要的就是几何。PDFium 的路径 API 正好给顶点与闭合标志。
// 所有 PDFium 调用都串到适配层那把进程级锁上（见文件开头的声明）。
class PdfiumPathReader {
public:
    explicit PdfiumPathReader(const QByteArray& bytes) : data_(bytes) {
        ensurePdfiumInitialized();
        const std::lock_guard<std::recursive_mutex> lock(detail::pdfiumMutex());
        access_.m_FileLen = static_cast<unsigned long>(bytes.size());
        access_.m_GetBlock = &PdfiumPathReader::readBlock;
        access_.m_Param = this;
        document_ = FPDF_LoadCustomDocument(&access_, nullptr);
    }

    ~PdfiumPathReader() {
        const std::lock_guard<std::recursive_mutex> lock(detail::pdfiumMutex());
        if (document_ != nullptr) FPDF_CloseDocument(document_);
    }

    PdfiumPathReader(const PdfiumPathReader&) = delete;
    PdfiumPathReader& operator=(const PdfiumPathReader&) = delete;

    bool isValid() const { return document_ != nullptr; }

    // 某一页的路径（媒体空间坐标，与内容流里的坐标一致）。
    QVector<PathSubpath> pageSubpaths(int index) const {
        QVector<PathSubpath> result;
        const std::lock_guard<std::recursive_mutex> lock(detail::pdfiumMutex());
        if (document_ == nullptr) return result;
        FPDF_PAGE page = FPDF_LoadPage(document_, index);
        if (page == nullptr) return result;
        const int objectCount = FPDFPage_CountObjects(page);
        for (int i = 0; i < objectCount; ++i) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
            if (object == nullptr) continue;
            if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_PATH) continue;
            collectPath(object, &result);
        }
        FPDF_ClosePage(page);
        return result;
    }

private:
    static int readBlock(void* param, unsigned long position, unsigned char* buffer, unsigned long size) {
        auto* self = static_cast<PdfiumPathReader*>(param);
        if (self == nullptr || self->data_.isEmpty()) return 0;
        if (static_cast<qint64>(position) + static_cast<qint64>(size) > self->data_.size()) return 0;
        memcpy(buffer, self->data_.constData() + position, size);
        return 1;
    }

    // 把三次贝塞尔打散成折线：DXF R12 没有曲线实体。
    static QVector<QPointF> flattenCubic(const QPointF& p0, const QPointF& p1, const QPointF& p2,
                                         const QPointF& p3) {
        QVector<QPointF> points;
        constexpr int kSteps = 8;
        for (int step = 1; step <= kSteps; ++step) {
            const double t = static_cast<double>(step) / kSteps;
            const double u = 1.0 - t;
            const double x = u * u * u * p0.x() + 3 * u * u * t * p1.x() + 3 * u * t * t * p2.x() +
                             t * t * t * p3.x();
            const double y = u * u * u * p0.y() + 3 * u * u * t * p1.y() + 3 * u * t * t * p2.y() +
                             t * t * t * p3.y();
            points.append(QPointF(x, y));
        }
        return points;
    }

    static void collectPath(FPDF_PAGEOBJECT object, QVector<PathSubpath>* out) {
        const int segments = FPDFPath_CountSegments(object);
        if (segments <= 0) return;
        PathSubpath current;
        QPointF currentPoint;
        for (int s = 0; s < segments; ++s) {
            FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(object, s);
            if (segment == nullptr) continue;
            float x = 0.0F;
            float y = 0.0F;
            if (!FPDFPathSegment_GetPoint(segment, &x, &y)) continue;
            const QPointF point(x, y);
            const int type = FPDFPathSegment_GetType(segment);
            if (type == FPDF_SEGMENT_MOVETO) {
                if (current.points.size() >= 2) out->append(current);
                current = PathSubpath();
                current.points.append(point);
                currentPoint = point;
            } else if (type == FPDF_SEGMENT_LINETO) {
                if (current.points.isEmpty()) continue;
                current.points.append(point);
                // 闭合的那一段会回到起点并带上闭合标志。
                if (FPDFPathSegment_GetClose(segment)) current.closed = true;
                currentPoint = point;
            } else if (type == FPDF_SEGMENT_BEZIERTO) {
                FS_POINTF first{};
                FS_POINTF second{};
                if (FPDFPath_GetBezierControlPoints(object, s, &first, &second)) {
                    const QVector<QPointF> curve = flattenCubic(currentPoint,
                                                                 QPointF(first.x, first.y),
                                                                 QPointF(second.x, second.y), point);
                    for (const QPointF& item : curve) current.points.append(item);
                } else {
                    current.points.append(point);
                }
                currentPoint = point;
            }
        }
        if (current.points.size() >= 2) out->append(current);
    }

    QByteArray data_;
    FPDF_FILEACCESS access_{};
    FPDF_DOCUMENT document_ = nullptr;
};

}  // namespace

// ============================================================== HTML ========

HtmlExportResult exportToHtml(const QVector<HtmlPage>& pages, const HtmlExportOptions& options) {
    HtmlExportResult result;
    if (pages.isEmpty()) {
        result.error = QStringLiteral("没有可导出的页面");
        return result;
    }
    result.html = buildHtml(pages, options);
    result.ok = true;
    return result;
}

QString exportPageToHtml(const TextPage& text, const QSizeF& pageSizePt,
                          const HtmlExportOptions& options) {
    // 没有文字的页面不产出 HTML：导出一个空壳没有意义。
    if (text.isEmpty()) return {};
    HtmlPage page;
    page.text = text;
    page.sizePt = pageSizePt;
    const HtmlExportResult result = exportToHtml({page}, options);
    return result.ok ? result.html : QString();
}

// ====================================================== 图像化 PDF =========

QByteArray exportToImageBasedPdf(const QByteArray& pdfBytes, const RasterizeOptions& options,
                                  QString* error) {
    if (error != nullptr) error->clear();
    if (pdfBytes.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("输入数据为空");
        return {};
    }
    const PdfOpenResult opened = openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        if (error != nullptr) *error = QStringLiteral("输入数据不是有效的 PDF 文件");
        return {};
    }
    const int pageCount = opened.document.pageCount();
    if (pageCount <= 0) {
        if (error != nullptr) *error = QStringLiteral("PDF 里没有页面");
        return {};
    }

    const double dpi = options.dpi > 0.0 ? options.dpi : 150.0;
    const double zoom = dpi / 72.0;

    PdfWriter writer;
    const int catalogId = writer.allocate();
    const int pagesId = writer.allocate();

    InvisibleTextFont invisibleFont;
    int fontId = -1;
    int descendantId = -1;
    int descriptorId = -1;
    int toUnicodeId = -1;
    if (options.keepTextLayer) {
        fontId = writer.allocate();
        descendantId = writer.allocate();
        descriptorId = writer.allocate();
        toUnicodeId = writer.allocate();
    }

    QVector<int> pageIds;
    for (int index = 0; index < pageCount; ++index) {
        const std::optional<PdfPage> page = opened.document.page(index);
        if (!page.has_value()) continue;
        // zoom=1 是 72 DPI，所以缓冲区尺寸就是「页宽(点) × dpi / 72」。
        const QImage image = page->render(zoom);
        if (image.isNull()) {
            if (error != nullptr) {
                *error = QStringLiteral("第 %1 页渲染失败（分辨率过高或页面为空）").arg(index + 1);
            }
            return {};
        }
        const QByteArray jpeg = encodeJpeg(image.convertToFormat(QImage::Format_RGB888),
                                           options.jpegQuality);
        if (jpeg.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("第 %1 页转成 JPEG 失败").arg(index + 1);
            return {};
        }

        const double width = page->widthPoints();
        const double height = page->heightPoints();

        // 内容流：先把图画满整页，再补一层看不见的文字。
        QByteArray content = "q\n" + fixedNumber(width, 4).toLatin1() + " 0 0 " +
                             fixedNumber(height, 4).toLatin1() + " 0 0 cm\n/Im0 Do\nQ\n";
        if (options.keepTextLayer) {
            const std::optional<TextPage> text = page->extractText();
            if (text.has_value()) {
                const MediaBox box = page->contentBox();
                const int rotation = page->rotationDegrees();
                for (const TextLine& line : buildTextLines(*text)) {
                    if (line.text.trimmed().isEmpty()) continue;
                    const QPointF position = mapToDisplay(QPointF(line.left, line.baseline), box,
                                                          rotation, 0.0);
                    double a = 1.0;
                    double b = 0.0;
                    double c = 0.0;
                    double d = 1.0;
                    textMatrixForRotation(rotation, &a, &b, &c, &d);
                    const double fontSize = line.fontSize > 0.5 ? line.fontSize : 10.0;
                    // 渲染模式 3 = 不可见：字形不画，但文字仍能被搜索、复制。
                    content += "BT /F1 " + fixedNumber(fontSize, 2).toLatin1() +
                               " Tf 3 Tr " + fixedNumber(a, 4).toLatin1() + " " +
                               fixedNumber(b, 4).toLatin1() + " " + fixedNumber(c, 4).toLatin1() +
                               " " + fixedNumber(d, 4).toLatin1() + " " +
                               fixedNumber(position.x(), 4).toLatin1() + " " +
                               fixedNumber(position.y(), 4).toLatin1() + " Tm " +
                               invisibleFont.encodeString(line.text) + " Tj ET\n";
                }
            }
        }

        const int contentId = writer.allocate();
        writer.setStream(contentId, QByteArray(), content);
        const int imageId = writer.allocate();
        writer.setStream(imageId,
                         "/Type /XObject /Subtype /Image /Width " +
                             QByteArray::number(image.width()) + " /Height " +
                             QByteArray::number(image.height()) +
                             " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode",
                         jpeg);
        const int pageId = writer.allocate();
        QByteArray resources = "/XObject << /Im0 " + QByteArray::number(imageId) + " 0 R >>";
        if (fontId > 0) resources += " /Font << /F1 " + QByteArray::number(fontId) + " 0 R >>";
        writer.set(pageId, "<< /Type /Page /Parent " + QByteArray::number(pagesId) +
                               " 0 R /MediaBox [0 0 " + fixedNumber(width, 4).toLatin1() + " " +
                               fixedNumber(height, 4).toLatin1() + "] /Resources << " + resources +
                               " >> /Contents " + QByteArray::number(contentId) + " 0 R >>");
        pageIds.append(pageId);
    }

    if (pageIds.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("PDF 里没有可渲染的页面");
        return {};
    }

    if (fontId > 0) {
        writer.set(fontId, invisibleFont.type0Dictionary(descendantId, toUnicodeId));
        writer.set(descendantId, invisibleFont.descendantDictionary(descriptorId));
        writer.set(descriptorId, invisibleFont.descriptorDictionary());
        writer.setStream(toUnicodeId, QByteArray(), invisibleFont.toUnicodeCMap());
    }

    QByteArray kids;
    for (int i = 0; i < pageIds.size(); ++i) {
        if (i > 0) kids += " ";
        kids += QByteArray::number(pageIds.at(i)) + " 0 R";
    }
    writer.set(pagesId, "<< /Type /Pages /Kids [" + kids + "] /Count " +
                            QByteArray::number(pageIds.size()) + " >>");
    writer.set(catalogId, "<< /Type /Catalog /Pages " + QByteArray::number(pagesId) + " 0 R >>");
    return writer.finish(catalogId);
}

// ============================================================== DXF ========

QByteArray exportPageToDxf(const TextPage& text, const QSizeF& pageSizePt,
                            const DxfExportOptions& options) {
    QVector<DxfEntity> entities;
    if (options.includeText) {
        for (const TextLine& line : buildTextLines(text)) {
            if (line.text.trimmed().isEmpty()) continue;
            DxfEntity entity;
            entity.kind = DxfEntity::Kind::Text;
            entity.text = line.text;
            // DXF 与 PDF 一样是 y 向上、原点在左下，坐标直接搬过去。
            entity.insertion = QPointF(line.left, line.baseline);
            entity.height = line.fontSize > 0.5 ? line.fontSize : 10.0;
            entities.append(entity);
        }
    }
    return writeDxf(entities, pageSizePt, options.scale);
}

QByteArray exportPdfToDxf(const QByteArray& pdfBytes, const DxfExportOptions& options,
                           QString* error) {
    if (error != nullptr) error->clear();
    if (pdfBytes.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("输入数据为空");
        return {};
    }
    const PdfOpenResult opened = openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        if (error != nullptr) *error = QStringLiteral("输入数据不是有效的 PDF 文件");
        return {};
    }
    const int pageCount = opened.document.pageCount();
    if (pageCount <= 0) {
        if (error != nullptr) *error = QStringLiteral("PDF 里没有页面");
        return {};
    }

    PdfiumPathReader reader(pdfBytes);

    QVector<DxfEntity> entities;
    double stackOffset = 0.0;
    double maxWidth = 0.0;
    double maxHeight = 0.0;
    for (int index = 0; index < pageCount; ++index) {
        const std::optional<PdfPage> page = opened.document.page(index);
        if (!page.has_value()) continue;
        MediaBox box = page->contentBox();
        if (box.width() <= 0.0 || box.height() <= 0.0) {
            box = MediaBox{0.0, 0.0, page->widthPoints(), page->heightPoints()};
        }
        const int rotation = page->rotationDegrees();

        // 矢量路径：折线与多段线。
        const QVector<PathSubpath> subpaths = reader.pageSubpaths(index);
        for (const PathSubpath& subpath : subpaths) {
            QVector<QPointF> points;
            points.reserve(subpath.points.size());
            for (const QPointF& point : subpath.points) {
                points.append(mapToDisplay(point, box, rotation, stackOffset));
            }
            if (points.size() == 2) {
                DxfEntity entity;
                entity.kind = DxfEntity::Kind::Line;
                entity.start = points.at(0);
                entity.end = points.at(1);
                entities.append(entity);
            } else if (points.size() > 2) {
                DxfEntity entity;
                entity.kind = DxfEntity::Kind::Polyline;
                entity.points = points;
                entity.closed = subpath.closed;
                entities.append(entity);
            }
        }

        // 文字（可选）。
        if (options.includeText) {
            const std::optional<TextPage> text = page->extractText();
            if (text.has_value()) {
                for (const TextLine& line : buildTextLines(*text)) {
                    if (line.text.trimmed().isEmpty()) continue;
                    DxfEntity entity;
                    entity.kind = DxfEntity::Kind::Text;
                    entity.text = line.text;
                    entity.insertion = mapToDisplay(QPointF(line.left, line.baseline), box, rotation,
                                                    stackOffset);
                    entity.height = line.fontSize > 0.5 ? line.fontSize : 10.0;
                    entities.append(entity);
                }
            }
        }

        // 多页竖着摞：一页占一块高度，几页的图形才不会叠在一起。
        stackOffset += page->heightPoints();
        maxWidth = qMax(maxWidth, page->widthPoints());
        maxHeight = qMax(maxHeight, stackOffset);
    }

    return writeDxf(entities, QSizeF(maxWidth, maxHeight), options.scale);
}

}  // namespace aipdf::pdf
