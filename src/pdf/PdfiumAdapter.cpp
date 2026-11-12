#include "pdf/PdfiumAdapter.h"

#include <fpdfview.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <mutex>

namespace aipdf::pdf {
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

struct PdfPage::Impl {
    // 持有文档实现，保证页面句柄存活期间文档不被关闭。
    std::shared_ptr<PdfDocument::Impl> document;
    FPDF_PAGE page = nullptr;

    ~Impl() {
        if (page != nullptr) {
            FPDF_ClosePage(page);
        }
    }
};

PdfPage::PdfPage(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
PdfPage::~PdfPage() = default;
PdfPage::PdfPage(PdfPage&&) noexcept = default;
PdfPage& PdfPage::operator=(PdfPage&&) noexcept = default;

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
    return FPDFPage_GetRotation(impl_->page);
}

QImage PdfPage::render(double zoom) const {
    if (!isValid() || zoom <= 0.0) {
        return {};
    }

    const double width = widthPoints();
    const double height = heightPoints();
    if (width <= 0.0 || height <= 0.0) {
        return {};
    }

    const int pixelWidth = static_cast<int>(width * zoom + 0.5);
    const int pixelHeight = static_cast<int>(height * zoom + 0.5);
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
        bitmap, impl_->page, 0, 0, pixelWidth, pixelHeight, 0, FPDF_ANNOT);
    FPDFBitmap_Destroy(bitmap);

    return image;
}

// ------------------------------------------------------------- PdfDocument ----

struct PdfDocument::Impl {
    QByteArray fileData;
    QString path;
    FPDF_FILEACCESS access{};
    FPDF_DOCUMENT handle = nullptr;

    ~Impl() {
        if (handle != nullptr) {
            FPDF_CloseDocument(handle);
        }
    }

    static int readBlock(void* param,
                         unsigned long position,
                         unsigned char* buffer,
                         unsigned long size) {
        auto* self = static_cast<Impl*>(param);
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

PdfDocument::PdfDocument(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
PdfDocument::~PdfDocument() = default;
PdfDocument::PdfDocument(PdfDocument&&) noexcept = default;
PdfDocument& PdfDocument::operator=(PdfDocument&&) noexcept = default;

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
    auto pageImpl = std::make_shared<PdfPage::Impl>();
    pageImpl->document = impl_;
    pageImpl->page = page;
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

    auto impl = std::make_shared<PdfDocument::Impl>();
    impl->fileData = data;
    impl->path = filePath;
    impl->access.m_FileLen = static_cast<unsigned long>(data.size());
    impl->access.m_GetBlock = &PdfDocument::Impl::readBlock;
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

}  // namespace aipdf::pdf
