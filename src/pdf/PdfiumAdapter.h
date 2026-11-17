#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

#include "pdf/Annotation.h"
#include "pdf/Navigation.h"
#include "pdf/TextPage.h"
#include "pdf/TableStructure.h"

#include <memory>
#include <optional>

namespace aipdf::pdf {

namespace detail {
struct DocumentImpl;
struct PageImpl;
}  // namespace detail

struct PdfOpenResult;

enum class PageObjectType { Unknown, Text, Path, Image, Shading, Form };

struct PageObjectInfo {
    int index = -1;
    PageObjectType type = PageObjectType::Unknown;
    QRectF bounds;
    double fontSize = 0.0;
    QString fontFamily;
    QColor fillColor = Qt::black;
    QString text;
    QVector<int> objectPath;
    int nestingDepth = 0;
    bool hasTransparency = false;
    int clipPathCount = 0;
    bool fontEmbedded = false;
    int fontFlags = -1;
    int fontWeight = -1;
    int italicAngle = 0;
    int groupId = -1;
    bool locked = false;
};

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

    // 往页面内容里加一段文字（页眉页脚、页码、水印都走这一条）。
    //
    // xPoints/yPoints 是**基线起点**的 PDF 用户空间坐标（左下原点，y 向上）。
    // rotationDegrees 逆时针为正；opacity 取 0..1。
    // 只支持标准 14 字体里的 Helvetica：中文需要嵌入字体，暂时不支持。
    // 往页面内容里放一张图（图片替换、图片水印都走这条）。
    // rectPoints 是 PDF 用户空间的矩形；图片被拉伸铺满它。
    bool addImageLayer(const QRectF& rectPoints, const QImage& image);

    // 删除与 rectPoints 重叠过半的图片对象（图像删除，E2）。
    bool removeImagesInRect(const QRectF& rectPoints);

    // 文本替换（行级）：
    //   1. 删掉与 rectPoints 相交面积过半的文本对象；
    //   2. 用一个白色矩形盖住原位置（防止残留的图形干扰阅读）；
    //   3. 在矩形左下角画上新文字。
    // 只支持标准 14 字体（西文）。
    bool replaceTextInRect(const QRectF& rectPoints, const QString& newText, double fontSize,
                           const QColor& color);

    QVector<PageObjectInfo> objectsInRect(const QRectF& rectPoints,
                                          double minimumOverlap = 0.15) const;
    // Recursive object tree. objectPath is stable within the current page
    // revision: {topLevel, child, grandchild...}. Nested objects intentionally
    // have index == -1 so callers cannot accidentally address them through a
    // top-level-only editing API.
    QVector<PageObjectInfo> objectsInRectDeep(const QRectF& rectPoints,
                                              double minimumOverlap = 0.15) const;
    TableStructure detectTableInRect(const QRectF& rectPoints) const;
    bool setTableCellText(const TableStructure& table, int row, int column,
                          const QString& text);
    bool transformObjects(const QVector<int>& indexes, double translateX, double translateY,
                          double scaleX = 1.0, double scaleY = 1.0);
    bool rotateObjects(const QVector<int>& indexes, double degrees);
    bool removeObjects(const QVector<int>& indexes);
    bool setTextObjectProperties(const QVector<int>& indexes, const QString& fontFamily,
                                 double fontSize, const QColor& color, double opacity = 1.0,
                                 double horizontalScale = 1.0, double baselineShift = 0.0);
    bool setTextObjectText(int index, const QString& text);
    bool setTextObjectText(const QVector<int>& objectPath, const QString& text);
    bool replaceImageObject(int index, const QImage& image);
    bool cropImageObject(int index, const QRectF& normalizedCrop);
    bool setObjectOpacity(const QVector<int>& indexes, double opacity);
    bool reorderObject(int index, int targetIndex);
    bool setObjectsGroup(const QVector<int>& indexes, int groupId);
    bool clearObjectsGroup(const QVector<int>& indexes);
    bool setObjectsLocked(const QVector<int>& indexes, bool locked);

