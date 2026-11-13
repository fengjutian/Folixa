#include "pdf/PdfiumAdapter.h"

#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>

#include <cstring>
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
    QByteArray fileData;
    QString path;
    // 重新打开文档时要用（isolatedCopy），PDFium 不提供查询口令的接口。
    QString password;
    FPDF_FILEACCESS access{};
    FPDF_DOCUMENT handle = nullptr;
    // 每页由本类写入的批注数量（页序号 → 数量）。
    //
    // 批注是**追加**在页面原有批注之后的，所以重新写入时：
    // 原有批注占据 [0, 原有数)，我们写的占据其后的区间。
    // 记住写了多少，就知道该从哪个下标开始删。
    QHash<int, int> writtenAnnotations;

    ~DocumentImpl() {
        if (handle != nullptr) {
            // 关闭句柄也要上锁：PDFium 的全局状态是共享的。
            const PdfiumGuard guard;
            FPDF_CloseDocument(handle);
        }
    }

    static int readBlock(void* param,
                         unsigned long position,
                         unsigned char* buffer,
                         unsigned long size) {
        auto* self = static_cast<DocumentImpl*>(param);
        if (self == nullptr || buffer == nullptr) {
            return 0;
        }
        if (static_cast<qint64>(position) + static_cast<qint64>(size) >
            static_cast<qint64>(self->fileData.size())) {
            return 0;
        }
        memcpy(buffer, self->fileData.constData() + position, size);
        return 1;
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
            const PdfiumGuard guard;
            FPDF_ClosePage(page);
        }
    }
};

}  // namespace detail

namespace {

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
            return QStringLiteral("PDFium 未知错误");
        case FPDF_ERR_FILE:
            return QStringLiteral("文件不存在或无法读取");
        case FPDF_ERR_FORMAT:
            return QStringLiteral("文件不是有效的 PDF");
        case FPDF_ERR_PASSWORD:
            return QStringLiteral("需要密码，或密码不正确");
        case FPDF_ERR_SECURITY:
            return QStringLiteral("不支持的安全处理器");
        case FPDF_ERR_PAGE:
            return QStringLiteral("页码越界");
        default:
            return QStringLiteral("PDFium 错误码 %1").arg(error);
    }
}

// 大纲遍历的安全上限。PDFium 明确要求调用方处理畸形文档里的环形书签引用
//（fpdf_doc.h: "the caller is responsible for handling circular bookmark
// references"），所以深度和条目数都要设上限，否则会无限展开。
constexpr int kMaxOutlineDepth = 32;
constexpr int kMaxOutlineItems = 10000;

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

// ------------------------------------------------------- 批注与 PDFium 的互转 ----

// 把 QString 转成 PDFium 要的 UTF-16LE 字符串（带终止符）。
std::vector<unsigned short> toWideString(const QString& text) {
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
        const std::vector<unsigned short> contents = toWideString(annotation.contents);
        FPDFAnnot_SetStringValue(annot, "Contents",
                                  reinterpret_cast<const FPDF_WIDESTRING>(contents.data()));
    }
    if (!annotation.author.isEmpty()) {
        const std::vector<unsigned short> author = toWideString(annotation.author);
        FPDFAnnot_SetStringValue(annot, "T", reinterpret_cast<const FPDF_WIDESTRING>(author.data()));
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

        // 批注正文（Contents）。
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
    if (!isValid()) {
        return false;
    }

    const detail::PdfiumGuard guard;

    // 先删掉上一次由本类写入的批注。
    //
    // 我们写的批注总是追加在页面原有批注之后，所以删的时候下标是固定的：
    // 删掉一个，后面的会前移一位，下一个还是同一个下标。
    int& written = impl_->document->writtenAnnotations[impl_->index];
    const int total = FPDFPage_GetAnnotCount(impl_->page);
    const int original = qMax(0, total - written);
    for (int i = 0; i < written; ++i) {
        FPDFPage_RemoveAnnot(impl_->page, original);
    }
    written = 0;

    // 再写新的。
    int created = 0;
    for (const Annotation& annotation : annotations) {
        if (createAnnotation(impl_->page, annotation)) {
            ++created;
        }
    }
    written = created;
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

// ------------------------------------------------------------ 打开文档 ----

PdfOpenResult openDocument(const QString& filePath, const QString& password) {
    PdfOpenResult result;
    initializePdfium();

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        result.errorMessage = QStringLiteral("文件不存在：%1").arg(filePath);
        return result;
    }
    if (info.size() > kMaxPdfFileSizeBytes) {
        result.errorMessage =
            QStringLiteral("文件过大：%1 字节，上限 %2 字节")
                .arg(info.size())
                .arg(kMaxPdfFileSizeBytes);
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.size() > kMaxPdfFileSizeBytes) {
        result.errorMessage = QStringLiteral("文件过大，拒绝加载");
        return result;
    }

    auto impl = std::make_shared<detail::DocumentImpl>();
    impl->fileData = data;
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

    // 新建独立的实现：文件字节用 QByteArray 隐式共享，不复制内存。
    auto impl = std::make_shared<detail::DocumentImpl>();
    impl->fileData = impl_->fileData;
    impl->path = impl_->path;
    impl->password = impl_->password;
    impl->access.m_FileLen = static_cast<unsigned long>(impl->fileData.size());
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
