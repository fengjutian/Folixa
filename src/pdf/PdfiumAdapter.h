#pragma once

#include <QImage>
#include <QString>

#include <memory>
#include <optional>

namespace aipdf::pdf {

namespace detail {
struct DocumentImpl;
struct PageImpl;
}  // namespace detail

struct PdfOpenResult;

// 单页句柄。页面生命周期依附于所属文档，文档关闭后页面自动失效。
class PdfPage {
public:
    PdfPage() = default;
    ~PdfPage();
    PdfPage(const PdfPage&) = default;
    PdfPage& operator=(const PdfPage&) = default;
    PdfPage(PdfPage&&) noexcept = default;
    PdfPage& operator=(PdfPage&&) noexcept = default;

    bool isValid() const;

    // 页面尺寸，单位为点（1/72 英寸），已包含页面旋转。
    double widthPoints() const;
    double heightPoints() const;
    // 页面旋转角度：0、90、180 或 270。
    int rotationDegrees() const;

    // 渲染页面。zoom=1 表示 72 DPI；返回值格式为 ARGB32。
    // 超出安全上限时返回空图。
    QImage render(double zoom = 1.0) const;

private:
    std::shared_ptr<detail::PageImpl> impl_;
    explicit PdfPage(std::shared_ptr<detail::PageImpl> impl);
    friend class PdfDocument;
};

// PDF 文档句柄。
//
// 注意：PDFium 的 FPDF_DOCUMENT 句柄只能在创建它的线程里使用，
// 不能跨线程共享。本类因此提供 isolatedCopy()：在调用线程里新建一份
// 独立句柄（文件字节用 QByteArray 隐式共享，不复制内存），
// 供渲染线程之类的其它线程各自持有。
class PdfDocument {
public:
    PdfDocument() = default;
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = default;
    PdfDocument& operator=(const PdfDocument&) = default;
    PdfDocument(PdfDocument&&) noexcept = default;
    PdfDocument& operator=(PdfDocument&&) noexcept = default;

    bool isValid() const;
    int pageCount() const;
    QString filePath() const;

    std::optional<PdfPage> page(int index) const;

    // 在当前线程创建一份独立的文档句柄。
    // 返回的文档只能在这个线程里使用；文件数据与原文档共享，不额外占内存。
    PdfDocument isolatedCopy() const;

private:
    std::shared_ptr<detail::DocumentImpl> impl_;
    explicit PdfDocument(std::shared_ptr<detail::DocumentImpl> impl);
    friend class PdfPage;
    friend PdfOpenResult openDocument(const QString& filePath, const QString& password);
};

// 打开结果。passwordRequired 为真时表示需要密码，可用密码重试。
struct PdfOpenResult {
    PdfDocument document;
    QString errorMessage;
    bool passwordRequired = false;
};

// 打开 PDF。filePath 不存在、格式损坏或密码错误时返回失败结果，不抛异常。
PdfOpenResult openDocument(const QString& filePath, const QString& password = QString());

// 安全上限：文件大小与渲染像素。
inline constexpr qint64 kMaxPdfFileSizeBytes = 256LL * 1024 * 1024;
inline constexpr int kMaxRenderPixelDimension = 8192;
inline constexpr qint64 kMaxRenderPixelCount = 64LL * 1024 * 1024;

}  // namespace aipdf::pdf
