#pragma once

#include <QImage>
#include <QString>

#include <memory>
#include <optional>

namespace aipdf::pdf {

class PdfDocument;

// 单页句柄。页面生命周期依附于所属文档，文档关闭后页面自动失效。
class PdfPage {
public:
    PdfPage() = default;
    ~PdfPage();
    PdfPage(PdfPage&&) noexcept;
    PdfPage& operator=(PdfPage&&) noexcept;

    bool isValid() const;

    // 页面尺寸，单位为点（1/72 英寸），已包含页面旋转。
    double widthPoints() const;
    double heightPoints() const;
    int rotationDegrees() const;

    // 渲染页面。zoom=1 表示 72 DPI；返回值格式为 ARGB32。
    // 超出安全上限时返回空图。
    QImage render(double zoom = 1.0) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit PdfPage(std::shared_ptr<Impl> impl);
    friend class PdfDocument;
};

// PDF 文档句柄。
class PdfDocument {
public:
    PdfDocument() = default;
    ~PdfDocument();
    PdfDocument(PdfDocument&&) noexcept;
    PdfDocument& operator=(PdfDocument&&) noexcept;

    bool isValid() const;
    int pageCount() const;
    QString filePath() const;

    std::optional<PdfPage> page(int index) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit PdfDocument(std::shared_ptr<Impl> impl);
    friend struct PdfOpenResult;
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
