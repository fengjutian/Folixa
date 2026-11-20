#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/kernel/DocumentOpen.h"
#include "pdf/kernel/MimeTypes.h"
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QtMath>
#include <QUrl>
#include <functional>

#include "i18n/Translate.h"

#include <fpdf_flatten.h>
#include <fpdf_formfill.h>

#include <fpdf_annot.h>
#include <fpdf_attachment.h>
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <QByteArray>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>

namespace aipdf::pdf {

namespace detail {

// PDFium 的接口**不是线程安全的**（见 fpdfview.h 顶部的说明）：
// 即使每个线程各自持有独立句柄，也必须保证同一时刻只有一个 PDFium 调用在执行，
// 否则会踩坏 PDFium 内部的共享状态——本项目踩到过：三个线程各持一份文档时，
// 表现为随机堆损坏，崩在 RtlAllocateHeap 上，堆损坏点却早得多。
//
// 因此所有 PDFium 调用都串到这一把进程级锁上。
inline std::recursive_mutex& pdfiumMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

// 递归锁：render() 内部还会调用 widthPoints() 等同样要上锁的函数，
// 用非递归锁会自锁死。
class PdfiumGuard {
public:
    PdfiumGuard() { pdfiumMutex().lock(); }
    ~PdfiumGuard() { pdfiumMutex().unlock(); }

    PdfiumGuard(const PdfiumGuard&) = delete;
    PdfiumGuard& operator=(const PdfiumGuard&) = delete;
};

struct DocumentImpl {
    // 文档字节的来源：按路径打开时文件留在磁盘上，PDFium 按需读块；
    // 从内存字节打开时整份字节由调用方持有（QByteArray 隐式共享）。
    std::shared_ptr<DocumentReader> reader;
    QString path;
    // 重新打开文档时要用（isolatedCopy），PDFium 不提供查询口令的接口。
    QString password;
    FPDF_FILEACCESS access{};
    FPDF_DOCUMENT handle = nullptr;
    // 编辑文字时复用已加载字体，避免每替换一行就在 PDF 中重复嵌入整套字体。
    FPDF_FONT latinEditingFont = nullptr;
    FPDF_FONT unicodeEditingFont = nullptr;
    QHash<QString, FPDF_FONT> editingFonts;
    // 表单填充环境：懒创建，关闭文档前必须先退出。
    FPDF_FORMHANDLE form = nullptr;
    // XFA 标记的判定结果：算一次就记住，别每次调用都重扫文件。
    mutable bool xfaChecked = false;
    mutable bool xfaPresent = false;

    ~DocumentImpl() {
        if (handle != nullptr) {
            // 关闭句柄也要上锁：PDFium 的全局状态是共享的。
            const detail::PdfiumGuard guard;
            if (latinEditingFont != nullptr) FPDFFont_Close(latinEditingFont);
            if (unicodeEditingFont != nullptr) FPDFFont_Close(unicodeEditingFont);
            for (FPDF_FONT font : std::as_const(editingFonts)) FPDFFont_Close(font);
            // 不调用 FPDFDOC_ExitFormFillEnvironment：
            // 本仓库固定的 PDFium 构建在退出表单环境时会 0xC0000005 崩溃
            // （与我们的自定义 FPDF_FILEACCESS 文档组合复现）。泄漏只有
            // 每个文档一个环境对象，可接受；等 PDFium 升级后再恢复退出。
            FPDF_CloseDocument(handle);
        }
    }

    // 没有表单也要能建环境：PDFium 用它统一管理可填写字段。
    FPDF_FORMHANDLE ensureForm() {
        if (form != nullptr || handle == nullptr) {
            return form;
        }
        FPDF_FORMFILLINFO info{};
        info.version = 2;
        form = FPDFDOC_InitFormFillEnvironment(handle, &info);
        return form;
    }

    static int readBlock(void* param,
                         unsigned long position,
                         unsigned char* buffer,
                         unsigned long size) {
        auto* self = static_cast<DocumentImpl*>(param);
        if (self == nullptr || buffer == nullptr || !self->reader) {
            return 0;
        }
        // 越界判断交给 DocumentReader：按路径打开的文档内存里没有整份字节，
        // 这里已经没有可以比对的长度了。
        const bool ok = self->reader->read(static_cast<qint64>(position),
                                          reinterpret_cast<char*>(buffer),
                                          static_cast<qint64>(size));
        return ok ? 1 : 0;
    }
};

struct PageImpl {
    // 持有文档实现，保证页面句柄存活期间文档不被关闭。
    std::shared_ptr<DocumentImpl> document;
    FPDF_PAGE page = nullptr;
    // 页序号（从 0 开始），文本层要带上。
    int index = 0;

    ~PageImpl() {
        if (page != nullptr) {
            // 关闭页面同样要上锁。
            const detail::PdfiumGuard guard;
            FPDF_ClosePage(page);
        }
    }
};

// 把 QString 转成 PDFium 要的 UTF-16LE 字符串（带终止符）。
inline std::vector<unsigned short> toWideString(const QString& text) {
    const QChar* chars = reinterpret_cast<const QChar*>(text.utf16());
    const int count = text.size();
    std::vector<unsigned short> wide;
    wide.reserve(static_cast<size_t>(count) + 1);
    for (int i = 0; i < count; ++i) {
        wide.push_back(chars[i].unicode());
    }
    wide.push_back(0);
    return wide;
}

// 读 PDFium 的宽字符串。
//
// 不能先用 (nullptr, 0) 探长度：GetFormFieldName 这一类接口在空缓冲上会直接
// 解引用，实测会以 0xC0000409 崩掉。这里先给一块足够大的缓冲，返回长度超出
// 再按实际大小重读一次。
inline QString readWideString(const std::function<unsigned long(void*, unsigned long)>& read) {
    // 留足余量：PDFium 会把结尾 NUL 也算进长度，贴着边界分配容易被写坏。
    std::vector<unsigned short> buffer(4096, 0);
    unsigned long length = read(buffer.data(), static_cast<unsigned long>((buffer.size() - 2) * 2));
    if (length > (buffer.size() - 2) * 2) {
        buffer.assign(length / 2 + 4, 0);
        length = read(buffer.data(), static_cast<unsigned long>((buffer.size() - 2) * 2));
    }
    if (length <= 2) {
        return {};
    }
    const int characters = static_cast<int>(qMin<unsigned long>(length / 2 - 1, buffer.size() - 1));
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()), characters);
}

}  // namespace detail

namespace {

// 复制一个文档来源，给另一个线程的独立句柄用。
std::shared_ptr<DocumentReader> cloneReader(
    const std::shared_ptr<DocumentReader>& source) {
    if (!source) {
        return nullptr;
    }
    if (source->kind() == DocumentSourceKind::MemoryBytes) {
        // 共享同一块缓冲：QByteArray 隐式共享，不复制数据。
        return DocumentReader::fromBytes(source->bytes());
    }
    // 按路径打开的文档：另开一个文件句柄读同一个文件，同样不占内存。
    return DocumentReader::openPath(source->filePath());
}

std::once_flag g_initializeFlag;

void initializePdfium() {
    std::call_once(g_initializeFlag, []() {
        FPDF_LIBRARY_CONFIG config = {};
        config.version = 2;
        FPDF_InitLibraryWithConfig(&config);
    });
}

QString describePdfiumError(unsigned long error) {
    switch (error) {
        case FPDF_ERR_SUCCESS:
            return {};
        case FPDF_ERR_UNKNOWN:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "PDFium 未知错误");
        case FPDF_ERR_FILE:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "文件不存在或无法读取");
        case FPDF_ERR_FORMAT:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "文件不是有效的 PDF");
        case FPDF_ERR_PASSWORD:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "需要密码，或密码不正确");
        case FPDF_ERR_SECURITY:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "不支持的安全处理器");
        case FPDF_ERR_PAGE:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "页码越界");
        default:
            return i18n::tr("aipdf::pdf::PdfiumAdapter", "PDFium 错误码 %1").arg(error);
    }
}

// 大纲遍历的安全上限。PDFium 明确要求调用方处理畸形文档里的环形书签引用
//（fpdf_doc.h: "the caller is responsible for handling circular bookmark
// references"），所以深度和条目数都要设上限，否则会无限展开。
constexpr int kMaxOutlineDepth = 32;
constexpr int kMaxOutlineItems = 10000;

bool needsEmbeddedUnicodeFont(const QString& text) {
    for (const QChar character : text) {
        if (character.unicode() > 0x00ff) return true;
    }
    return false;
}

FPDF_FONT loadEditingFont(detail::DocumentImpl* impl, const QString& text,
                          const QString& requestedFamily = QStringLiteral("Helvetica")) {
    if (impl == nullptr || impl->handle == nullptr) return nullptr;
    if (!needsEmbeddedUnicodeFont(text)) {
        const QHash<QString, QByteArray> standardFonts{
            {QStringLiteral("Helvetica"), "Helvetica"},
            {QStringLiteral("Helvetica Bold"), "Helvetica-Bold"},
            {QStringLiteral("Helvetica Oblique"), "Helvetica-Oblique"},
            {QStringLiteral("Helvetica Bold Oblique"), "Helvetica-BoldOblique"},
            {QStringLiteral("Times Roman"), "Times-Roman"},
            {QStringLiteral("Times Bold"), "Times-Bold"},
            {QStringLiteral("Times Italic"), "Times-Italic"},
            {QStringLiteral("Times Bold Italic"), "Times-BoldItalic"},
            {QStringLiteral("Courier"), "Courier"},
            {QStringLiteral("Courier Bold"), "Courier-Bold"},
            {QStringLiteral("Courier Oblique"), "Courier-Oblique"},
            {QStringLiteral("Courier Bold Oblique"), "Courier-BoldOblique"},
            {QStringLiteral("Symbol"), "Symbol"},
            {QStringLiteral("Zapf Dingbats"), "ZapfDingbats"},
        };
        const QString family = standardFonts.contains(requestedFamily)
            ? requestedFamily : QStringLiteral("Helvetica");
        if (!impl->editingFonts.contains(family))
            impl->editingFonts.insert(family, FPDFText_LoadStandardFont(
                impl->handle, standardFonts.value(family).constData()));
        return impl->editingFonts.value(family);
    }
    const QString unicodeKey = QStringLiteral("unicode:") + requestedFamily;
    if (impl->editingFonts.contains(unicodeKey)) return impl->editingFonts.value(unicodeKey);
    QStringList candidates;
    const QString configured = qEnvironmentVariable("AIPDF_CJK_FONT");
    if (!configured.isEmpty()) candidates.append(configured);
    if (requestedFamily == QStringLiteral("SimHei")) candidates.append(QStringLiteral("C:/Windows/Fonts/simhei.ttf"));
    else if (requestedFamily == QStringLiteral("SimSun")) candidates.append(QStringLiteral("C:/Windows/Fonts/simsun.ttc"));
    else if (requestedFamily == QStringLiteral("Microsoft YaHei")) candidates.append(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    candidates.append({QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
                       QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
                       QStringLiteral("C:/Windows/Fonts/simsunb.ttf"),
                       QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
                       QStringLiteral("/System/Library/Fonts/PingFang.ttc")});
    for (const QString& path : std::as_const(candidates)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = file.readAll();
        if (data.isEmpty() || data.size() > std::numeric_limits<uint32_t>::max()) continue;
        FPDF_FONT font = FPDFText_LoadFont(impl->handle,
                                           reinterpret_cast<const uint8_t*>(data.constData()),
                                           static_cast<uint32_t>(data.size()),
                                           FPDF_FONT_TRUETYPE, 1);
        if (font != nullptr) {
            impl->editingFonts.insert(unicodeKey, font);
            return font;
        }
    }
    return nullptr;
}

FPDF_PAGEOBJECT createEditingTextObject(FPDF_DOCUMENT document, FPDF_FONT font,
                                        const QString& text, double fontSize) {
    FPDF_PAGEOBJECT object = FPDFPageObj_CreateTextObj(document, font, fontSize);
    if (object == nullptr) return nullptr;
    const std::u16string wide = text.toStdU16String();
    if (!FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(wide.c_str()))) {
        FPDFPageObj_Destroy(object);
        return nullptr;
    }
    return object;
}

void copyPageObjectStrokeStyle(FPDF_PAGEOBJECT source, FPDF_PAGEOBJECT destination,
                               std::optional<unsigned int> alphaOverride = std::nullopt) {
    if (!source || !destination) return;
    unsigned int red = 0, green = 0, blue = 0, alpha = 255;
    if (FPDFPageObj_GetStrokeColor(source, &red, &green, &blue, &alpha))
        FPDFPageObj_SetStrokeColor(destination, red, green, blue,
                                   alphaOverride.value_or(alpha));
    float width = 0.0F;
    if (FPDFPageObj_GetStrokeWidth(source, &width))
        FPDFPageObj_SetStrokeWidth(destination, width);
    FPDFPageObj_SetLineJoin(destination, FPDFPageObj_GetLineJoin(source));
    FPDFPageObj_SetLineCap(destination, FPDFPageObj_GetLineCap(source));
    const int dashCount = FPDFPageObj_GetDashCount(source);
    float dashPhase = 0.0F;
    if (dashCount >= 0 && dashCount <= 1024 && FPDFPageObj_GetDashPhase(source, &dashPhase)) {
        QVector<float> dashArray(dashCount);
        if (dashCount == 0 || FPDFPageObj_GetDashArray(source, dashArray.data(), dashArray.size()))
            FPDFPageObj_SetDashArray(destination, dashArray.constData(), dashArray.size(), dashPhase);
    }
}

// PDFium 返回的字符串是 UTF-16LE（带终止符）。
QString decodeUtf16(const unsigned char* bytes, unsigned long byteCount) {
    if (bytes == nullptr || byteCount < 2) {
        return {};
    }
    // 去掉末尾的 UTF-16 终止符。
    const int charCount = int(byteCount / 2) - 1;
    if (charCount <= 0) {
        return {};
    }
    std::u16string utf16;
    utf16.resize(charCount);
    memcpy(utf16.data(), bytes, size_t(charCount) * 2);
    return QString::fromUtf16(utf16.c_str(), charCount);
}

// 书签标题。
QString bookmarkTitle(FPDF_BOOKMARK bookmark) {
    const unsigned long byteCount = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
    if (byteCount <= 2 || byteCount > 65536) {
        return {};
    }
    QByteArray buffer(int(byteCount), Qt::Uninitialized);
    const unsigned long written = FPDFBookmark_GetTitle(bookmark, buffer.data(), byteCount);
    if (written <= 2 || written > byteCount) {
        return {};
    }
    return decodeUtf16(reinterpret_cast<const unsigned char*>(buffer.constData()), written);
}

// 动作的目标页（内部跳转）。不是 GoTo 动作或目标无效时返回 -1。
int actionTargetPage(FPDF_DOCUMENT document, FPDF_ACTION action) {
    if (action == nullptr || FPDFAction_GetType(action) != PDFACTION_GOTO) {
        return -1;
    }
    FPDF_DEST dest = FPDFAction_GetDest(document, action);
    if (dest == nullptr) {
        return -1;
    }
    return FPDFDest_GetDestPageIndex(document, dest);
}

