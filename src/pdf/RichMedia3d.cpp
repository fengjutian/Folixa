#include "pdf/RichMedia3d.h"

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// 失败原因统一走这里。
//
// 为什么用中文直写而不是 tr()：这一层是领域层，翻译的锅在 app 层；
// 但错误信息会进日志、会显示给用户，所以必须是中文的、说人话的。
void setError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

// MIME 类型 → PDF 名字对象。
//
// PDF 的名字对象里不能直接出现 '/'，要写成 #2F（这与仓库里
// pdf::writePortfolio 写嵌入文件 /Subtype 的做法一致）。
QString mimeNameForPdf(const QString& mime) {
    QString escaped = mime.trimmed();
    escaped.replace(QLatin1Char('/'), QStringLiteral("#2F"));
    return QLatin1Char('/') + escaped;
}

// PDF 名字对象 → MIME 类型。
QString mimeFromPdfName(const QString& name) {
    QString value = name;
    if (value.startsWith(QLatin1Char('/'))) value = value.mid(1);
    value.replace(QStringLiteral("#2F"), QStringLiteral("/"));
    return value;
}

// 按扩展名猜 MIME 类型。
//
// 为什么要猜：PDF 的嵌入文件规范里 /Subtype 就是 MIME 类型，
// 调用方不传类型时总得给一个，总不能留空。
QString mimeFromFileName(const QString& fileName) {
    const QString lower = fileName.toLower();
    if (lower.endsWith(QStringLiteral(".mp3"))) return QStringLiteral("audio/mpeg");
    if (lower.endsWith(QStringLiteral(".wav"))) return QStringLiteral("audio/wav");
    if (lower.endsWith(QStringLiteral(".m4a"))) return QStringLiteral("audio/mp4");
    if (lower.endsWith(QStringLiteral(".aac"))) return QStringLiteral("audio/aac");
    if (lower.endsWith(QStringLiteral(".ogg"))) return QStringLiteral("audio/ogg");
    if (lower.endsWith(QStringLiteral(".mp4"))) return QStringLiteral("video/mp4");
    if (lower.endsWith(QStringLiteral(".m4v"))) return QStringLiteral("video/mp4");
    if (lower.endsWith(QStringLiteral(".mov"))) return QStringLiteral("video/quicktime");
    if (lower.endsWith(QStringLiteral(".avi"))) return QStringLiteral("video/x-msvideo");
    if (lower.endsWith(QStringLiteral(".webm"))) return QStringLiteral("video/webm");
    if (lower.endsWith(QStringLiteral(".mkv"))) return QStringLiteral("video/x-matroska");
    if (lower.endsWith(QStringLiteral(".png"))) return QStringLiteral("image/png");
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg"))) {
        return QStringLiteral("image/jpeg");
    }
    if (lower.endsWith(QStringLiteral(".gif"))) return QStringLiteral("image/gif");
    if (lower.endsWith(QStringLiteral(".u3d"))) return QStringLiteral("model/u3d");
    if (lower.endsWith(QStringLiteral(".prc"))) return QStringLiteral("model/prc");
    return QStringLiteral("application/octet-stream");
}

// MIME 类型 → RichMedia 实例类型（/Video、/Sound、/3D、/Flash）。
//
// PDF 只认这四种实例类型。图片这类 MIME 落到兜底分支：
// 写成 /Video 是承认「本程序不知道该归哪一类」，而不是假装知道。
QString instanceSubtypeForMime(const QString& mime) {
    if (mime.startsWith(QStringLiteral("audio/"))) return QStringLiteral("/Sound");
    if (mime.startsWith(QStringLiteral("video/"))) return QStringLiteral("/Video");
    if (mime.startsWith(QStringLiteral("model/"))) return QStringLiteral("/3D");
    return QStringLiteral("/Video");
}

// 名字对象规范化：补上开头的斜杠。
QString normalizeName(const QString& name) {
    const QString value = name.trimmed();
    if (value.isEmpty()) return QString();
    if (value.startsWith(QLatin1Char('/'))) return value;
    return QLatin1Char('/') + value;
}

#ifdef AIPDF_WITH_QPDF

// 给流写字典项。
//
// 为什么不能直接 stream.replaceKey(...)：qpdf 12 的 replaceKey 只认字典对象，
// 对流调用会被**静默忽略**（只在日志里留一条 warning），写出去的流就少了
// /Type、/Subtype 这些键——别的阅读器读不懂。
// 流的字典要通过 getDict() 拿。
void setStreamKey(QPDFObjectHandle& stream, const char* key, const QPDFObjectHandle& value) {
    QPDFObjectHandle dictionary = stream.getDict();
    dictionary.replaceKey(key, value);
}

// 读流的字典项。
//
// 为什么不能直接 stream.getKey(...)：qpdf 的 getKey 只认字典对象，
// 对流调用返回空——流的字典要先用 getDict() 拿。
QPDFObjectHandle streamKey(const QPDFObjectHandle& stream, const char* key) {
    if (!stream.isStream()) return QPDFObjectHandle();
    const QPDFObjectHandle dictionary = stream.getDict();
    if (!dictionary.isDictionary()) return QPDFObjectHandle();
    return dictionary.getKey(key);
}