    // 安全遮盖：把当前页以指定分辨率渲染，在像素层烧入黑块，然后删除原内容对象和
    // 批注，仅保留整页位图。这样底层文字/图片无法通过移除遮盖层恢复。
    // 为避免坐标误判，目前只接受未旋转页面。
    bool redactByRasterizing(const QRectF& rectPoints, double scale = 2.0);

    bool addTextLayer(const QString& text, double xPoints, double yPoints, double fontSize,
                      const QColor& color, double rotationDegrees = 0.0, double opacity = 1.0,
                      const QString& fontFamily = QStringLiteral("Helvetica"),
                      double horizontalScale = 1.0);

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

    // 设置页面的 CropBox（PDF user space，左下原点）。裁剪只改可见区域，
    // 不动页面内容：被裁掉的文字仍然可以搜索、也可以再恢复。
    // 矩形必须在 MediaBox 内，且宽高大于 0；否则返回 false。
    bool setCropBox(const MediaBox& box);

    // 页面背景：在内容最底层插入一个铺满页面的纯色矩形。
    bool setPageBackground(const QColor& color);

    // 调整页面尺寸。scaleContent 为真时按新尺寸等比缩放页面内容；
    // 为假时只换页面盒子，内容保持原始物理尺寸（可能被裁掉或留白）。
    bool resizePage(double widthPoints, double heightPoints, bool scaleContent);

    // 本页的链接注释（内部跳转与外部 URI）。
    //
    // 矩形是 **PDF user space**（左下原点），与字符框同一坐标系：
    // 页面自带的 /Rotate 没有被应用，要和渲染图像对齐得由上层做旋转变换。
    // 页面没有链接时返回空列表。
    QVector<PageLink> links() const;

    // 在指定区域创建一个外部 URI 链接注释。URI 会以 PDFium 要求的
    // 7-bit ASCII 形式写入（调用方应传入 QUrl::toEncoded() 的结果）。
    bool addUriLink(const QRectF& rectPoints, const QByteArray& encodedUri);

    // 扫描文本层中的 http(s):// 与 www. 地址，并给文字区域创建 URI 链接。
    // 已经存在的同地址、同区域链接不会重复创建。返回新增链接数量。
    int addDetectedUriLinks();

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
// 独立句柄（内存字节用 QByteArray 隐式共享，不复制内存；
// 按路径打开的文档则另开一个文件句柄，同样不复制数据），
// 供渲染线程之类的其它线程各自持有。
// 一个可填写的表单字段（AcroForm 的 Widget）。
struct FormField {
    int pageIndex = 0;
    QString name;
    QString value;
    // FPDF_FORMFIELD_* 的取值：1 按钮、2 复选、3 单选、4 下拉、5 列表、6 文本、7 签名。
    int type = 0;
    QRectF rect;
    // FPDF_FORMFLAG_READONLY（1 << 0）。
    bool readOnly = false;
    bool required = false;
    bool multiline = false;
    QString toolTip;
    QVector<QString> options;
};

struct DocumentPermissions {
    bool encrypted = false;
    int securityRevision = -1;
    bool allowPrinting = true;
    bool allowHighQualityPrinting = true;
    bool allowCopying = true;
    bool allowAccessibility = true;
    bool allowAnnotations = true;
    bool allowFormFilling = true;
    bool allowAssembly = true;
    bool allowModifying = true;
};

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

    // 累计从磁盘读了多少字节（交给 PDFium 的那部分数据）。
    //
    // 这是「有没有把整份文件读进内存」的直接证据：按路径打开大文件时，
    // 这个数应当远小于文件大小；整份 readAll 进内存的旧做法走的是内存字节，
    // 这个数是 0。
    qint64 bytesReadFromDisk() const;
    // 文档是否把整份文件留在了内存里。
    //
    // 按路径打开的是 false（文件在磁盘上，内存里只有按需读的块）；
    // 只有「从内存字节打开」才是 true。
    bool holdsWholeFileInMemory() const;