// 动作的 URI（外部链接）。PDFium 给的字节通常是 UTF-8，
// 但文档明确说过也可能不是，因此按 UTF-8 解，失败再退回 Latin-1。
QString actionUri(FPDF_DOCUMENT document, FPDF_ACTION action) {
    if (action == nullptr || FPDFAction_GetType(action) != PDFACTION_URI) {
        return {};
    }
    const unsigned long byteCount = FPDFAction_GetURIPath(document, action, nullptr, 0);
    if (byteCount == 0 || byteCount > 65536) {
        return {};
    }
    QByteArray buffer(int(byteCount) + 1, Qt::Uninitialized);
    const unsigned long written = FPDFAction_GetURIPath(document, action, buffer.data(), byteCount);
    if (written == 0 || written > byteCount) {
        return {};
    }
    buffer.truncate(int(written));
    // PDFium 的文档写明：返回的字节数**含结尾的 NUL**。
    // 不去掉它，URI 末尾会挂一个不可见的空字符——
    // 界面上看不出来，但拼出来的地址是错的，外部链接会打不开。
    while (buffer.endsWith('\0')) {
        buffer.chop(1);
    }
    if (buffer.isEmpty()) {
        return {};
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(buffer);
    if (decoder.hasError()) {
        return QString::fromLatin1(buffer);
    }
    return text;
}

// 读取一层书签（parent 为空表示顶层）。
void readOutlineLevel(FPDF_DOCUMENT document,
                      FPDF_BOOKMARK parent,
                      std::vector<OutlineItem>* out,
                      int depth,
                      int* itemCount) {
    if (out == nullptr || depth > kMaxOutlineDepth) {
        return;
    }
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetFirstChild(document, parent);
    while (bookmark != nullptr) {
        if (*itemCount >= kMaxOutlineItems) {
            return;
        }
        ++(*itemCount);
        OutlineItem item;
        item.title = bookmarkTitle(bookmark);
        FPDF_ACTION action = FPDFBookmark_GetAction(bookmark);
        item.pageIndex = actionTargetPage(document, action);
        item.uri = actionUri(document, action);
        if (action == nullptr) {
            // 没有 /A 动作时，目标写在 /Dest 里。
            // PDFium 的文档明确要求这样兜底：GetAction 返回 NULL 时
            // 应当改调 GetDest，只看动作会漏掉一大半书签。
            FPDF_DEST dest = FPDFBookmark_GetDest(document, bookmark);
            if (dest != nullptr) {
                item.pageIndex = FPDFDest_GetDestPageIndex(document, dest);
            }
        }
        readOutlineLevel(document, bookmark, &item.children, depth + 1, itemCount);
        out->push_back(std::move(item));
        bookmark = FPDFBookmark_GetNextSibling(document, bookmark);
    }
}

// 内存输出缓冲：FPDF_FILEWRITE 的写入回调往这里追加。
//
// FPDF_FILEWRITE 没有用户数据字段（只有 version 与 WriteBlock），
// 所以缓冲指针只能走文件级静态变量。
struct MemoryWriter {
    QByteArray data;
};

MemoryWriter* g_memoryWriterTarget = nullptr;

int writeToMemory(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    if (g_memoryWriterTarget == nullptr || data == nullptr) {
        return 0;
    }
    g_memoryWriterTarget->data.append(static_cast<const char*>(data), int(size));
    return 1;
}

// ------------------------------------------------------- 批注与 PDFium 的互转 ----

// 我们写的批注带一个 /NM 标记（"Folixa:<id>"）。
//
// 认批注靠标记，不靠「写在页面末尾第几条」：
// 页面一插入、删除、移动，末尾的位置就变了，按位置记账会认错批注，
// 表现出来就是重写批注变成了追加（同一个批注在页面上出现两次）。
constexpr auto kAnnotationNamePrefix = "Folixa:";

// 这条批注是不是我们写的。
bool isOurAnnotation(FPDF_ANNOTATION annot) {
    unsigned short buffer[64] = {0};
    const unsigned long length = FPDFAnnot_GetStringValue(annot, "NM", buffer, 64);
    constexpr size_t prefixLength = sizeof(kAnnotationNamePrefix) - 1;
    // 长度含结尾的 NUL：必须比“前缀 + 至少一位数字 + NUL”长。
    if (length <= prefixLength * 2 + 2) {
        return false;
    }
    for (size_t i = 0; i < prefixLength; ++i) {
        if (buffer[i] != static_cast<unsigned short>(kAnnotationNamePrefix[i])) {
            return false;
        }
    }
    // 前缀后面必须紧跟数字：别人恰好写了 "Folixa:note" 这种 /NM 时不能认成
    // 我们的批注——认错就会在同步时把它删掉。
    const unsigned short first = buffer[prefixLength];
    return first >= '0' && first <= '9';
}

// 从 /NM 里读回批注 id（"Folixa:<id>"）。不是我们的批注或解析失败返回 0。
//
// 缓冲区 64 个 UTF-16 单元（128 字节）；FPDFAnnot_GetStringValue 返回的是
// **所需**字节数，不是写入数，所以长度必须按缓冲区容量裁掉再构造 QString，
// 否则一个超长 /NM 就会让 fromUtf16 越界读栈。
quint64 annotationIdFromName(FPDF_ANNOTATION annot) {
    unsigned short buffer[64] = {0};
    const unsigned long length = FPDFAnnot_GetStringValue(annot, "NM", buffer, 64);
    constexpr int kMaxUnits = 64;
    if (length <= sizeof(kAnnotationNamePrefix)) {
        return 0;
    }
    const int available = static_cast<int>(length / 2) - 1;
    const int charCount = qBound(0, available, kMaxUnits);
    const QString name =
        QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer), charCount);
    const int prefixLength = static_cast<int>(sizeof(kAnnotationNamePrefix)) - 1;
    if (!name.startsWith(QString::fromLatin1(kAnnotationNamePrefix))) {
        return 0;
    }
    bool ok = false;
    const quint64 id = name.mid(prefixLength).toULongLong(&ok);
    return ok ? id : 0;
}

// PDFium 的 FS_RECTF 的 top 是**较高**的 y、bottom 是较低的 y；
// 我们的 QRectF 存的是原样的用户空间坐标，取 min/max 即可对上。
FS_RECTF toFsRect(const QRectF& rect) {
    FS_RECTF result = {};
    result.left = static_cast<float>(rect.left());
    result.right = static_cast<float>(rect.right());
    result.top = static_cast<float>(qMax(rect.top(), rect.bottom()));
    result.bottom = static_cast<float>(qMin(rect.top(), rect.bottom()));
    return result;
}

// 读回 PDFium 的矩形 → 用户空间 QRectF（y 值原样保留）。
QRectF fromFsRect(const FS_RECTF& rect) {
    return QRectF(QPointF(rect.left, rect.bottom), QPointF(rect.right, rect.top)).normalized();
}

// 写一条批注。失败返回 false。
bool createAnnotation(FPDF_PAGE page, const Annotation& annotation) {
    const int subtype = annotationTypeToPdfium(annotation.type);
    if (subtype == FPDF_ANNOT_UNKNOWN) {
        return false;
    }
    FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, static_cast<FPDF_ANNOTATION_SUBTYPE>(subtype));
    if (annot == nullptr) {
        return false;
    }

    const FS_RECTF rect = toFsRect(annotation.rect);
    FPDFAnnot_SetRect(annot, &rect);

    if (annotation.color.isValid()) {
        FPDFAnnot_SetColor(annot,
                           FPDFANNOT_COLORTYPE_Color,
                           static_cast<unsigned int>(annotation.color.red()),
                           static_cast<unsigned int>(annotation.color.green()),
                           static_cast<unsigned int>(annotation.color.blue()),
                           static_cast<unsigned int>(annotation.color.alpha()));
    }

    // 便签正文与作者。PDFium 要 UTF-16LE。
    if (!annotation.contents.isEmpty()) {
        const std::vector<unsigned short> contents = detail::toWideString(annotation.contents);
        FPDFAnnot_SetStringValue(annot, "Contents",
                                  reinterpret_cast<const FPDF_WIDESTRING>(contents.data()));
    }
    if (!annotation.author.isEmpty()) {
        const std::vector<unsigned short> author = detail::toWideString(annotation.author);
        FPDFAnnot_SetStringValue(annot, "T", reinterpret_cast<const FPDF_WIDESTRING>(author.data()));
    }

    // 打标记：认批注靠它，不靠它在页面上的位置。
    {
        const std::vector<unsigned short> name = detail::toWideString(
            QString::fromLatin1(kAnnotationNamePrefix) + QString::number(annotation.id));
        FPDFAnnot_SetStringValue(annot, "NM", reinterpret_cast<const FPDF_WIDESTRING>(name.data()));
    }

    // 高亮/下划线/删除线：逐行写四角点。
    for (const QRectF& quad : annotation.quads) {
        const double left = quad.left();
        const double right = quad.right();
        const double lower = qMin(quad.top(), quad.bottom());
        const double upper = qMax(quad.top(), quad.bottom());
        FS_QUADPOINTSF points = {};
        // 四角顺序：左上、右上、左下、右下（FS_QUADPOINTSF 是扁平的八个浮点数）。
        points.x1 = static_cast<float>(left);
        points.y1 = static_cast<float>(upper);
        points.x2 = static_cast<float>(right);
        points.y2 = static_cast<float>(upper);
        points.x3 = static_cast<float>(left);
        points.y3 = static_cast<float>(lower);
        points.x4 = static_cast<float>(right);
        points.y4 = static_cast<float>(lower);
        FPDFAnnot_AppendAttachmentPoints(annot, &points);
    }

    // 墨迹：逐条笔画。
    for (const QVector<QPointF>& stroke : annotation.strokes) {
        if (stroke.isEmpty()) {
            continue;
        }
        std::vector<FS_POINTF> points;
        points.reserve(static_cast<size_t>(stroke.size()));
        for (const QPointF& point : stroke) {
            points.push_back({static_cast<float>(point.x()), static_cast<float>(point.y())});
        }
        FPDFAnnot_AddInkStroke(annot, points.data(), points.size());
    }

    FPDFPage_CloseAnnot(annot);
    return true;
}

}  // namespace

// ---------------------------------------------------------------- PdfPage ----

PdfPage::PdfPage(std::shared_ptr<detail::PageImpl> impl) : impl_(std::move(impl)) {}
PdfPage::~PdfPage() = default;

bool PdfPage::isValid() const {
    return impl_ != nullptr && impl_->page != nullptr;
}

double PdfPage::widthPoints() const {
    if (!isValid()) {
        return 0.0;
    }
    const detail::PdfiumGuard guard;
    return FPDF_GetPageWidthF(impl_->page);
}

double PdfPage::heightPoints() const {
    if (!isValid()) {
        return 0.0;
    }
    const detail::PdfiumGuard guard;
    return FPDF_GetPageHeightF(impl_->page);
}

int PdfPage::rotationDegrees() const {
    if (!isValid()) {
        return 0;
    }
    const detail::PdfiumGuard guard;
    // PDFium 返回的是四分之一圈：0=0°，1=90°，2=180°，3=270°。
    return FPDFPage_GetRotation(impl_->page) * 90;
}

double PdfPage::displayWidthPoints() const {
    // PDFium 的 FPDF_GetPageWidthF 返回的**已经**是应用过页面 /Rotate 的尺寸
    // （/Rotate 90、MediaBox 200x100 的页面返回 100x200），
    // 所以显示尺寸直接用它，不要再手工互换宽高。
    return widthPoints();
}

double PdfPage::displayHeightPoints() const {
    return heightPoints();
}

QImage PdfPage::render(double zoom, int rotateQuarterTurns) const {
    if (!isValid() || zoom <= 0.0) {
        return {};
    }

    // 整段渲染都在锁内：PDFium 的渲染过程会读写全局共享状态。
    const detail::PdfiumGuard guard;

    const double width = widthPoints();
    const double height = heightPoints();
    if (width <= 0.0 || height <= 0.0) {
        return {};
    }

    // PDFium 渲染时**自己会应用**页面的 /Rotate（实测：/Rotate 90 的页面用
    // rotate=0 渲染，内容已经按 90° 顺时针转过），rotate 参数是在此基础上
    // 叠加的顺时针旋转。
    // 所以这里只传用户额外旋转；页面自带的旋转已经体现在输出尺寸上了。
    const int userTurns = ((rotateQuarterTurns % 4) + 4) % 4;
    const int totalTurns = userTurns;

    // 输出尺寸：显示尺寸再叠加用户旋转。
    double outputWidth = displayWidthPoints();
    double outputHeight = displayHeightPoints();
    if (userTurns == 1 || userTurns == 3) {
        std::swap(outputWidth, outputHeight);
    }

    const int pixelWidth = static_cast<int>(outputWidth * zoom + 0.5);
    const int pixelHeight = static_cast<int>(outputHeight * zoom + 0.5);
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return {};
    }
    if (pixelWidth > kMaxRenderPixelDimension || pixelHeight > kMaxRenderPixelDimension) {
        return {};
    }
    if (static_cast<qint64>(pixelWidth) * pixelHeight > kMaxRenderPixelCount) {
        return {};
    }

    QImage image(pixelWidth, pixelHeight, QImage::Format_ARGB32);
    if (image.isNull()) {
        return {};
    }
    image.fill(Qt::white);

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(pixelWidth,
                                              pixelHeight,
                                              FPDFBitmap_BGRA,
                                              reinterpret_cast<unsigned char*>(image.bits()),
                                              pixelWidth * 4);
    if (bitmap == nullptr) {
        return {};
    }

    FPDFBitmap_FillRect(bitmap, 0, 0, pixelWidth, pixelHeight, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(
        bitmap, impl_->page, 0, 0, pixelWidth, pixelHeight, totalTurns, FPDF_ANNOT);
    // 表单字段（输入框、复选框）不在页面内容流里，必须单独画一层；
    // 不画的话，用户只能看到空白框，填了什么完全看不见。
    if (impl_->document) {
        FPDF_FORMHANDLE form = impl_->document->ensureForm();
        if (form != nullptr) {
            FPDF_FFLDraw(form, bitmap, impl_->page, 0, 0, pixelWidth, pixelHeight, totalTurns,
                         FPDF_ANNOT);
        }
    }
    FPDFBitmap_Destroy(bitmap);

    return image;
}

MediaBox PdfPage::mediaBox() const {
    if (!isValid()) {
        return {};
    }
    const detail::PdfiumGuard guard;
    float left = 0.0F;
    float bottom = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    if (!FPDFPage_GetMediaBox(impl_->page, &left, &bottom, &right, &top)) {
        return MediaBox{0.0, 0.0, widthPoints(), heightPoints()};
    }
    return MediaBox{left, bottom, right, top};
}

MediaBox PdfPage::contentBox() const {
    if (!isValid()) {
        return {};
    }
    const detail::PdfiumGuard guard;
    float left = 0.0F;
    float bottom = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    // PDFium 渲染的是 CropBox（没有 CropBox 时内部退回 MediaBox），
    // 字符框必须相对同一个盒子，否则会和页面图像错位。
    if (FPDFPage_GetCropBox(impl_->page, &left, &bottom, &right, &top)) {
        return MediaBox{left, bottom, right, top};
    }
    if (FPDFPage_GetMediaBox(impl_->page, &left, &bottom, &right, &top)) {
        return MediaBox{left, bottom, right, top};
    }
    return MediaBox{0.0, 0.0, widthPoints(), heightPoints()};
}

bool PdfPage::setCropBox(const MediaBox& box) {
    if (!isValid()) {
        return false;
    }
    if (box.right <= box.left || box.top <= box.bottom) {
        return false;
    }
    const MediaBox media = mediaBox();
    if (box.left < media.left - 0.01 || box.right > media.right + 0.01 ||
        box.bottom < media.bottom - 0.01 || box.top > media.top + 0.01) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDFPage_SetCropBox(impl_->page,
                        static_cast<float>(box.left),
                        static_cast<float>(box.bottom),
                        static_cast<float>(box.right),
                        static_cast<float>(box.top));
    return true;
}

bool PdfPage::setPageBackground(const QColor& color) {
    if (!isValid() || !color.isValid()) {
        return false;
    }
    const detail::PdfiumGuard guard;
    const MediaBox box = contentBox();
    FPDF_PAGEOBJECT rect = FPDFPageObj_CreateNewRect(
        static_cast<float>(box.left), static_cast<float>(box.bottom),
        static_cast<float>(box.right), static_cast<float>(box.top));
    if (rect == nullptr) {
        return false;
    }
    FPDFPageObj_SetFillColor(rect, color.red(), color.green(), color.blue(), color.alpha());
    // 路径对象必须显式指定「填充、不描边」，否则它只是几何、不会被画出来。
    FPDFPath_SetDrawMode(rect, FPDF_FILLMODE_ALTERNATE, 0);
    // 插到索引 0：背景必须压在正文下面。
    if (!FPDFPage_InsertObjectAtIndex(impl_->page, rect, 0)) {
        FPDFPageObj_Destroy(rect);
        return false;
    }
    return FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::resizePage(double widthPoints, double heightPoints, bool scaleContent) {
    if (!isValid() || widthPoints <= 0.0 || heightPoints <= 0.0) {
        return false;
    }
    const detail::PdfiumGuard guard;
    const MediaBox box = contentBox();
    const double sourceWidth = box.right - box.left;
    const double sourceHeight = box.top - box.bottom;
    if (sourceWidth <= 0.0 || sourceHeight <= 0.0) {
        return false;
    }
    const double scaleX = widthPoints / sourceWidth;
    const double scaleY = heightPoints / sourceHeight;
    if (scaleContent) {
        // 以内容盒左下角为原点缩放：变换所有页面对象，再把盒子挪回 (0,0)。
        const int count = FPDFPage_CountObjects(impl_->page);
        for (int index = 0; index < count; ++index) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
            if (object == nullptr) {
                continue;
            }
            FPDFPageObj_Transform(object, scaleX, 0.0, 0.0, scaleY,
                                  -box.left * scaleX, -box.bottom * scaleY);
        }
    }
    FPDFPage_SetMediaBox(impl_->page, 0.0F, 0.0F,
                         static_cast<float>(widthPoints), static_cast<float>(heightPoints));
    FPDFPage_SetCropBox(impl_->page, 0.0F, 0.0F,
                        static_cast<float>(widthPoints), static_cast<float>(heightPoints));
    FPDFPage_GenerateContent(impl_->page);
    return true;
}

