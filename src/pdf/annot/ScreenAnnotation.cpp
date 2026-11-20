#include "pdf/annot/ScreenAnnotation.h"

#include <QtMath>

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

// ============================================================================
// 媒体容器判定
// ============================================================================

// 支持的容器白名单。
//
// 为什么按**文件头**判定而不是看扩展名：扩展名是可以随便改的，
// 「clip.mp4」里装一段文本照样叫 .mp4；文件头里写着标志的才是真的那种格式。
enum class MediaContainer {
    Unknown,
    Mp4,  // ISO BMFF（MP4 / MOV 家族）
    Avi,  // RIFF AVI
    Wav,  // RIFF WAVE
};

// 4 字节标签比较。
bool tagEquals(const char* data, const char* tag) {
    return data[0] == tag[0] && data[1] == tag[1] && data[2] == tag[2] && data[3] == tag[3];
}

// 按文件头认出容器；认不出来就是 Unknown。
//
// 越界安全：每读一个标签之前都先确认长度够——
//   * ISO BMFF 的盒类型在偏移 4..7，所以至少要 8 字节；
//   * RIFF 的形式类型在偏移 8..11，所以至少要 12 字节。
// 截断过的文件头（比如只有 'RIFF' 四个字节）一律判失败，绝不越界读。
MediaContainer detectContainer(const QByteArray& bytes) {
    const char* data = bytes.constData();
    const qsizetype size = bytes.size();
    // ISO BMFF：偏移 0..3 是盒长度，偏移 4..7 是盒类型。
    // 'ftyp' 是普通 MP4；分片式 MP4 没有 ftyp，第一盒是 'moov' 或 'moof'。
    if (size >= 8) {
        const char* type = data + 4;
        if (tagEquals(type, "ftyp") || tagEquals(type, "moov") || tagEquals(type, "moof")) {
            return MediaContainer::Mp4;
        }
    }
    // RIFF：'RIFF' + 4 字节长度 + 4 字节形式类型（偏移 8..11）。
    if (size >= 12 && tagEquals(data, "RIFF")) {
        const char* form = data + 8;
        if (tagEquals(form, "AVI ")) return MediaContainer::Avi;
        if (tagEquals(form, "WAVE")) return MediaContainer::Wav;
    }
    return MediaContainer::Unknown;
}

// 容器 → MIME 类型（"video/mp4" 这种写法，不带前导斜杠）。
//
// 为什么 MIME 由容器推出、而不是让调用方传：调用方传的 MIME 可以和文件头
// 对不上（「clip.mp4」里装着一段 AVI），写出去就成了「结构对、内容错」。
// 同一个值落在嵌入流的 /Subtype 与 MediaClip 的 /M 上；filespec 上不写 MIME。
QString mimeForContainer(MediaContainer container) {
    switch (container) {
        case MediaContainer::Mp4:
            return QStringLiteral("video/mp4");
        case MediaContainer::Avi:
            return QStringLiteral("video/x-msvideo");
        case MediaContainer::Wav:
            return QStringLiteral("audio/wav");
        case MediaContainer::Unknown:
            break;
    }
    return QString();
}

// 容器 → 默认文件名后缀（文件名留空时用）。
QString extensionForContainer(MediaContainer container) {
    switch (container) {
        case MediaContainer::Mp4:
            return QStringLiteral("mp4");
        case MediaContainer::Avi:
            return QStringLiteral("avi");
        case MediaContainer::Wav:
            return QStringLiteral("wav");
        case MediaContainer::Unknown:
            break;
    }
    return QString();
}

// 支持的容器清单（写进错误信息里，让人知道该换成什么）。
QString supportedContainersText() {
    return QStringLiteral("MP4（ISO BMFF）、AVI（RIFF AVI）、WAV（RIFF WAVE）");
}

#ifdef AIPDF_WITH_QPDF

// ============================================================================
// 对象读写的小工具
// ============================================================================

// 名字对象的值（带前导斜杠，例如 "/Screen"）。不是名字就返回空串。
QString nameOf(const QPDFObjectHandle& handle) {
    if (!handle.isName()) return QString();
    return QString::fromStdString(handle.getName());
}