// 名字对象的值（带前导斜杠，例如 "/U3D"）。不是名字就返回空串。
QString nameOf(const QPDFObjectHandle& handle) {
    if (!handle.isName()) return QString();
    return QString::fromStdString(handle.getName());
}

// 文本串的值（UTF-8）。不是文本串就返回空串。
QString textOf(const QPDFObjectHandle& handle) {
    if (!handle.isString()) return QString();
    return QString::fromUtf8(handle.getUTF8Value().c_str());
}

// 布尔值。不是布尔就返回 defaultValue。
bool boolOf(const QPDFObjectHandle& handle, bool defaultValue) {
    if (!handle.isBool()) return defaultValue;
    return handle.getBoolValue();
}

// 数值（整数或实数）。不是数值就返回 0。
double numberOf(const QPDFObjectHandle& handle) {
    if (handle.isInteger()) return static_cast<double>(handle.getIntValue());
    if (handle.isReal()) {
        // qpdf 的实数存的是字符串，这里自己转一次；
        // 转不动就当 0，而不是抛异常把整个读注解的过程带崩。
        try {
            return std::stod(handle.getRealValue());
        } catch (const std::exception&) {
            return 0.0;
        }
    }
    return 0.0;
}

// 写一个实数。
//
// qpdf 12 的 newReal 收的是字符串而不是 double，所以数字的格式化放在这里做。
QPDFObjectHandle newRealValue(double value) {
    return QPDFObjectHandle::newReal(QString::number(value, 'f', 4).toStdString());
}

// 矩形（PDF user space，左下原点）。
//
// PDF 的 /Rect 是 [ll ur] 四个数，这里读回来后按仓库约定装成 QRectF：
// x 是左边、y 是**下边**、宽高是正的。
QRectF rectOf(const QPDFObjectHandle& handle) {
    if (!handle.isArray()) return QRectF();
    const std::vector<QPDFObjectHandle> items = handle.getArrayAsVector();
    if (items.size() < 4) return QRectF();
    const double x1 = numberOf(items.at(0));
    const double y1 = numberOf(items.at(1));
    const double x2 = numberOf(items.at(2));
    const double y2 = numberOf(items.at(3));
    const double left = std::min(x1, x2);
    const double bottom = std::min(y1, y2);
    return QRectF(left, bottom, std::max(x1, x2) - left, std::max(y1, y2) - bottom);
}

// 写一个矩形（PDF user space）。
QPDFObjectHandle newRect(const QRectF& rect) {
    std::vector<QPDFObjectHandle> items;
    items.push_back(newRealValue(rect.left()));
    items.push_back(newRealValue(rect.top()));
    items.push_back(newRealValue(rect.right()));
    items.push_back(newRealValue(rect.bottom()));
    return QPDFObjectHandle::newArray(items);
}

// 颜色（DeviceRGB）。
QColor colorOf(const QPDFObjectHandle& handle) {
    if (!handle.isArray()) return QColor(Qt::white);
    const std::vector<QPDFObjectHandle> items = handle.getArrayAsVector();
    size_t start = 0;
    // 数组第一个元素是颜色空间名（/DeviceRGB）时，真正的数值从第二项开始。
    if (!items.empty() && items.front().isName()) start = 1;
    if (items.size() < start + 3) return QColor(Qt::white);
    const double red = qBound(0.0, numberOf(items.at(start)), 1.0);
    const double green = qBound(0.0, numberOf(items.at(start + 1)), 1.0);
    const double blue = qBound(0.0, numberOf(items.at(start + 2)), 1.0);
    return QColor::fromRgbF(red, green, blue);
}

// 写一个颜色（DeviceRGB 三元组 + 颜色空间名）。
QPDFObjectHandle newColor(const QColor& color) {
    std::vector<QPDFObjectHandle> items;
    items.push_back(QPDFObjectHandle::newName("/DeviceRGB"));
    items.push_back(newRealValue(color.redF()));
    items.push_back(newRealValue(color.greenF()));
    items.push_back(newRealValue(color.blueF()));
    return QPDFObjectHandle::newArray(items);
}

// 流解码后的字节数。
//
// 写出去的流没有压缩过滤器，所以解码后的长度就是写进去的字节数。
qint64 streamSize(QPDFObjectHandle stream) {
    if (!stream.isStream()) return 0;
    // getStreamData 不是 const 方法：这里按值收下一份可改的副本。
    const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
    if (!data) return 0;
    return static_cast<qint64>(data->getSize());
}

// 这一页上是不是已经有了一个「矩形完全相同」的同类注解。
//
// 为什么要查：两个一模一样的注解叠在一起会互相遮挡，界面上也没法分辨
// 点到了哪一个。宁可写失败让人换个位置，也不要悄悄覆盖。
bool hasSameRectAnnotation(const QPDFObjectHandle& page, const QString& subtype,
                           const QRectF& rect) {
    const QPDFObjectHandle annots = page.getKey("/Annots");
    if (!annots.isArray()) return false;
    for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
        if (!annot.isDictionary()) continue;
        if (nameOf(annot.getKey("/Subtype")) != subtype) continue;
        const QRectF existing = rectOf(annot.getKey("/Rect"));
        if (qFuzzyCompare(existing.left() + 1.0, rect.left() + 1.0) &&
            qFuzzyCompare(existing.top() + 1.0, rect.top() + 1.0) &&
            qFuzzyCompare(existing.width() + 1.0, rect.width() + 1.0) &&
            qFuzzyCompare(existing.height() + 1.0, rect.height() + 1.0)) {
            return true;
        }
    }
    return false;
}