    // 累计从磁盘读了多少字节（交给 PDFium 的那部分数据）。
    //
    // 这是「有没有把整份文件读进内存」的直接证据：按路径打开大文件时，
    // 这个数应当远小于文件大小；整份 readAll 进内存的旧做法走的是内存字节，
    // 这个数是 0。
    qint64 bytesReadFromDisk() const;
    // 文档是否把整份文件留在了内存里。
    //
    // 按路径打开的是 false（文件在磁盘上，内存里只有按需读的块）；
    // 只有「从内存字节打开」才是 true。
    bool holdsWholeFileInMemory() const;
    // 读取 PDF 标准安全处理器权限位；未加密文档全部允许。
    DocumentPermissions permissions() const;

    std::optional<PdfPage> page(int index) const;

    // 文档大纲（PDF 规范里也叫书签）。
    //
    // 返回一棵按文档顺序排列的树；没有大纲时返回空列表。
    // 条目的目标页解析不出来时 pageIndex 为 -1（条目本身仍然显示）。
    std::vector<OutlineItem> outline() const;

    // ------------------------------------------------------------ 表单 ----
    // 枚举所有页上的表单字段（按页、再按 /Annots 顺序）。
    QVector<FormField> formFields() const;
    // 填写：文本/下拉写 /V，勾选与单选写 /AS（取选项的导出值）。
    bool setFormFieldValue(int pageIndex, const QString& name, const QString& value);
    bool setFormFieldProperties(int pageIndex, const QString& oldName,
                                const QString& newName, const QString& toolTip,
                                bool readOnly, bool required, bool multiline);
    // 扁平化：把字段外观烧进页面内容，之后就是普通图形，不能再编辑。
    bool flattenPage(int pageIndex);

    // 是否是 XFA 动态表单。PDFium 没有 XFA 引擎，这类文档只能显式告知用户，
    // 不能静默显示成空白页（F6 的要求）。
    bool hasXfaForm() const;

    // 文档拼装：把另一份文档的页面全部追加到本文件末尾。
    bool appendDocumentPages(const PdfDocument& source);
    // 抽取指定页（0 基）生成一份新文档的字节。
    QByteArray extractPagesToBytes(const QVector<int>& pages) const;

    // 在当前线程创建一份独立的文档句柄。
    // 返回的文档只能在这个线程里使用；文件数据与原文档共享，不额外占内存。
    //
    // 注意：副本是从**源字节**重新解析出来的，所以内存里改过的东西
    // （批注、页面结构）在副本里看不到。要让副本跟上最新状态，
    // 先 serialize() 再 syncBytes()。
    //
    // 按路径打开的文档，副本会另开一个文件句柄重新读同一个文件：
    // 好处是仍然不占内存，代价是文件在这期间被换掉的话副本看到的是新内容。
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
    bool reloadFromBytes(const QByteArray& bytes, QString* error = nullptr);

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
    // 本次打开实际从磁盘读了多少字节。
    //
    // 打开失败时也有值：这时它能证明「没有先把整份文件读进内存再解析」。
    qint64 bytesRead = 0;
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
//
// 文件大小上限从 256 MiB 提到 2 GiB：评分卡 K4 要求「1 GB 级文件可开可翻」，
// 256 MiB 会把这类文档直接挡在门外（打开时给出「文件过大」）。
//
// 内存代价已经消掉了：打开按**路径**进行，文件留在磁盘上，
// PDFium 按需读块，内存占用不再与文件大小同量级（见 pdf/DocumentOpen.h）。
// 真正防住渲染爆炸的是下面两个像素上限，它们与文件大小无关，不会被这次放宽带松。
inline constexpr qint64 kMaxPdfFileSizeBytes = 2LL * 1024 * 1024 * 1024;
inline constexpr int kMaxRenderPixelDimension = 8192;
inline constexpr qint64 kMaxRenderPixelCount = 64LL * 1024 * 1024;

}  // namespace aipdf::pdf
