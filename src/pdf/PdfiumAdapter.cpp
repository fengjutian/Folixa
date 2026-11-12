#include "pdf/PdfiumAdapter.h"

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <cstring>
#include <mutex>

namespace aipdf::pdf {

namespace detail {

struct DocumentImpl {
    QByteArray fileData;
    QString path;
    // 重新打开文档时要用（isolatedCopy），PDFium 不提供查询口令的接口。
    QString password;
    FPDF_FILEACCESS access{};
    FPDF_DOCUMENT handle = nullptr;

    ~DocumentImpl() {
        if (handle != nullptr) {
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
    return FPDF_GetPageWidthF(impl_->page);
}

double PdfPage::heightPoints() const {
    if (!isValid()) {
        return 0.0;
    }
    return FPDF_GetPageHeightF(impl_->page);
}

int PdfPage::rotationDegrees() const {
    if (!isValid()) {
        return 0;
    }
    // PDFium 返回的是四分之一圈：0=0°，1=90°，2=180°，3=270°。
    return FPDFPage_GetRotation(impl_->page) * 90;
}

double PdfPage::displayWidthPoints() const {
    const double width = widthPoints();
    const double height = heightPoints();
    // /Rotate 为 90°/270° 时，显示尺寸的宽高互换。
    const int turns = (rotationDegrees() / 90) % 4;
    return (turns == 1 || turns == 3) ? height : width;
}

double PdfPage::displayHeightPoints() const {
    const double width = widthPoints();
    const double height = heightPoints();
    const int turns = (rotationDegrees() / 90) % 4;
    return (turns == 1 || turns == 3) ? width : height;
}

QImage PdfPage::render(double zoom, int rotateQuarterTurns) const {
    if (!isValid() || zoom <= 0.0) {
        return {};
    }

    const double width = widthPoints();
    const double height = heightPoints();
    if (width <= 0.0 || height <= 0.0) {
        return {};
    }

    // PDFium 渲染时不会自动应用页面的 /Rotate，
    // 所以要把「页面自带旋转 + 用户额外旋转」一起传进去。
    const int pageTurns = (rotationDegrees() / 90) % 4;
    const int userTurns = ((rotateQuarterTurns % 4) + 4) % 4;
    const int totalTurns = (pageTurns + userTurns) % 4;

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

QRectF PdfPage::mediaBox() const {
    if (!isValid()) {
        return {};
    }
    float left = 0.0F;
    float bottom = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    if (!FPDFPage_GetMediaBox(impl_->page, &left, &bottom, &right, &top)) {
        return QRectF(0.0, 0.0, widthPoints(), heightPoints());
    }
    return QRectF(left, bottom, right - left, top - bottom);
}

std::optional<TextPage> PdfPage::extractText() const {
    if (!isValid()) {
        return std::nullopt;
    }

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(impl_->page);
    if (textPage == nullptr) {
        return std::nullopt;
    }

    TextPage result;
    result.pageIndex = impl_->index;

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
        const QRectF box(left, bottom, right - left, top - bottom);

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
    return FPDF_GetPageCount(impl_->handle);
}

QString PdfDocument::filePath() const {
    return impl_ ? impl_->path : QString();
}

std::optional<PdfPage> PdfDocument::page(int index) const {
    if (!isValid() || index < 0 || index >= pageCount()) {
        return std::nullopt;
    }
    FPDF_PAGE page = FPDF_LoadPage(impl_->handle, index);
    if (page == nullptr) {
        return std::nullopt;
    }
    auto pageImpl = std::make_shared<detail::PageImpl>();
    pageImpl->document = impl_;
    pageImpl->page = page;
    pageImpl->index = index;
    return PdfPage(pageImpl);
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

    impl->handle = FPDF_LoadCustomDocument(&impl->access, passwordPointer);
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

    impl->handle = FPDF_LoadCustomDocument(&impl->access, passwordPointer);
    if (impl->handle == nullptr) {
        return PdfDocument();
    }
    return PdfDocument(impl);
}

}  // namespace aipdf::pdf