// 文本串的值（UTF-8）。不是文本串就返回空串。
QString textOf(const QPDFObjectHandle& handle) {
    if (!handle.isString()) return QString();
    return QString::fromUtf8(handle.getUTF8Value().c_str());
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
// qpdf 的 newReal 收的是字符串而不是 double，所以数字的格式化放在这里做。
QPDFObjectHandle newRealValue(double value) {
    return QPDFObjectHandle::newReal(QString::number(value, 'f', 4).toStdString());
}

// 写一个矩形（PDF user space）。
//
// PDF 的 /Rect 是 [llx lly urx ury]，第二个数是矩形的**下边**；
// QRectF 的 y 在本仓库里本来就是下边（见 ScreenMediaSpec::rect 的说明），
// 所以这里不用再翻轴，写出去再读回来是同一个矩形。
QPDFObjectHandle newRect(const QRectF& rect) {
    std::vector<QPDFObjectHandle> items;
    items.push_back(newRealValue(rect.left()));
    items.push_back(newRealValue(rect.top()));
    items.push_back(newRealValue(rect.right()));
    items.push_back(newRealValue(rect.bottom()));
    return QPDFObjectHandle::newArray(items);
}

// 读一个矩形（PDF user space），装成 y 是下边的 QRectF。
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

// 两个矩形是不是同一个（容差 0.01，够盖住写文件时的四舍五入）。
bool sameRect(const QRectF& left, const QRectF& right) {
    return qAbs(left.left() - right.left()) < 0.01 &&
           qAbs(left.top() - right.top()) < 0.01 &&
           qAbs(left.width() - right.width()) < 0.01 &&
           qAbs(left.height() - right.height()) < 0.01;
}

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
QPDFObjectHandle streamKey(const QPDFObjectHandle& stream, const char* key) {
    if (!stream.isStream()) return QPDFObjectHandle();
    const QPDFObjectHandle dictionary = stream.getDict();
    if (!dictionary.isDictionary()) return QPDFObjectHandle();
    return dictionary.getKey(key);
}

// 流里的字节数。
qint64 streamSize(QPDFObjectHandle stream) {
    if (!stream.isStream()) return 0;
    // getStreamData 不是 const 方法：这里按值收下一份可改的副本。
    const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
    if (!data) return 0;
    return static_cast<qint64>(data->getSize());
}

// 流里的字节本体。
QByteArray streamBytes(QPDFObjectHandle stream) {
    if (!stream.isStream()) return {};
    const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
    if (!data) return {};
    return QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                      static_cast<qsizetype>(data->getSize()));
}

// PDF 名字对象 → MIME 类型（"video/mp4"，不带前导斜杠）。
//
// qpdf 的 getName() 返回的是**未转义**形式：文件里写的 /video#2fmp4
// 读回来就是 /video/mp4，所以这里不用再自己解转义。
QString mimeFromPdfName(const QString& name) {
    QString value = name;
    if (value.startsWith(QLatin1Char('/'))) value = value.mid(1);
    value.replace(QStringLiteral("#2f"), QStringLiteral("/"));
    value.replace(QStringLiteral("#2F"), QStringLiteral("/"));
    return value;
}

// UTF-16BE + BOM 的字节串。
//
// 为什么不用 newUnicodeString：那个函数对纯 ASCII 会写成字面串 "(clip.mp4)"，
// 而 /UF 这一项要的就是「统一的 Unicode 形式」（BOM + UTF-16BE），
// 这样中文文件名在别的阅读器里也不会变成乱码。
QByteArray utf16BeWithBom(const QString& text) {
    const ushort* units = text.utf16();  // UTF-16 码元
    const int count = text.size();      // 码元个数（代理项算两个）
    QByteArray out("\xFE\xFF", 2);
    for (int i = 0; i < count; ++i) {
        const quint16 unit = units[i];
        out.append(static_cast<char>((unit >> 8) & 0xFF));  // 高字节在前
        out.append(static_cast<char>(unit & 0xFF));
    }
    return out;
}

