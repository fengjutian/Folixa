#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

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
};

// 一个待导出的页面。sizePt 是页面显示尺寸（点）。
struct HtmlPage {
    TextPage text;
    QSizeF sizePt;
};

// 把若干页导成一个 HTML 文档：每页一个 <div class="page">，页内每行一个
// <span class="line">。
//
// 行与词之间的空格会原样保留（HTML 默认会把连续空格折叠掉，所以行内样式里
// 带了 white-space: pre-wrap）。没有文字的页也会占一个 div——页数要看得出来。
HtmlExportResult exportToHtml(const QVector<HtmlPage>& pages,
                               const HtmlExportOptions& options = HtmlExportOptions{});

// 单页版本。页面没有文字时返回空串（没有可导出的内容）。
QString exportPageToHtml(const TextPage& text, const QSizeF& pageSizePt,
                          const HtmlExportOptions& options = HtmlExportOptions{});

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
QByteArray exportPdfToDxf(const QByteArray& pdfBytes, const DxfExportOptions& options,
                           QString* error = nullptr);

// ------------------------------------------------------------ 已知边界 ----
//
// 这三条导出都是「尽力还原」，不是格式转换器。界面上的提示文案要照实说：
//
//   HTML：不含图片，也不含矢量图形；字号是从字形高度反推的估算值，
//         所以和原版排版只大致相符（位置、行、词间空格是准的）。
//   图像化 PDF：矢量内容变成了像素，不可再编辑；图片一律是 JPEG（有损）；
//         文字默认不可选中，keepTextLayer=true 时靠一层不可见文字恢复可搜索，
//         那层文字不改变页面外观，但复制出来的位置是估算的。
//   DXF：只导路径与（可选的）文字，不导颜色、线宽、图层与样式；
//         曲线会被打散成折线；用的是 LWPOLYLINE（严格说是 R13+ 的实体，
//         但 R12 之后的读档器普遍兼容）。

}  // namespace aipdf::pdf