// 把注解追加到页面的 /Annots 里。
void appendAnnotation(QPDFObjectHandle& page, const QPDFObjectHandle& annotation) {
    std::vector<QPDFObjectHandle> annots;
    const QPDFObjectHandle existing = page.getKey("/Annots");
    if (existing.isArray()) annots = existing.getArrayAsVector();
    annots.push_back(annotation);
    page.replaceKey("/Annots", QPDFObjectHandle::newArray(annots));
}

// 3D 视图字典（对应 PDF 的 3D view dictionary）。
QPDFObjectHandle buildThreeDView(QPDF& pdf, const ThreeDViewSpec& spec) {
    QPDFObjectHandle view = QPDFObjectHandle::newDictionary();
    view.replaceKey("/Type", QPDFObjectHandle::newName("/3DView"));
    view.replaceKey("/XN", QPDFObjectHandle::newUnicodeString(spec.externalName.toUtf8().constData()));
    view.replaceKey("/IN", QPDFObjectHandle::newUnicodeString(spec.internalName.toUtf8().constData()));

    // 相机到世界的变换矩阵：只认 12 个数，数量不对就不写这一项。
    if (spec.cameraToWorld.size() == 12) {
        std::vector<QPDFObjectHandle> matrix;
        matrix.reserve(12);
        for (const double value : spec.cameraToWorld) matrix.push_back(newRealValue(value));
        view.replaceKey("/C2W", QPDFObjectHandle::newArray(matrix));
    }
    if (spec.hasOrbitDistance) view.replaceKey("/CO", newRealValue(spec.orbitDistance));

    // 投影字典。
    QPDFObjectHandle projection = QPDFObjectHandle::newDictionary();
    projection.replaceKey("/Type", QPDFObjectHandle::newName("/3DProjection"));
    projection.replaceKey("/P",
                          QPDFObjectHandle::newName(normalizeName(spec.projectionType).toLatin1().constData()));
    projection.replaceKey("/Fov", newRealValue(spec.fieldOfView));
    view.replaceKey("/P", pdf.makeIndirectObject(projection));

    // 背景字典。
    if (spec.hasBackground) {
        QPDFObjectHandle background = QPDFObjectHandle::newDictionary();
        background.replaceKey("/Type", QPDFObjectHandle::newName("/3DBackground"));
        background.replaceKey("/C", newColor(spec.background));
        background.replaceKey("/S", QPDFObjectHandle::newBool(spec.backgroundShadow));
        background.replaceKey("/I", QPDFObjectHandle::newBool(spec.backgroundIlluminated));
        view.replaceKey("/BG", pdf.makeIndirectObject(background));
    }

    // 渲染模式字典。
    QPDFObjectHandle renderMode = QPDFObjectHandle::newDictionary();
    renderMode.replaceKey("/Type", QPDFObjectHandle::newName("/3DRenderMode"));
    renderMode.replaceKey("/RS",
                          QPDFObjectHandle::newName(normalizeName(spec.renderStyle).toLatin1().constData()));
    renderMode.replaceKey("/O", newRealValue(spec.renderOpacity));
    view.replaceKey("/RM", pdf.makeIndirectObject(renderMode));

    // 光照方案字典。
    QPDFObjectHandle lighting = QPDFObjectHandle::newDictionary();
    lighting.replaceKey("/Type", QPDFObjectHandle::newName("/3DLightingScheme"));
    lighting.replaceKey("/Subtype",
                        QPDFObjectHandle::newName(normalizeName(spec.lightingScheme).toLatin1().constData()));
    view.replaceKey("/LS", pdf.makeIndirectObject(lighting));
    return view;
}

// 读回 3D 视图字典。
ThreeDViewSpec readThreeDView(const QPDFObjectHandle& view) {
    ThreeDViewSpec spec;
    if (!view.isDictionary()) return spec;
    spec.externalName = textOf(view.getKey("/XN"));
    spec.internalName = textOf(view.getKey("/IN"));

    const QPDFObjectHandle matrix = view.getKey("/C2W");
    if (matrix.isArray()) {
        for (const QPDFObjectHandle& item : matrix.getArrayAsVector()) {
            spec.cameraToWorld.append(numberOf(item));
        }
    }
    const QPDFObjectHandle orbit = view.getKey("/CO");
    if (orbit.isNumber()) {
        spec.hasOrbitDistance = true;
        spec.orbitDistance = numberOf(orbit);
    }
    const QPDFObjectHandle projection = view.getKey("/P");
    if (projection.isDictionary()) {
        spec.projectionType = nameOf(projection.getKey("/P"));
        spec.fieldOfView = numberOf(projection.getKey("/Fov"));
    }
    const QPDFObjectHandle background = view.getKey("/BG");
    if (background.isDictionary()) {
        spec.hasBackground = true;
        spec.background = colorOf(background.getKey("/C"));
        spec.backgroundShadow = boolOf(background.getKey("/S"), false);
        spec.backgroundIlluminated = boolOf(background.getKey("/I"), true);
    }
    const QPDFObjectHandle renderMode = view.getKey("/RM");
    if (renderMode.isDictionary()) {
        spec.renderStyle = nameOf(renderMode.getKey("/RS"));
        spec.renderOpacity = numberOf(renderMode.getKey("/O"));
    }
    const QPDFObjectHandle lighting = view.getKey("/LS");
    if (lighting.isDictionary()) {
        spec.lightingScheme = nameOf(lighting.getKey("/Subtype"));
    }
    return spec;
}