// 写一个 UTF-16BE 文本串。
QPDFObjectHandle newUtf16BeString(const QString& text) {
    const QByteArray bytes = utf16BeWithBom(text);
    return QPDFObjectHandle::newString(
        std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
}

// 把注解追加到页面的 /Annots 里。
void appendAnnotation(QPDFObjectHandle& page, const QPDFObjectHandle& annotation) {
    std::vector<QPDFObjectHandle> annots;
    const QPDFObjectHandle existing = page.getKey("/Annots");
    if (existing.isArray()) annots = existing.getArrayAsVector();
    annots.push_back(annotation);
    page.replaceKey("/Annots", QPDFObjectHandle::newArray(annots));
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

// ============================================================================
// /Assets 名字树
// ============================================================================

// 名字树里的一项：键（文本串）与它指向的对象。
struct AssetEntry {
    QString key;
    QPDFObjectHandle value;
};

// 把 catalog 的 /Assets 名字树摊平成一张表。
//
// 为什么要摊平：写进去时要查「有没有同名」、要决定加在哪儿；
// 摊平之后这两件事都是一次线性查找。
// 原来的树节点如果是 /Kids 分片的，摊平后写回成一个扁平的 /Names 节点——
// 名字树的语义（键 → 值）不变，只是不再分片。
QVector<AssetEntry> collectAssetEntries(const QPDFObjectHandle& node, int depth) {
    QVector<AssetEntry> entries;
    if (!node.isDictionary() || depth > 32) return entries;
    const QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        const std::vector<QPDFObjectHandle> items = names.getArrayAsVector();
        for (size_t i = 0; i + 1 < items.size(); i += 2) {
            AssetEntry entry;
            entry.key = textOf(items.at(i));
            entry.value = items.at(i + 1);
            entries.append(entry);
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            entries.append(collectAssetEntries(kid, depth + 1));
        }
    }
    return entries;
}

// 名字树里还没被用过的名字。
//
// 为什么要加序号：文档里已经有同名媒体时，两个片段会指向同一个键，
// 后写的就把先写的盖掉了。宁可换个名字并存，也不要静默覆盖。
QString uniqueAssetName(const QVector<AssetEntry>& entries, const QString& desired) {
    const QString base = desired.trimmed().isEmpty() ? QStringLiteral("clip.mp4") : desired.trimmed();
    QString candidate = base;
    int counter = 2;
    while (true) {
        bool taken = false;
        for (const AssetEntry& entry : entries) {
            if (entry.key == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken) return candidate;
        // 序号插在扩展名之前："clip.mp4" → "clip (2).mp4"
        const int dot = base.lastIndexOf(QLatin1Char('.'));
        const QString stem = dot > 0 ? base.left(dot) : base;
        const QString suffix = dot > 0 ? base.mid(dot) : QString();
        candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(counter).arg(suffix);
        ++counter;
    }
}

// 按名字从 /Assets 名字树里找到对应的对象（这里是 MediaClip）。
//
// 找不到时返回空对象；index（可空）里写它在名字树里的序号，找不到是 -1。
QPDFObjectHandle findAsset(const QPDFObjectHandle& catalog, const QString& key, int* index) {
    const QVector<AssetEntry> entries = collectAssetEntries(catalog.getKey("/Assets"), 0);
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).key == key) {
            if (index != nullptr) *index = i;
            return entries.at(i).value;
        }
    }
    if (index != nullptr) *index = -1;
    return QPDFObjectHandle();
}

// MediaClip → 文件说明 → 嵌入文件流。
QPDFObjectHandle clipStream(const QPDFObjectHandle& clip) {
    if (!clip.isDictionary()) return QPDFObjectHandle();
    const QPDFObjectHandle filespec = clip.getKey("/D");
    if (!filespec.isDictionary()) return QPDFObjectHandle();
    const QPDFObjectHandle embedded = filespec.getKey("/EF");
    if (!embedded.isDictionary()) return QPDFObjectHandle();
    const QPDFObjectHandle stream = embedded.getKey("/F");
    if (!stream.isStream()) return QPDFObjectHandle();
    return stream;
}

// ============================================================================
// 写完之后的自检
// ============================================================================

