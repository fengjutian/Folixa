// 页面图形提取：矢量路径 + 内嵌位图。
//
// 实现口径与同目录 PageGraphics.h 的约定一一对应：
//   - 坐标是 PDF 用户空间（左下原点、y 向上）；
//   - 路径点已经乘过内容流里的变换矩阵（cm），线宽按 CTM 的缩放因子换算；
//   - 图片从页面的 XObject 资源取，解成 PNG 或 JPEG 字节；
//   - 不认识的算子、畸形内容流、资源缺失一律跳过，不能让整页失败；
//   - 这一页确实没有图形不是失败：ok=true 且 paths/images 都为空。
//
// 为什么走 qpdf 而不是 PDFium：PDFium 的公开 API 只给页面对象的包围盒，
// 不给路径顶点，颜色与线宽也拿不全；内容流这一层只有 qpdf 能读全。
// 未启用 qpdf 的构建下，下面所有取数都走 #else 分支，如实报错，不冒充。

#include "pdf/content/PageGraphics.h"

#include "pdf/content/ContentStream.h"
// extractCharFontSizes 的声明在这里（导出层是它的调用方），实现放在本文件。
#include "pdf/convert/ExportFormats.h"

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QImageWriter>
#include <QString>

#include <cmath>
#include <limits>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <memory>
#include <string>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// ============================================================ 仿射矩阵 =========

// 一个仿射变换矩阵 [a b c d e f]：
// (x, y) → (a*x + c*y + e, b*x + d*y + f)。
struct Matrix {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};

// 矩阵连乘：先做 n，再做 m。PDF 的 cm 就是这个顺序（cm 的矩阵在前）。
Matrix multiply(const Matrix& m, const Matrix& n) {
    Matrix result;
    result.a = m.a * n.a + m.c * n.b;
    result.b = m.b * n.a + m.d * n.b;
    result.c = m.a * n.c + m.c * n.d;
    result.d = m.b * n.c + m.d * n.d;
    result.e = m.a * n.e + m.c * n.f + m.e;
    result.f = m.b * n.e + m.d * n.f + m.f;
    return result;
}

QPointF applyMatrix(const Matrix& m, const QPointF& point) {
    return QPointF(m.a * point.x() + m.c * point.y() + m.e,
                   m.b * point.x() + m.d * point.y() + m.f);
}

// 缩放因子：线宽、字号都要按它换算到用户空间。
double scaleFactor(const Matrix& m) {
    const double determinant = qAbs(m.a * m.d - m.b * m.c);
    return determinant > 0.0 ? std::sqrt(determinant) : 1.0;
}

// CMYK（PDF 的分量值都是 0..1）转 RGB。
GraphicsRgb rgbFromCmyk(double c, double m, double y, double k) {
    GraphicsRgb rgb;
    rgb.r = (1.0 - c) * (1.0 - k);
    rgb.g = (1.0 - m) * (1.0 - k);
    rgb.b = (1.0 - y) * (1.0 - k);
    return rgb;
}

#ifdef AIPDF_WITH_QPDF

// ============================================================ qpdf 小工具 ======

// 对象的字典：流对象本身**不是**字典（qpdf 里 isDictionary() 对流返回假），
// 要拿它的字典得走 getDict()。
//
// 为什么必须这么写：图像与 Form 都是流。用 getKey 直接读流上的键会拿到空值，
// 图就会「静静地消失」——不报错，只是导出的 HTML 里没有图。
QPDFObjectHandle dictionaryOf(const QPDFObjectHandle& object) {
    return object.isStream() ? object.getDict() : object;
}

// 取整数。类型不对就当没有这个键——畸形文件不该让整页失败。
int intValue(const QPDFObjectHandle& object) {
    if (!object.isNumber()) return 0;
    const long long value = object.getIntValue();
    if (value > 2147483647LL) return 2147483647;
    if (value < -2147483648LL) return -2147483648;
    return static_cast<int>(value);
}

double numberValue(const QPDFObjectHandle& object) {
    if (!object.isNumber()) return 0.0;
    return object.getNumericValue();
}

// 读一个流的内容。qpdf 的 getStreamData 不是 const 方法，所以按值收一份句柄。
QByteArray streamData(const QPDFObjectHandle& stream, bool specialized = false) {
    if (!stream.isStream()) return {};
    QPDFObjectHandle copy = stream;
    try {
        const std::shared_ptr<Buffer> data =
            copy.getStreamData(specialized ? qpdf_dl_specialized : qpdf_dl_generalized);
        if (!data) return {};
        return QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                          static_cast<qsizetype>(data->getSize()));
    } catch (const std::exception&) {
        // 解不开的流（损坏的 Flate、缺字典的 JPX 之类）当没有内容。
        return {};
    }
}

// 一页（或者一个 Form）的 XObject 表。
QHash<QByteArray, QPDFObjectHandle> collectXobjects(const QPDFObjectHandle& resources) {
    QHash<QByteArray, QPDFObjectHandle> result;
    if (!resources.isDictionary()) return result;
    const QPDFObjectHandle xobjects = resources.getKeyIfDict("/XObject");
    if (!xobjects.isDictionary()) return result;
    for (const std::string& key : xobjects.getKeys()) {
        result.insert(QByteArray::fromStdString(key), xobjects.getKey(key));
    }
    return result;
}

// ============================================================ 图像解码 =========