QVector<PageLink> PdfPage::links() const {
    if (!isValid()) {
        return {};
    }

    // 链接注释的读取全程在锁内：PDFium 解析页面注释时会动全局状态。
    const detail::PdfiumGuard guard;

    QVector<PageLink> result;
    int startPos = 0;
    FPDF_LINK link = nullptr;
    // FPDFLink_Enumerate 按 Z 序枚举本页的链接注释（越靠后越在上层）。
    while (FPDFLink_Enumerate(impl_->page, &startPos, &link)) {
        if (link == nullptr) {
            break;
        }
        FS_RECTF rect = {};
        if (!FPDFLink_GetAnnotRect(link, &rect)) {
            continue;
        }
        PageLink item;
        item.rect = QRectF(QPointF(rect.left, rect.bottom),
                            QPointF(rect.right, rect.top)).normalized();

        // 先看直接的目标（内部跳转），再看动作（GoTo 或 URI）。
        FPDF_DEST dest = FPDFLink_GetDest(impl_->document->handle, link);
        if (dest != nullptr) {
            item.pageIndex = FPDFDest_GetDestPageIndex(impl_->document->handle, dest);
        }
        if (item.pageIndex < 0) {
            FPDF_ACTION action = FPDFLink_GetAction(link);
            if (action != nullptr) {
                if (FPDFAction_GetType(action) == PDFACTION_GOTO) {
                    FPDF_DEST actionDest = FPDFAction_GetDest(impl_->document->handle, action);
                    if (actionDest != nullptr) {
                        item.pageIndex = FPDFDest_GetDestPageIndex(impl_->document->handle, actionDest);
                    }
                } else {
                    item.uri = actionUri(impl_->document->handle, action);
                }
            }
        }
        result.append(item);
    }
    return result;
}

bool PdfPage::addUriLink(const QRectF& rectPoints, const QByteArray& encodedUri) {
    if (!isValid() || rectPoints.isEmpty() || encodedUri.isEmpty()) {
        return false;
    }
    for (const char value : encodedUri) {
        if (static_cast<unsigned char>(value) > 0x7f) {
            return false;
        }
    }

    const detail::PdfiumGuard guard;
    const int annotationIndex = FPDFPage_GetAnnotCount(impl_->page);
    FPDF_ANNOTATION annotation = FPDFPage_CreateAnnot(impl_->page, FPDF_ANNOT_LINK);
    if (annotation == nullptr) {
        return false;
    }
    const QRectF normalized = rectPoints.normalized();
    const FS_RECTF rect{static_cast<float>(normalized.left()),
                        static_cast<float>(normalized.top()),
                        static_cast<float>(normalized.right()),
                        static_cast<float>(normalized.bottom())};
    const bool success = FPDFAnnot_SetRect(annotation, &rect) &&
                         FPDFAnnot_SetURI(annotation, encodedUri.constData());
    FPDFPage_CloseAnnot(annotation);
    if (!success && annotationIndex >= 0) {
        FPDFPage_RemoveAnnot(impl_->page, annotationIndex);
    }
    return success;
}

int PdfPage::addDetectedUriLinks() {
    const std::optional<TextPage> textPage = extractText();
    if (!textPage.has_value() || textPage->chars.isEmpty()) {
        return 0;
    }

    QString text;
    QVector<int> starts;
    QVector<int> ends;
    starts.reserve(textPage->chars.size());
    ends.reserve(textPage->chars.size());
    for (const TextChar& character : textPage->chars) {
        starts.append(text.size());
        text.append(character.text);
        ends.append(text.size());
    }

    static const QRegularExpression urlPattern(
        QStringLiteral(R"((?:(?:https?://)|(?:www\.))[^\s<>\[\]{}"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QVector<PageLink> existing = links();
    int added = 0;
    QRegularExpressionMatchIterator matches = urlPattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        int matchStart = match.capturedStart();
        int matchEnd = match.capturedEnd();
        QString value = match.captured();
        while (!value.isEmpty() && QStringLiteral(".,;:!?)]}").contains(value.back())) {
            value.chop(1);
            --matchEnd;
        }
        if (value.isEmpty() || matchEnd <= matchStart) {
            continue;
        }

        QRectF rect;
        for (int index = 0; index < textPage->chars.size(); ++index) {
            if (ends.at(index) <= matchStart || starts.at(index) >= matchEnd) {
                continue;
            }
            rect = rect.united(textPage->chars.at(index).box);
        }
        if (rect.isEmpty()) {
            continue;
        }
        QUrl url = QUrl::fromUserInput(value);
        if (!url.isValid() || url.scheme().isEmpty()) {
            continue;
        }
        const QString normalizedUri = QString::fromUtf8(url.toEncoded());
        const bool duplicate = std::any_of(existing.cbegin(), existing.cend(),
                                           [&rect, &normalizedUri](const PageLink& link) {
            return link.uri == normalizedUri &&
                   link.rect.intersects(rect.adjusted(-1.0, -1.0, 1.0, 1.0));
        });
        if (!duplicate && addUriLink(rect, url.toEncoded())) {
            ++added;
        }
    }
    return added;
}

QVector<Annotation> PdfPage::annotations() const {
    if (!isValid()) {
        return {};
    }

    const detail::PdfiumGuard guard;

    QVector<Annotation> result;
    const int count = FPDFPage_GetAnnotCount(impl_->page);
    for (int index = 0; index < count; ++index) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(impl_->page, index);
        if (annot == nullptr) {
            continue;
        }
        const int subtype = static_cast<int>(FPDFAnnot_GetSubtype(annot));
        Annotation item;
        if (!annotationTypeFromPdfium(subtype, &item.type)) {
            // 链接、Widget 等不是本版本管理的批注类型。
            FPDFPage_CloseAnnot(annot);
            continue;
        }
        item.pageIndex = impl_->index;

        // 我们的批注带 /NM 标记（"Folixa:<id>"）：id 从这里读回来，
        // 这样存盘再打开之后，批注的 id 还在。
        item.id = annotationIdFromName(annot);

        FS_RECTF rect = {};
        if (FPDFAnnot_GetRect(annot, &rect)) {
            item.rect = fromFsRect(rect);
        }

        unsigned int r = 0;
        unsigned int g = 0;
        unsigned int b = 0;
        unsigned int a = 255;
        if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_Color, &r, &g, &b, &a)) {
            item.color = QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b),
                                static_cast<int>(a));
        } else {
            item.color = defaultAnnotationColor(item.type);
        }

        // 文档里已经带外观流：PDFium 会自己把它画出来，界面就别再画一遍。
        item.hasAppearance = FPDFAnnot_HasKey(annot, "AP") != 0;

        // 批注正文（Contents）。
        // 作者与审阅状态：回复线程与"谁批的、批成什么状态"都靠它们。
        const std::pair<const char*, QString*> fields[] = {
            { "T", &item.author },
            { "State", &item.reviewState },
        };
        for (const auto& field : fields) {
            const unsigned long fieldLength =
                FPDFAnnot_GetStringValue(annot, field.first, nullptr, 0);
            if (fieldLength > 2 && fieldLength < 4096) {
                std::vector<unsigned short> value(fieldLength / 2 + 2, 0);
                if (FPDFAnnot_GetStringValue(annot, field.first, value.data(),
                                             static_cast<unsigned long>(value.size() * 2)) > 2) {
                    *field.second = QString::fromUtf16(
                        reinterpret_cast<const char16_t*>(value.data()));
                }
            }
        }

        const unsigned long needed =
            FPDFAnnot_GetStringValue(annot, "Contents", nullptr, 0);
        if (needed > 2 && needed < 65536) {
            std::vector<unsigned short> buffer(needed / 2 + 2, 0);
            const unsigned long written = FPDFAnnot_GetStringValue(
                annot, "Contents", reinterpret_cast<FPDF_WCHAR*>(buffer.data()), needed);
            if (written > 2 && written <= needed) {
                item.contents = QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()),
                                                   static_cast<int>(written / 2) - 1);
            }
        }

        // 四角点（高亮一类）。
        const size_t quadCount = FPDFAnnot_CountAttachmentPoints(annot);
        for (size_t quad = 0; quad < quadCount; ++quad) {
            FS_QUADPOINTSF points = {};
            if (!FPDFAnnot_GetAttachmentPoints(annot, quad, &points)) {
                continue;
            }
            const double left = qMin(points.x1, points.x3);
            const double right = qMax(points.x2, points.x4);
            const double lower = qMin(points.y1, points.y3);
            const double upper = qMax(points.y2, points.y4);
            item.quads.append(QRectF(QPointF(left, lower), QPointF(right, upper)).normalized());
        }

        FPDFPage_CloseAnnot(annot);
        result.append(item);
    }
    return result;
}

bool PdfPage::setAnnotations(const QVector<Annotation>& annotations) {
    // 单参版本保持原来的语义：这一页上所有本程序写的批注都重写。
    return setAnnotations(annotations, /*removeIds=*/std::nullopt);
}

bool PdfPage::setAnnotations(const QVector<Annotation>& annotations,
                             const QSet<quint64>& removeIds) {
    return setAnnotations(annotations, std::optional<QSet<quint64>>(removeIds));
}

bool PdfPage::setAnnotations(const QVector<Annotation>& annotations,
                             std::optional<QSet<quint64>> removeIds) {
    if (!isValid()) {
        return false;
    }

    const detail::PdfiumGuard guard;

    // 先删掉要重写的那批批注。
    //
    // 认它们靠 /NM 标记，不靠「写在末尾第几条」：
    // 页面被插入、删除、移动之后，末尾的位置就变了，
    // 按位置记账会把原文档自带的批注误删，或者把自己的批注留下来变成重复。
    //
    // removeIds 有值时只删这些 id：文件里其它软件写的批注、以及本程序读不回来
    // 因而不接管的批注（几何/附件/声音）一律不动。没有这个过滤时，用户在同页
    // 新增一条批注就会把上次会话保存的批注整页删掉。
    QVector<int> ours;
    const int total = FPDFPage_GetAnnotCount(impl_->page);
    for (int i = 0; i < total; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(impl_->page, i);
        if (annot == nullptr) {
            continue;
        }
        const bool isOurs = isOurAnnotation(annot);
        const quint64 id = isOurs ? annotationIdFromName(annot) : 0;
        FPDFPage_CloseAnnot(annot);
        if (isOurs && (!removeIds.has_value() || removeIds->contains(id))) {
            ours.append(i);
        }
    }
    // 从后往前删：删掉后面的不会影响前面的下标。
    for (int i = ours.size() - 1; i >= 0; --i) {
        FPDFPage_RemoveAnnot(impl_->page, ours[i]);
    }

    // 再写新的。
    int created = 0;
    for (const Annotation& annotation : annotations) {
        if (createAnnotation(impl_->page, annotation)) {
            ++created;
        }
    }
    return created == static_cast<int>(annotations.size());
}

std::optional<TextPage> PdfPage::extractText() const {
    if (!isValid()) {
        return std::nullopt;
    }

    // 文本解析全程在锁内：PDFium 解析字体与页面时会动全局缓存。
    const detail::PdfiumGuard guard;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(impl_->page);
    if (textPage == nullptr) {
        return std::nullopt;
    }

    TextPage result;
    result.pageIndex = impl_->index;
    // 页面几何一起带回去：上层要靠它把字符框变换到显示空间，
    // 而页面的 /Rotate 与有效盒子不必再回到 PDFium 去问一次。
    result.contentBox = contentBox();
    result.rotationDegrees = rotationDegrees();

    const int count = FPDFText_CountChars(textPage);
    result.chars.reserve(count);

    // 代理对：PDFium 给的是 UTF-16 码元，高代理项要等下一个低代理项。
    QString pendingHighSurrogate;
    QRectF pendingBox;

    for (int i = 0; i < count; ++i) {
        const unsigned short unit = FPDFText_GetUnicode(textPage, i);
        if (FPDFText_HasUnicodeMapError(textPage, i)) {
            result.unicodeMapError = true;
        }

        double left = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        double top = 0.0;
        FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top);
        // 注意：PDFium 的 bottom/top 命名与直觉相反——
        // 它给出的 bottom 是**较高**的那个 y，top 是**较低**的那个 y
        // （例如基线 y=60、字高 20 的 'H'，会得到 bottom=80.05、top=60）。
        // 这里统一取小值作纵坐标、大值作高度，避免依赖这个命名。
        const double boxBottom = qMin(top, bottom);
        const double boxHeight = qAbs(top - bottom);
        const QRectF box(left, boxBottom, right - left, boxHeight);

        if (unit >= 0xD800 && unit <= 0xDBFF) {
            pendingHighSurrogate = QString(QChar(static_cast<char16_t>(unit)));
            pendingBox = box;
            continue;
        }

        TextChar character;
        character.box = box;
        character.index = i;
        if (unit >= 0xDC00 && unit <= 0xDFFF && !pendingHighSurrogate.isEmpty()) {
            // 代理对：合并成一个字符，框取两者的并集。
            pendingHighSurrogate.append(QChar(static_cast<char16_t>(unit)));
            character.text = pendingHighSurrogate;
            character.box = pendingBox.united(box);
            pendingHighSurrogate.clear();
        } else {
            character.text = QString(QChar(static_cast<char16_t>(unit)));
        }
        result.chars.append(character);
    }

    if (!pendingHighSurrogate.isEmpty()) {
        // 落单的代理项：单独作为字符保留。
        TextChar character;
        character.text = pendingHighSurrogate;
        character.box = pendingBox;
        character.index = count > 0 ? count - 1 : 0;
        result.chars.append(character);
    }

    FPDFText_ClosePage(textPage);
    return result;
}

// ------------------------------------------------------- 批注类型 ↔ PDFium ----

int annotationTypeToPdfium(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return FPDF_ANNOT_HIGHLIGHT;
        case AnnotationType::Underline:
            return FPDF_ANNOT_UNDERLINE;
        case AnnotationType::StrikeOut:
            return FPDF_ANNOT_STRIKEOUT;
        case AnnotationType::Squiggly:
            return FPDF_ANNOT_SQUIGGLY;
        case AnnotationType::Ellipse:
            return FPDF_ANNOT_CIRCLE;
        case AnnotationType::Line:
        case AnnotationType::Arrow:
            return FPDF_ANNOT_LINE;
        case AnnotationType::Polygon:
        case AnnotationType::Cloud:
            return FPDF_ANNOT_POLYGON;
        case AnnotationType::FreeText:
            return FPDF_ANNOT_FREETEXT;
        case AnnotationType::Stamp:
            return FPDF_ANNOT_STAMP;
        case AnnotationType::Measure:
            return FPDF_ANNOT_LINE;
        case AnnotationType::FileAttachment:
            return FPDF_ANNOT_FILEATTACHMENT;
        case AnnotationType::Sound:
            return FPDF_ANNOT_SOUND;

        case AnnotationType::Note:
            return FPDF_ANNOT_TEXT;
        case AnnotationType::Square:
            return FPDF_ANNOT_SQUARE;
        case AnnotationType::Ink:
            return FPDF_ANNOT_INK;
    }
    return FPDF_ANNOT_UNKNOWN;
}

bool annotationTypeFromPdfium(int subtype, AnnotationType* out) {
    if (out == nullptr) {
        return false;
    }
    switch (subtype) {
        case FPDF_ANNOT_HIGHLIGHT:
            *out = AnnotationType::Highlight;
            return true;
        case FPDF_ANNOT_UNDERLINE:
            *out = AnnotationType::Underline;
            return true;
        case FPDF_ANNOT_STRIKEOUT:
            *out = AnnotationType::StrikeOut;
            return true;
        case FPDF_ANNOT_SQUIGGLY:
            *out = AnnotationType::Squiggly;
            return true;
        case FPDF_ANNOT_CIRCLE:
            *out = AnnotationType::Ellipse;
            return true;
        case FPDF_ANNOT_LINE:
            *out = AnnotationType::Line;
            return true;
        case FPDF_ANNOT_POLYGON:
            *out = AnnotationType::Polygon;
            return true;
        case FPDF_ANNOT_FREETEXT:
            *out = AnnotationType::FreeText;
            return true;
        case FPDF_ANNOT_STAMP:
            *out = AnnotationType::Stamp;
            return true;
        case FPDF_ANNOT_FILEATTACHMENT:
            *out = AnnotationType::FileAttachment;
            return true;
        case FPDF_ANNOT_SOUND:
            *out = AnnotationType::Sound;
            return true;

        case FPDF_ANNOT_TEXT:
            *out = AnnotationType::Note;
            return true;
        case FPDF_ANNOT_SQUARE:
            *out = AnnotationType::Square;
            return true;
        case FPDF_ANNOT_INK:
            *out = AnnotationType::Ink;
            return true;
        default:
            // 链接、Widget 等不是本版本管理的批注类型。
            return false;
    }
}

// ------------------------------------------------------------- PdfDocument ----
PdfDocument::PdfDocument(std::shared_ptr<detail::DocumentImpl> impl)
    : impl_(std::move(impl)) {}
PdfDocument::~PdfDocument() = default;

bool PdfDocument::isValid() const {
    return impl_ != nullptr && impl_->handle != nullptr;
}

int PdfDocument::pageCount() const {
    if (!isValid()) {
        return 0;
    }
    const detail::PdfiumGuard guard;
    return FPDF_GetPageCount(impl_->handle);
}

QString PdfDocument::filePath() const {
    return impl_ ? impl_->path : QString();
}