// 重新打开写出的字节，确认两件事：注解真的落在目标页上，媒体字节一个没变。
//
// 为什么要有这一步：qpdf 的写盘是「写出即成功」，但对象图里少挂了一个引用
// 这类错误它不会报错——文件能打开，内容却少了。自检把这类静默失败挡住。
bool verifyWritten(const QByteArray& written, const ScreenMediaSpec& spec,
                   const QString& assetName, QString* error) {
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("screen-verify.pdf", written.constData(),
                          static_cast<size_t>(written.size()));
    const QPDFObjectHandle catalog = pdf.getRoot();
    const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
    if (spec.pageIndex < 0 || spec.pageIndex >= static_cast<int>(pages.size())) {
        setError(error, QStringLiteral("自检失败：写出的文档里没有第 %1 页")
                       .arg(spec.pageIndex + 1));
        return false;
    }
    const QPDFObjectHandle page = pages.at(static_cast<size_t>(spec.pageIndex));
    const QPDFObjectHandle annots = page.getKey("/Annots");
    if (!annots.isArray()) {
        setError(error, QStringLiteral("自检失败：写出的第 %1 页上没有注解数组")
                       .arg(spec.pageIndex + 1));
        return false;
    }
    bool found = false;
    for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
        if (!annot.isDictionary()) continue;
        if (nameOf(annot.getKey("/Subtype")) != QStringLiteral("/Screen")) continue;
        if (!sameRect(rectOf(annot.getKey("/Rect")), spec.rect)) continue;
        found = true;
        break;
    }
    if (!found) {
        setError(error, QStringLiteral("自检失败：写出的第 %1 页上找不到刚写的 /Screen 注解")
                       .arg(spec.pageIndex + 1));
        return false;
    }

    const QPDFObjectHandle clip = findAsset(catalog, assetName, nullptr);
    const QPDFObjectHandle stream = clipStream(clip);
    if (!stream.isStream()) {
        setError(error, QStringLiteral("自检失败：/Assets 名字树里找不到刚写的媒体「%1」")
                       .arg(assetName));
        return false;
    }
    const QByteArray embedded = streamBytes(stream);
    if (embedded != spec.mediaBytes) {
        setError(error, QStringLiteral("自检失败：嵌进去的媒体字节与输入不一致（写进去 %1 字节，读回来 %2 字节）")
                       .arg(spec.mediaBytes.size())
                       .arg(embedded.size()));
        return false;
    }
    return true;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

// ============================================================================
// 写
// ============================================================================