// 一个颜色空间的解释结果。
struct ColorSpaceInfo {
    enum class Kind { Gray, Rgb, Cmyk, Indexed, Unknown };
    Kind kind = Kind::Unknown;
    int components = 1;       // 每个像素的分量数（Indexed 恒为 1）
    int baseComponents = 3;   // Indexed 的基色空间分量数
    QByteArray palette;       // Indexed 的调色板字节
};

// Indexed 的 Lookup 可以是流，也可以是字符串数组。
QByteArray paletteBytes(const QPDFObjectHandle& lookup) {
    if (lookup.isStream()) return streamData(lookup, true);
    if (lookup.isArray()) {
        QByteArray bytes;
        for (const QPDFObjectHandle& item : lookup.getArrayAsVector()) {
            if (item.isString()) bytes += QByteArray::fromStdString(item.getStringValue());
        }
        return bytes;
    }
    return {};
}

ColorSpaceInfo parseColorSpace(const QPDFObjectHandle& space) {
    ColorSpaceInfo info;
    if (space.isName()) {
        const std::string name = space.getName();
        if (name == "/DeviceGray" || name == "/CalGray" || name == "/G") {
            info.kind = ColorSpaceInfo::Kind::Gray;
            info.components = 1;
        } else if (name == "/DeviceRGB" || name == "/CalRGB" || name == "/RGB") {
            info.kind = ColorSpaceInfo::Kind::Rgb;
            info.components = 3;
        } else if (name == "/DeviceCMYK" || name == "/CMYK") {
            info.kind = ColorSpaceInfo::Kind::Cmyk;
            info.components = 4;
        } else {
            info.kind = ColorSpaceInfo::Kind::Unknown;
            info.components = 1;
        }
        return info;
    }
    if (!space.isArray()) return info;
    const std::vector<QPDFObjectHandle> items = space.getArrayAsVector();
    if (items.empty()) return info;
    const std::string family = items.front().isName() ? items.front().getName() : std::string();
    if (family == "/ICCBased") {
        const QPDFObjectHandle profile = items.size() > 1 ? items[1] : QPDFObjectHandle::newNull();
        const int components = intValue(dictionaryOf(profile).getKey("/N"));
        info.components = components > 0 ? components : 3;
        if (info.components == 1) {
            info.kind = ColorSpaceInfo::Kind::Gray;
        } else if (info.components == 3) {
            info.kind = ColorSpaceInfo::Kind::Rgb;
        } else if (info.components == 4) {
            info.kind = ColorSpaceInfo::Kind::Cmyk;
        } else {
            info.kind = ColorSpaceInfo::Kind::Unknown;
        }
        return info;
    }
    if (family == "/Indexed" || family == "/I") {
        info.kind = ColorSpaceInfo::Kind::Indexed;
        info.components = 1;
        if (items.size() > 1) {
            const ColorSpaceInfo base = parseColorSpace(items[1]);
            info.baseComponents = base.components > 0 ? base.components : 3;
        }
        if (items.size() > 3) info.palette = paletteBytes(items[3]);
        return info;
    }
    if (family == "/CalRGB" || family == "/Lab") {
        info.kind = family == "/Lab" ? ColorSpaceInfo::Kind::Unknown : ColorSpaceInfo::Kind::Rgb;
        info.components = 3;
        return info;
    }
    if (family == "/CalGray" || family == "/Separation") {
        // Separation 是单分量，导成灰度是最接近的近似。
        info.kind = ColorSpaceInfo::Kind::Gray;
        info.components = 1;
        return info;
    }
    return info;
}

// 一份原始采样数据。PDF 的行是按字节对齐的，行内按位从高到低排。
struct RawSamples {
    QByteArray data;
    int width = 0;
    int components = 0;
    int bits = 8;
    int rowBytes = 0;

    bool isValid() {
        if (width <= 0 || components <= 0) return false;
        if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) return false;
        rowBytes = (width * components * bits + 7) / 8;
        return rowBytes > 0 && data.size() >= rowBytes;
    }

    // 某个像素的某个分量的原始值（未按 /Decode 变换）。
    int raw(int row, int index) const {
        const int offset = row * rowBytes * 8 + index * bits;
        if (bits == 8) {
            const int byteIndex = offset / 8;
            return byteIndex < data.size() ? static_cast<quint8>(data.at(byteIndex)) : 0;
        }
        if (bits == 16) {
            // 16 位取高字节：低 8 位对肉眼没有区别。
            const int byteIndex = offset / 8;
            return byteIndex < data.size() ? static_cast<quint8>(data.at(byteIndex)) : 0;
        }
        const int byteIndex = offset / 8;
        if (byteIndex >= data.size()) return 0;
        const int shift = 8 - bits - (offset % 8);
        const int mask = (1 << bits) - 1;
        return (static_cast<quint8>(data.at(byteIndex)) >> shift) & mask;
    }
};

// /Decode：每个分量一对 [最小 最大]，默认 [0 1]。
QVector<double> decodeArray(const QPDFObjectHandle& object, int components) {
    QVector<double> result;
    result.reserve(components);
    for (int i = 0; i < components; ++i) {
        result.append(0.0);
        result.append(1.0);
    }
    if (!object.isArray()) return result;
    const std::vector<QPDFObjectHandle> items = object.getArrayAsVector();
    for (int i = 0; i < components && static_cast<int>(items.size()) > 2 * i + 1; ++i) {
        result[2 * i] = numberValue(items[static_cast<size_t>(2 * i)]);
        result[2 * i + 1] = numberValue(items[static_cast<size_t>(2 * i + 1)]);
    }
    return result;
}