DocumentPermissions PdfDocument::permissions() const {
    DocumentPermissions result;
    if (!impl_ || impl_->handle == nullptr) return result;
    const detail::PdfiumGuard guard;
    result.securityRevision = FPDF_GetSecurityHandlerRevision(impl_->handle);
    result.encrypted = result.securityRevision >= 0;
    if (!result.encrypted) return result;
    const unsigned long bits = FPDF_GetDocPermissions(impl_->handle);
    result.allowPrinting = (bits & (1UL << 2)) != 0;
    result.allowModifying = (bits & (1UL << 3)) != 0;
    result.allowCopying = (bits & (1UL << 4)) != 0;
    result.allowAnnotations = (bits & (1UL << 5)) != 0;
    result.allowFormFilling = (bits & (1UL << 8)) != 0;
    result.allowAccessibility = (bits & (1UL << 9)) != 0;
    result.allowAssembly = (bits & (1UL << 10)) != 0;
    result.allowHighQualityPrinting = (bits & (1UL << 11)) != 0;
    return result;
}

std::optional<PdfPage> PdfDocument::page(int index) const {
    if (!isValid() || index < 0 || index >= pageCount()) {
        return std::nullopt;
    }
    FPDF_PAGE page = nullptr;
    {
        const detail::PdfiumGuard guard;
        page = FPDF_LoadPage(impl_->handle, index);
    }
    if (page == nullptr) {
        return std::nullopt;
    }
    auto pageImpl = std::make_shared<detail::PageImpl>();
    pageImpl->document = impl_;
    pageImpl->page = page;
    pageImpl->index = index;
    return PdfPage(pageImpl);
}


// ---------------------------------------------------------------- 表单 ----

namespace {}  // namespace

QVector<FormField> PdfDocument::formFields() const {
    QVector<FormField> fields;
    if (!impl_ || impl_->handle == nullptr) {
        return fields;
    }
    const detail::PdfiumGuard guard;
    FPDF_FORMHANDLE form = impl_->ensureForm();
    if (form == nullptr) {
        return fields;
    }

    const int pages = FPDF_GetPageCount(impl_->handle);
    for (int pageIndex = 0; pageIndex < pages; ++pageIndex) {
        FPDF_PAGE page = FPDF_LoadPage(impl_->handle, pageIndex);
        if (page == nullptr) {
            continue;
        }
        const int annots = FPDFPage_GetAnnotCount(page);
        for (int i = 0; i < annots; ++i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (annot == nullptr) {
                continue;
            }
            const int type = FPDFAnnot_GetFormFieldType(form, annot);
            if (type <= 0) {
                FPDFPage_CloseAnnot(annot);
                continue;
            }
            FormField field;
            field.pageIndex = pageIndex;
            field.type = type;
            field.name = detail::readWideString([&](void* buffer, unsigned long size) {
                return FPDFAnnot_GetFormFieldName(
                    form, annot, static_cast<FPDF_WCHAR*>(buffer), size);
            });
            field.value = detail::readWideString([&](void* buffer, unsigned long size) {
                return FPDFAnnot_GetFormFieldValue(
                    form, annot, static_cast<FPDF_WCHAR*>(buffer), size);
            });
            FS_RECTF rect{};
            if (FPDFAnnot_GetRect(annot, &rect)) {
                field.rect = fromFsRect(rect);
            }
            const int flags = FPDFAnnot_GetFormFieldFlags(form, annot);
            field.readOnly = (flags & FPDF_FORMFLAG_READONLY) != 0;
            field.required = (flags & FPDF_FORMFLAG_REQUIRED) != 0;
            field.multiline = (flags & FPDF_FORMFLAG_TEXT_MULTILINE) != 0;
            field.toolTip = detail::readWideString([&](void* buffer, unsigned long size) {
                return FPDFAnnot_GetStringValue(annot, "TU", static_cast<FPDF_WCHAR*>(buffer), size);
            });
            const int optionCount = FPDFAnnot_GetOptionCount(form, annot);
            for (int option = 0; option < optionCount; ++option) {
                field.options.append(detail::readWideString([&](void* buffer, unsigned long size) {
                    return FPDFAnnot_GetOptionLabel(form, annot, option,
                                                    static_cast<FPDF_WCHAR*>(buffer), size);
                }));
            }

            fields.append(field);
            FPDFPage_CloseAnnot(annot);
        }
        FPDF_ClosePage(page);
    }
    return fields;
}

bool PdfDocument::setFormFieldValue(int pageIndex, const QString& name, const QString& value) {
    if (!impl_ || impl_->handle == nullptr) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDF_FORMHANDLE form = impl_->ensureForm();
    if (form == nullptr) {
        return false;
    }
    FPDF_PAGE page = FPDF_LoadPage(impl_->handle, pageIndex);
    if (page == nullptr) {
        return false;
    }
    bool changed = false;
    const int annots = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < annots && !changed; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (annot == nullptr) {
            continue;
        }
        const QString fieldName = detail::readWideString([&](void* buffer, unsigned long size) {
            return FPDFAnnot_GetFormFieldName(form, annot, static_cast<FPDF_WCHAR*>(buffer), size);
        });
        if (fieldName != name) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }
        const int type = FPDFAnnot_GetFormFieldType(form, annot);
        // 勾选/单选改的是"外观状态"（/AS），文本与下拉改的是值（/V）。
        const char* key = (type == FPDF_FORMFIELD_CHECKBOX || type == FPDF_FORMFIELD_RADIOBUTTON)
                              ? "AS"
                              : "V";
        const std::u16string wide = value.toStdU16String();
        changed = FPDFAnnot_SetStringValue(
                      annot, key,
                      reinterpret_cast<FPDF_WIDESTRING>(wide.c_str())) != 0;
        FPDFPage_CloseAnnot(annot);
    }
    FPDF_ClosePage(page);
    return changed;
}

bool PdfDocument::setFormFieldProperties(int pageIndex, const QString& oldName,
                                         const QString& newName, const QString& toolTip,
                                         bool readOnly, bool required, bool multiline) {
    if (!impl_ || impl_->handle == nullptr || newName != oldName) return false;
    const detail::PdfiumGuard guard;
    FPDF_FORMHANDLE form = impl_->ensureForm();
    if (form == nullptr) return false;
    FPDF_PAGE page = FPDF_LoadPage(impl_->handle, pageIndex);
    if (page == nullptr) return false;
    bool changed = false;
    const int annots = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < annots && !changed; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (annot == nullptr) continue;
        const QString fieldName = detail::readWideString([&](void* buffer, unsigned long size) {
            return FPDFAnnot_GetFormFieldName(form, annot, static_cast<FPDF_WCHAR*>(buffer), size);
        });
        if (fieldName != oldName) {
            FPDFPage_CloseAnnot(annot);
            continue;
        }
        int flags = FPDFAnnot_GetFormFieldFlags(form, annot);
        flags = readOnly ? (flags | FPDF_FORMFLAG_READONLY) : (flags & ~FPDF_FORMFLAG_READONLY);
        flags = required ? (flags | FPDF_FORMFLAG_REQUIRED) : (flags & ~FPDF_FORMFLAG_REQUIRED);
        flags = multiline ? (flags | FPDF_FORMFLAG_TEXT_MULTILINE)
                          : (flags & ~FPDF_FORMFLAG_TEXT_MULTILINE);
        const std::u16string tipWide = toolTip.toStdU16String();
        const bool tipSet = FPDFAnnot_SetStringValue(
                                annot, "TU",
                                reinterpret_cast<FPDF_WIDESTRING>(tipWide.c_str())) != 0;
        const bool flagsSet = FPDFAnnot_SetFormFieldFlags(form, annot, flags) != 0;
        changed = tipSet && flagsSet;
        FPDFPage_CloseAnnot(annot);
    }
    FPDF_ClosePage(page);
    return changed;
}

qint64 PdfDocument::bytesReadFromDisk() const {
    if (!impl_ || !impl_->reader) {
        return 0;
    }
    return impl_->reader->bytesReadFromDisk();
}

bool PdfDocument::holdsWholeFileInMemory() const {
    if (!impl_ || !impl_->reader) {
        return false;
    }
    return impl_->reader->holdsWholeFileInMemory();
}

bool PdfDocument::hasXfaForm() const {
    if (!impl_ || !impl_->reader) {
        return false;
    }
    if (!impl_->xfaChecked) {
        // 直接扫原始字节：XFA 是 AcroForm 里的 /XFA 键，PDFium 不给我们读它的接口。
        // 按路径打开的文档内存里没有整份字节，扫描器会分块读文件，不把文件读进内存。
        impl_->xfaPresent = documentContainsMarker(*impl_->reader, QByteArrayLiteral("/XFA"));
        impl_->xfaChecked = true;
    }
    return impl_->xfaPresent;
}

bool PdfDocument::appendDocumentPages(const PdfDocument& source) {
    if (!isValid() || !source.isValid()) {
        return false;
    }
    const detail::PdfiumGuard guard;
    // 空页码范围表示"全部页面"。
    //
    // 插入位置必须给当前页数：FPDF_ImportPages 的最后一个参数是 0 基的插入页号，
    // 传 0 会把对方的页插到**最前面**——与「合并（追加到末尾）」的预期正好相反，
    // 用户选了要合并的文件，结果它跑到了自己文档前面。
    const int insertAt = FPDF_GetPageCount(impl_->handle);
    return FPDF_ImportPages(impl_->handle, source.impl_->handle, "", insertAt) != 0;
}

QByteArray PdfDocument::extractPagesToBytes(const QVector<int>& pages) const {
    if (!isValid() || pages.isEmpty()) {
        return {};
    }
    const detail::PdfiumGuard guard;
    FPDF_DOCUMENT created = FPDF_CreateNewDocument();
    if (created == nullptr) {
        return {};
    }
    QStringList range;
    for (int page : pages) {
        range.append(QString::number(page + 1));  // PDFium 的页号从 1 开始
    }
    const QByteArray rangeBytes = range.join(',').toLatin1();
    QByteArray output;
    if (FPDF_ImportPages(created, impl_->handle, rangeBytes.constData(), 0)) {
        MemoryWriter writer;
        FPDF_FILEWRITE fileWrite = {};
        fileWrite.version = 1;
        fileWrite.WriteBlock = &writeToMemory;
        g_memoryWriterTarget = &writer;
        if (FPDF_SaveAsCopy(created, &fileWrite, 0)) {
            output = writer.data;
        }
        g_memoryWriterTarget = nullptr;
    }
    FPDF_CloseDocument(created);
    return output;
}

bool PdfDocument::flattenPage(int pageIndex) {
    if (!impl_ || impl_->handle == nullptr) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDF_PAGE page = FPDF_LoadPage(impl_->handle, pageIndex);
    if (page == nullptr) {
        return false;
    }
    const int result = FPDFPage_Flatten(page, FLAT_NORMALDISPLAY);
    FPDF_ClosePage(page);
    return result == FLATTEN_SUCCESS;
}

// ---------------------------------------------------------------- 附件 ----

namespace {

// 附件操作失败时统一给一句可读原因；error 可以为空。
void setAttachmentError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

QVector<DocumentAttachment> PdfDocument::attachments() const {
    QVector<DocumentAttachment> result;
    if (!impl_ || impl_->handle == nullptr) {
        return result;
    }
    const detail::PdfiumGuard guard;
    const int count = FPDFDoc_GetAttachmentCount(impl_->handle);
    for (int index = 0; index < count; ++index) {
        FPDF_ATTACHMENT attachment = FPDFDoc_GetAttachment(impl_->handle, index);
        if (attachment == nullptr) {
            continue;
        }
        DocumentAttachment entry;
        entry.name = detail::readWideString([&](void* buffer, unsigned long size) {
            return FPDFAttachment_GetName(attachment,
                                          static_cast<FPDF_WCHAR*>(buffer), size);
        });
        if (entry.name.isEmpty()) {
            // 名字读不出来的条目在界面上没有任何可操作的内容，跳过。
            continue;
        }
        entry.description = detail::readWideString([&](void* buffer, unsigned long size) {
            return FPDFAttachment_GetDescription(attachment,
                                                static_cast<FPDF_WCHAR*>(buffer), size);
        });
        entry.mimeType = detail::readWideString([&](void* buffer, unsigned long size) {
            return FPDFAttachment_GetSubtype(attachment,
                                            static_cast<FPDF_WCHAR*>(buffer), size);
        });
        if (entry.mimeType.isEmpty()) {
            // PDFium 的 FPDFAttachment_GetSubtype 读不回来——它是从 /Params 里读的，
            // 而 PDFium 自己写的时候就把键写进了 /Params（非标准位置）。
            // 所以这里按文件名后缀兜底推断：保存链路会用 qpdf 把真正的 /Subtype
            // 补进嵌入文件流（pdf::applyEmbeddedFileMimeTypes），但**读回**这一侧
            // 仍然只能靠文件名——不能因为读不回来就让界面上那一列永远空着。
            entry.mimeType = QString::fromLatin1(mimeTypeForFileName(entry.name));
        }
        unsigned long fileSize = 0;
        // 空缓冲只问长度：PDFium 文档说这时不会写 buffer。
        if (FPDFAttachment_GetFile(attachment, nullptr, 0, &fileSize)) {
            entry.size = static_cast<qint64>(fileSize);
        }
        result.append(entry);
    }
    return result;
}

QByteArray PdfDocument::attachmentData(int index, QString* error) const {
    if (!impl_ || impl_->handle == nullptr) {
        setAttachmentError(error, i18n::tr("aipdf::pdf::PdfiumAdapter", "文档无效"));
        return {};
    }
    const detail::PdfiumGuard guard;
    FPDF_ATTACHMENT attachment = FPDFDoc_GetAttachment(impl_->handle, index);
    if (attachment == nullptr) {
        setAttachmentError(error, i18n::tr("aipdf::pdf::PdfiumAdapter", "附件不存在"));
        return {};
    }
    unsigned long size = 0;
    if (!FPDFAttachment_GetFile(attachment, nullptr, 0, &size)) {
        setAttachmentError(error,
                           i18n::tr("aipdf::pdf::PdfiumAdapter", "附件内容读不出来"));
        return {};
    }
    if (size == 0) {
        return {};
    }
    QByteArray data(static_cast<int>(size), Qt::Uninitialized);
    unsigned long written = size;
    if (!FPDFAttachment_GetFile(attachment, data.data(), size, &written) || written > size) {
        setAttachmentError(error,
                           i18n::tr("aipdf::pdf::PdfiumAdapter", "附件内容读不出来"));
        return {};
    }
    data.truncate(static_cast<int>(written));
    return data;
}

bool PdfDocument::addAttachment(const QString& name,
                                const QByteArray& data,
                                const QString& description,
                                QString* error) {
    if (!impl_ || impl_->handle == nullptr) {
        setAttachmentError(error, i18n::tr("aipdf::pdf::PdfiumAdapter", "文档无效"));
        return false;
    }
    if (name.trimmed().isEmpty()) {
        setAttachmentError(error,
                           i18n::tr("aipdf::pdf::PdfiumAdapter", "附件名称不能为空"));
        return false;
    }
    if (data.isEmpty()) {
        setAttachmentError(error,
                           i18n::tr("aipdf::pdf::PdfiumAdapter", "附件内容为空"));
        return false;
    }

    const detail::PdfiumGuard guard;
    const std::vector<unsigned short> wideName = detail::toWideString(name);
    FPDF_ATTACHMENT attachment = FPDFDoc_AddAttachment(
        impl_->handle, reinterpret_cast<FPDF_WIDESTRING>(wideName.data()));
    if (attachment == nullptr) {
        // PDFium 只在这几种情况下失败：名字为空、重名，或名字树已经太深。
        setAttachmentError(
            error, i18n::tr("aipdf::pdf::PdfiumAdapter",
                            "加不进去：同名附件已存在，或附件数量已达上限"));
        return false;
    }
    if (!FPDFAttachment_SetFile(attachment, impl_->handle, data.constData(),
                                static_cast<unsigned long>(data.size()))) {
        // 半成品条目（有名字、没有内容）留着比没有更糟：删掉它再报错。
        for (int index = FPDFDoc_GetAttachmentCount(impl_->handle) - 1; index >= 0; --index) {
            if (FPDFDoc_GetAttachment(impl_->handle, index) == attachment) {
                FPDFDoc_DeleteAttachment(impl_->handle, index);
                break;
            }
        }
        setAttachmentError(error,
                           i18n::tr("aipdf::pdf::PdfiumAdapter", "附件内容写入失败"));
        return false;
    }
    // SetFile 会清掉除文件流以外的字典项，所以说明必须写在它后面。
    if (!description.isEmpty()) {
        const std::vector<unsigned short> wideDescription = detail::toWideString(description);
        FPDFAttachment_SetDescription(
            attachment, reinterpret_cast<FPDF_WIDESTRING>(wideDescription.data()));
    }
    // 这里**不写 MIME 类型**（/Subtype），是实测出来的限制，不是漏写：
    // PDFium 的嵌入文件接口只能把键写进 /Params（写出来的还是
    // /Params<</#2FSubtype(text/plain)…>> 这种非标准位置），够不到
    // 嵌入文件流字典；而 PDFium 自己的 FPDFAttachment_GetSubtype 也读不回来。
    // 写一个谁都读不到的位置只会让文件里多一个假键，不如不写。
    // 别的阅读器因此只能靠**文件名后缀**判断这个附件的类型。
    return true;
}

bool PdfDocument::removeAttachment(int index, QString* error) {
    if (!impl_ || impl_->handle == nullptr) {
        setAttachmentError(error, i18n::tr("aipdf::pdf::PdfiumAdapter", "文档无效"));
        return false;
    }
    const detail::PdfiumGuard guard;
    if (index < 0 || index >= FPDFDoc_GetAttachmentCount(impl_->handle)) {
        setAttachmentError(error, i18n::tr("aipdf::pdf::PdfiumAdapter", "附件不存在"));
        return false;
    }
    if (!FPDFDoc_DeleteAttachment(impl_->handle, index)) {
        setAttachmentError(error,
                           i18n::tr("aipdf::pdf::PdfiumAdapter", "附件删除失败"));
        return false;
    }
    return true;
}

bool PdfPage::removeImagesInRect(const QRectF& rectPoints) {
    if (!impl_ || impl_->page == nullptr || rectPoints.isEmpty()) {
        return false;
    }
    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    QVector<FPDF_PAGEOBJECT> victims;
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, i);
        if (object == nullptr || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE) {
            continue;
        }
        float left = 0.0F;
        float bottom = 0.0F;
        float right = 0.0F;
        float top = 0.0F;
        if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) {
            continue;
        }
        const QRectF objectRect(left, bottom, right - left, top - bottom);
        const QRectF overlap = objectRect.intersected(rectPoints);
        const double area = objectRect.width() * objectRect.height();
        if (area > 0.0 && overlap.width() * overlap.height() >= area * 0.5) {
            victims.append(object);
        }
    }
    if (victims.isEmpty()) {
        return false;
    }
    for (FPDF_PAGEOBJECT object : victims) {
        FPDFPage_RemoveObject(impl_->page, object);
    }
    return FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::addImageLayer(const QRectF& rectPoints, const QImage& image) {
    if (!impl_ || impl_->page == nullptr || image.isNull() || rectPoints.isEmpty()) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDF_DOCUMENT document = impl_->document ? impl_->document->handle : nullptr;
    if (document == nullptr) {
        return false;
    }
    FPDF_PAGEOBJECT object = FPDFPageObj_NewImageObj(document);
    if (object == nullptr) {
        return false;
    }
    // 用 PDFium 自己分配内存的位图：CreateEx 借的是我们的缓冲区，
    // 销毁位图之后图像对象就没有像素可读了，渲染出来是一片空白。
    const QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
    FPDF_BITMAP bitmap = FPDFBitmap_Create(rgba.width(), rgba.height(), 1);
    if (bitmap != nullptr) {
        auto* buffer = static_cast<uchar*>(FPDFBitmap_GetBuffer(bitmap));
        const int stride = FPDFBitmap_GetStride(bitmap);
        for (int y = 0; y < rgba.height(); ++y) {
            std::memcpy(buffer + y * stride, rgba.constScanLine(y),
                        static_cast<size_t>(rgba.width()) * 4);
        }
    }
    if (bitmap == nullptr) {
        FPDFPageObj_Destroy(object);
        return false;
    }
    bool ok = FPDFImageObj_SetBitmap(&impl_->page, 1, object, bitmap) != 0;
    FPDFBitmap_Destroy(bitmap);
    if (!ok) {
        FPDFPageObj_Destroy(object);
        return false;
    }
    // 图片对象默认是 1x1 单位：缩放到目标矩形再平移过去。
    FPDFPageObj_Transform(object, rectPoints.width(), 0.0, 0.0, rectPoints.height(),
                          rectPoints.left(), rectPoints.top());
    FPDFPage_InsertObject(impl_->page, object);
    return FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::replaceTextInRect(const QRectF& rectPoints, const QString& newText, double fontSize,
                                const QColor& color) {
    if (!impl_ || impl_->page == nullptr || rectPoints.isEmpty()) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDF_DOCUMENT document = impl_->document ? impl_->document->handle : nullptr;
    if (document == nullptr) {
        return false;
    }

    // 先完整创建替换对象。字体缺失或字符无法编码时，不得删除原文字。
    FPDF_PAGEOBJECT replacement = nullptr;
    if (!newText.isEmpty()) {
        FPDF_FONT font = loadEditingFont(impl_->document.get(), newText);
        if (font == nullptr) return false;
        replacement = createEditingTextObject(document, font, newText, fontSize);
        if (replacement == nullptr) return false;
        FPDFPageObj_SetFillColor(replacement, color.red(), color.green(), color.blue(), 255);
        FPDFPageObj_Transform(replacement, 1.0, 0.0, 0.0, 1.0,
                              rectPoints.left(), rectPoints.top() + fontSize * 0.2);
    }

    // 1) 只删除几乎完全落在目标区域内的文本对象。较低阈值会把横跨多行的
    // 共享文本对象整段删除；这类对象保留在底层，由白色遮盖块处理视觉替换。
    const int count = FPDFPage_CountObjects(impl_->page);
    QVector<FPDF_PAGEOBJECT> victims;
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, i);
        if (object == nullptr || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) {
            continue;
        }
        float left = 0.0F;
        float bottom = 0.0F;
        float right = 0.0F;
        float top = 0.0F;
        if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) {
            continue;
        }
        const QRectF objectRect(left, bottom, right - left, top - bottom);
        const QRectF overlap = objectRect.intersected(rectPoints);
        const double objectArea = objectRect.width() * objectRect.height();
        if (objectArea <= 0.0) {
            continue;
        }
        if (overlap.width() * overlap.height() >= objectArea * 0.9) {
            victims.append(object);
        }
    }
    for (FPDF_PAGEOBJECT object : victims) {
        FPDFPage_RemoveObject(impl_->page, object);
    }

    // 2) 白色遮盖块：原文的图形（下划线、边框）可能还在。
    FPDF_PAGEOBJECT cover = FPDFPageObj_CreateNewRect(
        static_cast<float>(rectPoints.left()), static_cast<float>(rectPoints.top()),
        static_cast<float>(rectPoints.width()), static_cast<float>(rectPoints.height()));
    if (cover != nullptr) {
        FPDFPageObj_SetFillColor(cover, 255, 255, 255, 255);
        FPDFPage_InsertObject(impl_->page, cover);
    }

    // 3) 最后插入新文字，保证位于白色遮盖块之上。
    if (replacement != nullptr) FPDFPage_InsertObject(impl_->page, replacement);
    const bool ok = FPDFPage_GenerateContent(impl_->page) != 0;
    return ok;
}