QByteArray writeScreenMediaAnnotation(const QByteArray& input,
                                      const ScreenMediaSpec& spec,
                                      QString* error) {
    if (error != nullptr) error->clear();
    if (input.isEmpty()) {
        setError(error, QStringLiteral("没有可写入的 PDF 内容"));
        return {};
    }
    if (spec.mediaBytes.isEmpty()) {
        setError(error, QStringLiteral("媒体字节为空，没有可写入的内容"));
        return {};
    }
    if (spec.rect.width() <= 0.0 || spec.rect.height() <= 0.0) {
        setError(error, QStringLiteral("页面内播放的矩形宽高必须大于 0"));
        return {};
    }
    // 只放行白名单里的容器：写出一份「结构好看、实际放不出声」的页面没有意义，
    // 不如当场说清楚要换格式还是改用附件。
    // 容器由**文件头**判定：扩展名可以随便改，头上的标志改不了。
    const MediaContainer container = detectContainer(spec.mediaBytes);
    if (container == MediaContainer::Unknown) {
        setError(error, QStringLiteral("页面内播放支持 %1；「%2」的文件头不是其中之一"
                                       "（MP4 看偏移 4 处的 ftyp 盒，AVI / WAV 看 RIFF 头的形式类型），"
                                       "请先转成这三种格式之一，或改用「作为附件」方式")
                       .arg(supportedContainersText(),
                            spec.fileName.trimmed().isEmpty() ? QStringLiteral("这份媒体")
                                                              : spec.fileName.trimmed()));
        return {};
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("本次构建没有启用 qpdf，写不了页面内播放注解"));
    return {};
#else
    try {
        QPDF pdf;
        pdf.processMemoryFile("screen-annotation.pdf", input.constData(),
                              static_cast<size_t>(input.size()));
        QPDFObjectHandle catalog = pdf.getRoot();
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
            setError(error, QStringLiteral("页面内播放的页码超出范围（文档共 %1 页，要写第 %2 页）")
                           .arg(pages.size())
                           .arg(spec.pageIndex + 1));
            return {};
        }
        QPDFObjectHandle page = pages.at(static_cast<size_t>(spec.pageIndex));

        // MIME 由容器决定：调用方不用传，也不会出现「名字说 mp4、内容是 avi」。
        const QString mime = mimeForContainer(container);
        const QString fileName = spec.fileName.trimmed().isEmpty()
                                     ? QStringLiteral("clip.") + extensionForContainer(container)
                                     : spec.fileName.trimmed();

        // ---- 媒体本体：嵌入文件流 ----
        // 字节原样搬运：一个字节都不改——媒体文件能不能播是它自己的事，
        // 这里要做的是「原封不动地装进 PDF」。
        const std::string mediaData(spec.mediaBytes.constData(),
                                    static_cast<size_t>(spec.mediaBytes.size()));
        QPDFObjectHandle file = QPDFObjectHandle::newStream(&pdf, mediaData);
        // 媒体流不压缩：MP4 / AVI 里的视频轨本来就是熵编码过的，再套一层
        // FlateDecode 压不动却要白花 CPU；而「文件里就是原始字节」对播放
        // 与事后核对都更省事（/Params /Size 与实际字节数也就对得上了）。
        file.setFilterOnWrite(false);
        setStreamKey(file, "/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
        // MIME 类型写成名字对象：'/' 必须转义成 #2f，qpdf 会自己做这件事，
        // 所以这里传的是**未转义**的 "/video/mp4" 这类值。
        setStreamKey(file, "/Subtype",
                     QPDFObjectHandle::newName(("/" + mime).toLatin1().constData()));
        QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
        params.replaceKey("/Size",
                          QPDFObjectHandle::newInteger(static_cast<long long>(spec.mediaBytes.size())));
        setStreamKey(file, "/Params", params);
        const QPDFObjectHandle fileRef = pdf.makeIndirectObject(file);

        // ---- 文件说明（Filespec）----
        QPDFObjectHandle filespec = QPDFObjectHandle::newDictionary();
        filespec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
        filespec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(fileName.toUtf8().constData()));
        filespec.replaceKey("/UF", newUtf16BeString(fileName));
        QPDFObjectHandle embeddedFiles = QPDFObjectHandle::newDictionary();
        embeddedFiles.replaceKey("/F", fileRef);
        embeddedFiles.replaceKey("/UF", fileRef);
        filespec.replaceKey("/EF", embeddedFiles);
        const QPDFObjectHandle filespecRef = pdf.makeIndirectObject(filespec);

        // ---- 媒体片段（MediaClip）----
        QPDFObjectHandle clip = QPDFObjectHandle::newDictionary();
        clip.replaceKey("/Type", QPDFObjectHandle::newName("/MediaClip"));
        clip.replaceKey("/S", QPDFObjectHandle::newName("/MCD"));
        clip.replaceKey("/D", filespecRef);
        clip.replaceKey("/M", QPDFObjectHandle::newUnicodeString(mime.toUtf8().constData()));
        QPDFObjectHandle clipHandler = QPDFObjectHandle::newDictionary();
        clipHandler.replaceKey("/S", QPDFObjectHandle::newName("/MR"));
        clipHandler.replaceKey("/D", QPDFObjectHandle::newInteger(0));
        clip.replaceKey("/MH", clipHandler);
        const QPDFObjectHandle clipRef = pdf.makeIndirectObject(clip);

        // ---- /Assets 名字树 ----
        // 名字树的键是文本串（不是 PDF 名字对象），所以中文文件名也放得下。
        // 同名不覆盖：已经有同名就加序号换一个名字，两个片段并存。
        QVector<AssetEntry> assets = collectAssetEntries(catalog.getKey("/Assets"), 0);
        const QString assetName = uniqueAssetName(assets, fileName);
        assets.append(AssetEntry{assetName, clipRef});
        // 名字树的键必须按升序排（PDF 规范对 /Names 数组的要求）。
        std::stable_sort(assets.begin(), assets.end(),
                         [](const AssetEntry& left, const AssetEntry& right) {
                             return left.key < right.key;
                         });
        QPDFObjectHandle assetNames = QPDFObjectHandle::newArray();
        for (const AssetEntry& entry : assets) {
            assetNames.appendItem(
                QPDFObjectHandle::newUnicodeString(entry.key.toUtf8().constData()));
            assetNames.appendItem(entry.value);
        }
        QPDFObjectHandle assetsNode = QPDFObjectHandle::newDictionary();
        assetsNode.replaceKey("/Names", assetNames);
        catalog.replaceKey("/Assets", assetsNode);

        // ---- rendition（放映对象）----
        // /N 用的是**名字树里的键**：读的时候就是按这个名字去找 MediaClip。
        QPDFObjectHandle rendition = QPDFObjectHandle::newDictionary();
        rendition.replaceKey("/Type", QPDFObjectHandle::newName("/Rendition"));
        rendition.replaceKey("/S", QPDFObjectHandle::newName("/MR"));
        rendition.replaceKey("/OP", QPDFObjectHandle::newInteger(0));
        rendition.replaceKey("/N",
                             QPDFObjectHandle::newUnicodeString(assetName.toUtf8().constData()));
        QPDFObjectHandle renditionHandler = QPDFObjectHandle::newDictionary();
        renditionHandler.replaceKey("/S", QPDFObjectHandle::newName("/MR"));
        renditionHandler.replaceKey("/D", QPDFObjectHandle::newInteger(0));
        renditionHandler.replaceKey("/R", QPDFObjectHandle::newName("/C"));
        rendition.replaceKey("/MH", renditionHandler);
        const QPDFObjectHandle renditionRef = pdf.makeIndirectObject(rendition);

        // ---- 动作：播放这个 rendition ----
        QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
        action.replaceKey("/S", QPDFObjectHandle::newName("/Rendition"));
        action.replaceKey("/R", renditionRef);
        action.replaceKey("/OP", QPDFObjectHandle::newInteger(0));

        // ---- 注解 ----
        QPDFObjectHandle annotation = QPDFObjectHandle::newDictionary();
        annotation.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        annotation.replaceKey("/Subtype", QPDFObjectHandle::newName("/Screen"));
        annotation.replaceKey("/Rect", newRect(spec.rect));
        if (page.isIndirect()) annotation.replaceKey("/P", page);
        // /F 4 = Print（可打印）。/Screen 是隐藏型注解，**不写 /AP**：
        // 写一个假外观反而会让阅读器把它当页面占位图画出来。
        annotation.replaceKey("/F", QPDFObjectHandle::newInteger(4));
        annotation.replaceKey("/A", pdf.makeIndirectObject(action));

        appendAnnotation(page, pdf.makeIndirectObject(annotation));

        const QByteArray written = writeOut(pdf, error);
        if (written.isEmpty()) return {};

        if (!verifyWritten(written, spec, assetName, error)) return {};
        return written;
    } catch (const std::exception& failure) {
        setError(error, QStringLiteral("写入页面内播放注解失败：%1")
                       .arg(QString::fromUtf8(failure.what())));
        return {};
    }