// 把一个采样值按 /Decode 变换到 0..1。
double applyDecode(int rawValue, int bits, double low, double high) {
    const double maximum = bits >= 16 ? 65535.0 : static_cast<double>((1 << bits) - 1);
    const double value = maximum > 0.0 ? rawValue / maximum : 0.0;
    return low + value * (high - low);
}

// 解出来的图像字节。
struct DecodedImage {
    QByteArray bytes;
    QString mime;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

// 把原始采样转成 RGBA 位图。
//
// 为什么自己解而不是交给 QImage：PDF 的图像数据是「分量 + 位深」的裸采样，
// 不是 PNG/JPEG 那种有文件头的格式，QImage 解不了。
bool buildImage(const RawSamples& samples, const ColorSpaceInfo& space,
                const QVector<double>& decode, QImage* out) {
    const int width = samples.width;
    const int height = samples.data.size() / qMax(1, samples.rowBytes);
    if (width <= 0 || height <= 0) return false;
    QImage image(width, height, QImage::Format_RGB888);
    const int bits = samples.bits;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const int base = column * space.components;
            double r = 0.0;
            double g = 0.0;
            double b = 0.0;
            if (space.kind == ColorSpaceInfo::Kind::Indexed) {
                const int index = samples.raw(row, base);
                const int offset = index * space.baseComponents;
                if (offset + space.baseComponents <= space.palette.size()) {
                    if (space.baseComponents == 1) {
                        const double value = static_cast<quint8>(space.palette.at(offset)) / 255.0;
                        r = g = b = value;
                    } else if (space.baseComponents >= 3) {
                        r = static_cast<quint8>(space.palette.at(offset)) / 255.0;
                        g = static_cast<quint8>(space.palette.at(offset + 1)) / 255.0;
                        b = static_cast<quint8>(space.palette.at(offset + 2)) / 255.0;
                    }
                }
            } else if (space.kind == ColorSpaceInfo::Kind::Rgb && space.components >= 3) {
                r = applyDecode(samples.raw(row, base), bits, decode[0], decode[1]);
                g = applyDecode(samples.raw(row, base + 1), bits, decode[2], decode[3]);
                b = applyDecode(samples.raw(row, base + 2), bits, decode[4], decode[5]);
            } else if (space.kind == ColorSpaceInfo::Kind::Cmyk && space.components >= 4) {
                const double c = applyDecode(samples.raw(row, base), bits, decode[0], decode[1]);
                const double m = applyDecode(samples.raw(row, base + 1), bits, decode[2], decode[3]);
                const double y = applyDecode(samples.raw(row, base + 2), bits, decode[4], decode[5]);
                const double k = applyDecode(samples.raw(row, base + 3), bits, decode[6], decode[7]);
                const GraphicsRgb rgb = rgbFromCmyk(c, m, y, k);
                r = rgb.r;
                g = rgb.g;
                b = rgb.b;
            } else {
                const double value = applyDecode(samples.raw(row, base), bits, decode[0], decode[1]);
                r = g = b = value;
            }
            const QRgb pixel = qRgb(qRound(qBound(0.0, r, 1.0) * 255.0),
                                    qRound(qBound(0.0, g, 1.0) * 255.0),
                                    qRound(qBound(0.0, b, 1.0) * 255.0));
            image.setPixelColor(column, row, QColor::fromRgb(pixel));
        }
    }
    *out = image;
    return true;
}

// 把 /SMask（一张灰度图）贴成 alpha 通道。
//
// 为什么值得做：带透明通道的 PNG 导成 HTML 后，透明区域会变成黑块，
// 视觉上与原页差得很远。
void applySoftMask(const QPDFObjectHandle& mask, QImage* image) {
    if (!mask.isStream()) return;
    const QPDFObjectHandle dictionary = dictionaryOf(mask);
    const int width = intValue(dictionary.getKey("/Width"));
    const int height = intValue(dictionary.getKey("/Height"));
    if (width != image->width() || height != image->height()) return;
    if (intValue(dictionary.getKey("/BitsPerComponent")) != 8) return;
    RawSamples samples;
    samples.data = streamData(mask, true);
    samples.width = width;
    samples.components = 1;
    samples.bits = 8;
    if (!samples.isValid()) return;

    QImage withAlpha = image->convertToFormat(QImage::Format_ARGB32);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const int alpha = samples.raw(row, column);
            const QRgb rgb = withAlpha.pixel(column, row);
            withAlpha.setPixel(column, row, qRgba(qRed(rgb), qGreen(rgb), qBlue(rgb), alpha));
        }
    }
    *image = withAlpha;
}