QVector<PageObjectInfo> PdfPage::objectsInRect(const QRectF& rectPoints,
                                                double minimumOverlap) const {
    QVector<PageObjectInfo> result;
    if (!impl_ || impl_->page == nullptr || rectPoints.isEmpty()) return result;
    minimumOverlap = std::clamp(minimumOverlap, 0.0, 1.0);
    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(impl_->page);
    for (int index = 0; index < count; ++index) {
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        if (object == nullptr) continue;
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) continue;
        const QRectF bounds(left, bottom, right - left, top - bottom);
        const QRectF overlap = bounds.intersected(rectPoints);
        const double area = bounds.width() * bounds.height();
        if (area <= 0.0 || overlap.width() * overlap.height() < area * minimumOverlap) continue;
        PageObjectInfo info;
        info.index = index;
        info.objectPath = {index};
        info.bounds = bounds;
        info.hasTransparency = FPDFPageObj_HasTransparency(object) != 0;
        if (FPDF_CLIPPATH clipPath = FPDFPageObj_GetClipPath(object))
            info.clipPathCount = qMax(0, FPDFClipPath_CountPaths(clipPath));
        switch (FPDFPageObj_GetType(object)) {
        case FPDF_PAGEOBJ_TEXT: info.type = PageObjectType::Text; break;
        case FPDF_PAGEOBJ_PATH: info.type = PageObjectType::Path; break;
        case FPDF_PAGEOBJ_IMAGE: info.type = PageObjectType::Image; break;
        case FPDF_PAGEOBJ_SHADING: info.type = PageObjectType::Shading; break;
        case FPDF_PAGEOBJ_FORM: info.type = PageObjectType::Form; break;
        default: break;
        }
        float size = 0.0F;
        if (info.type == PageObjectType::Text && FPDFTextObj_GetFontSize(object, &size)) info.fontSize = size;
        if (info.type == PageObjectType::Text) {
            FPDF_FONT font = FPDFTextObj_GetFont(object);
            if (font) {
                info.fontEmbedded = FPDFFont_GetIsEmbedded(font) == 1;
                info.fontFlags = FPDFFont_GetFlags(font);
                info.fontWeight = FPDFFont_GetWeight(font);
                FPDFFont_GetItalicAngle(font, &info.italicAngle);
            }
            const size_t length = font ? FPDFFont_GetFamilyName(font, nullptr, 0) : 0;
            if (length > 1 && length < 4096) {
                QByteArray name(static_cast<qsizetype>(length), Qt::Uninitialized);
                if (FPDFFont_GetFamilyName(font, name.data(), name.size()) > 0)
                    info.fontFamily = QString::fromUtf8(name.constData());
            }
            if (textPage) {
                const unsigned long bytes = FPDFTextObj_GetText(object, textPage, nullptr, 0);
                if (bytes >= 2 && bytes < 1024 * 1024) {
                    QVector<FPDF_WCHAR> buffer(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
                    if (FPDFTextObj_GetText(object, textPage, buffer.data(), bytes))
                        info.text = QString::fromUtf16(
                            reinterpret_cast<const char16_t*>(buffer.constData())).remove(QChar::Null);
                }
            }
        }
        unsigned int r = 0, g = 0, b = 0, a = 255;
        if (FPDFPageObj_GetFillColor(object, &r, &g, &b, &a)) info.fillColor = QColor(r, g, b, a);
        const int markCount = FPDFPageObj_CountMarks(object);
        for (int markIndex = 0; markIndex < markCount; ++markIndex) {
            FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex));
            unsigned long bytes = 0;
            if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes < 2) continue;
            QVector<FPDF_WCHAR> name(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
            if (!FPDFPageObjMark_GetName(mark, name.data(), bytes, &bytes)) continue;
            const QString markName = QString::fromUtf16(
                reinterpret_cast<const char16_t*>(name.constData())).remove(QChar::Null);
            if (markName == QStringLiteral("FolixaGroup")) {
                int value = -1;
                if (FPDFPageObjMark_GetParamIntValue(mark, "id", &value)) info.groupId = value;
            } else if (markName == QStringLiteral("FolixaLocked")) {
                info.locked = true;
            }
        }
        result.append(info);
    }
    if (textPage) FPDFText_ClosePage(textPage);
    return result;
}

QVector<PageObjectInfo> PdfPage::objectsInRectDeep(const QRectF& rectPoints,
                                                   double minimumOverlap) const {
    QVector<PageObjectInfo> result;
    if (!impl_ || !impl_->page || rectPoints.isEmpty()) return result;
    minimumOverlap = std::clamp(minimumOverlap, 0.0, 1.0);
    const detail::PdfiumGuard guard;
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(impl_->page);
    struct Matrix { double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };
    const auto compose = [](const Matrix& parent, const FS_MATRIX& local) {
        return Matrix{
            parent.a * local.a + parent.c * local.b,
            parent.b * local.a + parent.d * local.b,
            parent.a * local.c + parent.c * local.d,
            parent.b * local.c + parent.d * local.d,
            parent.a * local.e + parent.c * local.f + parent.e,
            parent.b * local.e + parent.d * local.f + parent.f};
    };
    const auto mapPoint = [](const Matrix& matrix, double x, double y) {
        return QPointF(matrix.a * x + matrix.c * y + matrix.e,
                       matrix.b * x + matrix.d * y + matrix.f);
    };
    std::function<void(FPDF_PAGEOBJECT, const QVector<int>&, const Matrix&)> visit;
    visit = [&](FPDF_PAGEOBJECT object, const QVector<int>& path, const Matrix& parent) {
        if (!object) return;
        float left = 0, bottom = 0, right = 0, top = 0;
        if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) return;
        const QPolygonF corners{mapPoint(parent, left, bottom), mapPoint(parent, right, bottom),
                                mapPoint(parent, right, top), mapPoint(parent, left, top)};
        const QRectF bounds = corners.boundingRect();
        const QRectF overlap = bounds.intersected(rectPoints);
        const double area = bounds.width() * bounds.height();
        if (area > 0.0 && overlap.width() * overlap.height() >= area * minimumOverlap) {
            PageObjectInfo info;
            info.index = path.size() == 1 ? path.constFirst() : -1;
            info.objectPath = path;
            info.nestingDepth = path.size() - 1;
            info.bounds = bounds;
            info.hasTransparency = FPDFPageObj_HasTransparency(object) != 0;
            if (FPDF_CLIPPATH clip = FPDFPageObj_GetClipPath(object))
                info.clipPathCount = qMax(0, FPDFClipPath_CountPaths(clip));
            switch (FPDFPageObj_GetType(object)) {
            case FPDF_PAGEOBJ_TEXT: info.type = PageObjectType::Text; break;
            case FPDF_PAGEOBJ_PATH: info.type = PageObjectType::Path; break;
            case FPDF_PAGEOBJ_IMAGE: info.type = PageObjectType::Image; break;
            case FPDF_PAGEOBJ_SHADING: info.type = PageObjectType::Shading; break;
            case FPDF_PAGEOBJ_FORM: info.type = PageObjectType::Form; break;
            default: break;
            }
            if (info.type == PageObjectType::Text) {
                float size = 0.0F;
                if (FPDFTextObj_GetFontSize(object, &size)) info.fontSize = size;
                if (FPDF_FONT font = FPDFTextObj_GetFont(object)) {
                    info.fontEmbedded = FPDFFont_GetIsEmbedded(font) == 1;
                    info.fontFlags = FPDFFont_GetFlags(font);
                    info.fontWeight = FPDFFont_GetWeight(font);
                    FPDFFont_GetItalicAngle(font, &info.italicAngle);
                    const size_t length = FPDFFont_GetFamilyName(font, nullptr, 0);
                    if (length > 1 && length < 4096) {
                        QByteArray name(static_cast<qsizetype>(length), Qt::Uninitialized);
                        if (FPDFFont_GetFamilyName(font, name.data(), name.size()) > 0)
                            info.fontFamily = QString::fromUtf8(name.constData());
                    }
                }
                if (textPage) {
                    const unsigned long bytes = FPDFTextObj_GetText(object, textPage, nullptr, 0);
                    if (bytes >= 2 && bytes < 1024 * 1024) {
                        QVector<FPDF_WCHAR> buffer(bytes / sizeof(FPDF_WCHAR));
                        if (FPDFTextObj_GetText(object, textPage, buffer.data(), bytes))
                            info.text = QString::fromUtf16(
                                reinterpret_cast<const char16_t*>(buffer.constData())).remove(QChar::Null);
                    }
                }
            }
            unsigned int red = 0, green = 0, blue = 0, alpha = 255;
            if (FPDFPageObj_GetFillColor(object, &red, &green, &blue, &alpha))
                info.fillColor = QColor(red, green, blue, alpha);
            result.append(info);
        }
        if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_FORM) return;
        FS_MATRIX local{};
        if (!FPDFPageObj_GetMatrix(object, &local)) local = FS_MATRIX{1, 0, 0, 1, 0, 0};
        const Matrix childParent = compose(parent, local);
        const int childCount = FPDFFormObj_CountObjects(object);
        for (int child = 0; child < childCount; ++child) {
            QVector<int> childPath = path;
            childPath.append(child);
            visit(FPDFFormObj_GetObject(object, static_cast<unsigned long>(child)), childPath,
                  childParent);
        }
    };
    const Matrix identity;
    const int count = FPDFPage_CountObjects(impl_->page);
    for (int index = 0; index < count; ++index)
        visit(FPDFPage_GetObject(impl_->page, index), QVector<int>{index}, identity);
    if (textPage) FPDFText_ClosePage(textPage);
    return result;
}

bool PdfPage::setTextObjectText(const QVector<int>& objectPath, const QString& text) {
    if (objectPath.isEmpty()) return false;
    if (objectPath.size() == 1) return setTextObjectText(objectPath.constFirst(), text);
    Q_UNUSED(text);
    // PDFium exposes mutable child handles, but FPDFPage_GenerateContent()
    // serializes only the page stream, not a nested Form XObject stream.  A
    // mutation would therefore disappear after save/reopen.  Reject it until
    // the qpdf resource-tree writer is used for this path.
    return false;
}