#endif
}

// ============================================================================
// 读
// ============================================================================

QVector<ScreenMediaAnnotation> readScreenMediaAnnotations(const QByteArray& input,
                                                          QString* error) {
    QVector<ScreenMediaAnnotation> result;
    if (error != nullptr) error->clear();
    if (input.isEmpty()) {
        setError(error, QStringLiteral("没有可读的内容"));
        return result;
    }
#ifndef AIPDF_WITH_QPDF
    setError(error, QStringLiteral("本次构建没有启用 qpdf，读不了页面内播放注解"));
    return result;
#else
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("screen-read.pdf", input.constData(),
                              static_cast<size_t>(input.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        for (size_t i = 0; i < pages.size(); ++i) {
            const QPDFObjectHandle annots = pages.at(i).getKey("/Annots");
            if (!annots.isArray()) continue;
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                if (!annot.isDictionary()) continue;
                if (nameOf(annot.getKey("/Subtype")) != QStringLiteral("/Screen")) continue;

                ScreenMediaAnnotation info;
                info.pageIndex = static_cast<int>(i);
                info.rect = rectOf(annot.getKey("/Rect"));

                // 注解 → 动作 → rendition → 名字（= /Assets 名字树里的键）
                const QPDFObjectHandle action = annot.getKey("/A");
                const QPDFObjectHandle rendition =
                    action.isDictionary() ? action.getKey("/R") : QPDFObjectHandle();
                const QString assetName =
                    rendition.isDictionary() ? textOf(rendition.getKey("/N")) : QString();
                info.fileName = assetName;

                int clipIndex = -1;
                const QPDFObjectHandle clip = findAsset(catalog, assetName, &clipIndex);
                info.mediaClipId = clipIndex;

                // MediaClip → 文件说明 → 嵌入流
                if (clip.isDictionary()) {
                    const QPDFObjectHandle filespec = clip.getKey("/D");
                    if (filespec.isDictionary()) {
                        const QString name = textOf(filespec.getKey("/F"));
                        if (!name.isEmpty()) info.fileName = name;
                    }
                    const QString declared = textOf(clip.getKey("/M"));
                    if (!declared.isEmpty()) info.mimeType = declared;
                }
                const QPDFObjectHandle stream = clipStream(clip);
                if (stream.isStream()) {
                    info.mediaSize = streamSize(stream);
                    const QString subtype = nameOf(streamKey(stream, "/Subtype"));
                    if (!subtype.isEmpty()) info.mimeType = mimeFromPdfName(subtype);
                }
                result.append(info);
            }
        }
        return result;
    } catch (const std::exception& failure) {
        setError(error, QStringLiteral("读取页面内播放注解失败：%1")
                       .arg(QString::fromUtf8(failure.what())));
        return {};
    }
#endif
}

}  // namespace aipdf::pdf