// 解一个图像 XObject。
bool decodeImageXObject(const QPDFObjectHandle& object, DecodedImage* out) {
    const QPDFObjectHandle dictionary = dictionaryOf(object);
    const int width = intValue(dictionary.getKey("/Width"));
    const int height = intValue(dictionary.getKey("/Height"));
    if (width <= 0 || height <= 0) return false;

    // 过滤器名字（可能是数组）。
    QStringList filters;
    const QPDFObjectHandle filter = dictionary.getKey("/Filter");
    if (filter.isName()) {
        filters.append(QString::fromStdString(filter.getName()));
    } else if (filter.isArray()) {
        for (const QPDFObjectHandle& item : filter.getArrayAsVector()) {
            if (item.isName()) filters.append(QString::fromStdString(item.getName()));
        }
    }

    // DCTDecode：流里的字节就是 JPEG 本身，不用解也不用重编码。
    if (filters.contains(QStringLiteral("/DCTDecode"))) {
        const QByteArray data = streamData(object, true);
        if (data.isEmpty()) return false;
        out->bytes = data;
        out->mime = QStringLiteral("image/jpeg");
        out->pixelWidth = width;
        out->pixelHeight = height;
        return true;
    }
    // 这些图像专用的过滤器没有解码器，跳过这一张（不能让整页失败）。
    for (const QString& name : filters) {
        if (name == QStringLiteral("/JPXDecode") || name == QStringLiteral("/JBIG2Decode") ||
            name == QStringLiteral("/CCITTFaxDecode")) {
            return false;
        }
    }

    const ColorSpaceInfo space = parseColorSpace(dictionary.getKey("/ColorSpace"));
    if (space.kind == ColorSpaceInfo::Kind::Unknown && space.components <= 0) return false;

    RawSamples samples;
    samples.data = streamData(object, true);
    samples.width = width;
    samples.components = space.components;
    samples.bits = intValue(dictionary.getKey("/BitsPerComponent"));
    if (samples.bits <= 0) samples.bits = 8;
    if (!samples.isValid()) return false;

    const QVector<double> decode = decodeArray(dictionary.getKey("/Decode"), space.components);
    QImage image;
    if (!buildImage(samples, space, decode, &image)) return false;

    const QPDFObjectHandle softMask = dictionary.getKey("/SMask");
    if (softMask.isStream()) applySoftMask(softMask, &image);

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly)) return false;
    QImageWriter writer(&buffer, "PNG");
    if (!writer.write(image)) return false;
    buffer.close();
    if (png.isEmpty()) return false;
    out->bytes = png;
    out->mime = QStringLiteral("image/png");
    out->pixelWidth = image.width();
    out->pixelHeight = image.height();
    return true;
}

// ============================================================ 内容流解释器 =====

// 一个操作数：数字或者名字。
struct Operand {
    bool isNumber = false;
    double number = 0.0;
    QByteArray name;
};

// 图形状态。q/Q 压栈的是它的一份拷贝。
struct GraphicsState {
    Matrix ctm;
    double lineWidth = 1.0;
    GraphicsRgb fill;
    GraphicsRgb stroke;
};

// 把一页（或一个 Form）的内容流走一遍，收集路径与图片。
class GraphicsExtractor {
public:
    explicit GraphicsExtractor(const QHash<QByteArray, QPDFObjectHandle>& xobjects)
        : xobjects_(xobjects) {}

    void interpret(const QByteArray& content);

    QVector<GraphicsPath> paths;
    QVector<GraphicsImage> images;

private:
    void applyOperator(const QByteArray& op, const QVector<Operand>& operands);

    // 路径构造：点在这里就乘上当时的 CTM（PDF 的路径点是在构造时变换的）。
    void moveTo(const QPointF& point);
    void lineTo(const QPointF& point);
    void curveTo(const QPointF& control1, const QPointF& control2, const QPointF& end);
    void closePath();
    void endPath();

    void paint(bool fill, bool stroke);
    void doXObject(const QByteArray& name);
    void interpretForm(const QPDFObjectHandle& form);

    GraphicsState state_;
    QVector<GraphicsState> stack_;
    QVector<GraphicsSegment> segments_;
    QPointF currentPoint_;
    QPointF subpathStart_;
    bool hasCurrentPoint_ = false;
    // cs/CS 定的颜色空间分量数：sc/scn 按它取多少个分量。
    int colorSpaceComponents_ = 1;
    QHash<QByteArray, QPDFObjectHandle> xobjects_;
};

// 取末尾的 count 个数字（不足就少给，不报错）。
QVector<double> trailingNumbers(const QVector<Operand>& operands, int count) {
    QVector<double> values;
    for (int i = 0; i < count; ++i) {
        const int index = operands.size() - 1 - i;
        if (index < 0) break;
        const Operand& operand = operands.at(index);
        if (!operand.isNumber) break;
        values.prepend(operand.number);
    }
    return values;
}

QByteArray trailingName(const QVector<Operand>& operands, int fromEnd) {
    const int index = operands.size() - 1 - fromEnd;
    if (index < 0 || index >= operands.size()) return {};
    return operands.at(index).name;
}

void GraphicsExtractor::moveTo(const QPointF& point) {
    GraphicsSegment segment;
    segment.verb = GraphicsVerb::Move;
    segment.p0 = currentPoint_;
    segment.p1 = applyMatrix(state_.ctm, point);
    segments_.append(segment);
    currentPoint_ = segment.p1;
    subpathStart_ = segment.p1;
    hasCurrentPoint_ = true;
}

void GraphicsExtractor::lineTo(const QPointF& point) {
    GraphicsSegment segment;
    segment.verb = GraphicsVerb::Line;
    segment.p0 = currentPoint_;
    segment.p1 = applyMatrix(state_.ctm, point);
    segments_.append(segment);
    currentPoint_ = segment.p1;
    hasCurrentPoint_ = true;
}

void GraphicsExtractor::curveTo(const QPointF& control1, const QPointF& control2, const QPointF& end) {
    GraphicsSegment segment;
    segment.verb = GraphicsVerb::Curve;
    segment.p0 = currentPoint_;
    segment.p1 = applyMatrix(state_.ctm, control1);
    segment.p2 = applyMatrix(state_.ctm, control2);
    segment.p3 = applyMatrix(state_.ctm, end);
    segments_.append(segment);
    currentPoint_ = segment.p3;
    hasCurrentPoint_ = true;
}

void GraphicsExtractor::closePath() {
    if (segments_.isEmpty()) return;
    GraphicsSegment segment;
    segment.verb = GraphicsVerb::Close;
    segment.p0 = currentPoint_;
    segment.p1 = subpathStart_;
    segments_.append(segment);
    // 闭合之后当前点回到子路径起点（PDF 规范如此）。
    currentPoint_ = subpathStart_;
    hasCurrentPoint_ = true;
}

