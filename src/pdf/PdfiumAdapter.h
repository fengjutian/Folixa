#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

#include "pdf/Annotation.h"
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

    // 本页的批注（不含链接注释）。
    //
    // 矩形是 **PDF user space**（左下原点），与字符框同一坐标系。
    //
    // 注意：PDFium 只会渲染**自带外观流（/AP）**的批注。
    // 新建的批注没有 /AP，而且 159 的公开 API 无法生成合规的 /AP
    // （FPDFAnnot_SetAP 只写字典、不建 XObject，见 fpdf_annot.h 的说明），
    // 所以这些批注**在 PDFium 的渲染里是看不见的**——显示由应用层负责。
    // 本函数保证的是数据在：写进去能读回来，撤销时能删干净。
    QVector<Annotation> annotations() const;

    // 把一批批注写进本页：先删掉上一次由本类写入的批注，再写新的。
    //
    // 这是「模型 → 文档」的单向投影：调用方要保证传入的是**该页的全部批注**。
    // 撤销时再调用一次即可（传空列表就是全撤掉）。
    bool setAnnotations(const QVector<Annotation>& annotations);

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
    //
    // 注意：副本是从**源字节**重新解析出来的，所以内存里改过的东西
    // （批注、页面结构）在副本里看不到。要让副本跟上最新状态，
    // 先 serialize() 再 syncBytes()。
    PdfDocument isolatedCopy() const;

    // ---------------------------------------------------------- 页面结构 ----
    //
    // 页面结构操作直接改 PDFium 文档（v0.8.0 的页面管理）。
    //
    // PDFium 159 没有「移动页」的接口：移动的实现是
    // 「把该页导入成副本插到新位置 + 删掉原件」。
    // 实测（build/probe/page_probe.cpp）：
    //   - 导入的副本会带上原页的内容、批注与页面自带旋转；
    //   - 但被移动页上的**内部链接目标会失效**，
    //     指向被移动页的链接同样失效，PDFium 没有公开接口能修回来；
    //   - 大纲（书签）指向被移动页的那一条会解析不出目标，其余条目不受影响；
    //   - 删除页同理：只有指向被删页的那一条书签失效。
    // 插入空白页与旋转页对大纲完全没有影响。
    //
    // pageMapping 是「旧页序号 → 新页序号」的映射（被删的页是 -1）。
    // 调用方要用它把批注、选区这类按页编号的数据一起搬过去。

    // 在 index 处插入一张 width×height（点）的空白页。
    bool insertBlankPage(int index,
                         double width,
                         double height,
                         QVector<int>* pageMapping = nullptr);
    // 删除第 index 页。
    bool deletePage(int index, QVector<int>* pageMapping = nullptr);
    // 设置第 index 页**自身**的旋转（四分之一圈：0=0°，1=90°，2=180°，3=270°）。
    // 这是写进文档的 /Rotate，与界面上「旋转视图」不是一回事。
    bool setPageRotation(int index, int quarterTurns);
    // 把第 from 页移动到第 to 页（to 是移动**之后**的目标下标）。
    bool movePage(int from, int to, QVector<int>* pageMapping = nullptr);

    // 备份一页：返回一份只含该页的临时文档（撤销删除时用）。
    PdfDocument backupPage(int index) const;
    // 把备份文档里的页恢复到 index 位置。
    bool restorePageFrom(const PdfDocument& backup, int index);

    // 序列化：把当前文档（含内存里的改动）写成 PDF 字节。
    // 失败返回空数组，并通过 error 说明原因。
    QByteArray serialize(QString* error = nullptr) const;
    // 用新的字节覆盖文档的源数据。
    //
    // 目的：isolatedCopy() 是从源字节重新解析的，内存里的改动它看不到。
    // 结构性改动之后调一次，渲染线程与后台线程开出来的副本才是最新的。
    void syncBytes(const QByteArray& bytes);

private:
    std::shared_ptr<detail::DocumentImpl> impl_;
    explicit PdfDocument(std::shared_ptr<detail::DocumentImpl> impl);
    // 接管一个已经存在的 PDFium 句柄（备份文档用）。
    // 句柄类型用 void*：这个头文件刻意不包含 PDFium 的头文件。
    static PdfDocument adoptHandle(void* handle, const QString& path);
    friend class PdfPage;
    friend PdfOpenResult openDocument(const QString& filePath, const QString& password);
    friend PdfOpenResult openDocumentFromBytes(const QByteArray& data,
                                               const QString& filePath,
                                               const QString& password);
};

// 打开结果。passwordRequired 为真时表示需要密码，可用密码重试。
struct PdfOpenResult {
    PdfDocument document;
    QString errorMessage;
    bool passwordRequired = false;
};

// 打开 PDF。filePath 不存在、格式损坏或密码错误时返回失败结果，不抛异常。
PdfOpenResult openDocument(const QString& filePath, const QString& password = QString());

// 从内存里的 PDF 字节打开文档（不落盘）。
// 用于校验刚写出来的文件、以及从恢复日志重放。
PdfOpenResult openDocumentFromBytes(const QByteArray& data,
                                     const QString& filePath = QString(),
                                     const QString& password = QString());

// 批注类型 ↔ PDFium 注释子类型（FPDF_ANNOT_*）。
// 放在适配层是因为 PDFium 的常量不能漏进纯数据层。
int annotationTypeToPdfium(AnnotationType type);
// PDFium 子类型 → 批注类型。链接等不认识的类型返回 false。
bool annotationTypeFromPdfium(int subtype, AnnotationType* out);

// 安全上限：文件大小与渲染像素。
inline constexpr qint64 kMaxPdfFileSizeBytes = 256LL * 1024 * 1024;
inline constexpr int kMaxRenderPixelDimension = 8192;
inline constexpr qint64 kMaxRenderPixelCount = 64LL * 1024 * 1024;

}  // namespace aipdf::pdf