bool PdfPage::setTextObjectText(int index, const QString& text) {
    if (!impl_ || impl_->page == nullptr || index < 0 ||
        index >= FPDFPage_CountObjects(impl_->page)) return false;
    const detail::PdfiumGuard guard;
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
    if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) return false;
    QString normalizedText = text;
    normalizedText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalizedText.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (normalizedText.contains(QLatin1Char('\n'))) {
        float fontSize = 12.0F;
        FPDFTextObj_GetFontSize(object, &fontSize);
        QString family = QStringLiteral("Helvetica");
        if (FPDF_FONT oldFont = FPDFTextObj_GetFont(object)) {
            const size_t length = FPDFFont_GetFamilyName(oldFont, nullptr, 0);
            if (length > 1 && length < 4096) {
                QByteArray name(static_cast<qsizetype>(length), Qt::Uninitialized);
                if (FPDFFont_GetFamilyName(oldFont, name.data(), name.size()) > 0)
                    family = QString::fromUtf8(name.constData());
            }
        }
        // Reuse the original font first.  This preserves CID/subset font
        // metrics whenever PDFium can encode every requested line; only fall
        // back to an editing font when the original font rejects a glyph.
        FPDF_FONT originalFont = FPDFTextObj_GetFont(object);
        FPDF_FONT font = originalFont;
        if (!font)
            font = loadEditingFont(impl_->document.get(), normalizedText,
                                   QStringLiteral("Microsoft YaHei"));
        FS_MATRIX baseMatrix{};
        unsigned int red = 0, green = 0, blue = 0, alpha = 255;
        if (!font || !FPDFPageObj_GetMatrix(object, &baseMatrix) ||
            !FPDFPageObj_GetFillColor(object, &red, &green, &blue, &alpha)) return false;
        const FPDF_TEXT_RENDERMODE mode = FPDFTextObj_GetTextRenderMode(object);
        const QStringList lines = normalizedText.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        QVector<FPDF_PAGEOBJECT> replacements;
        replacements.reserve(lines.size());
        const double leading = qMax(1.0, static_cast<double>(fontSize) * 1.2);
        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            if (lines.at(lineIndex).isEmpty()) continue;
            FPDF_PAGEOBJECT replacement = createEditingTextObject(
                impl_->document->handle, font, lines.at(lineIndex), fontSize);
            if (!replacement && font == originalFont) {
                font = loadEditingFont(impl_->document.get(), normalizedText, family);
                if (!font)
                    font = loadEditingFont(impl_->document.get(), normalizedText,
                                           QStringLiteral("Microsoft YaHei"));
                replacement = font ? createEditingTextObject(
                    impl_->document->handle, font, lines.at(lineIndex), fontSize) : nullptr;
            }
            if (!replacement) {
                for (FPDF_PAGEOBJECT created : replacements) FPDFPageObj_Destroy(created);
                return false;
            }
            FS_MATRIX matrix = baseMatrix;
            matrix.e -= static_cast<float>(baseMatrix.c * leading * lineIndex);
            matrix.f -= static_cast<float>(baseMatrix.d * leading * lineIndex);
            FPDFPageObj_SetMatrix(replacement, &matrix);
            FPDFPageObj_SetFillColor(replacement, red, green, blue, alpha);
            copyPageObjectStrokeStyle(object, replacement);
            FPDFTextObj_SetTextRenderMode(replacement, mode);
            for (int markIndex = 0; markIndex < FPDFPageObj_CountMarks(object); ++markIndex)
                if (FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(
                        object, static_cast<unsigned long>(markIndex)))
                    FPDFPageObj_AddExistingMark(replacement, mark);
            replacements.append(replacement);
        }
        if (replacements.isEmpty()) {
            const std::u16string empty;
            if (!FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(empty.c_str())))
                return false;
            return FPDFPage_GenerateContent(impl_->page) != 0;
        }
        if (!FPDFPage_RemoveObject(impl_->page, object)) {
            for (FPDF_PAGEOBJECT replacement : replacements) FPDFPageObj_Destroy(replacement);
            return false;
        }
        for (int lineIndex = 0; lineIndex < replacements.size(); ++lineIndex)
            FPDFPage_InsertObjectAtIndex(impl_->page, replacements.at(lineIndex),
                                         static_cast<size_t>(index + lineIndex));
        FPDFPageObj_Destroy(object);
        return FPDFPage_GenerateContent(impl_->page) != 0;
    }
    const std::u16string wide = text.toStdU16String();
    if (!needsEmbeddedUnicodeFont(text) &&
        FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(wide.c_str())))
        return FPDFPage_GenerateContent(impl_->page) != 0;
    float fontSize = 12.0F;
    FPDFTextObj_GetFontSize(object, &fontSize);
    QString family = QStringLiteral("Microsoft YaHei");
    if (FPDF_FONT oldFont = FPDFTextObj_GetFont(object)) {
        const size_t length = FPDFFont_GetFamilyName(oldFont, nullptr, 0);
        if (length > 1 && length < 4096) {
            QByteArray name(static_cast<qsizetype>(length), Qt::Uninitialized);
            if (FPDFFont_GetFamilyName(oldFont, name.data(), name.size()) > 0)
                family = QString::fromUtf8(name.constData());
        }
    }
    FPDF_FONT font = loadEditingFont(impl_->document.get(), text, family);
    if (!font) font = loadEditingFont(impl_->document.get(), text, QStringLiteral("Microsoft YaHei"));
    FPDF_PAGEOBJECT replacement = font
        ? createEditingTextObject(impl_->document->handle, font, text, fontSize) : nullptr;
    if (!replacement) return false;
    FS_MATRIX matrix{};
    unsigned int r = 0, g = 0, b = 0, a = 255;
    if (!FPDFPageObj_GetMatrix(object, &matrix) ||
        !FPDFPageObj_SetMatrix(replacement, &matrix)) {
        FPDFPageObj_Destroy(replacement);
        return false;
    }
    FPDFPageObj_GetFillColor(object, &r, &g, &b, &a);
    FPDFPageObj_SetFillColor(replacement, r, g, b, a);
    copyPageObjectStrokeStyle(object, replacement);
    const FPDF_TEXT_RENDERMODE mode = FPDFTextObj_GetTextRenderMode(object);
    FPDFTextObj_SetTextRenderMode(replacement, mode);
    for (int markIndex = 0; markIndex < FPDFPageObj_CountMarks(object); ++markIndex)
        if (FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex)))
            FPDFPageObj_AddExistingMark(replacement, mark);
    if (!FPDFPage_RemoveObject(impl_->page, object) ||
        !FPDFPage_InsertObjectAtIndex(impl_->page, replacement, static_cast<size_t>(index))) {
        FPDFPageObj_Destroy(replacement);
        return false;
    }
    FPDFPageObj_Destroy(object);
    return FPDFPage_GenerateContent(impl_->page) != 0;
}

namespace {

// PDFium 的位图 → QImage。
//
// **字节序是这个函数存在的全部理由**：PDFium 给回来的位图是 BGRA
// （蓝、绿、红、alpha 四个字节依次排），而 QImage 的 32 位格式
// （Format_ARGB32 / Format_RGB32）存的是 0xAARRGGBB 这样的数值。
//
// 直接 memcpy 是不是就对？取决于机器的字节序：小端机上
// 0xAARRGGBB 在内存里恰好是 B,G,R,A，跟 PDFium 一样；大端机上就反了。
// 与其让「对不对」取决于运行平台，不如**逐像素显式换一次**：
// 任何机器上都对，代价只是慢一点（导出图片本来就不是热路径）。
//
// 顺带说明：Qt 里**没有** Format_ARGB32_BGRA 这个格式
// （Format_RGBA8888 的内存顺序才是 R,G,B,A），所以「换个格式包一下」
// 这条路走不通，只能自己换通道。
QImage bitmapToImage(FPDF_BITMAP bitmap) {
    if (bitmap == nullptr) {
        return QImage();
    }
    const int width = FPDFBitmap_GetWidth(bitmap);
    const int height = FPDFBitmap_GetHeight(bitmap);
    const int stride = FPDFBitmap_GetStride(bitmap);
    const auto* buffer = static_cast<const uchar*>(FPDFBitmap_GetBuffer(bitmap));
    if (width <= 0 || height <= 0 || buffer == nullptr) {
        return QImage();
    }

    const int format = FPDFBitmap_GetFormat(bitmap);
    if (format == FPDFBitmap_Gray) {
        QImage image(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y) {
            std::memcpy(image.scanLine(y), buffer + y * stride, static_cast<size_t>(width));
        }
        return image;
    }
    if (format == FPDFBitmap_BGR) {
        // 三个字节：蓝、绿、红。Qt 的 RGB888 是红、绿、蓝 —— 顺序相反，
        // 直接 memcpy 会把红蓝弄反（实测过：红色会变成蓝色）。
        QImage image(width, height, QImage::Format_RGB888);
        for (int y = 0; y < height; ++y) {
            const uchar* source = buffer + y * stride;
            uchar* target = image.scanLine(y);
            for (int x = 0; x < width; ++x) {
                target[x * 3 + 0] = source[x * 3 + 2];
                target[x * 3 + 1] = source[x * 3 + 1];
                target[x * 3 + 2] = source[x * 3 + 0];
            }
        }
        return image;
    }

    // BGRA / BGRx：四个字节，蓝、绿、红、alpha。
    QImage image(width, height, QImage::Format_ARGB32);
    const bool hasAlpha = format == FPDFBitmap_BGRA;
    for (int y = 0; y < height; ++y) {
        const uchar* source = buffer + y * stride;
        auto* target = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const uchar blue = source[x * 4 + 0];
            const uchar green = source[x * 4 + 1];
            const uchar red = source[x * 4 + 2];
            // BGRx 的第四个字节是「无用」，不是 alpha：当成 alpha 会整张图全透明。
            const uchar alpha = hasAlpha ? source[x * 4 + 3] : 255;
            target[x] = qRgba(red, green, blue, alpha);
        }
    }
    return image;
}

// 图片的落位矩形（PDF user space）。
//
// 顶层对象直接用 FPDFPageObj_GetBounds（图片对象的矩形已经是页面坐标）；
// Form XObject 里的图片要再乘上 Form 的变换矩阵，否则坐标是 Form 自己
// 那套坐标系里的，落到页面上会跑偏。
QRectF imageBounds(FPDF_PAGEOBJECT object, const double transform[6]) {
    float left = 0.0F;
    float bottom = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) {
        return QRectF();
    }
    const auto mapPoint = [transform](double x, double y) {
        return QPointF(transform[0] * x + transform[2] * y + transform[4],
                       transform[1] * x + transform[3] * y + transform[5]);
    };
    QPolygonF corners;
    corners << mapPoint(left, bottom) << mapPoint(right, bottom) << mapPoint(right, top)
            << mapPoint(left, top);
    return corners.boundingRect();
}

// 渲染位图的最大面积（平方点）。
//
// 渲染位图是按落位尺寸（点）开的，一页最大那么大。整页大图时
// 再开一块这么大的位图只为看有没有透明，不值当。
constexpr double kMaxRenderedAreaPoints = 4000000.0;

// 图里有没有「不完全不透明」的像素。
//
// 用来判断图片带不带软掩码（soft mask）：带掩码的图片，背景应该是透的。
bool hasTransparency(const QImage& image) {
    if (image.isNull() || !image.hasAlphaChannel()) {
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 255) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

bool PdfPage::replaceImageObject(int index, const QImage& image) {
    if (!impl_ || impl_->page == nullptr || image.isNull() || index < 0 ||
        index >= FPDFPage_CountObjects(impl_->page)) return false;
    const detail::PdfiumGuard guard;
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
    if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE) return false;
    const QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
    FPDF_BITMAP bitmap = FPDFBitmap_Create(rgba.width(), rgba.height(), 1);
    if (!bitmap) return false;
    auto* buffer = static_cast<uchar*>(FPDFBitmap_GetBuffer(bitmap));
    const int stride = FPDFBitmap_GetStride(bitmap);
    for (int y = 0; y < rgba.height(); ++y)
        std::memcpy(buffer + y * stride, rgba.constScanLine(y),
                    static_cast<size_t>(rgba.width()) * 4);
    const bool ok = FPDFImageObj_SetBitmap(&impl_->page, 1, object, bitmap) != 0;
    FPDFBitmap_Destroy(bitmap);
    return ok && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::cropImageObject(int index, const QRectF& normalizedCrop) {
    if (!impl_ || impl_->page == nullptr || index < 0 ||
        index >= FPDFPage_CountObjects(impl_->page)) return false;
    const QRectF crop = normalizedCrop.normalized().intersected(QRectF(0, 0, 1, 1));
    if (crop.isEmpty()) return false;
    const detail::PdfiumGuard guard;
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
    if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_IMAGE) return false;
    FPDF_BITMAP bitmap = FPDFImageObj_GetBitmap(object);
    if (!bitmap) return false;
    const QImage source = bitmapToImage(bitmap);
    FPDFBitmap_Destroy(bitmap);
    if (source.isNull()) return false;
    const int width = source.width();
    const int height = source.height();
    const QRect pixels(qFloor(crop.left() * width), qFloor(crop.top() * height),
                       qMax(1, qCeil(crop.width() * width)),
                       qMax(1, qCeil(crop.height() * height)));
    const bool replaced = replaceImageObject(index, source.copy(pixels.intersected(source.rect())));
    if (replaced) {
        for (int markIndex = FPDFPageObj_CountMarks(object) - 1; markIndex >= 0; --markIndex) {
            FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex));
            unsigned long bytes = 0;
            if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes < 2) continue;
            QVector<FPDF_WCHAR> name(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
            if (FPDFPageObjMark_GetName(mark, name.data(), bytes, &bytes) &&
                QString::fromUtf16(reinterpret_cast<const char16_t*>(name.constData()))
                        .remove(QChar::Null) == QStringLiteral("FolixaOpacity"))
                FPDFPageObj_RemoveMark(object, mark);
        }
    }
    return replaced;
}

QVector<PdfImage> PdfPage::extractImages(double maxDpi) const {
    QVector<PdfImage> images;
    if (!impl_ || !impl_->page) {
        return images;
    }
    const detail::PdfiumGuard guard;

    // 变换矩阵：Form XObject 里的图片要用它换算回页面坐标。
    struct Matrix {
        double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
    };
    const auto compose = [](const Matrix& parent, const FS_MATRIX& local) {
        return Matrix{parent.a * local.a + parent.c * local.b, parent.b * local.a + parent.d * local.b,
                      parent.a * local.c + parent.c * local.d, parent.b * local.c + parent.d * local.d,
                      parent.a * local.e + parent.c * local.f + parent.e,
                      parent.b * local.e + parent.d * local.f + parent.f};
    };

    // 把图片对象变成一条 PdfImage。
    const auto take = [&](FPDF_PAGEOBJECT object, const Matrix& parent) {
        const double transform[6] = {parent.a, parent.b, parent.c, parent.d, parent.e, parent.f};
        const QRectF bounds = imageBounds(object, transform);
        if (bounds.isEmpty()) {
            return;
        }

        // 原始位图：**原生分辨率**（不含掩码，也不含矩阵）。
        FPDF_BITMAP raw = FPDFImageObj_GetBitmap(object);
        QImage native = raw != nullptr ? bitmapToImage(raw) : QImage();
        if (raw != nullptr) {
            FPDFBitmap_Destroy(raw);
        }

        // 渲染位图：按**落位尺寸**画的（72 DPI），带上了软掩码（soft mask），
        // 也就是透明度。原生位图不带掩码。
        //
        // 它的大小是落位矩形（点），一页最多这么大，所以按面积设个上限：
        // 超大页面上的整页大图不值得为了透明度再开一块这么大的位图。
        QImage masked;
        if (bounds.width() * bounds.height() <= kMaxRenderedAreaPoints) {
            FPDF_BITMAP rendered =
                FPDFImageObj_GetRenderedBitmap(impl_->document->handle, impl_->page, object);
            if (rendered != nullptr) {
                masked = bitmapToImage(rendered);
                FPDFBitmap_Destroy(rendered);
            }
        }

        // 选哪一份：
        //   - 原生位图带透明 → 用它（分辨率高）；
        //   - 只有渲染位图带透明（软掩码）→ 用它（保透明要紧）；
        //   - 否则用像素多的那份（原生位图分辨率高时用它）。
        QImage picture;
        if (native.isNull()) {
            picture = masked;
        } else if (hasTransparency(masked) && !hasTransparency(native)) {
            picture = masked;
        } else if (masked.isNull()) {
            picture = native;
        } else {
            picture = native.width() * native.height() >= masked.width() * masked.height() ? native
                                                                                           : masked;
        }
        if (picture.isNull()) {
            return;
        }

        // DPI 上限：按落位尺寸算出这张图的实际分辨率，超了就等比缩小。
        //
        // 为什么要有上限：600 DPI 的扫描图按原样塞进包里，一个包就能到
        // 几百 MB（ZIP 又是 store 不压缩）。150 DPI 已经足够屏幕上看清。
        if (maxDpi > 0.0 && bounds.width() > 0.0 && bounds.height() > 0.0) {
            const double dpiX = picture.width() * 72.0 / bounds.width();
            const double dpiY = picture.height() * 72.0 / bounds.height();
            const double dpi = qMax(dpiX, dpiY);
            if (dpi > maxDpi) {
                const double factor = maxDpi / dpi;
                const QSize target(qMax(1, qRound(picture.width() * factor)),
                                   qMax(1, qRound(picture.height() * factor)));
                picture = picture.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }

        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly)) {
            return;
        }
        // PNG：无损、对 alpha 友好，而且 Qt 写出来的 PNG 不带时间戳，
        // 同样的输入永远得到同样的字节（导出结果可复现）。
        if (!picture.save(&buffer, "PNG")) {
            return;
        }
        buffer.close();
        if (encoded.isEmpty()) {
            return;
        }

        PdfImage image;
        image.pageIndex = impl_->index;
        image.bounds = bounds;
        image.bytes = encoded;
        image.mimeType = QStringLiteral("image/png");
        image.pixelWidth = picture.width();
        image.pixelHeight = picture.height();
        images.append(image);
    };

    // 递归：顶层对象 + Form XObject 里嵌套的对象。
    std::function<void(FPDF_PAGEOBJECT, const Matrix&)> visit;
    visit = [&](FPDF_PAGEOBJECT object, const Matrix& parent) {
        if (object == nullptr) {
            return;
        }
        const int type = FPDFPageObj_GetType(object);
        if (type == FPDF_PAGEOBJ_IMAGE) {
            take(object, parent);
            return;
        }
        if (type != FPDF_PAGEOBJ_FORM) {
            return;
        }
        FS_MATRIX local{};
        if (!FPDFPageObj_GetMatrix(object, &local)) {
            local = FS_MATRIX{1, 0, 0, 1, 0, 0};
        }
        const Matrix childParent = compose(parent, local);
        const int childCount = FPDFFormObj_CountObjects(object);
        for (int child = 0; child < childCount; ++child) {
            visit(FPDFFormObj_GetObject(object, static_cast<unsigned long>(child)), childParent);
        }
    };

    const Matrix identity;
    const int count = FPDFPage_CountObjects(impl_->page);
    for (int index = 0; index < count; ++index) {
        visit(FPDFPage_GetObject(impl_->page, index), identity);
    }

    // 按页面上的位置排序（从上到下）：导出时要按这个顺序把图片放进包里。
    std::sort(images.begin(), images.end(), [](const PdfImage& left, const PdfImage& right) {
        if (left.bounds.top() != right.bounds.top()) {
            return left.bounds.top() > right.bounds.top();
        }
        return left.bounds.left() < right.bounds.left();
    });
    return images;
}

