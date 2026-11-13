#pragma once

#include <QImage>
#include <QString>

#include "pdf/Navigation.h"
#include "pdf/TextPage.h"

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

    // 页面尺寸，单位为点（1/72 英寸）。
    // 注意：PDFium 返回的尺寸**已经包含**页面自带的 /Rotate 旋转
    // （/Rotate 90、MediaBox 200x100 的页面返回 100x200）。
    double widthPoints() const;
    double heightPoints() const;
    // 页面自带旋转角度：0、90、180 或 270。
    int rotationDegrees() const;

    // 显示尺寸（不含用户额外旋转）：与 widthPoints()/heightPoints() 相同，
    // 因为那已经是应用过 /Rotate 的尺寸。
    double displayWidthPoints() const;
    double displayHeightPoints() const;

    // 渲染页面。zoom=1 表示 72 DPI；返回值格式为 ARGB32。
    // 页面自带的 /Rotate 由 PDFium 自己应用；rotateQuarterTurns 是在其之上
    // 再叠加的用户旋转（0/1/2/3，即 0°/90°/180°/270°）。
    // 超出安全上限时返回空图。
    QImage render(double zoom = 1.0, int rotateQuarterTurns = 0) const;

    // 提取本页文本层。页面没有文本层时返回空值。
    //
    // 字符框是 **PDF user space**（左下原点），不含页面 /Rotate，
    // 也不含用户旋转；要和渲染图像对齐必须由上层做旋转变换。
    // 缺 ToUnicode 的字体会导致 unicodeMapError 为真，
    // 此时仍然会给出能映射到 Unicode 的那些字符。
    std::optional<TextPage> extractText() const;

    // 页面的 MediaBox（PDF user space，左下原点）。
    MediaBox mediaBox() const;
    // 页面的有效盒子：优先 CropBox，没有 CropBox 时退回 MediaBox。
    // PDFium 渲染的就是这个区域，字符框相对它才能和页面图像对齐。
    MediaBox contentBox() const;

    // 本页的链接注释（内部跳转与外部 URI）。
    //
    // 矩形是 **PDF user space**（左下原点），与字符框同一坐标系：
    // 页面自带的 /Rotate 没有被应用，要和渲染图像对齐得由上层做旋转变换。
    // 页面没有链接时返回空列表。
    QVector<PageLink> links() const;

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

    // 文档大纲（PDF 规范里也叫书签）。
    //
    // 返回一棵按文档顺序排列的树；没有大纲时返回空列表。
    // 条目的目标页解析不出来时 pageIndex 为 -1（条目本身仍然显示）。
    std::vector<OutlineItem> outline() const;

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
