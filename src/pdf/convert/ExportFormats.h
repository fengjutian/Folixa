#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/TextPage.h"

namespace aipdf::pdf {

// ============================================================ 导出格式模块 ==
//
// 一个领域层模块，负责三种「换个格式交付」的导出：
//
//   1. 导出 HTML       —— 文字与基本排版变成一个 HTML 文件；
//   2. 图像化 PDF      —— 每页渲染成位图，重新写一份每页一张图的 PDF；
//   3. 导出 DXF        —— 页面上的矢量路径变成 AutoCAD 能读的实体。
//
// 三条链路的共同点：**输入是 PDF 的字节或文本层，输出是另一种格式的字节**，
// 不碰界面、不写盘。因此界面层只管选参数、收字节、报错。
//
// 坐标系约定（整个模块只有这一套，别混）：
//   - PDF 用户空间：原点在左下、y 向上；
//   - Qt/QImage：原点在左上、y 向下；
//   - DXF：原点在左下、y 向上（与 PDF 一致，**不需要翻转**）。
// 渲染出来的位图天然是「上下颠倒」的（PDFium 自己处理），导出时不要再去翻。

// ------------------------------------------------------------------ HTML --

struct HtmlExportOptions {
    // 是否写 <style> 段。关掉之后只剩结构标签与行内样式，
    // 适合要塞进别人的页面模板时用。
    bool includeStyles = true;
    // 兜底字号（点）：只有当页面文字层没给出可用字框时才会用到。
    double fontSizePt = 11.0;
};

struct HtmlExportResult {
    bool ok = false;
    QString html;
    // 失败原因（中文硬编码；领域层不放 tr()）。
    QString error;
    // 字号来源：true = 至少有一行文字用的是「字形高度反推」的估算字号。
    //
    // 为什么要有这个标志：真实字号只能从内容流里的 Tf 取，取不到时只能估算，
    // 而这两种结果对用户不是一回事——界面必须能如实说清这次导的是哪一种。
    bool fontSizeEstimated = false;
    // 估算原因（中文）。没有走过估算时为空。
    QString fontSizeNote;
    // 这次导出了多少条矢量路径、多少张图片（界面用来汇报）。
    int vectorPathCount = 0;
    int imageCount = 0;
};

// 一个待导出的页面。sizePt 是页面显示尺寸（点）。
struct HtmlPage {
    TextPage text;
    QSizeF sizePt;
    // 这一页的矢量路径与内嵌位图（可选）。
    //
    // 为什么由调用方给：图形要靠 qpdf 读内容流（pdf::extractPageGraphics），
    // 领域层的导出函数不该自己再去打开一遍 PDF。调用方没给就是「这一页没有图形」。
    PageGraphics graphics;
    // 每个字符的真实字号（点），与 text.chars 一一对应；0 表示没有真实字号。
    //
    // 为什么按字符给：内容流里的 Tf 与 PDFium 的字符没有一一对应关系，
    // 只能按几何位置匹配（pdf::extractCharFontSizes）。按行给会让调用方
    // 不得不重复实现一遍「分行」的逻辑。
    QVector<double> charFontSizes;
};

// 把若干页导成一个 HTML 文档：每页一个 <div class="page">，页内每行一个
// <span class="line">。
//
// 行与词之间的空格会原样保留（HTML 默认会把连续空格折叠掉，所以行内样式里
// 带了 white-space: pre-wrap）。没有文字的页也会占一个 div——页数要看得出来。
//
// 页面带了 graphics 就会额外输出矢量图形（每页一个内嵌 <svg>）与图片
// （<img src="data:image/...">）；带了 charFontSizes 就用真实字号，没带就退回
// 字形高度估算，并在结果里标明走了估算。
HtmlExportResult exportToHtml(const QVector<HtmlPage>& pages,
                               const HtmlExportOptions& options = HtmlExportOptions{});

// 单页版本。页面没有文字时返回空串（没有可导出的内容）。
QString exportPageToHtml(const TextPage& text, const QSizeF& pageSizePt,
                          const HtmlExportOptions& options = HtmlExportOptions{});

// 单页版本（带图形与真实字号）。返回完整结果，调用方能知道字号是不是估算的。
HtmlExportResult exportPageToHtml(const HtmlPage& page,
                                  const HtmlExportOptions& options = HtmlExportOptions{});

// 把内容流里的真实字号（Tf）匹配到文本层的每个字符上。
//
// 返回值与 text.chars 一一对应，单位是点；0 表示这个字符没有匹配到真实字号
// （导出时那一行退回字形高度估算）。
//
// 为什么要按几何位置匹配：内容流里的 Tf 与 PDFium 给的字符不是一一对应的
// （内容流按绘制顺序，PDFium 按自己的顺序，而且一个 Tj 可能画好几个字符），
// 只能靠「基线相同 + 起点在字符左边」来配对。
//
// 实现放在 pdf/content/PageGraphics.cpp：内容流的解析都在那一层，
// 这一层只负责「格式映射」。用它的目标要一起链上 PageGraphics.cpp。
QVector<double> extractCharFontSizes(const QByteArray& pdfBytes, const TextPage& text,
                                     int pageIndex);

// 把一页 PDF 变成可导出的 HtmlPage：文字由调用方给（走 PDFium），
// 图形与真实字号由本函数补齐。
//
// 为什么写成 inline：它要调用内容流那两个函数，写成普通函数会让「只是链上
// 导出模块」的目标也被迫链上 PageGraphics.cpp；inline 之后只有真正调用它的
// 目标才需要那两个符号。
inline HtmlPage buildHtmlPage(const QByteArray& pdfBytes, const TextPage& text,
                              const QSizeF& sizePt, int pageIndex) {
    HtmlPage page;
    page.text = text;
    page.sizePt = sizePt;
    const PageGraphics graphics = extractPageGraphics(pdfBytes, pageIndex);
    // 图形没取到（没启用 qpdf 或这一页真的没有图形）就留空：
    // 导出层会按「这一页没有图形」处理，不会报错。
    if (graphics.ok) page.graphics = graphics;
    page.charFontSizes = extractCharFontSizes(pdfBytes, text, pageIndex);
    return page;
}

// ---------------------------------------------------------- 图像化 PDF --

struct RasterizeOptions {
    // 渲染分辨率。缓冲区尺寸 = 页宽(点) × dpi / 72。
    double dpi = 150.0;
    // true = 额外把原文字层写成不可见文字写回去（看得见的字形来自图片，
    // 但文字仍可被搜索、复制）。
    bool keepTextLayer = false;
    // JPEG 质量（1..100）。图像一律以 JPEG（DCTDecode）内嵌。
    int jpegQuality = 90;
};

// 把 PDF 的每一页渲染成位图，写成一份新的 PDF：每页一张图，页面尺寸不变。
//
// 文字不可再选中（除非 keepTextLayer=true），矢量也一并变成了像素——
// 这是「图像化」的意思，不是缺陷。
QByteArray exportToImageBasedPdf(const QByteArray& pdfBytes, const RasterizeOptions& options,
                                  QString* error = nullptr);

// ------------------------------------------------------------------ DXF --

struct DxfExportOptions {
    // 用户空间 → DXF 单位的缩放。1 = 点。
    double scale = 1.0;
    // 文字要不要也导（作为 TEXT 实体）。
    bool includeText = false;
};

// 把一页的文本层导成 DXF（AutoCAD R12 ASCII）。
//
// 页面里没有矢量路径时（文本层只有文字），产出的是一份只有文字实体的 DXF。
QByteArray exportPageToDxf(const TextPage& text, const QSizeF& pageSizePt,
                            const DxfExportOptions& options = DxfExportOptions{});

// 把整份 PDF 的矢量路径导成 DXF。
//
// 多页时按页码顺序竖着摞起来（每页占一块高度），免得几页的图形叠在一起。
// 页面只有位图、没有矢量路径时，ENTITIES 段会是空的——不拿包围盒冒充路径。
//
// 这条是**老路径**（AutoCAD R12、只有折线与可选文字、曲线被打散成折线）。
// 需要真曲线、尺寸标注、图层与颜色线宽的，走 pdf/convert/DxfExport.h 里的
// exportPdfToCadDxf：两边并存，老调用方不受影响。
QByteArray exportPdfToDxf(const QByteArray& pdfBytes, const DxfExportOptions& options,
                           QString* error = nullptr);

// ------------------------------------------------------------ 已知边界 ----
//
// 这三条导出都是「尽力还原」，不是格式转换器。界面上的提示文案要照实说：
//
//   HTML：矢量路径会画成内嵌 <svg>，内嵌位图会变成 <img>（base64 内联）；
//         字号优先用内容流里的真实字号（Tf），取不到的那几行才退回字形高度
//         估算，并在结果里标明走了估算；
//         不含剪裁路径、渐变、注解与表单，也不含内联图像（BI/ID/EI）；
//         文字位置与图形位置都是「与原版大致相符」，不是像素级还原。
//   图像化 PDF：矢量内容变成了像素，不可再编辑；图片一律是 JPEG（有损）；
//         文字默认不可选中，keepTextLayer=true 时靠一层不可见文字恢复可搜索，
//         那层文字不改变页面外观，但复制出来的位置是估算的。
//   DXF（老路径 exportPdfToDxf）：只导路径与（可选的）文字，不导颜色、线宽、
//         图层与样式；曲线会被打散成折线；用的是 LWPOLYLINE（严格说是 R13+ 的
//         实体，但 R12 之后的读档器普遍兼容）。
//   DXF（深度路径 exportPdfToCadDxf，见 pdf/convert/DxfExport.h）：
//         三次贝塞尔保形（导成 SPLINE 真曲线实体）；尺寸标注单独分层；
//         写 LAYER 表、颜色（索引色 + 真彩）与线宽；多页竖摞。
//         仍然**做不到**的：位图不导出（DXF 这一版不嵌位图，只统计张数）；
//         PDF 里的 /Dim 批注对象不识别（底座不解析批注外观流），
//         尺寸标注只认「内容流里画出来的箭头 + 尺寸线 + 数字」这一种；
//         渐变、图案填充与剪裁路径仍然丢失。

}  // namespace aipdf::pdf