bool PdfPage::setObjectOpacity(const QVector<int>& indexes, double opacity) {
    if (!impl_ || !impl_->page || indexes.isEmpty() || opacity < 0.0 || opacity > 1.0) return false;
    const detail::PdfiumGuard guard;
    bool changed = false;
    for (int index : indexes) {
        if (index < 0 || index >= FPDFPage_CountObjects(impl_->page)) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        if (object && FPDFPageObj_GetType(object) == FPDF_PAGEOBJ_IMAGE) {
            FPDF_PAGEOBJECTMARK opacityMark = nullptr;
            for (int markIndex = 0; markIndex < FPDFPageObj_CountMarks(object); ++markIndex) {
                FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex));
                unsigned long bytes = 0;
                if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes < 2) continue;
                QVector<FPDF_WCHAR> name(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
                if (FPDFPageObjMark_GetName(mark, name.data(), bytes, &bytes) &&
                    QString::fromUtf16(reinterpret_cast<const char16_t*>(name.constData()))
                            .remove(QChar::Null) == QStringLiteral("FolixaOpacity")) {
                    opacityMark = mark;
                    break;
                }
            }
            FPDF_BITMAP bitmap = FPDFImageObj_GetBitmap(object);
            if (!bitmap) continue;
            QImage image = bitmapToImage(bitmap);
            FPDFBitmap_Destroy(bitmap);
            if (image.isNull()) continue;
            image = image.convertToFormat(QImage::Format_ARGB32);
            if (opacityMark) {
                unsigned long sourceSize = 0;
                if (FPDFPageObjMark_GetParamBlobValue(opacityMark, "source", nullptr, 0, &sourceSize) &&
                    sourceSize > 0 && sourceSize < 256UL * 1024UL * 1024UL) {
                    QByteArray source(static_cast<qsizetype>(sourceSize), Qt::Uninitialized);
                    if (FPDFPageObjMark_GetParamBlobValue(
                            opacityMark, "source", reinterpret_cast<unsigned char*>(source.data()),
                            sourceSize, &sourceSize)) {
                        const QImage original = QImage::fromData(source, "PNG");
                        if (!original.isNull()) image = original.convertToFormat(QImage::Format_ARGB32);
                    }
                }
            }
            QByteArray originalPng;
            if (!opacityMark) {
                QBuffer pngBuffer(&originalPng);
                pngBuffer.open(QIODevice::WriteOnly);
                image.save(&pngBuffer, "PNG");
            }
            for (int y = 0; y < image.height(); ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]),
                                    qBound(0, qRound(qAlpha(line[x]) * opacity), 255));
            }
            if (replaceImageObject(index, image)) {
                if (!opacityMark) opacityMark = FPDFPageObj_AddMark(object, "FolixaOpacity");
                if (opacityMark) {
                    if (!originalPng.isEmpty())
                        FPDFPageObjMark_SetBlobParam(
                            impl_->document->handle, object, opacityMark, "source",
                            reinterpret_cast<const unsigned char*>(originalPng.constData()),
                            static_cast<unsigned long>(originalPng.size()));
                    FPDFPageObjMark_SetFloatParam(impl_->document->handle, object, opacityMark,
                                                  "value", static_cast<float>(opacity));
                }
                changed = true;
            }
            continue;
        }
        unsigned int r = 255, g = 255, b = 255, a = 255;
        if (object && FPDFPageObj_GetFillColor(object, &r, &g, &b, &a) &&
            FPDFPageObj_SetFillColor(object, r, g, b, qRound(opacity * 255.0))) changed = true;
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::reorderObject(int index, int targetIndex) {
    if (!impl_ || !impl_->page) return false;
    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    if (index < 0 || index >= count || targetIndex < 0 || targetIndex >= count || index == targetIndex)
        return false;
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
    if (!object || !FPDFPage_RemoveObject(impl_->page, object)) return false;
    if (!FPDFPage_InsertObjectAtIndex(impl_->page, object, static_cast<size_t>(targetIndex))) {
        FPDFPage_InsertObjectAtIndex(impl_->page, object, static_cast<size_t>(qMin(index, count - 1)));
        return false;
    }
    return FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::setObjectsGroup(const QVector<int>& indexes, int groupId) {
    if (!impl_ || !impl_->page || !impl_->document || groupId < 0) return false;
    const detail::PdfiumGuard guard;
    bool changed = false;
    for (int index : indexes) {
        if (index < 0 || index >= FPDFPage_CountObjects(impl_->page)) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        if (!object) continue;
        FPDF_PAGEOBJECTMARK mark = FPDFPageObj_AddMark(object, "FolixaGroup");
        if (mark && FPDFPageObjMark_SetIntParam(impl_->document->handle, object, mark, "id", groupId))
            changed = true;
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::clearObjectsGroup(const QVector<int>& indexes) {
    if (!impl_ || !impl_->page) return false;
    const detail::PdfiumGuard guard;
    bool changed = false;
    for (int index : indexes) {
        if (index < 0 || index >= FPDFPage_CountObjects(impl_->page)) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        for (int markIndex = FPDFPageObj_CountMarks(object) - 1; markIndex >= 0; --markIndex) {
            FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex));
            unsigned long bytes = 0;
            if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes < 2) continue;
            QVector<FPDF_WCHAR> name(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
            if (FPDFPageObjMark_GetName(mark, name.data(), bytes, &bytes) &&
                QString::fromUtf16(reinterpret_cast<const char16_t*>(name.constData()))
                        .remove(QChar::Null) == QStringLiteral("FolixaGroup")) {
                changed = FPDFPageObj_RemoveMark(object, mark) || changed;
            }
        }
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::setObjectsLocked(const QVector<int>& indexes, bool locked) {
    if (!impl_ || !impl_->page) return false;
    const detail::PdfiumGuard guard;
    bool changed = false;
    for (int index : indexes) {
        if (index < 0 || index >= FPDFPage_CountObjects(impl_->page)) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        if (!object) continue;
        if (locked) {
            changed = FPDFPageObj_AddMark(object, "FolixaLocked") != nullptr || changed;
        } else {
            for (int markIndex = FPDFPageObj_CountMarks(object) - 1; markIndex >= 0; --markIndex) {
                FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex));
                unsigned long bytes = 0;
                if (!mark || !FPDFPageObjMark_GetName(mark, nullptr, 0, &bytes) || bytes < 2) continue;
                QVector<FPDF_WCHAR> name(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
                if (FPDFPageObjMark_GetName(mark, name.data(), bytes, &bytes) &&
                    QString::fromUtf16(reinterpret_cast<const char16_t*>(name.constData()))
                            .remove(QChar::Null) == QStringLiteral("FolixaLocked"))
                    changed = FPDFPageObj_RemoveMark(object, mark) || changed;
            }
        }
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::transformObjects(const QVector<int>& indexes, double translateX, double translateY,
                               double scaleX, double scaleY) {
    if (!impl_ || impl_->page == nullptr || indexes.isEmpty() || scaleX <= 0.0 || scaleY <= 0.0) return false;
    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    QRectF groupBounds;
    for (int index : indexes) {
        if (index < 0 || index >= count) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        float left = 0, bottom = 0, right = 0, top = 0;
        if (object && FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top))
            groupBounds = groupBounds.united(QRectF(left, bottom, right - left, top - bottom));
    }
    if (groupBounds.isEmpty()) return false;
    bool changed = false;
    QVector<int> sorted = indexes;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int index : sorted) {
        if (index < 0 || index >= count) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        if (!object) continue;
        FPDFPageObj_Transform(object, scaleX, 0.0, 0.0, scaleY,
                              translateX + groupBounds.left() * (1.0 - scaleX),
                              translateY + groupBounds.top() * (1.0 - scaleY));
        changed = true;
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::rotateObjects(const QVector<int>& indexes, double degrees) {
    if (!impl_ || impl_->page == nullptr || indexes.isEmpty() || qFuzzyIsNull(degrees)) return false;
    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    QRectF groupBounds;
    for (int index : indexes) {
        if (index < 0 || index >= count) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        float left = 0, bottom = 0, right = 0, top = 0;
        if (object && FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top))
            groupBounds = groupBounds.united(QRectF(left, bottom, right - left, top - bottom));
    }
    if (groupBounds.isEmpty()) return false;
    const double radians = qDegreesToRadians(degrees);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const QPointF center = groupBounds.center();
    const double translateX = center.x() - cosine * center.x() + sine * center.y();
    const double translateY = center.y() - sine * center.x() - cosine * center.y();
    bool changed = false;
    for (int index : indexes) {
        if (index < 0 || index >= count) continue;
        if (FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index)) {
            FPDFPageObj_Transform(object, cosine, sine, -sine, cosine, translateX, translateY);
            changed = true;
        }
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::removeObjects(const QVector<int>& indexes) {
    if (!impl_ || impl_->page == nullptr || indexes.isEmpty()) return false;
    const detail::PdfiumGuard guard;
    QVector<int> sorted = indexes;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    bool changed = false;
    for (int index : sorted) {
        if (index < 0 || index >= FPDFPage_CountObjects(impl_->page)) continue;
        if (FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index)) {
            FPDFPage_RemoveObject(impl_->page, object);
            FPDFPageObj_Destroy(object);
            changed = true;
        }
    }
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::setTextObjectProperties(const QVector<int>& indexes, const QString& fontFamily,
                                      double fontSize, const QColor& color, double opacity,
                                      double horizontalScale, double baselineShift) {
    if (!impl_ || impl_->page == nullptr || indexes.isEmpty() || fontSize <= 0.0 ||
        opacity < 0.0 || opacity > 1.0 || horizontalScale <= 0.0) return false;
    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(impl_->page);
    if (!textPage) return false;
    bool changed = false;
    QVector<int> sorted = indexes;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int index : sorted) {
        if (index < 0 || index >= count) continue;
        FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, index);
        if (!object || FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_TEXT) continue;
        const unsigned long bytes = FPDFTextObj_GetText(object, textPage, nullptr, 0);
        if (bytes < 2 || bytes > 1024 * 1024) continue;
        QVector<FPDF_WCHAR> buffer(static_cast<qsizetype>(bytes / sizeof(FPDF_WCHAR)));
        if (!FPDFTextObj_GetText(object, textPage, buffer.data(), bytes)) continue;
        const QString text = QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.constData()));
        FPDF_FONT font = loadEditingFont(impl_->document.get(), text, fontFamily);
        if (!font) continue;
        FPDF_PAGEOBJECT replacement = createEditingTextObject(impl_->document->handle, font, text, fontSize);
        if (!replacement) continue;
        FS_MATRIX matrix{};
        if (!FPDFPageObj_GetMatrix(object, &matrix) || !FPDFPageObj_SetMatrix(replacement, &matrix)) {
            FPDFPageObj_Destroy(replacement);
            continue;
        }
        matrix.a *= horizontalScale;
        matrix.b *= horizontalScale;
        matrix.f += baselineShift;
        FPDFPageObj_SetMatrix(replacement, &matrix);
        FPDFPageObj_SetFillColor(replacement, color.red(), color.green(), color.blue(),
                                 qRound(255.0 * opacity));
        copyPageObjectStrokeStyle(object, replacement,
                                  static_cast<unsigned int>(qRound(255.0 * opacity)));
        const FPDF_TEXT_RENDERMODE mode = FPDFTextObj_GetTextRenderMode(object);
        FPDFTextObj_SetTextRenderMode(replacement, mode);
        for (int markIndex = 0; markIndex < FPDFPageObj_CountMarks(object); ++markIndex)
            if (FPDF_PAGEOBJECTMARK mark = FPDFPageObj_GetMark(object, static_cast<unsigned long>(markIndex)))
                FPDFPageObj_AddExistingMark(replacement, mark);
        FPDFPage_RemoveObject(impl_->page, object);
        FPDFPage_InsertObjectAtIndex(impl_->page, replacement, static_cast<size_t>(index));
        FPDFPageObj_Destroy(object);
        changed = true;
    }
    FPDFText_ClosePage(textPage);
    return changed && FPDFPage_GenerateContent(impl_->page) != 0;
}

bool PdfPage::redactByRasterizing(const QRectF& rectPoints, double scale) {
    return redactByRasterizing(QVector<QRectF>{rectPoints}, scale);
}

bool PdfPage::redactByRasterizing(const QVector<QRectF>& rects, double scale) {
    if (!isValid() || rects.isEmpty() || scale <= 0.0 || rotationDegrees() != 0) {
        return false;
    }
    const MediaBox box = contentBox();
    const QRectF pageRect(QPointF(box.left, box.bottom), QPointF(box.right, box.top));
    QVector<QRectF> clipped;
    clipped.reserve(rects.size());
    for (const QRectF& rect : rects) {
        if (rect.isEmpty()) {
            continue;
        }
        const QRectF intersection = rect.normalized().intersected(pageRect.normalized());
        if (!intersection.isEmpty()) {
            clipped.append(intersection);
        }
    }
    if (clipped.isEmpty()) return false;

    QImage image = render(scale);
    if (image.isNull()) return false;
    {
        QPainter painter(&image);
        for (const QRectF& area : clipped) {
            const QRect pixelRect(
                qFloor((area.left() - box.left) * scale),
                qFloor((box.top - area.bottom()) * scale),
                qCeil(area.width() * scale), qCeil(area.height() * scale));
            painter.fillRect(pixelRect.adjusted(-2, -2, 2, 2), Qt::black);
        }
    }

    const detail::PdfiumGuard guard;
    const int count = FPDFPage_CountObjects(impl_->page);
    QVector<FPDF_PAGEOBJECT> objects;
    objects.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (FPDF_PAGEOBJECT object = FPDFPage_GetObject(impl_->page, i)) objects.append(object);
    }
    for (FPDF_PAGEOBJECT object : objects) FPDFPage_RemoveObject(impl_->page, object);
    for (int index = FPDFPage_GetAnnotCount(impl_->page) - 1; index >= 0; --index) {
        FPDFPage_RemoveAnnot(impl_->page, index);
    }
    if (!FPDFPage_GenerateContent(impl_->page)) return false;

    // addImageLayer 自己会拿 PDFium 锁；先退出当前临界区需要显式分段。
    // PdfiumGuard 是递归锁，直接调用也是安全的。
    return addImageLayer(pageRect.normalized(), image);
}

bool PdfPage::addTextLayer(const QString& text, double xPoints, double yPoints, double fontSize,
                           const QColor& color, double rotationDegrees, double opacity,
                           const QString& fontFamily, double horizontalScale) {
    if (!impl_ || impl_->page == nullptr || text.isEmpty() || fontSize <= 0.0) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDF_DOCUMENT document = impl_->document ? impl_->document->handle : nullptr;
    if (document == nullptr) {
        return false;
    }
    FPDF_FONT font = loadEditingFont(impl_->document.get(), text, fontFamily);
    if (font == nullptr) {
        return false;
    }
    FPDF_PAGEOBJECT object = createEditingTextObject(document, font, text, fontSize);
    if (object == nullptr) {
        return false;
    }
    FPDFPageObj_SetFillColor(object, color.red(), color.green(), color.blue(),
                             static_cast<unsigned int>(std::clamp(opacity, 0.0, 1.0) * 255.0));
    // 先平移到基线起点，再绕该点旋转。
    FPDFPageObj_Transform(object, horizontalScale, 0.0, 0.0, 1.0, xPoints, yPoints);
    if (std::abs(rotationDegrees) > 0.001) {
        const double radians = rotationDegrees * M_PI / 180.0;
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        FPDFPageObj_Transform(object, c, s, -s, c, 0.0, 0.0);
    }
    FPDFPage_InsertObject(impl_->page, object);
    const bool ok = FPDFPage_GenerateContent(impl_->page) != 0;
    return ok;
}