// 3D 视图框（/3DB）。
//
// PDF 要求它用「注解目标坐标系」表达：原点在注解矩形中心。
// 对外的接口用 PDF user space（和 rect 同一套坐标），
// 写进文件时在这里换算过去，读回来时再换算回来。
QPDFObjectHandle newViewBox(const QRectF& annotationRect, const QRectF& viewBox) {
    const QRectF box = (viewBox.isNull() || viewBox.isEmpty()) ? annotationRect : viewBox;
    const double centerX = annotationRect.left() + annotationRect.width() * 0.5;
    const double centerY = annotationRect.top() + annotationRect.height() * 0.5;
    std::vector<QPDFObjectHandle> items;
    items.push_back(newRealValue(box.left() - centerX));
    items.push_back(newRealValue(box.top() - centerY));
    items.push_back(newRealValue(box.right() - centerX));
    items.push_back(newRealValue(box.bottom() - centerY));
    return QPDFObjectHandle::newArray(items);
}

// 读回 3D 视图框（换算回 PDF user space）。
QRectF readViewBox(const QPDFObjectHandle& handle, const QRectF& annotationRect) {
    if (!handle.isArray()) return annotationRect;
    const std::vector<QPDFObjectHandle> items = handle.getArrayAsVector();
    if (items.size() < 4) return annotationRect;
    const double centerX = annotationRect.left() + annotationRect.width() * 0.5;
    const double centerY = annotationRect.top() + annotationRect.height() * 0.5;
    const double x1 = numberOf(items.at(0)) + centerX;
    const double y1 = numberOf(items.at(1)) + centerY;
    const double x2 = numberOf(items.at(2)) + centerX;
    const double y2 = numberOf(items.at(3)) + centerY;
    const double left = std::min(x1, x2);
    const double bottom = std::min(y1, y2);
    return QRectF(left, bottom, std::max(x1, x2) - left, std::max(y1, y2) - bottom);
}

// 在名字树里找第一个文件说明字典。
//
// 名字树可能有 /Kids 分层，所以要递归找；深度设上限，
// 坏文件里的名字树可能套很深，不设限会把自己栈爆掉。
QPDFObjectHandle findFirstFilespec(const QPDFObjectHandle& node, int depth = 0) {
    if (depth > 16 || !node.isDictionary()) return QPDFObjectHandle();
    const QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        const std::vector<QPDFObjectHandle> items = names.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            if (items.at(i + 1).isDictionary()) return items.at(i + 1);
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            const QPDFObjectHandle found = findFirstFilespec(kid, depth + 1);
            if (!found.isNull()) return found;
        }
    }
    return QPDFObjectHandle();
}

