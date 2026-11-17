#include "pdf/PdfAChecker.h"

#include "pdf/ContentStream.h"

#include <QSet>
#include <QStringList>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <utility>
#include <vector>
#endif

namespace aipdf::pdf {

namespace {

#ifdef AIPDF_WITH_QPDF

// 资源字典的递归深度上限：坏文件里图案与表单 XObject 可能互相引用。
constexpr int kMaxResourceDepth = 8;

// ---------------------------------------------------------- 小工具函数 ----

// PDF 里的名字对象（/DeviceRGB 之类）转成不带斜杠的文本。
QString nameText(const std::string& name) {
    QString text = QString::fromUtf8(name.c_str());
    if (text.startsWith(QLatin1Char('/'))) text = text.mid(1);
    return text;
}

// 资源字典里的键（qpdf 给的是带斜杠的 "/Im1"）去掉斜杠后当名字用。
QString resourceName(const std::string& key) {
    QString name = QString::fromUtf8(key.c_str());
    if (name.startsWith(QLatin1Char('/'))) name = name.mid(1);
    return name;
}

// 对象的编号（报告里用来定位）；不是间接对象时给 -1。
int objectIdOf(const QPDFObjectHandle& handle) {
    return handle.isIndirect() ? handle.getObjectID() : -1;
}

// 取流对象的字典。
//
// 为什么需要这一步：qpdf 里「流对象」与「字典对象」是两种类型，
// isDictionary() 对流返回 false，在流对象上直接 getKey() 拿不到东西。
QPDFObjectHandle dictOf(const QPDFObjectHandle& object) {
    return object.isStream() ? object.getDict() : object;
}

// 读一个流的内容。
//
// 为什么按值拷贝：qpdf 的 getStreamData() 是非 const 方法，
// 拿 const 引用调用不了。
QByteArray streamDataOf(const QPDFObjectHandle& stream) {
    if (!stream.isStream()) return QByteArray();
    QPDFObjectHandle copy = stream;
    std::shared_ptr<Buffer> data = copy.getStreamData(qpdf_dl_generalized);
    if (!data) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                      static_cast<int>(data->getSize()));
}

// 文件头里的版本号，例如 "1.7"。
QString pdfHeaderVersion(const QByteArray& bytes) {
    const int at = bytes.indexOf("%PDF-");
    if (at < 0) return QString();
    int cursor = at + 5;
    QString major;
    while (cursor < bytes.size() && bytes.at(cursor) >= '0' && bytes.at(cursor) <= '9') {
        major += QChar(bytes.at(cursor));
        ++cursor;
    }
    if (major.isEmpty()) return QString();
    QString minor;
    if (cursor < bytes.size() && bytes.at(cursor) == '.') {
        ++cursor;
        while (cursor < bytes.size() && bytes.at(cursor) >= '0' && bytes.at(cursor) <= '9') {
            minor += QChar(bytes.at(cursor));
            ++cursor;
        }
    }
    if (minor.isEmpty()) minor = QStringLiteral("0");
    return major + QLatin1Char('.') + minor;
}

// 一个字体是否内嵌了字体程序。
//
// 为什么查三个键：/FontFile 是 Type1，/FontFile2 是 TrueType/OpenType，
// /FontFile3 是 CFF/Type1C——字体程序挂在哪个键里取决于字体类型。
bool fontIsEmbedded(const QPDFObjectHandle& font) {
    QVector<QPDFObjectHandle> descriptors;
    const QPDFObjectHandle subtype = font.getKey("/Subtype");
    if (subtype.isName() && subtype.getName() == "/Type0") {
        // 复合字体的字体程序挂在 /DescendantFonts 里的每一个子字体上。
        const QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
        if (descendants.isArray()) {
            for (const QPDFObjectHandle& item : descendants.getArrayAsVector()) {
                descriptors.append(item.getKey("/FontDescriptor"));
            }
        }
    } else {
        descriptors.append(font.getKey("/FontDescriptor"));
    }
    for (const QPDFObjectHandle& descriptor : descriptors) {
        if (!descriptor.isDictionary()) continue;
        if (descriptor.getKey("/FontFile").isStream()) return true;
        if (descriptor.getKey("/FontFile2").isStream()) return true;
        if (descriptor.getKey("/FontFile3").isStream()) return true;
    }
    return false;
}

// 字体在字典里的基名（/BaseFont），出错时给个占位名。
QString baseFontName(const QPDFObjectHandle& font) {
    const QPDFObjectHandle base = font.getKey("/BaseFont");
    if (!base.isName()) return QStringLiteral("(unnamed font)");
    QString name = nameText(base.getName());
    // 子集前缀（"ABCDEF+"）只是文件里对字体的编号，不是字体名。
    if (name.size() > 7 && name.at(6) == QLatin1Char('+')) {
        bool subsetTag = true;
        for (int i = 0; i < 6; ++i) {
            const QChar ch = name.at(i);
            if (!((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
                  (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')))) {
                subsetTag = false;
                break;
            }
        }
        if (subsetTag) name = name.mid(7);
    }
    return name;
}

// 从 XMP 包里读一个属性的值，例如 pdfaid:part="2" 或 <pdfaid:part>2</pdfaid:part>。
//
// 为什么只做字面量扫描而不解析 XML：XMP 里这条属性的写法就那么几种
// （rdf:Description 上的属性，或 pdfaid:part 元素），做完整的 XML/RDF 解析
// 要引入一个 XML 解析器，代价远超这一条检查的收益。
// 代价是：属性前缀不是 pdfaid（自己绑了同一个命名空间 URI）时会漏检——
// 漏检的方向是「不报错」，不会给出错误的结论。
QString xmpAttribute(const QByteArray& xmp, const char* attribute) {
    const QByteArray key(attribute);
    const auto isBlank = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    };
    int at = xmp.indexOf(key);
    while (at >= 0) {
        int cursor = at + key.size();
        while (cursor < xmp.size() && isBlank(xmp.at(cursor))) ++cursor;
        if (cursor < xmp.size() && xmp.at(cursor) == '=') {
            ++cursor;
            while (cursor < xmp.size() && isBlank(xmp.at(cursor))) ++cursor;
            if (cursor >= xmp.size()) return QString();
            const char quote = xmp.at(cursor);
            if (quote != '"' && quote != '\'') return QString();
            const int end = xmp.indexOf(quote, cursor + 1);
            if (end < 0) return QString();
            return QString::fromUtf8(xmp.mid(cursor + 1, end - cursor - 1)).trimmed();
        }
        if (cursor < xmp.size() && xmp.at(cursor) == '>') {
            // 元素写法：<pdfaid:part>2</pdfaid:part>
            const int end = xmp.indexOf('<', cursor);
            if (end < 0) return QString();
            return QString::fromUtf8(xmp.mid(cursor + 1, end - cursor - 1)).trimmed();
        }
        at = xmp.indexOf(key, at + 1);
    }
    return QString();
}

// ------------------------------------------------------------ ICC 头部 ----

// ICC 配置文件头里预检关心的那几个字段。
struct IccHeader {
    bool ok = false;
    int majorVersion = 0;
    QByteArray deviceClass;  // "mntr"（显示器）/ "prtr"（打印）
    QByteArray dataSpace;    // "RGB " / "CMYK" / "GRAY"
    QStringList problems;     // ASCII 说明
};

// 读 ICC 配置文件的头并挑出 PDF/A-2 的硬要求。
//
// 为什么只看头：ISO 19005-2 6.2.4.2 要求嵌入的配置文件是「显示器或打印」的
// 配置文件，数据色彩空间是 GRAY / RGB / CMYK，且版本低于 5。
// 这三条全在 ICC 头的固定偏移上，不用解析整个配置文件。
IccHeader readIccHeader(const QByteArray& bytes) {
    IccHeader header;
    // ICC 头 128 字节 + 4 字节标签计数 = 132 字节是最小长度。
    if (bytes.size() < 132) {
        header.problems.append(QStringLiteral("profile is shorter than 132 bytes"));
        return header;
    }
    if (bytes.mid(36, 4) != QByteArray("acsp")) {
        header.problems.append(QStringLiteral("missing 'acsp' signature at offset 36"));
    }
    header.majorVersion = static_cast<unsigned char>(bytes.at(8));
    header.deviceClass = bytes.mid(12, 4);
    header.dataSpace = bytes.mid(16, 4);

    if (header.deviceClass != QByteArray("mntr") && header.deviceClass != QByteArray("prtr")) {
        header.problems.append(QStringLiteral("device class '%1' must be 'mntr' or 'prtr'")
                                   .arg(QString::fromLatin1(header.deviceClass)));
    }
    if (header.dataSpace != QByteArray("GRAY") && header.dataSpace != QByteArray("RGB ") &&
        header.dataSpace != QByteArray("CMYK")) {
        header.problems.append(QStringLiteral("data colour space '%1' must be GRAY, RGB or CMYK")
                                   .arg(QString::fromLatin1(header.dataSpace)));
    }
    if (header.majorVersion >= 5) {
        header.problems.append(QStringLiteral("ICC version %1 is not allowed (PDF/A requires a "
                                               "version below 5)")
                                   .arg(header.majorVersion));
    }
    header.ok = header.problems.isEmpty();
    return header;
}

// --------------------------------------------------------- 色彩空间扫描 ----

// 一条色彩空间问题：类别 + ASCII 说明。
using ColourSpaceProblem = QPair<QString, QString>;

// 一个色彩空间问题的类别。
constexpr const char* kProblemCie = "cie";
constexpr const char* kProblemProfileMissing = "profile-missing";
constexpr const char* kProblemBaseInvalid = "base-invalid";
constexpr const char* kProblemUnknown = "unknown";

// 扫描结果：用到的设备色彩空间 + 各类问题。
struct ColourSpaceScan {
    QSet<QString> deviceSpaces;
    QVector<ColourSpaceProblem> problems;
};

// 是不是设备色彩空间。
bool isDeviceSpaceName(const QString& name) {
    return name == QLatin1String("DeviceGray") || name == QLatin1String("DeviceRGB") ||
           name == QLatin1String("DeviceCMYK");
}

// 一个色彩空间是不是「设备色彩空间或由 ICC 定义的色彩空间」。
//
// 为什么这么判：PDF/A 要求色彩要么是设备空间，要么由 ICC 配置文件定义；
// Lab / CalRGB / CalGray 这类 CIE 空间不能用 ICC 表达，属于不被允许的一类。
bool isDeviceOrIccSpace(const QPDFObjectHandle& space) {
    if (space.isName()) return isDeviceSpaceName(nameText(space.getName()));
    if (!space.isArray()) return false;
    const std::vector<QPDFObjectHandle> items = space.getArrayAsVector();
    if (items.empty() || !items.front().isName()) return false;
    if (nameText(items.front().getName()) != QLatin1String("ICCBased")) return false;
    return items.size() > 1 && items.at(1).isStream();
}

// 扫一个色彩空间。
void scanColourSpace(const QPDFObjectHandle& space, const QString& where, ColourSpaceScan* scan) {
    if (space.isName()) {
        const QString name = nameText(space.getName());
        if (isDeviceSpaceName(name)) {
            scan->deviceSpaces.insert(name);
            return;
        }
        if (name == QLatin1String("Pattern")) return;
        scan->problems.append(qMakePair(QString::fromLatin1(kProblemUnknown),
                                        QStringLiteral("%1: unsupported colour space /%2")
                                            .arg(where, name)));
        return;
    }
    if (!space.isArray()) {
        scan->problems.append(qMakePair(QString::fromLatin1(kProblemUnknown),
                                        QStringLiteral("%1: colour space is neither a name nor "
                                                       "an array")
                                            .arg(where)));
        return;
    }
    const std::vector<QPDFObjectHandle> items = space.getArrayAsVector();
    if (items.empty() || !items.front().isName()) {
        scan->problems.append(qMakePair(QString::fromLatin1(kProblemUnknown),
                                        QStringLiteral("%1: colour space array has no name "
                                                       "selector")
                                            .arg(where)));
        return;
    }
    const QString kind = nameText(items.front().getName());
    if (kind == QLatin1String("ICCBased")) {
        if (items.size() < 2 || !items.at(1).isStream()) {
            scan->problems.append(
                qMakePair(QString::fromLatin1(kProblemProfileMissing),
                          QStringLiteral("%1: ICCBased colour space has no embedded ICC profile "
                                         "stream")
                              .arg(where)));
        }
        return;
    }
    if (kind == QLatin1String("CalRGB") || kind == QLatin1String("CalGray") ||
        kind == QLatin1String("Lab")) {
        scan->problems.append(
            qMakePair(QString::fromLatin1(kProblemCie),
                      QStringLiteral("%1: %2 colour space is not allowed; PDF/A requires colour "
                                     "to be defined by an ICC profile")
                          .arg(where, kind)));
        return;
    }
    if (kind == QLatin1String("Indexed")) {
        if (items.size() < 2) {
            scan->problems.append(qMakePair(QString::fromLatin1(kProblemUnknown),
                                            QStringLiteral("%1: Indexed colour space has no base "
                                                           "colour space")
                                                .arg(where)));
            return;
        }
        const QPDFObjectHandle base = items.at(1);
        if (!isDeviceOrIccSpace(base)) {
            scan->problems.append(
                qMakePair(QString::fromLatin1(kProblemBaseInvalid),
                          QStringLiteral("%1: Indexed base colour space is neither a device colour "
                                         "space nor ICC based")
                              .arg(where)));
            return;
        }
        if (base.isName()) scan->deviceSpaces.insert(nameText(base.getName()));
        return;
    }
    if (kind == QLatin1String("Separation") || kind == QLatin1String("DeviceN")) {
        if (items.size() < 3) {
            scan->problems.append(qMakePair(QString::fromLatin1(kProblemUnknown),
                                            QStringLiteral("%1: %2 colour space has no alternate "
                                                           "colour space")
                                                .arg(where, kind)));
            return;
        }
        const QPDFObjectHandle alternate = items.at(2);
        if (!isDeviceOrIccSpace(alternate)) {
            scan->problems.append(
                qMakePair(QString::fromLatin1(kProblemBaseInvalid),
                          QStringLiteral("%1: %2 alternate colour space is neither a device colour "
                                         "space nor ICC based")
                              .arg(where, kind)));
            return;
        }
        if (alternate.isName()) scan->deviceSpaces.insert(nameText(alternate.getName()));
        return;
    }
    if (kind == QLatin1String("Pattern")) return;
    if (isDeviceSpaceName(kind)) {
        scan->deviceSpaces.insert(kind);
        return;
    }
    scan->problems.append(qMakePair(QString::fromLatin1(kProblemUnknown),
                                    QStringLiteral("%1: unsupported colour space %2")
                                        .arg(where, kind)));
}

// 扫一页的资源字典：颜色空间、图像、图案、渐变。
void scanResources(const QPDFObjectHandle& resources, int depth, ColourSpaceScan* scan) {
    if (depth > kMaxResourceDepth) return;
    if (!resources.isDictionary()) return;

    const QPDFObjectHandle colourSpaces = resources.getKey("/ColorSpace");
    if (colourSpaces.isDictionary()) {
        for (const std::string& key : colourSpaces.getKeys()) {
            scanColourSpace(colourSpaces.getKey(key),
                            QStringLiteral("resource /ColorSpace /%1").arg(resourceName(key)), scan);
        }
    }

    const QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if (xobjects.isDictionary()) {
        for (const std::string& key : xobjects.getKeys()) {
            const QPDFObjectHandle object = xobjects.getKey(key);
            if (!object.isStream()) continue;
            const QPDFObjectHandle dict = object.getDict();
            const QPDFObjectHandle subtype = dict.getKey("/Subtype");
            if (!subtype.isName()) continue;
            const QString where = QStringLiteral("xobject /%1").arg(resourceName(key));
            if (subtype.getName() == "/Image") {
                scanColourSpace(dict.getKey("/ColorSpace"), where + QStringLiteral(" /ColorSpace"),
                                scan);
            } else if (subtype.getName() == "/Form") {
                scanResources(dict.getKey("/Resources"), depth + 1, scan);
            }
        }
    }

    const QPDFObjectHandle patterns = resources.getKey("/Pattern");
    if (patterns.isDictionary()) {
        for (const std::string& key : patterns.getKeys()) {
            const QPDFObjectHandle pattern = patterns.getKey(key);
            if (!pattern.isDictionary()) continue;
            const QString where = QStringLiteral("pattern /%1").arg(resourceName(key));
            scanColourSpace(pattern.getKey("/ColorSpace"), where + QStringLiteral(" /ColorSpace"),
                            scan);
            scanResources(pattern.getKey("/Resources"), depth + 1, scan);
        }
    }

    const QPDFObjectHandle shadings = resources.getKey("/Shading");
    if (shadings.isDictionary()) {
        for (const std::string& key : shadings.getKeys()) {
            const QPDFObjectHandle shading = shadings.getKey(key);
            if (!shading.isDictionary()) continue;
            scanColourSpace(shading.getKey("/ColorSpace"),
                            QStringLiteral("shading /%1 /ColorSpace").arg(resourceName(key)), scan);
        }
    }
}

// --------------------------------------------------------- 内容流扫描 ----

// 内容流里用到的设备色彩空间。
//
// 为什么扫内容流：设备色彩空间可以完全不出现在资源字典里——
// 内容流里一个 `k` 操作符就是 DeviceCMYK。这种文件在资源层面看不出用了 CMYK，
// 但它配上一个 RGB 的 OutputIntent 就是 PDF/A-2 6.2.4.3 的违规。
void collectContentDeviceSpaces(const QByteArray& content, QSet<QString>* spaces) {
    const ContentParseResult parsed = ContentStream::tokenize(content);
    for (const ContentToken& token : parsed.tokens) {
        if (token.type == ContentTokenType::Operator) {
            if (token.raw == "rg" || token.raw == "RG") {
                spaces->insert(QStringLiteral("DeviceRGB"));
            } else if (token.raw == "k" || token.raw == "K") {
                spaces->insert(QStringLiteral("DeviceCMYK"));
            } else if (token.raw == "g" || token.raw == "G") {
                spaces->insert(QStringLiteral("DeviceGray"));
            }
        } else if (token.type == ContentTokenType::Name) {
            const QString name = QString::fromLatin1(token.raw);
            if (isDeviceSpaceName(name)) spaces->insert(name);
        }
    }
}

// 内容流里有没有内联图像用了 LZW。
//
// 为什么连内联图像一起查：内联图像的过滤名写在内容流里，
// 不进任何流字典，只查流字典会漏掉它。
bool contentHasInlineLzw(const QByteArray& content) {
    const ContentParseResult parsed = ContentStream::tokenize(content);
    for (const ContentToken& token : parsed.tokens) {
        if (token.type != ContentTokenType::InlineImage) continue;
        // 内联图像 token 从 BI 到 EI，缩写表（"/LZW /FlateDecode"）
        // 与内联字典都在里面。
        if (token.raw.contains("LZW")) return true;
    }
    return false;
}

// ----------------------------------------------------------- 透明与注解 ----

// 一页的透明情况。
struct TransparencyScan {
    bool usesTransparency = false;       // 页面用到了透明
    bool hasTransparencyGroup = false;   // 页面有 /Group << /S /Transparency >>
    QStringList softMaskProblems;        // 软掩码 /TR 的问题
    QStringList notes;                   // ASCII 说明
};

// 扫一页的透明情况。
void scanPageTransparency(const QPDFObjectHandle& page, const QPDFObjectHandle& resources,
                          TransparencyScan* scan) {
    const QPDFObjectHandle group = page.getKey("/Group");
    if (group.isDictionary()) {
        const QPDFObjectHandle subtype = group.getKey("/S");
        if (subtype.isName() && subtype.getName() == "/Transparency") {
            scan->hasTransparencyGroup = true;
        }
    }
    if (!resources.isDictionary()) return;
    const QPDFObjectHandle states = resources.getKey("/ExtGState");
    if (!states.isDictionary()) return;
    for (const std::string& key : states.getKeys()) {
        const QPDFObjectHandle state = states.getKey(key);
        if (!state.isDictionary()) continue;
        const QPDFObjectHandle fillAlpha = state.getKey("/ca");
        const QPDFObjectHandle strokeAlpha = state.getKey("/CA");
        const bool translucent =
            (fillAlpha.isNumber() && fillAlpha.getNumericValue() < 1.0) ||
            (strokeAlpha.isNumber() && strokeAlpha.getNumericValue() < 1.0);
        if (translucent) {
            scan->usesTransparency = true;
            scan->notes.append(QStringLiteral("extgstate /%1 sets a constant alpha below 1.0")
                                   .arg(resourceName(key)));
        }
        const QPDFObjectHandle softMask = state.getKey("/SMask");
        // /SMask /None 表示「没有软掩码」，不算用到透明。
        // 注意：qpdf 的 isInitialized() 是「句柄指向了一个对象」，
        // 取不到的键给出的是默认句柄（false），所以要先判它有没有取到。
        bool softMaskActive = false;
        if (softMask.isInitialized()) {
            if (softMask.isName()) {
                softMaskActive = softMask.getName() != "/None";
            } else {
                softMaskActive = !softMask.isNull();
            }
        }
        if (softMaskActive) {
            scan->usesTransparency = true;
            scan->notes.append(QStringLiteral("extgstate /%1 uses a soft mask").arg(resourceName(key)));
            // 组软掩码（字典形式）允许 /TR，但只能是 /Identity。
            if (softMask.isDictionary()) {
                const QPDFObjectHandle transfer = softMask.getKey("/TR");
                if (transfer.isName() && transfer.getName() != "/Identity") {
                    scan->softMaskProblems.append(
                        QStringLiteral("extgstate /%1: soft mask /TR is %2, PDF/A requires "
                                       "/Identity")
                            .arg(resourceName(key), nameText(transfer.getName())));
                }
            }
        }
    }
}

// 一条注解问题：类别 + ASCII 说明。
using AnnotationProblem = QPair<QString, QString>;

constexpr const char* kAnnotForbidden = "forbidden";
constexpr const char* kAnnotAppearance = "appearance";
constexpr const char* kAnnotFlags = "flags";

// PDF/A 禁止的注解类型。
bool isForbiddenAnnotation(const QString& subtype) {
    return subtype == QLatin1String("Movie") || subtype == QLatin1String("Screen") ||
           subtype == QLatin1String("RichMedia") || subtype == QLatin1String("3D") ||
           subtype == QLatin1String("Sound");
}

// 扫一页的注解。
void scanAnnotations(const QPDFObjectHandle& page, int pageIndex,
                     QVector<AnnotationProblem>* problems) {
    const QPDFObjectHandle annots = page.getKey("/Annots");
    if (!annots.isArray()) return;
    for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
        if (!annot.isDictionary()) continue;
        const QPDFObjectHandle subtype = annot.getKey("/Subtype");
        const QString type = subtype.isName() ? nameText(subtype.getName()) : QString();
        const QString where = QStringLiteral("annotation /%1 on page %2")
                                  .arg(type.isEmpty() ? QStringLiteral("?") : type)
                                  .arg(pageIndex + 1);

        if (isForbiddenAnnotation(type)) {
            problems->append(qMakePair(QString::fromLatin1(kAnnotForbidden),
                                       QStringLiteral("%1: annotation type /%2 is forbidden in "
                                                      "PDF/A")
                                           .arg(where, type)));
        }

        // /F 标志：PDF/A-2 6.3.2 要求设上 Print，且不得是隐藏类标志。
        int flags = 0;
        const QPDFObjectHandle flagValue = annot.getKey("/F");
        if (flagValue.isInteger()) flags = static_cast<int>(flagValue.getIntValue());
        if ((flags & 4) == 0) {
            problems->append(qMakePair(QString::fromLatin1(kAnnotFlags),
                                       QStringLiteral("%1: Print flag (4) is not set").arg(where)));
        }
        // 隐藏类标志的位不是连续的（1 / 2 / 32 / 512），要按位取。
        struct HiddenFlag {
            int bit;
            const char* label;
        };
        static const HiddenFlag kHiddenFlags[] = {
            {1, "Invisible(1)"}, {2, "Hidden(2)"}, {32, "NoView(32)"}, {512, "ToggleNoView(512)"},
        };
        QStringList hit;
        for (const HiddenFlag& hidden : kHiddenFlags) {
            if ((flags & hidden.bit) != 0) hit.append(QString::fromLatin1(hidden.label));
        }
        if (!hit.isEmpty()) {
            problems->append(qMakePair(QString::fromLatin1(kAnnotFlags),
                                       QStringLiteral("%1: hidden flag(s) set: %2")
                                           .arg(where, hit.join(QStringLiteral(", ")))));
        }

        // 外观流：除 Popup 与 Link 外都要有 /AP。
        if (type == QLatin1String("Popup") || type == QLatin1String("Link")) continue;
        const QPDFObjectHandle rect = annot.getKey("/Rect");
        double width = 0.0;
        double height = 0.0;
        if (rect.isArray()) {
            const std::vector<QPDFObjectHandle> items = rect.getArrayAsVector();
            if (items.size() == 4 && items.at(0).isNumber() && items.at(1).isNumber() &&
                items.at(2).isNumber() && items.at(3).isNumber()) {
                width = items.at(2).getNumericValue() - items.at(0).getNumericValue();
                height = items.at(3).getNumericValue() - items.at(1).getNumericValue();
            }
        }
        // 面积为零的矩形是「不可见注解」，PDF/A 不要求它有外观流。
        if (width * height <= 0.0) continue;
        if (!annot.hasKey("/AP")) {
            problems->append(qMakePair(QString::fromLatin1(kAnnotAppearance),
                                       QStringLiteral("%1: no /AP appearance stream").arg(where)));
        }
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QString PdfAPreflightReport::summary() const {
    return QStringLiteral("PDF/A-2b preflight: %1 checks, %2 errors, %3 warnings, %4 passed "
                          "(preflight only, not a conformance certification)")
        .arg(findings.size())
        .arg(errorCount)
        .arg(warningCount)
        .arg(passCount);
}

QString pdfAPreflightDisclaimer() {
    return QStringLiteral(
        "PDF/A-2b preflight only: it screens the PDF/A requirements that can be read from the "
        "file structure. A clean preflight is not a conformance certification - veraPDF (a Java "
        "tool) is required for that.");
}

PdfAPreflightReport checkPdfAPreflight(const QByteArray& pdfBytes, QString* error) {
    PdfAPreflightReport report;

    // 失败也要给一份看得懂的报告：调用方只看 finding 就够，
    // 但报告里保留一条 Error 结论，界面上不必另外拼文案。
    const auto fail = [&report, error](const QString& reason, const QString& code) {
        report = PdfAPreflightReport();
        PdfAPreflightFinding finding;
        finding.code = code;
        finding.clause = QStringLiteral("6.1.2");
        finding.title = QStringLiteral("Preflight cannot run on this file");
        finding.detail = reason;
        finding.suggestion = QStringLiteral("Open a readable PDF and run the preflight again.");
        finding.severity = PdfAPreflightSeverity::Error;
        report.findings.append(finding);
        report.errorCount = 1;
        report.warningCount = 0;
        report.passCount = 0;
        if (error != nullptr) *error = reason;
        return report;
    };

    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("empty input: there are no PDF bytes to inspect"),
                    QStringLiteral("a-empty-document"));
    }
    // 先看文件头：不是 PDF 就别让解析器去猜（解析器对带前导垃圾的 PDF 很宽容，
    // 会把它「修好」再返回，那不是我们要的检查结果）。
    const int headerLimit = static_cast<int>(qMin<qsizetype>(pdfBytes.size(), 1024));
    if (!pdfBytes.left(headerLimit).contains("%PDF-")) {
        return fail(QStringLiteral("no %PDF- header in the first 1024 bytes"),
                    QStringLiteral("a-not-a-pdf"));
    }

#ifndef AIPDF_WITH_QPDF
    return fail(QStringLiteral("this build has no qpdf, PDF/A preflight is unavailable"),
                QStringLiteral("a-qpdf-unavailable"));
#else
    try {
        QPDF pdf;
        // qpdf 的警告不往 stderr 上倒：这里要的是自己的结论，不是解析器的抱怨。
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("pdf-a-preflight.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));

        const QPDFObjectHandle root = pdf.getRoot();
        if (!root.isDictionary()) {
            return fail(QStringLiteral("the document catalog is not a dictionary (file may be "
                                       "damaged)"),
                        QStringLiteral("a-unreadable-document"));
        }

        const auto add = [&report](const QString& code, const QString& clause, const QString& title,
                                   const QString& detail, const QString& suggestion,
                                   PdfAPreflightSeverity severity) {
            PdfAPreflightFinding finding;
            finding.code = code;
            finding.clause = clause;
            finding.title = title;
            finding.detail = detail;
            finding.suggestion = suggestion;
            finding.severity = severity;
            report.findings.append(finding);
        };

        // 故意不要加 const：getPageContents() 与 QPDFPageObjectHelper 都要可写句柄。
        std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

        // ------------------------------------------------ 1. 文件格式 ----
        const QString headerVersion = pdfHeaderVersion(pdfBytes);
        if (headerVersion == QLatin1String("1.7")) {
            add(QStringLiteral("a-pdf-version-ok"), QStringLiteral("6.1.2"),
                QStringLiteral("File header is %PDF-1.7"),
                QStringLiteral("the file header declares %PDF-1.7, the version PDF/A-2 is based "
                               "on"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-pdf-version-unsupported"), QStringLiteral("6.1.2"),
                QStringLiteral("File header is not %PDF-1.7"),
                QStringLiteral("the file header declares %PDF-%1; PDF/A-2 is based on PDF 1.7")
                    .arg(headerVersion.isEmpty() ? QStringLiteral("(unreadable)") : headerVersion),
                QStringLiteral("re-export the file as PDF 1.7 with a PDF/A-2 profile"),
                PdfAPreflightSeverity::Error);
        }

        // ------------------------------------------------ 2. 文件标识 ----
        const QPDFObjectHandle fileId = pdf.getTrailer().getKey("/ID");
        if (fileId.isArray() && fileId.getArrayAsVector().size() == 2) {
            add(QStringLiteral("a-file-id-ok"), QStringLiteral("6.1.3"),
                QStringLiteral("Trailer has a file identifier"),
                QStringLiteral("the trailer carries an /ID entry with two strings"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-missing-file-id"), QStringLiteral("6.1.3"),
                QStringLiteral("Trailer has no file identifier"),
                QStringLiteral("the trailer has no /ID entry with two strings; PDF/A requires a "
                               "file identifier"),
                QStringLiteral("write an /ID entry with two 16-byte strings into the trailer"),
                PdfAPreflightSeverity::Error);
        }

        // -------------------------------------------------- 3. 加密 ----
        if (pdf.isEncrypted()) {
            add(QStringLiteral("a-document-encrypted"), QStringLiteral("6.1.3"),
                QStringLiteral("Document is encrypted"),
                QStringLiteral("the document uses the standard security handler; PDF/A forbids "
                               "any encryption, including owner-password-only restrictions"),
                QStringLiteral("remove the encryption and write a plain archive copy"),
                PdfAPreflightSeverity::Error);
        } else {
            add(QStringLiteral("a-document-not-encrypted"), QStringLiteral("6.1.3"),
                QStringLiteral("Document is not encrypted"),
                QStringLiteral("the document is not encrypted"),
                QString(), PdfAPreflightSeverity::Pass);
        }

        // -------------------------------------------------- 4. XMP ----
        const QPDFObjectHandle metadata = root.getKey("/Metadata");
        if (!metadata.isStream()) {
            add(QStringLiteral("a-missing-xmp-metadata"), QStringLiteral("6.6.2.1"),
                QStringLiteral("No XMP metadata"),
                QStringLiteral("the document catalog has no /Metadata stream; PDF/A-2 requires XMP "
                               "metadata"),
                QStringLiteral("add an XMP metadata stream to the document catalog"),
                PdfAPreflightSeverity::Error);
        } else {
            const QPDFObjectHandle metadataDict = metadata.getDict();
            const QPDFObjectHandle subtype = metadataDict.getKey("/Subtype");
            const bool isXml = subtype.isName() && subtype.getName() == "/XML";
            const QByteArray xmp = streamDataOf(metadata);
            if (!isXml) {
                add(QStringLiteral("a-xmp-metadata-invalid"), QStringLiteral("6.6.2.1"),
                    QStringLiteral("Metadata stream is not XML"),
                    QStringLiteral("the /Metadata stream has no /Subtype /XML"),
                    QStringLiteral("set /Type /Metadata and /Subtype /XML on the metadata stream"),
                    PdfAPreflightSeverity::Error);
            } else if (xmp.isEmpty()) {
                add(QStringLiteral("a-xmp-metadata-invalid"), QStringLiteral("6.6.2.1"),
                    QStringLiteral("Metadata stream is empty"),
                    QStringLiteral("the /Metadata stream has no readable content"),
                    QStringLiteral("write a well-formed XMP packet into the metadata stream"),
                    PdfAPreflightSeverity::Error);
            } else {
                const QString part = xmpAttribute(xmp, "pdfaid:part");
                const QString conformance = xmpAttribute(xmp, "pdfaid:conformance");
                report.declaredPart = part;
                report.declaredConformance = conformance;
                if (part.isEmpty() && conformance.isEmpty()) {
                    add(QStringLiteral("a-xmp-missing-pdfaid"), QStringLiteral("6.6.2.3"),
                        QStringLiteral("XMP does not declare PDF/A"),
                        QStringLiteral("the XMP packet has no pdfaid:part and no "
                                       "pdfaid:conformance attribute"),
                        QStringLiteral("add pdfaid:part=\"2\" and pdfaid:conformance=\"B\" to the "
                                       "XMP packet"),
                        PdfAPreflightSeverity::Error);
                } else if (part != QLatin1String("2")) {
                    add(QStringLiteral("a-xmp-wrong-part"), QStringLiteral("6.6.2.3"),
                        QStringLiteral("XMP declares the wrong PDF/A part"),
                        QStringLiteral("the XMP packet declares pdfaid:part=\"%1\"; this preflight "
                                       "targets PDF/A-2 (part 2)")
                            .arg(part.isEmpty() ? QStringLiteral("(missing)") : part),
                        QStringLiteral("declare pdfaid:part=\"2\" in the XMP packet"),
                        PdfAPreflightSeverity::Error);
                } else if (conformance.compare(QLatin1String("B"), Qt::CaseInsensitive) != 0) {
                    add(QStringLiteral("a-xmp-wrong-conformance"), QStringLiteral("6.6.2.3"),
                        QStringLiteral("XMP declares the wrong conformance level"),
                        QStringLiteral("the XMP packet declares pdfaid:conformance=\"%1\"; this "
                                       "preflight targets PDF/A-2b (conformance B)")
                            .arg(conformance.isEmpty() ? QStringLiteral("(missing)") : conformance),
                        QStringLiteral("declare pdfaid:conformance=\"B\" in the XMP packet"),
                        PdfAPreflightSeverity::Error);
                } else {
                    add(QStringLiteral("a-xmp-pdfaid-ok"), QStringLiteral("6.6.2.3"),
                        QStringLiteral("XMP declares PDF/A-2b"),
                        QStringLiteral("the XMP packet declares pdfaid:part=\"%1\" and "
                                       "pdfaid:conformance=\"%2\"")
                            .arg(part, conformance),
                        QString(), PdfAPreflightSeverity::Pass);
                }
            }
        }

        // ------------------------------------------ 5. OutputIntent ----
        bool hasOutputIntent = false;
        QString intentDataSpace;
        const QPDFObjectHandle intents = root.getKey("/OutputIntents");
        if (!intents.isArray()) {
            add(QStringLiteral("a-missing-output-intent"), QStringLiteral("6.2.3"),
                QStringLiteral("No output intent"),
                QStringLiteral("the document catalog has no /OutputIntents array; PDF/A-2 requires "
                               "an output intent with /S /GTS_PDFA2"),
                QStringLiteral("add an /OutputIntents array with a /GTS_PDFA2 output intent and an "
                               "embedded /DestOutputProfile"),
                PdfAPreflightSeverity::Error);
        } else {
            QPDFObjectHandle pdfaIntent;
            QStringList foundSubtypes;
            for (const QPDFObjectHandle& item : intents.getArrayAsVector()) {
                if (!item.isDictionary()) continue;
                const QPDFObjectHandle subtype = item.getKey("/S");
                const QString name = subtype.isName() ? nameText(subtype.getName()) : QString();
                foundSubtypes.append(name.isEmpty() ? QStringLiteral("(no /S)") : name);
                if (name == QLatin1String("GTS_PDFA2") && !pdfaIntent.isInitialized()) {
                    pdfaIntent = item;
                }
            }
            // 注意：qpdf 的 isInitialized() 是「句柄指向了一个对象」，
            // 默认构造的句柄是 false，所以「还没找到」要写成 !isInitialized()。
            if (!pdfaIntent.isInitialized()) {
                add(QStringLiteral("a-output-intent-wrong-subtype"), QStringLiteral("6.2.3"),
                    QStringLiteral("No PDF/A-2 output intent"),
                    QStringLiteral("the /OutputIntents array has no entry with /S /GTS_PDFA2 "
                                   "(found: %1)")
                        .arg(foundSubtypes.join(QStringLiteral(", "))),
                    QStringLiteral("add an output intent with /S /GTS_PDFA2 and an embedded "
                                   "/DestOutputProfile"),
                    PdfAPreflightSeverity::Error);
            } else {
                hasOutputIntent = true;
                const QPDFObjectHandle profile = pdfaIntent.getKey("/DestOutputProfile");
                if (!profile.isStream()) {
                    add(QStringLiteral("a-output-intent-icc-missing"), QStringLiteral("6.2.3"),
                        QStringLiteral("Output intent has no embedded ICC profile"),
                        QStringLiteral("the /GTS_PDFA2 output intent has no /DestOutputProfile ICC "
                                       "stream; PDF/A requires the colour profile to be embedded "
                                       "in the file"),
                        QStringLiteral("embed an ICC profile as /DestOutputProfile of the output "
                                       "intent"),
                        PdfAPreflightSeverity::Error);
                } else {
                    const IccHeader header = readIccHeader(streamDataOf(profile));
                    if (!header.ok) {
                        add(QStringLiteral("a-output-intent-icc-invalid"), QStringLiteral("6.2.4.2"),
                            QStringLiteral("Embedded ICC profile is not usable for PDF/A"),
                            QStringLiteral("the /DestOutputProfile stream does not satisfy the "
                                           "PDF/A profile requirements: %1")
                                .arg(header.problems.join(QStringLiteral("; "))),
                            QStringLiteral("embed a display (mntr) or output (prtr) profile whose "
                                           "colour space is GRAY, RGB or CMYK and whose version "
                                           "is below 5"),
                            PdfAPreflightSeverity::Error);
                    } else {
                        intentDataSpace = QString::fromLatin1(header.dataSpace);
                        add(QStringLiteral("a-output-intent-ok"), QStringLiteral("6.2.3"),
                            QStringLiteral("Output intent with an embedded ICC profile"),
                            QStringLiteral("the document has a /GTS_PDFA2 output intent with an "
                                           "embedded ICC profile (class %1, colour space %2, "
                                           "version %3)")
                                .arg(QString::fromLatin1(header.deviceClass), intentDataSpace)
                                .arg(header.majorVersion),
                            QString(), PdfAPreflightSeverity::Pass);
                    }
                }
            }
        }

        // -------------------------------------------------- 6. 字体 ----
        QStringList notEmbeddedFonts;
        QSet<int> reportedFonts;
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle resources =
                QPDFPageObjectHelper(pages.at(i)).getAttribute("/Resources", false);
            if (!resources.isDictionary()) continue;
            const QPDFObjectHandle fontDict = resources.getKey("/Font");
            if (!fontDict.isDictionary()) continue;
            for (const std::string& key : fontDict.getKeys()) {
                const QPDFObjectHandle font = fontDict.getKey(key);
                const int objectId = objectIdOf(font);
                if (objectId >= 0 && reportedFonts.contains(objectId)) continue;
                if (objectId >= 0) reportedFonts.insert(objectId);
                if (fontIsEmbedded(font)) continue;
                notEmbeddedFonts.append(QStringLiteral("%1 (resource /%2, page %3)")
                                             .arg(baseFontName(font), resourceName(key))
                                             .arg(static_cast<int>(i) + 1));
            }
        }
        if (notEmbeddedFonts.isEmpty()) {
            add(QStringLiteral("a-fonts-embedded"), QStringLiteral("6.2.11"),
                QStringLiteral("All fonts are embedded"),
                QStringLiteral("every font used by a page carries a font program"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-font-not-embedded"), QStringLiteral("6.2.11"),
                QStringLiteral("Some fonts are not embedded"),
                QStringLiteral("these fonts have no font program: %1")
                    .arg(notEmbeddedFonts.join(QStringLiteral(", "))),
                QStringLiteral("export with font embedding switched on, or convert the text to "
                               "outlines"),
                PdfAPreflightSeverity::Error);
        }

        // --------------------------------- 7. 过滤 / 外部引用 / JS ----
        QStringList lzwObjects;
        QStringList externalReferences;
        QStringList javascriptObjects;
        for (const QPDFObjectHandle& object : pdf.getAllObjects()) {
            const bool isStream = object.isStream();
            if (!isStream && !object.isDictionary()) continue;
            const QPDFObjectHandle dict = dictOf(object);
            if (!dict.isDictionary()) continue;
            const int objectId = objectIdOf(object);
            const QString where = objectId >= 0 ? QStringLiteral("object %1").arg(objectId)
                                                : QStringLiteral("direct object");
            if (isStream) {
                const QPDFObjectHandle filter = dict.getKey("/Filter");
                QStringList filters;
                if (filter.isName()) {
                    filters.append(QString::fromStdString(filter.getName()));
                } else if (filter.isArray()) {
                    for (const QPDFObjectHandle& item : filter.getArrayAsVector()) {
                        if (item.isName()) filters.append(QString::fromStdString(item.getName()));
                    }
                }
                if (filters.contains("/LZWDecode")) {
                    lzwObjects.append(where);
                }
                if (dict.hasKey("/F") || dict.hasKey("/FFilter") || dict.hasKey("/FDecodeParms")) {
                    externalReferences.append(where);
                }
            }
            const QPDFObjectHandle actionType = dict.getKey("/S");
            if ((actionType.isName() && actionType.getName() == "/JavaScript") ||
                dict.hasKey("/JS")) {
                javascriptObjects.append(where);
            }
        }

        // 内联图像的过滤名写在内容流里，不进流字典，要单独扫。
        for (size_t i = 0; i < pages.size(); ++i) {
            QByteArray content;
            for (QPDFObjectHandle& stream : pages.at(i).getPageContents()) {
                if (!stream.isStream()) continue;
                content.append(streamDataOf(stream));
                content.append('\n');
            }
            if (content.isEmpty()) continue;
            if (contentHasInlineLzw(content)) {
                lzwObjects.append(QStringLiteral("inline image on page %1")
                                      .arg(static_cast<int>(i) + 1));
            }
        }

        if (lzwObjects.isEmpty()) {
            add(QStringLiteral("a-no-lzw-compression"), QStringLiteral("6.1.7"),
                QStringLiteral("No LZW compression"),
                QStringLiteral("no stream or inline image uses LZW compression"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-lzw-compression"), QStringLiteral("6.1.7"),
                QStringLiteral("LZW compression is used"),
                QStringLiteral("these streams or inline images use LZW compression: %1")
                    .arg(lzwObjects.join(QStringLiteral(", "))),
                QStringLiteral("re-encode the affected data with Flate compression"),
                PdfAPreflightSeverity::Error);
        }

        if (externalReferences.isEmpty()) {
            add(QStringLiteral("a-no-external-file-reference"), QStringLiteral("6.1.7"),
                QStringLiteral("No external file references"),
                QStringLiteral("no stream points at an external file"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-external-file-reference"), QStringLiteral("6.1.7"),
                QStringLiteral("Streams reference external files"),
                QStringLiteral("these streams have an /F, /FFilter or /FDecodeParms entry: %1")
                    .arg(externalReferences.join(QStringLiteral(", "))),
                QStringLiteral("store the referenced data inside the PDF and drop the external "
                               "reference"),
                PdfAPreflightSeverity::Error);
        }

        if (javascriptObjects.isEmpty()) {
            add(QStringLiteral("a-no-javascript"), QStringLiteral("6.4.4"),
                QStringLiteral("No JavaScript"),
                QStringLiteral("the document has no JavaScript action"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-javascript-action"), QStringLiteral("6.4.4"),
                QStringLiteral("JavaScript is present"),
                QStringLiteral("these objects carry a JavaScript action: %1")
                    .arg(javascriptObjects.join(QStringLiteral(", "))),
                QStringLiteral("remove the JavaScript actions"),
                PdfAPreflightSeverity::Error);
        }

        // --------------------------------------------- 8. 色彩空间 ----
        ColourSpaceScan colourScan;
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle resources =
                QPDFPageObjectHelper(pages.at(i)).getAttribute("/Resources", false);
            scanResources(resources, 0, &colourScan);
            QByteArray content;
            for (QPDFObjectHandle& stream : pages.at(i).getPageContents()) {
                if (!stream.isStream()) continue;
                content.append(streamDataOf(stream));
                content.append('\n');
            }
            if (!content.isEmpty()) collectContentDeviceSpaces(content, &colourScan.deviceSpaces);
        }

        QStringList colourProblemDetails;
        for (const ColourSpaceProblem& problem : colourScan.problems) {
            colourProblemDetails.append(problem.second);
        }
        // 设备色彩空间与 OutputIntent 对不对得上（6.2.4.3）。
        if (hasOutputIntent && !intentDataSpace.isEmpty()) {
            QStringList mismatches;
            const auto covers = [&intentDataSpace](const QString& device) {
                if (intentDataSpace == QLatin1String("RGB ")) {
                    return device == QLatin1String("DeviceRGB") ||
                           device == QLatin1String("DeviceGray");
                }
                if (intentDataSpace == QLatin1String("CMYK")) {
                    return device == QLatin1String("DeviceCMYK") ||
                           device == QLatin1String("DeviceGray");
                }
                if (intentDataSpace == QLatin1String("GRAY")) {
                    return device == QLatin1String("DeviceGray");
                }
                return true;
            };
            for (const QString& device : colourScan.deviceSpaces) {
                if (covers(device)) continue;
                mismatches.append(QStringLiteral("%1 content with a %2 output intent")
                                      .arg(device, intentDataSpace.trimmed()));
            }
            if (!mismatches.isEmpty()) {
                colourProblemDetails.append(
                    QStringLiteral("device colour space does not match the output intent: %1")
                        .arg(mismatches.join(QStringLiteral(", "))));
            }
        }

        if (colourProblemDetails.isEmpty()) {
            add(QStringLiteral("a-colorspaces-ok"), QStringLiteral("6.2.4"),
                QStringLiteral("Colour spaces are usable for PDF/A"),
                QStringLiteral("the document uses device colour spaces or ICC based colour spaces, "
                               "and they match the output intent"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            add(QStringLiteral("a-colorspace-not-allowed"), QStringLiteral("6.2.4"),
                QStringLiteral("Colour spaces are not allowed for PDF/A"),
                QStringLiteral("%1").arg(colourProblemDetails.join(QStringLiteral("; "))),
                QStringLiteral("redefine the colour with an ICC profile or a device colour space "
                               "that matches the output intent"),
                PdfAPreflightSeverity::Error);
        }

        // -------------------------------------------------- 9. 透明 ----
        bool transparencyUsed = false;
        bool transparencyWithoutGroup = false;
        QStringList transparencyNotes;
        QStringList softMaskProblems;
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle resources =
                QPDFPageObjectHelper(pages.at(i)).getAttribute("/Resources", false);
            TransparencyScan pageScan;
            scanPageTransparency(pages.at(i), resources, &pageScan);
            if (!pageScan.usesTransparency) continue;
            transparencyUsed = true;
            for (const QString& note : pageScan.notes) {
                transparencyNotes.append(
                    QStringLiteral("page %1: %2").arg(static_cast<int>(i) + 1).arg(note));
            }
            for (const QString& problem : pageScan.softMaskProblems) {
                softMaskProblems.append(problem);
            }
            if (!pageScan.hasTransparencyGroup) transparencyWithoutGroup = true;
        }

        if (!softMaskProblems.isEmpty()) {
            add(QStringLiteral("a-softmask-tr-not-identity"), QStringLiteral("6.2.10.2"),
                QStringLiteral("Soft mask transfer function is not /Identity"),
                QStringLiteral("%1").arg(softMaskProblems.join(QStringLiteral("; "))),
                QStringLiteral("remove the /TR entry from the soft mask or set it to /Identity"),
                PdfAPreflightSeverity::Error);
        }
        if (!transparencyUsed) {
            add(QStringLiteral("a-no-transparency"), QStringLiteral("6.2.10"),
                QStringLiteral("No transparency is used"),
                QStringLiteral("the document does not use transparency"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            if (!hasOutputIntent) {
                add(QStringLiteral("a-transparency-without-output-intent"), QStringLiteral("6.2.4.3"),
                    QStringLiteral("Transparency without an output intent"),
                    QStringLiteral("the document uses transparency (%1) but has no output intent; "
                                   "PDF/A requires colour to be device independent")
                        .arg(transparencyNotes.join(QStringLiteral("; "))),
                    QStringLiteral("add a /GTS_PDFA2 output intent with an embedded ICC profile"),
                    PdfAPreflightSeverity::Error);
            }
            if (transparencyWithoutGroup) {
                add(QStringLiteral("a-transparency-without-page-group"), QStringLiteral("6.2.10"),
                    QStringLiteral("Transparency without a page transparency group"),
                    QStringLiteral("a page uses transparency but has no /Group << /S "
                                   "/Transparency >> dictionary"),
                    QStringLiteral("add a transparency group to the pages that use transparency"),
                    PdfAPreflightSeverity::Warning);
            }
            if (hasOutputIntent && !transparencyWithoutGroup) {
                add(QStringLiteral("a-transparency-allowed"), QStringLiteral("6.2.10"),
                    QStringLiteral("Transparency is allowed in PDF/A-2"),
                    QStringLiteral("the document uses transparency (%1); PDF/A-2 allows "
                                   "transparency, and the output intent and page transparency "
                                   "group are in place")
                        .arg(transparencyNotes.join(QStringLiteral("; "))),
                    QString(), PdfAPreflightSeverity::Pass);
            }
        }

        // ------------------------------------------------ 10. 注解 ----
        QVector<AnnotationProblem> annotationProblems;
        for (size_t i = 0; i < pages.size(); ++i) {
            scanAnnotations(pages.at(i), static_cast<int>(i), &annotationProblems);
        }
        QStringList forbiddenAnnotations;
        QStringList missingAppearances;
        QStringList flagProblems;
        for (const AnnotationProblem& problem : annotationProblems) {
            if (problem.first == QLatin1String(kAnnotForbidden)) {
                forbiddenAnnotations.append(problem.second);
            } else if (problem.first == QLatin1String(kAnnotAppearance)) {
                missingAppearances.append(problem.second);
            } else {
                flagProblems.append(problem.second);
            }
        }
        if (annotationProblems.isEmpty()) {
            add(QStringLiteral("a-annotations-ok"), QStringLiteral("6.3"),
                QStringLiteral("Annotations satisfy the PDF/A rules"),
                QStringLiteral("the document has no annotation that breaks the PDF/A rules"),
                QString(), PdfAPreflightSeverity::Pass);
        } else {
            if (!forbiddenAnnotations.isEmpty()) {
                add(QStringLiteral("a-forbidden-annotation"), QStringLiteral("6.3"),
                    QStringLiteral("Forbidden annotation type"),
                    QStringLiteral("%1").arg(forbiddenAnnotations.join(QStringLiteral("; "))),
                    QStringLiteral("remove the multimedia and 3D annotations"),
                    PdfAPreflightSeverity::Error);
            }
            if (!missingAppearances.isEmpty()) {
                add(QStringLiteral("a-annotation-missing-appearance"), QStringLiteral("6.3.3"),
                    QStringLiteral("Annotation has no appearance stream"),
                    QStringLiteral("%1").arg(missingAppearances.join(QStringLiteral("; "))),
                    QStringLiteral("give the annotation an /AP appearance stream"),
                    PdfAPreflightSeverity::Error);
            }
            if (!flagProblems.isEmpty()) {
                add(QStringLiteral("a-annotation-flags"), QStringLiteral("6.3.2"),
                    QStringLiteral("Annotation flags are wrong"),
                    QStringLiteral("%1").arg(flagProblems.join(QStringLiteral("; "))),
                    QStringLiteral("set the Print flag and clear Invisible, Hidden, NoView and "
                                   "ToggleNoView"),
                    PdfAPreflightSeverity::Error);
            }
        }

        // 计数统一在最后按结论的实际级别重算一遍：
        // 边加结论边加计数很容易漏一处，重算则永远与结论一致。
        report.errorCount = 0;
        report.warningCount = 0;
        report.passCount = 0;
        for (const PdfAPreflightFinding& finding : report.findings) {
            if (finding.severity == PdfAPreflightSeverity::Error) ++report.errorCount;
            else if (finding.severity == PdfAPreflightSeverity::Warning) ++report.warningCount;
            else ++report.passCount;
        }
        if (error != nullptr) error->clear();
        return report;
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("parsing this PDF failed: %1")
                        .arg(QString::fromUtf8(exception.what())),
                    QStringLiteral("a-unreadable-document"));
    }
#endif
}

}  // namespace aipdf::pdf