void GraphicsExtractor::endPath() {
    segments_.clear();
    currentPoint_ = QPointF();
    subpathStart_ = QPointF();
    hasCurrentPoint_ = false;
}

void GraphicsExtractor::paint(bool fill, bool stroke) {
    if (!segments_.isEmpty() && (fill || stroke)) {
        GraphicsPath path;
        path.segments = segments_;
        path.filled = fill;
        path.stroked = stroke;
        path.fillColor = state_.fill;
        path.strokeColor = state_.stroke;
        path.lineWidth = state_.lineWidth * scaleFactor(state_.ctm);
        paths.append(path);
    }
    endPath();
}

void GraphicsExtractor::interpretForm(const QPDFObjectHandle& form) {
    // Form 的内容流按「q … Q」执行：状态进来了，出去时恢复原样。
    const GraphicsState savedState = state_;
    const QHash<QByteArray, QPDFObjectHandle> savedXobjects = xobjects_;

    Matrix formMatrix;
    const QPDFObjectHandle dictionary = dictionaryOf(form);
    const QPDFObjectHandle matrix = dictionary.getKey("/Matrix");
    if (matrix.isArray()) {
        const std::vector<QPDFObjectHandle> items = matrix.getArrayAsVector();
        if (items.size() == 6) {
            formMatrix.a = numberValue(items[0]);
            formMatrix.b = numberValue(items[1]);
            formMatrix.c = numberValue(items[2]);
            formMatrix.d = numberValue(items[3]);
            formMatrix.e = numberValue(items[4]);
            formMatrix.f = numberValue(items[5]);
        }
    }
    state_.ctm = multiply(formMatrix, state_.ctm);

    // Form 自带资源时用它自己的，没带就沿用外层的。
    const QPDFObjectHandle resources = dictionary.getKey("/Resources");
    if (resources.isDictionary()) xobjects_ = collectXobjects(resources);

    const QByteArray content = streamData(form);
    if (!content.isEmpty()) interpret(content);

    state_ = savedState;
    xobjects_ = savedXobjects;
}

void GraphicsExtractor::doXObject(const QByteArray& name) {
    if (name.isEmpty()) return;
    const auto it = xobjects_.constFind(name);
    // 资源里没有这个名字：跳过（内容流与资源不匹配的文件不少见）。
    if (it == xobjects_.constEnd()) return;
    const QPDFObjectHandle object = it.value();
    if (!object.isStream()) return;
    const QPDFObjectHandle subtype = dictionaryOf(object).getKey("/Subtype");
    if (subtype.isNameAndEquals("/Image")) {
        DecodedImage decoded;
        if (!decodeImageXObject(object, &decoded)) return;
        // 图片画在单位正方形里，摆位由 CTM 决定。
        const QPointF corners[4] = {applyMatrix(state_.ctm, QPointF(0.0, 0.0)),
                                    applyMatrix(state_.ctm, QPointF(1.0, 0.0)),
                                    applyMatrix(state_.ctm, QPointF(1.0, 1.0)),
                                    applyMatrix(state_.ctm, QPointF(0.0, 1.0))};
        double minX = corners[0].x();
        double maxX = corners[0].x();
        double minY = corners[0].y();
        double maxY = corners[0].y();
        for (const QPointF& corner : corners) {
            minX = qMin(minX, corner.x());
            maxX = qMax(maxX, corner.x());
            minY = qMin(minY, corner.y());
            maxY = qMax(maxY, corner.y());
        }
        GraphicsImage image;
        // 矩形的约定与文本层的字符框一致：top() 是纵坐标较小的那条边，
        // bottom() 是较大的一条边（PDF 用户空间是 y 向上）。
        image.rect = QRectF(minX, minY, maxX - minX, maxY - minY);
        image.data = decoded.bytes;
        image.mime = decoded.mime;
        image.pixelWidth = decoded.pixelWidth;
        image.pixelHeight = decoded.pixelHeight;
        images.append(image);
        return;
    }
    if (subtype.isNameAndEquals("/Form")) {
        interpretForm(object);
    }
}