// 把 PDF 写成字节。
QByteArray writeOut(QPDF& pdf, QString* error) {
    QPDFWriter writer(pdf);
    // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
    writer.setObjectStreamMode(qpdf_o_disable);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer || buffer->getSize() == 0) {
        setError(error, QStringLiteral("qpdf 没有写出内容"));
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<qsizetype>(buffer->getSize()));
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QByteArray writeThreeDAnnotation(const QByteArray& pdfBytes, const ThreeDAnnotationSpec& spec,
                                 QString* error) {
    if (error != nullptr) error->clear();
    if (pdfBytes.isEmpty()) {
        setError(error, QStringLiteral("没有可写入的 PDF 内容"));
        return {};
    }
    if (spec.modelBytes.isEmpty()) {
        setError(error, QStringLiteral("3D 模型字节为空，没有可写入的模型"));
        return {};
    }
    if (spec.rect.width() <= 0.0 || spec.rect.height() <= 0.0) {
        setError(error, QStringLiteral("3D 注解的矩形宽高必须大于 0"));
        return {};
    }
    if (!spec.fallback.rgb.isEmpty() && !spec.fallback.isValid()) {
        setError(error,
                 QStringLiteral("降级图像的像素数据长度与宽高对不上（应为 %1 字节，实际 %2 字节）")
                     .arg(spec.fallback.width * spec.fallback.height * 3)
                     .arg(spec.fallback.rgb.size()));
        return {};
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("本次构建没有启用 qpdf，写不了 3D 注解"));
    return {};
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("3d-annotation.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            setError(error, QStringLiteral("PDF 的根对象不是字典"));
            return {};
        }
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        if (pages.empty()) {
            setError(error, QStringLiteral("PDF 里没有页面"));
            return {};
        }
        if (spec.pageIndex < 0 || spec.pageIndex >= static_cast<int>(pages.size())) {
            setError(error, QStringLiteral("3D 注解的页码超出范围（文档共 %1 页，要写第 %2 页）")
                           .arg(pages.size())
                           .arg(spec.pageIndex + 1));
            return {};
        }
        QPDFObjectHandle page = pages.at(static_cast<size_t>(spec.pageIndex));
        if (hasSameRectAnnotation(page, QStringLiteral("/3D"), spec.rect)) {
            setError(error, QStringLiteral("第 %1 页上已经有一个矩形相同的 3D 注解，请换个位置")
                           .arg(spec.pageIndex + 1));
            return {};
        }

        // ---- 3D 流（模型本体）----
        // PRC 字节原样写进去：既不解析也不改写，模型合不合法由那份字节自己负责。
        const std::string modelData(spec.modelBytes.constData(),
                                    static_cast<size_t>(spec.modelBytes.size()));
        QPDFObjectHandle model = QPDFObjectHandle::newStream(&pdf, modelData);
        setStreamKey(model, "/Type", QPDFObjectHandle::newName("/3D"));
        setStreamKey(model, "/Subtype", QPDFObjectHandle::newName("/U3D"));

        // ---- 3D 视图字典 ----
        QPDFObjectHandle view = buildThreeDView(pdf, spec.view);
        const QPDFObjectHandle viewRef = pdf.makeIndirectObject(view);
        // /VA 里列出的就是 3D 流提供的视图；/DV 指明默认视图。
        setStreamKey(model, "/VA", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{viewRef}));
        setStreamKey(model, "/DV",
                     QPDFObjectHandle::newUnicodeString(spec.view.internalName.toUtf8().constData()));
        const QPDFObjectHandle modelRef = pdf.makeIndirectObject(model);

        // ---- 激活字典 ----
        QPDFObjectHandle activation = QPDFObjectHandle::newDictionary();
        activation.replaceKey("/A",
                             QPDFObjectHandle::newName(normalizeName(spec.activation).toLatin1().constData()));
        activation.replaceKey("/AIS", QPDFObjectHandle::newName("/L"));
        activation.replaceKey("/D", QPDFObjectHandle::newName("/PC"));
        activation.replaceKey("/DIS", QPDFObjectHandle::newName("/U"));
        activation.replaceKey("/TB", QPDFObjectHandle::newBool(true));
        activation.replaceKey("/NP", QPDFObjectHandle::newBool(false));
        const QPDFObjectHandle activationRef = pdf.makeIndirectObject(activation);

        // ---- 注解字典 ----
        QPDFObjectHandle annotation = QPDFObjectHandle::newDictionary();
        annotation.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        annotation.replaceKey("/Subtype", QPDFObjectHandle::newName("/3D"));
        annotation.replaceKey("/Rect", newRect(spec.rect));
        if (page.isIndirect()) annotation.replaceKey("/P", page);
        annotation.replaceKey("/3DD", modelRef);
        annotation.replaceKey("/3DV", viewRef);
        annotation.replaceKey("/3DA", activationRef);
        annotation.replaceKey("/3DI", QPDFObjectHandle::newBool(spec.interactive));
        annotation.replaceKey("/3DB", newViewBox(spec.rect, spec.viewBox));

        // ---- 降级图像：普通外观流 ----
        // PDF 里「不支持 3D 时显示什么」是注解的普通外观（/AP /N），
        // 3D 视图框（/3DB）是模型在注解里占多大地方——两者不是一回事。
        if (spec.fallback.isValid()) {
            // qCompress 出来的是 zlib 格式，正好是 /FlateDecode 要的。
            const QByteArray compressed = qCompress(spec.fallback.rgb);
            QPDFObjectHandle image = QPDFObjectHandle::newStream(
                &pdf, std::string(compressed.constData(), static_cast<size_t>(compressed.size())));
            setStreamKey(image, "/Type", QPDFObjectHandle::newName("/XObject"));
            setStreamKey(image, "/Subtype", QPDFObjectHandle::newName("/Image"));
            setStreamKey(image, "/Width", QPDFObjectHandle::newInteger(spec.fallback.width));
            setStreamKey(image, "/Height", QPDFObjectHandle::newInteger(spec.fallback.height));
            setStreamKey(image, "/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
            setStreamKey(image, "/BitsPerComponent", QPDFObjectHandle::newInteger(8));
            setStreamKey(image, "/Filter", QPDFObjectHandle::newName("/FlateDecode"));
            const QPDFObjectHandle imageRef = pdf.makeIndirectObject(image);

            const double width = spec.fallback.width;
            const double height = spec.fallback.height;
            const QByteArray formContent =
                QStringLiteral("q %1 0 0 %2 0 0 cm /Im0 Do\n").arg(width).arg(height).toLatin1();
            QPDFObjectHandle form = QPDFObjectHandle::newStream(
                &pdf, std::string(formContent.constData(), static_cast<size_t>(formContent.size())));
            setStreamKey(form, "/Type", QPDFObjectHandle::newName("/XObject"));
            setStreamKey(form, "/Subtype", QPDFObjectHandle::newName("/Form"));
            setStreamKey(form, "/FormType", QPDFObjectHandle::newInteger(1));
            setStreamKey(form, "/BBox", newRect(QRectF(0.0, 0.0, width, height)));
            QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
            xobjects.replaceKey("/Im0", imageRef);
            QPDFObjectHandle formResources = QPDFObjectHandle::newDictionary();
            formResources.replaceKey("/XObject", xobjects);
            setStreamKey(form, "/Resources", formResources);
            const QPDFObjectHandle formRef = pdf.makeIndirectObject(form);

            QPDFObjectHandle appearance = QPDFObjectHandle::newDictionary();
            appearance.replaceKey("/N", formRef);
            annotation.replaceKey("/AP", appearance);
        }

        appendAnnotation(page, pdf.makeIndirectObject(annotation));

        const QByteArray written = writeOut(pdf, error);
        if (written.isEmpty()) return {};

        // 自检：写出来的字节必须能重新解析出这个 3D 注解。
        // 这一步挡住的是「写出了字节，但 /Annots 根本没生效」的静默失败。
        const RichMediaReadResult check = readRichMedia(written);
        if (!check.ok()) {
            setError(error, QStringLiteral("写出的 3D 注解读不回来：%1").arg(check.error));
            return {};
        }
        bool found = false;
        for (const ThreeDAnnotationInfo& info : check.threeD) {
            if (info.pageIndex == spec.pageIndex) found = true;
        }
        if (!found) {
            setError(error, QStringLiteral("写出的 PDF 里找不到刚写的 3D 注解"));
            return {};
        }
        return written;
    } catch (const std::exception& failure) {
        setError(error, QStringLiteral("写入 3D 注解失败：%1")
                       .arg(QString::fromUtf8(failure.what())));
        return {};
    }
