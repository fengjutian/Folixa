#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "doc/convert/FontFlattening.h"
#include "doc/export/OfdExport.h"
#include "doc/export/OfficeExport.h"
#include "pdf/convert/ExportFormats.h"
#include "pdf/convert/DxfExport.h"
#include "pdf/kernel/TextPage.h"

namespace aipdf::app {

// 转换与导出的动作集合。
//
// 每个动作都只做一件事：收参数 → 调领域层 → 交字节。
// 界面上的提示文案由 ConversionReports 提供，文案必须照实说。
//
// 为什么单独一个文件：这些动作要链到 PDFium 与 qpdf（渲染、解析），
// 而三个报告对话框（印前检查 / 活页夹 / 疑似错误）只用得到结构体。
// 分开之后，单测可以只编对话框那一半。
class ConversionTools final {
public:
    // 导出 HTML。失败时返回空字节并把中文原因写进 error。
    //
    // result 可选：给了就能拿到「字号是不是估算的」「导了多少条路径、多少张图」，
    // 界面要照实汇报，不许只说一句「导出完成」。
    static QByteArray exportHtml(const QVector<pdf::HtmlPage>& pages,
                                 pdf::HtmlExportResult* result = nullptr,
                                 QString* error = nullptr);
    // 导出图像化 PDF。
    static QByteArray exportImageBasedPdf(const QByteArray& pdfBytes,
                                          const pdf::RasterizeOptions& options = {},
                                          QString* error = nullptr);
    // 导出 CAD（DXF）：老路径（R12、只有折线与可选文字）。
    static QByteArray exportDxf(const QByteArray& pdfBytes,
                                const pdf::DxfExportOptions& options = {},
                                QString* error = nullptr);
    // 导出 CAD（DXF）：深度路径（真曲线实体、标注单独分层、图层表、颜色与线宽）。
    //
    // 返回整个结果：实体计数与图层清单要照实汇报给用户，
    // 不能只说一句「导出完成」。
    static pdf::CadDxfResult exportCadDxf(const QByteArray& pdfBytes,
                                           const pdf::CadDxfOptions& options = {});
    // 导出到 MS Office 的一种格式（只导一页的文本层）。
    //
    // 注意这一条是**单页快路径**：它吃的是一页的文本层，产出也只有文字。
    // 「导出到 MS Office」的整篇能力走 `doc::PdfToOffice`
    // （`exportPdfToWord` / `exportPdfToPptx` / `exportPdfToExcel`，
    // 见 src/doc/export/PdfToOffice.h），那条路会带真图片与真表格。
    static QByteArray exportOffice(doc::ExportFormat format, const pdf::TextPage& text,
                                   const QSizeF& pageSizePt, QString* error = nullptr);
    // 导出到 OFD。
    //
    // 每页要带文字与图形（矢量路径 + 内嵌图片）；图形由调用方用
    // pdf::extractDocumentGraphics 取好塞进 doc::OfdPageContent。
    // result 可选：给了就能拿到写进包里的路径数与图片数，界面要照实汇报。
    static QByteArray exportOfd(const QVector<doc::OfdPageContent>& pages, const QSizeF& pageSizePt,
                                const doc::OfdExportOptions& options = {},
                                doc::OfdExportResult* result = nullptr, QString* error = nullptr);
    // PDF 转曲（默认矢量轮廓化；需要旧的栅格化行为就显式选 doc::FlattenMode::Raster）。
    //
    // report 可选：给了就把「转了多少字、哪些没转、为什么」一并带出来。
    // 界面必须把这份汇报给用户看——轮廓化不可能对所有文档都成功，
    // 悄悄跳过再宣称「全部转曲」是不允许的。
    static QByteArray flattenFonts(const QByteArray& pdfBytes,
                                   const doc::FlattenOptions& options = {},
                                   doc::FlattenReport* report = nullptr,
                                   QString* error = nullptr);
};

}  // namespace aipdf::app