void GraphicsExtractor::applyOperator(const QByteArray& op, const QVector<Operand>& operands) {
    // ---- 图形状态 ----
    if (op == "q") {
        stack_.append(state_);
        return;
    }
    if (op == "Q") {
        // 栈空时的 Q 是畸形内容流：忽略，不让整页失败。
        if (!stack_.isEmpty()) state_ = stack_.takeLast();
        return;
    }
    if (op == "cm") {
        const QVector<double> values = trailingNumbers(operands, 6);
        if (values.size() == 6) {
            Matrix matrix;
            matrix.a = values[0];
            matrix.b = values[1];
            matrix.c = values[2];
            matrix.d = values[3];
            matrix.e = values[4];
            matrix.f = values[5];
            state_.ctm = multiply(matrix, state_.ctm);
        }
        return;
    }
    if (op == "w") {
        const QVector<double> values = trailingNumbers(operands, 1);
        if (values.size() == 1) state_.lineWidth = values[0];
        return;
    }
    if (op == "gs") {
        // 外部图形状态（/LW、/CA 等）暂时不解析：跳过即可，不影响其它内容。
        return;
    }

    // ---- 颜色 ----
    if (op == "g" || op == "G") {
        const QVector<double> values = trailingNumbers(operands, 1);
        if (values.size() == 1) {
            const GraphicsRgb rgb{values[0], values[0], values[0]};
            if (op == "g") state_.fill = rgb; else state_.stroke = rgb;
        }
        return;
    }
    if (op == "rg" || op == "RG") {
        const QVector<double> values = trailingNumbers(operands, 3);
        if (values.size() == 3) {
            const GraphicsRgb rgb{values[0], values[1], values[2]};
            if (op == "rg") state_.fill = rgb; else state_.stroke = rgb;
        }
        return;
    }
    if (op == "k" || op == "K") {
        const QVector<double> values = trailingNumbers(operands, 4);
        if (values.size() == 4) {
            const GraphicsRgb rgb = rgbFromCmyk(values[0], values[1], values[2], values[3]);
            if (op == "k") state_.fill = rgb; else state_.stroke = rgb;
        }
        return;
    }
    if (op == "cs" || op == "CS") {
        // 颜色空间名决定了后面 sc/scn 有几个分量。
        const QByteArray name = trailingName(operands, 0);
        colorSpaceComponents_ = 1;
        if (name == "/DeviceRGB" || name == "/CalRGB" || name == "/RGB") colorSpaceComponents_ = 3;
        else if (name == "/DeviceCMYK" || name == "/CMYK") colorSpaceComponents_ = 4;
        return;
    }
    if (op == "sc" || op == "SC" || op == "scn" || op == "SCN") {
        const int expected = qMax(1, colorSpaceComponents_);
        const QVector<double> values = trailingNumbers(operands, expected);
        if (values.size() < expected) return;
        GraphicsRgb rgb = state_.fill;
        if (expected == 1) {
            rgb = GraphicsRgb{values[0], values[0], values[0]};
        } else if (expected == 3) {
            rgb = GraphicsRgb{values[0], values[1], values[2]};
        } else {
            rgb = rgbFromCmyk(values[0], values[1], values[2], values[3]);
        }
        if (op == "sc" || op == "scn") state_.fill = rgb; else state_.stroke = rgb;
        return;
    }

    // ---- 路径构造 ----
    if (op == "m") {
        const QVector<double> values = trailingNumbers(operands, 2);
        if (values.size() == 2) moveTo(QPointF(values[0], values[1]));
        return;
    }
    if (op == "l") {
        const QVector<double> values = trailingNumbers(operands, 2);
        if (values.size() == 2) lineTo(QPointF(values[0], values[1]));
        return;
    }
    if (op == "c") {
        const QVector<double> values = trailingNumbers(operands, 6);
        if (values.size() == 6) {
            curveTo(QPointF(values[0], values[1]), QPointF(values[2], values[3]),
                    QPointF(values[4], values[5]));
        }
        return;
    }
    if (op == "v") {
        // 第一个控制点就是当前点。
        const QVector<double> values = trailingNumbers(operands, 4);
        if (values.size() == 4) {
            curveTo(currentPoint_, QPointF(values[0], values[1]), QPointF(values[2], values[3]));
        }
        return;
    }
    if (op == "y") {
        // 第二个控制点就是终点。
        const QVector<double> values = trailingNumbers(operands, 4);
        if (values.size() == 4) {
            curveTo(QPointF(values[0], values[1]), QPointF(values[2], values[3]),
                    QPointF(values[2], values[3]));
        }
        return;
    }
    if (op == "h") {
        closePath();
        return;
    }
    if (op == "re") {
        const QVector<double> values = trailingNumbers(operands, 4);
        if (values.size() == 4) {
            const double x = values[0];
            const double y = values[1];
            const double width = values[2];
            const double height = values[3];
            moveTo(QPointF(x, y));
            lineTo(QPointF(x + width, y));
            lineTo(QPointF(x + width, y + height));
            lineTo(QPointF(x, y + height));
            closePath();
        }
        return;
    }

    // ---- 路径绘制 ----
    if (op == "n") {
        endPath();
        return;
    }
    if (op == "f" || op == "F") {
        paint(true, false);
        return;
    }
    if (op == "f*") {
        paint(true, false);
        return;
    }
    if (op == "B" || op == "B*") {
        paint(true, true);
        return;
    }
    if (op == "b" || op == "b*") {
        closePath();
        paint(true, true);
        return;
    }
    if (op == "S") {
        paint(false, true);
        return;
    }
    if (op == "s") {
        closePath();
        paint(false, true);
        return;
    }

    // ---- XObject ----
    if (op == "Do") {
        doXObject(trailingName(operands, 0));
        return;
    }

    // 其余算子（文字、剪裁、着色器、标记内容……）一律跳过：
    // 不认识的算子不该让整页的图形丢掉。
}

void GraphicsExtractor::interpret(const QByteArray& content) {
    const ContentParseResult parsed = ContentStream::tokenize(content);
    // tokenize 失败时 tokens 里仍有已经解析出来的部分，接着处理它们：
    // 畸形内容流不该让整页的图形全丢。
    QVector<Operand> operands;
    int nesting = 0;
    for (const ContentToken& token : parsed.tokens) {
        switch (token.type) {
        case ContentTokenType::Whitespace:
        case ContentTokenType::Comment:
        case ContentTokenType::InlineImage:
        case ContentTokenType::LiteralString:
        case ContentTokenType::HexString:
            continue;
        case ContentTokenType::ArrayStart:
        case ContentTokenType::DictionaryStart:
            ++nesting;
            continue;
        case ContentTokenType::ArrayEnd:
        case ContentTokenType::DictionaryEnd:
            if (nesting > 0) --nesting;
            continue;
        case ContentTokenType::Number: {
            if (nesting > 0) continue;
            double value = 0.0;
            bool ok = false;
            value = token.raw.toDouble(&ok);
            if (!ok) continue;
            Operand operand;
            operand.isNumber = true;
            operand.number = value;
            operands.append(operand);
            continue;
        }
        case ContentTokenType::Name: {
            if (nesting > 0) continue;
            Operand operand;
            operand.name = token.raw;
            operands.append(operand);
            continue;
        }
        case ContentTokenType::Operator: {
            if (nesting > 0) continue;
            const QVector<Operand> current = operands;
            operands.clear();
            applyOperator(token.raw, current);
            continue;
        }
        }
    }
}