#endif
}

QByteArray writeMediaAnnotation(const QByteArray& pdfBytes, const MediaAnnotationSpec& spec,
                                QString* error) {
    if (error != nullptr) error->clear();
    if (pdfBytes.isEmpty()) {
        setError(error, QStringLiteral("没有可写入的 PDF 内容"));
        return {};
    }
    if (spec.embedded && spec.mediaBytes.isEmpty()) {
        setError(error, QStringLiteral("内嵌媒体的字节为空，没有可写入的内容"));
        return {};
    }
    if (!spec.embedded && spec.externalPath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("外链媒体没有给路径"));
        return {};
    }
    if (spec.rect.width() <= 0.0 || spec.rect.height() <= 0.0) {
        setError(error, QStringLiteral("多媒体注解的矩形宽高必须大于 0"));
        return {};
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("本次构建没有启用 qpdf，写不了多媒体注解"));
    return {};
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("rich-media-annotation.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            setError(error, QStringLiteral("PDF 的根对象不是字典"));
            return {};
        }
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        if (pages.empty()) {
            setError(error, QStringLiteral("PDF 里没有页面"));
            return {};
        }
        if (spec.pageIndex < 0 || spec.pageIndex >= static_cast<int>(pages.size())) {
            setError(error, QStringLiteral("多媒体注解的页码超出范围（文档共 %1 页，要写第 %2 页）")
                           .arg(pages.size())
                           .arg(spec.pageIndex + 1));
            return {};
        }
        QPDFObjectHandle page = pages.at(static_cast<size_t>(spec.pageIndex));
        if (hasSameRectAnnotation(page, QStringLiteral("/RichMedia"), spec.rect)) {
            setError(error, QStringLiteral("第 %1 页上已经有一个矩形相同的多媒体注解，请换个位置")
                           .arg(spec.pageIndex + 1));
            return {};
        }

        // ---- MIME 与实例类型 ----
        QString mime = spec.mimeType.trimmed();
        if (mime.isEmpty()) mime = mimeFromFileName(spec.fileName);
        const QString instanceSubtype = spec.instanceSubtype.trimmed().isEmpty()
                                            ? instanceSubtypeForMime(mime)
                                            : normalizeName(spec.instanceSubtype);

        // ---- 文件说明（Filespec）----
        const QString displayName = spec.embedded ? spec.fileName : spec.externalPath;
        QPDFObjectHandle filespec = QPDFObjectHandle::newDictionary();
        filespec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
        filespec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(displayName.toUtf8().constData()));
        filespec.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(displayName.toUtf8().constData()));
        if (spec.embedded) {
            const std::string mediaData(spec.mediaBytes.constData(),
                                        static_cast<size_t>(spec.mediaBytes.size()));
            QPDFObjectHandle file = QPDFObjectHandle::newStream(&pdf, mediaData);
            setStreamKey(file, "/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            setStreamKey(file, "/Subtype",
                         QPDFObjectHandle::newName(mimeNameForPdf(mime).toLatin1().constData()));
            QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
            params.replaceKey("/Size",
                              QPDFObjectHandle::newInteger(static_cast<long long>(spec.mediaBytes.size())));
            setStreamKey(file, "/Params", params);
            const QPDFObjectHandle fileRef = pdf.makeIndirectObject(file);

            QPDFObjectHandle embeddedFiles = QPDFObjectHandle::newDictionary();
            embeddedFiles.replaceKey("/F", fileRef);
            embeddedFiles.replaceKey("/UF", fileRef);
            filespec.replaceKey("/EF", embeddedFiles);
        }
        const QPDFObjectHandle filespecRef = pdf.makeIndirectObject(filespec);

        // ---- 资源名字树（/Assets）----
        // 名字树的键是文本串，不是 PDF 名字对象，所以中文文件名也放得下。
        // 外链时可能没给文件名，用一个占位名顶上（键只是个索引，不影响播放）。
        const QString assetName =
            spec.fileName.trimmed().isEmpty() ? QStringLiteral("media") : spec.fileName.trimmed();
        std::vector<QPDFObjectHandle> assetNames;
        assetNames.push_back(QPDFObjectHandle::newUnicodeString(assetName.toUtf8().constData()));
        assetNames.push_back(filespecRef);
        QPDFObjectHandle assets = QPDFObjectHandle::newDictionary();
        assets.replaceKey("/Names", QPDFObjectHandle::newArray(assetNames));

        // ---- 配置与实例 ----
        QPDFObjectHandle instance = QPDFObjectHandle::newDictionary();
        instance.replaceKey("/Type", QPDFObjectHandle::newName("/RichMediaInstance"));
        instance.replaceKey("/Subtype", QPDFObjectHandle::newName(instanceSubtype.toLatin1().constData()));
        instance.replaceKey("/Asset", filespecRef);
        const QPDFObjectHandle instanceRef = pdf.makeIndirectObject(instance);

        QPDFObjectHandle configuration = QPDFObjectHandle::newDictionary();
        configuration.replaceKey("/Type", QPDFObjectHandle::newName("/RichMediaConfiguration"));
        configuration.replaceKey("/Name", QPDFObjectHandle::newUnicodeString("Configuration1"));
        configuration.replaceKey("/Instances",
                                 QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{instanceRef}));
        const QPDFObjectHandle configurationRef = pdf.makeIndirectObject(configuration);

        QPDFObjectHandle content = QPDFObjectHandle::newDictionary();
        content.replaceKey("/Type", QPDFObjectHandle::newName("/RichMediaContent"));
        content.replaceKey("/Assets", assets);
        content.replaceKey("/Configurations",
                           QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{configurationRef}));
        const QPDFObjectHandle contentRef = pdf.makeIndirectObject(content);

        // ---- 设置字典 ----
        QPDFObjectHandle activation = QPDFObjectHandle::newDictionary();
        activation.replaceKey("/A", QPDFObjectHandle::newName("/PO"));
        activation.replaceKey("/AIS", QPDFObjectHandle::newName("/L"));
        activation.replaceKey("/D", QPDFObjectHandle::newName("/PC"));
        activation.replaceKey("/DIS", QPDFObjectHandle::newName("/U"));
        QPDFObjectHandle settings = QPDFObjectHandle::newDictionary();
        settings.replaceKey("/Type", QPDFObjectHandle::newName("/RichMediaSettings"));
        settings.replaceKey("/Activation", activation);
        const QPDFObjectHandle settingsRef = pdf.makeIndirectObject(settings);

        // ---- 注解字典 ----
        QPDFObjectHandle annotation = QPDFObjectHandle::newDictionary();
        annotation.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        annotation.replaceKey("/Subtype", QPDFObjectHandle::newName("/RichMedia"));
        annotation.replaceKey("/Rect", newRect(spec.rect));
        if (page.isIndirect()) annotation.replaceKey("/P", page);
        annotation.replaceKey("/RichMediaContent", contentRef);
        annotation.replaceKey("/RichMediaSettings", settingsRef);

        appendAnnotation(page, pdf.makeIndirectObject(annotation));

        const QByteArray written = writeOut(pdf, error);
        if (written.isEmpty()) return {};

        const RichMediaReadResult check = readRichMedia(written);
        if (!check.ok()) {
            setError(error, QStringLiteral("写出的多媒体注解读不回来：%1").arg(check.error));
            return {};
        }
        bool found = false;
        for (const MediaAnnotationInfo& info : check.media) {
            if (info.pageIndex == spec.pageIndex) found = true;
        }
        if (!found) {
            setError(error, QStringLiteral("写出的 PDF 里找不到刚写的多媒体注解"));
            return {};
        }
        return written;
    } catch (const std::exception& failure) {
        setError(error, QStringLiteral("写入多媒体注解失败：%1")
                       .arg(QString::fromUtf8(failure.what())));
        return {};
    }