std::vector<OutlineItem> PdfDocument::outline() const {
    if (!isValid()) {
        return {};
    }

    // 大纲遍历全程在锁内。
    const detail::PdfiumGuard guard;

    std::vector<OutlineItem> result;
    int itemCount = 0;
    readOutlineLevel(impl_->handle, nullptr, &result, 0, &itemCount);
    return result;
}

// ----------------------------------------------------------- 页面结构操作 ----

bool PdfDocument::insertBlankPage(int index,
                                  double width,
                                  double height,
                                  QVector<int>* pageMapping) {
    if (!isValid() || width <= 0.0 || height <= 0.0) {
        return false;
    }
    const int count = pageCount();
    if (index < 0 || index > count) {
        return false;
    }

    const detail::PdfiumGuard guard;
    FPDF_PAGE page = FPDFPage_New(impl_->handle,
                                   index,
                                   static_cast<float>(width),
                                   static_cast<float>(height));
    if (page == nullptr) {
        return false;
    }
    FPDF_ClosePage(page);

    // 批注不用记账：认批注靠 /NM 标记，与它在页面上的位置无关。
    if (pageMapping != nullptr) {
        pageMapping->resize(count);
        for (int i = 0; i < count; ++i) {
            (*pageMapping)[i] = (i < index) ? i : i + 1;
        }
    }
    return true;
}

bool PdfDocument::deletePage(int index, QVector<int>* pageMapping) {
    if (!isValid()) {
        return false;
    }
    const int count = pageCount();
    if (index < 0 || index >= count) {
        return false;
    }

    {
        const detail::PdfiumGuard guard;
        FPDFPage_Delete(impl_->handle, index);
    }
    if (pageCount() != count - 1) {
        return false;
    }

    if (pageMapping != nullptr) {
        pageMapping->resize(count);
        for (int i = 0; i < count; ++i) {
            (*pageMapping)[i] = (i < index) ? i : (i == index ? -1 : i - 1);
        }
    }
    return true;
}

bool PdfDocument::setPageRotation(int index, int quarterTurns) {
    if (!isValid()) {
        return false;
    }
    const detail::PdfiumGuard guard;
    FPDF_PAGE page = FPDF_LoadPage(impl_->handle, index);
    if (page == nullptr) {
        return false;
    }
    // FPDFPage_SetRotation 收的是**四分之一圈**（0..3），不是角度。
    FPDFPage_SetRotation(page, ((quarterTurns % 4) + 4) % 4);
    FPDF_ClosePage(page);
    return true;
}

bool PdfDocument::movePage(int from, int to, QVector<int>* pageMapping) {
    if (!isValid()) {
        return false;
    }
    const int count = pageCount();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return false;
    }

    // PDFium 159 没有移动页的接口，只能「导入副本 + 删除原件」。
    //
    // 副本先落到一份临时文档里，再从临时文档导回主文档：
    // 同一份文档自导自入 PDFium 不保证安全。
    FPDF_DOCUMENT scratch = nullptr;
    {
        const detail::PdfiumGuard guard;
        scratch = FPDF_CreateNewDocument();
        if (scratch == nullptr) {
            return false;
        }
        const int sourceIndex = from;
        if (!FPDF_ImportPagesByIndex(scratch, impl_->handle, &sourceIndex, 1, 0)) {
            FPDF_CloseDocument(scratch);
            return false;
        }
    }

    // 副本插回主文档的位置。
    //
    // 把 from 删掉之后，剩下的序列是 R；副本要落在最终序列的第 to 位，
    // 也就是插进 R 的第 to 个位置。而 R 的下标与主文档当下标号的差别
    // 取决于 from 在前面还是后面：
    //   - from 在 to 前面（to >= from）：主文档里插到 to + 1，
    //     删掉原件后副本正好落到 to；
    //   - from 在 to 后面：主文档里插到 to，删掉原件后副本落在 to。
    const int insertAt = (to >= from) ? to + 1 : to;
    {
        const detail::PdfiumGuard guard;
        const int scratchIndex = 0;
        if (!FPDF_ImportPagesByIndex(impl_->handle, scratch, &scratchIndex, 1, insertAt)) {
            FPDF_CloseDocument(scratch);
            return false;
        }
        // 删掉原件：它在插入之后的下标是 from + (insertAt <= from ? 1 : 0)。
        const int originalAt = (insertAt <= from) ? from + 1 : from;
        FPDFPage_Delete(impl_->handle, originalAt);
        FPDF_CloseDocument(scratch);
    }

    if (pageCount() != count) {
        return false;
    }

    if (pageMapping != nullptr) {
        pageMapping->resize(count);
        for (int i = 0; i < count; ++i) {
            if (i == from) {
                (*pageMapping)[i] = to;
            } else if (to > from && i > from && i <= to) {
                (*pageMapping)[i] = i - 1;
            } else if (to < from && i >= to && i < from) {
                (*pageMapping)[i] = i + 1;
            } else {
                (*pageMapping)[i] = i;
            }
        }
    }
    return true;
}

PdfDocument PdfDocument::backupPage(int index) const {
    if (!isValid()) {
        return PdfDocument();
    }
    const int count = pageCount();
    if (index < 0 || index >= count) {
        return PdfDocument();
    }

    const detail::PdfiumGuard guard;
    FPDF_DOCUMENT scratch = FPDF_CreateNewDocument();
    if (scratch == nullptr) {
        return PdfDocument();
    }
    const int sourceIndex = index;
    if (!FPDF_ImportPagesByIndex(scratch, impl_->handle, &sourceIndex, 1, 0)) {
        FPDF_CloseDocument(scratch);
        return PdfDocument();
    }
    return adoptHandle(scratch, QString());
}

bool PdfDocument::restorePageFrom(const PdfDocument& backup, int index) {
    if (!isValid() || !backup.isValid()) {
        return false;
    }
    const int count = pageCount();
    if (index < 0 || index > count) {
        return false;
    }
    const detail::PdfiumGuard guard;
    const int scratchIndex = 0;
    if (!FPDF_ImportPagesByIndex(impl_->handle,
                                 backup.impl_->handle,
                                 &scratchIndex,
                                 1,
                                 index)) {
        return false;
    }
    return pageCount() == count + 1;
}

QByteArray PdfDocument::serialize(QString* error) const {
    if (!isValid()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::pdf::PdfiumAdapter", "文档无效");
        }
        return {};
    }

    const detail::PdfiumGuard guard;

    MemoryWriter writer;
    FPDF_FILEWRITE fileWrite = {};
    fileWrite.version = 1;
    fileWrite.WriteBlock = &writeToMemory;
    g_memoryWriterTarget = &writer;
    const FPDF_BOOL saved = FPDF_SaveAsCopy(impl_->handle, &fileWrite, 0);
    g_memoryWriterTarget = nullptr;

    if (!saved) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::pdf::PdfiumAdapter", "PDFium 保存失败");
        }
        return {};
    }
    return writer.data;
}

void PdfDocument::syncBytes(const QByteArray& bytes) {
    if (impl_ == nullptr) {
        return;
    }
    // 换成内存字节来源：文档的内容已经与磁盘上的文件不同（改过了），
    // 再按路径读就会读到旧内容。
    impl_->reader = DocumentReader::fromBytes(bytes);
    impl_->access.m_FileLen = static_cast<unsigned long>(bytes.size());
    // 内容换了，XFA 的判定是按字节扫出来的，缓存一起作废。
    impl_->xfaChecked = false;
    impl_->xfaPresent = false;
}

bool PdfDocument::reloadFromBytes(const QByteArray& bytes, QString* error) {
    if (!impl_ || bytes.isEmpty()) return false;
    initializePdfium();
    const detail::PdfiumGuard guard;

    // 关键：FPDF_FILEACCESS 必须活在**文档实现对象自己**身上。
    //
    // PDFium 会保存这个结构（或至少保存其中的 m_Param）指向的内存；以前这里
    // 用一个临时的 replacement 对象去加载，再把 access swap 过来，函数返回时
    // replacement 析构，PDFium 手里的 m_Param 就悬空了 —— 重载之后的第一次
    // 懒加载就是 UAF。现在直接改动 impl_->access，让 PDFium 拿到的指针与
    // 文档同寿。
    auto newReader = DocumentReader::fromBytes(bytes);
    if (!newReader) {
        if (error) *error = i18n::tr("aipdf::pdf::PdfiumAdapter", "无法读取新的文档字节");
        return false;
    }

    // 换 reader 之前先备份旧的：加载失败要原样退回去，旧句柄仍然可用。
    const std::shared_ptr<DocumentReader> oldReader = impl_->reader;
    const FPDF_FILEACCESS oldAccess = impl_->access;

    impl_->reader = newReader;
    impl_->access = FPDF_FILEACCESS{};
    impl_->access.m_FileLen = static_cast<unsigned long>(bytes.size());
    impl_->access.m_GetBlock = &detail::DocumentImpl::readBlock;
    impl_->access.m_Param = impl_.get();

    const QByteArray password = impl_->password.toUtf8();
    const FPDF_DOCUMENT handle = FPDF_LoadCustomDocument(
        &impl_->access, password.isEmpty() ? nullptr : password.constData());
    if (handle == nullptr) {
        impl_->reader = oldReader;
        impl_->access = oldAccess;
        if (error) *error = describePdfiumError(FPDF_GetLastError());
        return false;
    }

    // 新句柄已经加载成功，再收旧句柄：旧文档的字体对象属于旧文档，先关字体。
    // 表单环境按本文件既有约定不调用 Exit（固定构建会崩），随文档一起丢弃。
    if (impl_->handle != nullptr) {
        if (impl_->latinEditingFont != nullptr) FPDFFont_Close(impl_->latinEditingFont);
        if (impl_->unicodeEditingFont != nullptr) FPDFFont_Close(impl_->unicodeEditingFont);
        for (FPDF_FONT font : std::as_const(impl_->editingFonts)) FPDFFont_Close(font);
        FPDF_CloseDocument(impl_->handle);
    }
    impl_->handle = handle;
    impl_->form = nullptr;
    impl_->latinEditingFont = nullptr;
    impl_->unicodeEditingFont = nullptr;
    impl_->editingFonts.clear();
    impl_->access.m_Param = impl_.get();
    // 内容换了，XFA 的判定缓存一起作废。
    impl_->xfaChecked = false;
    impl_->xfaPresent = false;
    if (error) error->clear();
    return true;
}

PdfDocument PdfDocument::adoptHandle(void* handle, const QString& path) {
    if (handle == nullptr) {
        return PdfDocument();
    }
    auto impl = std::make_shared<detail::DocumentImpl>();
    impl->path = path;
    impl->handle = static_cast<FPDF_DOCUMENT>(handle);
    return PdfDocument(impl);
}

// ------------------------------------------------------------ 打开文档 ----

PdfOpenResult openDocument(const QString& filePath, const QString& password) {
    PdfOpenResult result;
    initializePdfium();

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        result.errorMessage =
            i18n::tr("aipdf::pdf::PdfiumAdapter", "文件不存在：%1").arg(filePath);
        return result;
    }
    if (info.size() > kMaxPdfFileSizeBytes) {
        result.errorMessage =
            i18n::tr("aipdf::pdf::PdfiumAdapter", "文件过大：%1 字节，上限 %2 字节")
                .arg(info.size())
                .arg(kMaxPdfFileSizeBytes);
        return result;
    }

    // 按路径打开：文件留在磁盘上，PDFium 按需读块。
    // 整份 readAll 进内存的旧做法让 1 GB 文件带来 1 GB 常驻内存，
    // 内存占用直接与文件大小同量级——这里必须避免。
    QString openError;
    auto reader = DocumentReader::openPath(filePath, &openError);
    if (!reader) {
        result.errorMessage =
            i18n::tr("aipdf::pdf::PdfiumAdapter", "无法读取文件：%1").arg(openError);
        return result;
    }

    auto impl = std::make_shared<detail::DocumentImpl>();
    impl->reader = reader;
    impl->path = filePath;
    impl->password = password;
    impl->access.m_FileLen = static_cast<unsigned long>(reader->size());
    impl->access.m_GetBlock = &detail::DocumentImpl::readBlock;
    impl->access.m_Param = impl.get();

    const QByteArray passwordBytes = password.toUtf8();
    const char* passwordPointer = passwordBytes.isEmpty() ? nullptr : passwordBytes.constData();

    {
        const detail::PdfiumGuard guard;
        impl->handle = FPDF_LoadCustomDocument(&impl->access, passwordPointer);
    }
    if (impl->handle == nullptr) {
        const unsigned long error = FPDF_GetLastError();
        result.errorMessage = describePdfiumError(error);
        result.passwordRequired = (error == FPDF_ERR_PASSWORD);
        result.bytesRead = reader->bytesReadFromDisk();
        return result;
    }

    // 「能加载」不等于「能用」：坏文件的页面树可能整个烂掉，
    // 表现为打开了却没有一页能显示。这种文档宁可当成打开失败，
    // 给一句可读原因，也不要把一个空壳交给界面。
    {
        const detail::PdfiumGuard guard;
        const int pages = FPDF_GetPageCount(impl->handle);
        FPDF_PAGE probe = pages > 0 ? FPDF_LoadPage(impl->handle, 0) : nullptr;
        if (probe == nullptr) {
            FPDF_CloseDocument(impl->handle);
            impl->handle = nullptr;
            result.errorMessage = pages > 0
                                      ? i18n::tr("aipdf::pdf::PdfiumAdapter",
                                                 "文档的页面无法加载（文件可能已损坏）")
                                      : i18n::tr("aipdf::pdf::PdfiumAdapter",
                                                 "文档里没有任何页面（文件可能已损坏）");
            result.bytesRead = reader->bytesReadFromDisk();
            return result;
        }
        FPDF_ClosePage(probe);
    }

    result.document = PdfDocument(impl);
    result.bytesRead = reader->bytesReadFromDisk();
    return result;
}

PdfOpenResult openDocumentFromBytes(const QByteArray& data,
                                     const QString& filePath,
                                     const QString& password) {
    PdfOpenResult result;
    initializePdfium();

    if (data.isEmpty()) {
        result.errorMessage = i18n::tr("aipdf::pdf::PdfiumAdapter", "内容为空");
        return result;
    }
    if (data.size() > kMaxPdfFileSizeBytes) {
        result.errorMessage = i18n::tr("aipdf::pdf::PdfiumAdapter", "内容过大，拒绝加载");
        return result;
    }

    auto impl = std::make_shared<detail::DocumentImpl>();
    impl->reader = DocumentReader::fromBytes(data);
    impl->path = filePath;
    impl->password = password;
    impl->access.m_FileLen = static_cast<unsigned long>(data.size());
    impl->access.m_GetBlock = &detail::DocumentImpl::readBlock;
    impl->access.m_Param = impl.get();

    const QByteArray passwordBytes = password.toUtf8();
    const char* passwordPointer = passwordBytes.isEmpty() ? nullptr : passwordBytes.constData();

    {
        const detail::PdfiumGuard guard;
        impl->handle = FPDF_LoadCustomDocument(&impl->access, passwordPointer);
    }
    if (impl->handle == nullptr) {
        const unsigned long error = FPDF_GetLastError();
        result.errorMessage = describePdfiumError(error);
        result.passwordRequired = (error == FPDF_ERR_PASSWORD);
        return result;
    }

    result.document = PdfDocument(impl);
    return result;
}

PdfDocument PdfDocument::isolatedCopy() const {
    if (!isValid()) {
        return PdfDocument();
    }

    initializePdfium();

    // 新建独立的实现：内存字节用 QByteArray 隐式共享，不复制内存；
    // 按路径打开的文档另开一个文件句柄，同样不复制数据。
    auto impl = std::make_shared<detail::DocumentImpl>();
    impl->reader = cloneReader(impl_->reader);
    if (!impl->reader) {
        return PdfDocument();
    }
    impl->path = impl_->path;
    impl->password = impl_->password;
    impl->access.m_FileLen = static_cast<unsigned long>(impl->reader->size());
    impl->access.m_GetBlock = &detail::DocumentImpl::readBlock;
    impl->access.m_Param = impl.get();

    const QByteArray passwordBytes = impl->password.toUtf8();
    const char* passwordPointer =
        passwordBytes.isEmpty() ? nullptr : passwordBytes.constData();

    {
        const detail::PdfiumGuard guard;
        impl->handle = FPDF_LoadCustomDocument(&impl->access, passwordPointer);
    }
    if (impl->handle == nullptr) {
        return PdfDocument();
    }
    return PdfDocument(impl);
}

}  // namespace aipdf::pdf