// ====================================================== 文字绘制（真实字号） ==

// 一段文字绘制：在哪儿、用多大字号。
struct TextRun {
    QPointF origin;
    double fontSize = 0.0;
};

// 走一遍内容流，收集文字绘制段。
//
// 为什么不用 PDFium 的文本对象字号：PDFium 给的是「文本对象」的字号，
// 一个对象里可能混着好几段不同字号的文字；内容流里的 Tf 才是一段文字的真实字号。
class TextRunCollector {
public:
    QVector<TextRun> runs;

    void interpret(const QByteArray& content);

private:
    struct State {
        Matrix ctm;  // 当前变换矩阵
        Matrix tm;   // 文本矩阵
        Matrix tlm;  // 行矩阵
        double fontSize = 0.0;
        double leading = 0.0;
        double rise = 0.0;
    };

    void applyOperator(const QByteArray& op, const QVector<double>& numbers);
    void moveToNextLine();
    void showText();

    State state_;
    QVector<State> stack_;
};

void TextRunCollector::moveToNextLine() {
    Matrix translation;
    translation.f = -state_.leading;
    state_.tlm = multiply(translation, state_.tlm);
    state_.tm = state_.tlm;
}

void TextRunCollector::showText() {
    if (state_.fontSize <= 0.0) return;
    TextRun run;
    // 文字起点：文本空间里的 (0, 上升量) 经文本矩阵、再经 CTM。
    const QPointF inTextSpace = applyMatrix(state_.tm, QPointF(0.0, state_.rise));
    run.origin = applyMatrix(state_.ctm, inTextSpace);
    // 字号：Tf 的字号还要乘上文本矩阵与 CTM 的缩放。
    run.fontSize = state_.fontSize * scaleFactor(state_.tm) * scaleFactor(state_.ctm);
    runs.append(run);
}

void TextRunCollector::applyOperator(const QByteArray& op, const QVector<double>& numbers) {
    if (op == "q") {
        stack_.append(state_);
        return;
    }
    if (op == "Q") {
        if (!stack_.isEmpty()) state_ = stack_.takeLast();
        return;
    }
    if (op == "cm") {
        if (numbers.size() < 6) return;
        Matrix matrix;
        matrix.a = numbers[0];
        matrix.b = numbers[1];
        matrix.c = numbers[2];
        matrix.d = numbers[3];
        matrix.e = numbers[4];
        matrix.f = numbers[5];
        state_.ctm = multiply(matrix, state_.ctm);
        return;
    }
    if (op == "BT") {
        state_.tm = Matrix();
        state_.tlm = Matrix();
        return;
    }
    if (op == "Tf") {
        if (!numbers.isEmpty()) state_.fontSize = numbers.last();
        return;
    }
    if (op == "Tm") {
        if (numbers.size() < 6) return;
        Matrix matrix;
        matrix.a = numbers[0];
        matrix.b = numbers[1];
        matrix.c = numbers[2];
        matrix.d = numbers[3];
        matrix.e = numbers[4];
        matrix.f = numbers[5];
        state_.tm = matrix;
        state_.tlm = matrix;
        return;
    }
    if (op == "Td" || op == "TD") {
        if (numbers.size() < 2) return;
        if (op == "TD") state_.leading = -numbers[1];
        Matrix translation;
        translation.e = numbers[0];
        translation.f = numbers[1];
        state_.tlm = multiply(translation, state_.tlm);
        state_.tm = state_.tlm;
        return;
    }
    if (op == "T*") {
        moveToNextLine();
        return;
    }
    if (op == "Ts") {
        if (!numbers.isEmpty()) state_.rise = numbers.last();
        return;
    }
    if (op == "Tj" || op == "TJ") {
        showText();
        return;
    }
    if (op == "'" || op == "\"") {
        // 这两个算子先换行再画字。
        moveToNextLine();
        showText();
        return;
    }
}

void TextRunCollector::interpret(const QByteArray& content) {
    const ContentParseResult parsed = ContentStream::tokenize(content);
    QVector<double> numbers;
    int nesting = 0;
    for (const ContentToken& token : parsed.tokens) {
        switch (token.type) {
        case ContentTokenType::Number: {
            if (nesting > 0) continue;
            bool ok = false;
            const double value = token.raw.toDouble(&ok);
            if (ok) numbers.append(value);
            continue;
        }
        case ContentTokenType::ArrayStart:
        case ContentTokenType::DictionaryStart:
            ++nesting;
            continue;
        case ContentTokenType::ArrayEnd:
        case ContentTokenType::DictionaryEnd:
            if (nesting > 0) --nesting;
            continue;
        case ContentTokenType::Operator: {
            if (nesting > 0) continue;
            const QVector<double> operands = numbers;
            numbers.clear();
            applyOperator(token.raw, operands);
            continue;
        }
        default:
            continue;
        }
    }
}

// ============================================================ 页面级入口 =====