#endif
}

RichMediaReadResult readRichMedia(const QByteArray& pdfBytes) {
    RichMediaReadResult result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("没有可读的内容");
        return result;
    }
#ifndef AIPDF_WITH_QPDF
    result.error = QStringLiteral("本次构建没有启用 qpdf，读不了 3D 与多媒体注解");
    return result;
#else
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("rich-media-read.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle page = pages.at(i);
            const QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isArray()) continue;
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                if (!annot.isDictionary()) continue;
                const QString subtype = nameOf(annot.getKey("/Subtype"));
                if (subtype == QStringLiteral("/3D")) {
                    ThreeDAnnotationInfo info;
                    info.pageIndex = static_cast<int>(i);
                    info.rect = rectOf(annot.getKey("/Rect"));
                    info.viewBox = readViewBox(annot.getKey("/3DB"), info.rect);
                    info.view = readThreeDView(annot.getKey("/3DV"));
                    info.interactive = boolOf(annot.getKey("/3DI"), true);
                    const QPDFObjectHandle activation = annot.getKey("/3DA");
                    if (activation.isDictionary()) {
                        info.activation = nameOf(activation.getKey("/A"));
                    }
                    const QPDFObjectHandle model = annot.getKey("/3DD");
                    if (model.isStream()) {
                        info.hasModel = true;
                        info.modelEmbedded = true;
                        info.modelSize = streamSize(model);
                        info.modelMimeType = nameOf(streamKey(model, "/Subtype"));
                    } else if (model.isDictionary()) {
                        // 3D 参考字典：模型在别处，这里只是引用。
                        info.hasModel = true;
                        info.modelEmbedded = false;
                        const QString reference = textOf(model.getKey("/F"));
                        if (!reference.isEmpty()) info.modelFileName = reference;
                        const QPDFObjectHandle referenced = model.getKey("/3DD");
                        if (referenced.isStream()) {
                            info.modelMimeType = nameOf(streamKey(referenced, "/Subtype"));
                            info.modelSize = streamSize(referenced);
                        }
                    }
                    const QPDFObjectHandle appearance = annot.getKey("/AP");
                    if (appearance.isDictionary()) {
                        info.hasFallbackImage = appearance.getKey("/N").isStream();
                    }
                    result.threeD.append(info);
                } else if (subtype == QStringLiteral("/RichMedia")) {
                    MediaAnnotationInfo info;
                    info.pageIndex = static_cast<int>(i);
                    info.rect = rectOf(annot.getKey("/Rect"));
                    const QPDFObjectHandle content = annot.getKey("/RichMediaContent");
                    if (content.isDictionary()) {
                        const QPDFObjectHandle filespec = findFirstFilespec(content.getKey("/Assets"));
                        if (filespec.isDictionary()) {
                            QString name = textOf(filespec.getKey("/UF"));
                            if (name.isEmpty()) name = textOf(filespec.getKey("/F"));
                            info.fileName = name;
                            const QPDFObjectHandle embeddedFiles = filespec.getKey("/EF");
                            if (embeddedFiles.isDictionary()) {
                                QPDFObjectHandle file = embeddedFiles.getKey("/F");
                                if (!file.isStream()) file = embeddedFiles.getKey("/UF");
                                if (file.isStream()) {
                                    info.embedded = true;
                                    info.mediaSize = streamSize(file);
                                    info.mimeType = mimeFromPdfName(nameOf(streamKey(file, "/Subtype")));
                                }
                            }
                            if (!info.embedded) info.externalPath = name;
                        }
                        const QPDFObjectHandle configurations = content.getKey("/Configurations");
                        if (configurations.isArray()) {
                            for (const QPDFObjectHandle& configuration : configurations.getArrayAsVector()) {
                                if (!configuration.isDictionary()) continue;
                                const QPDFObjectHandle instances = configuration.getKey("/Instances");
                                if (!instances.isArray()) continue;
                                for (const QPDFObjectHandle& instance : instances.getArrayAsVector()) {
                                    if (!instance.isDictionary()) continue;
                                    const QString instanceSubtype = nameOf(instance.getKey("/Subtype"));
                                    if (!instanceSubtype.isEmpty()) {
                                        info.instanceSubtype = instanceSubtype;
                                    }
                                    break;
                                }
                                break;
                            }
                        }
                    }
                    result.media.append(info);
                }
            }
        }
        if (result.threeD.isEmpty() && result.media.isEmpty()) {
            result.notes.append(QStringLiteral("这份 PDF 里没有 3D 与多媒体注解"));
        }
        return result;
    } catch (const std::exception& failure) {
        result.error = QStringLiteral("解析这份 PDF 失败：%1")
                           .arg(QString::fromUtf8(failure.what()));
        result.threeD.clear();
        result.media.clear();
        return result;
    }
#endif
}

QVector<ThreeDAnnotationInfo> readThreeDAnnotations(const QByteArray& pdfBytes, int pageIndex,
                                                      QString* error) {
    if (error != nullptr) error->clear();
    const RichMediaReadResult all = readRichMedia(pdfBytes);
    if (!all.ok()) {
        setError(error, all.error);
        return {};
    }
    QVector<ThreeDAnnotationInfo> result;
    for (const ThreeDAnnotationInfo& info : all.threeD) {
        if (info.pageIndex == pageIndex) result.append(info);
    }
    return result;
}

QVector<MediaAnnotationInfo> readMediaAnnotations(const QByteArray& pdfBytes, int pageIndex,
                                                    QString* error) {
    if (error != nullptr) error->clear();
    const RichMediaReadResult all = readRichMedia(pdfBytes);
    if (!all.ok()) {
        setError(error, all.error);
        return {};
    }
    QVector<MediaAnnotationInfo> result;
    for (const MediaAnnotationInfo& info : all.media) {
        if (info.pageIndex == pageIndex) result.append(info);
    }
    return result;
}

}  // namespace aipdf::pdf