// 一页的内容流（多段内容流按顺序接起来）。
QByteArray pageContent(QPDFObjectHandle page) {
    QByteArray content;
    for (QPDFObjectHandle& stream : page.getPageContents()) {
        if (!stream.isStream()) continue;
        content += streamData(stream);
        content += '\n';
    }
    return content;
}

// 页面的有效盒子（CropBox，缺省退回 MediaBox）。
QRectF pageBox(QPDFObjectHandle page) {
    QPDFPageObjectHelper helper(page);
    const QPDFObjectHandle box = helper.getCropBox(false, false);
    if (!box.isArray()) return QRectF();
    const std::vector<QPDFObjectHandle> items = box.getArrayAsVector();
    if (items.size() < 4) return QRectF();
    const double left = numberValue(items[0]);
    const double bottom = numberValue(items[1]);
    const double right = numberValue(items[2]);
    const double top = numberValue(items[3]);
    return QRectF(QPointF(left, bottom), QPointF(right, top));
}

PageGraphics extractFromPage(QPDFObjectHandle page) {
    PageGraphics result;
    QPDFPageObjectHelper helper(page);
    const QHash<QByteArray, QPDFObjectHandle> xobjects =
        collectXobjects(helper.getAttribute("/Resources", false));
    const QByteArray content = pageContent(page);

    GraphicsExtractor extractor(xobjects);
    extractor.interpret(content);
    result.paths = extractor.paths;
    result.images = extractor.images;

    const QRectF box = pageBox(page);
    result.sizePt = QSizeF(box.width(), box.height());
    return result;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

// ============================================================== 对外接口 =====

PageGraphics extractPageGraphics(const QByteArray& pdfBytes, int pageIndex) {
    PageGraphics result;
#ifdef AIPDF_WITH_QPDF
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("输入数据为空");
        return result;
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("page-graphics.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
            result.error = QStringLiteral("页序号超出范围：第 %1 页").arg(pageIndex + 1);
            return result;
        }
        result = extractFromPage(pages.at(static_cast<size_t>(pageIndex)));
        // 这一页没有图形不是失败：ok=true，paths/images 为空。
        result.ok = true;
        return result;
    } catch (const std::exception& exception) {
        result.ok = false;
        result.error = QStringLiteral("读取页面图形失败：%1").arg(QString::fromUtf8(exception.what()));
        return result;
    }
#else
    Q_UNUSED(pdfBytes)
    Q_UNUSED(pageIndex)
    result.ok = false;
    result.error = QStringLiteral("当前构建未启用 qpdf，无法提取页面图形");
    return result;
#endif
}

QVector<PageGraphics> extractDocumentGraphics(const QByteArray& pdfBytes, QString* error) {
    if (error != nullptr) error->clear();
#ifdef AIPDF_WITH_QPDF
    if (pdfBytes.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("输入数据为空");
        return {};
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("page-graphics.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        QVector<PageGraphics> result;
        result.reserve(static_cast<int>(pages.size()));
        for (const QPDFObjectHandle& page : pages) {
            PageGraphics graphics = extractFromPage(page);
            graphics.ok = true;
            result.append(graphics);
        }
        return result;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = QStringLiteral("读取页面图形失败：%1").arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
#else
    Q_UNUSED(pdfBytes)
    if (error != nullptr) *error = QStringLiteral("当前构建未启用 qpdf，无法提取页面图形");
    return {};
#endif
}

// 把内容流里的真实字号（Tf）匹配到文本层的每个字符上。
//
// 实现放在这一层：内容流的解析（词法 + 算子）都在这里，
// 导出那一层只负责「格式映射」，不该自己再去读一遍 PDF。
//
// 声明在 pdf/convert/ExportFormats.h（导出层是它的调用方）。
QVector<double> extractCharFontSizes(const QByteArray& pdfBytes, const TextPage& text,
                                     int pageIndex) {
    QVector<double> sizes(text.chars.size(), 0.0);
#ifdef AIPDF_WITH_QPDF
    QVector<TextRun> runs;
    try {
        QPDF pdf;
        pdf.processMemoryFile("font-sizes.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pageIndex >= 0 && pageIndex < static_cast<int>(pages.size())) {
            TextRunCollector collector;
            collector.interpret(pageContent(pages.at(static_cast<size_t>(pageIndex))));
            runs = collector.runs;
        }
    } catch (const std::exception&) {
        // 读不到内容流就当没有真实字号：调用方会退回估算。
        runs.clear();
    }
    for (int i = 0; i < text.chars.size(); ++i) {
        const TextChar& character = text.chars.at(i);
        if (character.text.isEmpty()) continue;
        const QRectF box = character.box;
        const double baseline = qMin(box.top(), box.bottom());
        const double left = box.left();
        double bestSize = 0.0;
        double bestDistance = std::numeric_limits<double>::max();
        for (const TextRun& run : runs) {
            if (run.fontSize <= 0.0) continue;
            // 基线要对得上：容差按字号给——字母的下伸部会让字框底边低于基线。
            const double tolerance = qMax(2.0, 0.5 * run.fontSize);
            if (qAbs(run.origin.y() - baseline) > tolerance) continue;
            // 起点在字符左边、且最靠近的那一段，就是画出这个字符的那一段。
            const double distance = left - run.origin.x();
            if (distance < -0.5) continue;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestSize = run.fontSize;
            }
        }
        sizes[i] = bestSize;
    }
#else
    Q_UNUSED(pdfBytes)
    Q_UNUSED(pageIndex)
#endif
    return sizes;
}

}  // namespace aipdf::pdf
