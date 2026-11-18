#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "doc/FontFlattening.h"
#include "doc/OfdExport.h"
#include "doc/OfficeExport.h"
#include "pdf/convert/ExportFormats.h"
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
    static QByteArray exportHtml(const QVector<pdf::HtmlPage>& pages, QString* error = nullptr);
    // 导出图像化 PDF。
    static QByteArray exportImageBasedPdf(const QByteArray& pdfBytes,
                                          const pdf::RasterizeOptions& options = {},
                                          QString* error = nullptr);
    // 导出 CAD（DXF）。
    static QByteArray exportDxf(const QByteArray& pdfBytes,
                                const pdf::DxfExportOptions& options = {},
                                QString* error = nullptr);
    // 导出到 MS Office 的一种格式（只导一页的文本层）。
    static QByteArray exportOffice(doc::ExportFormat format, const pdf::TextPage& text,
                                   const QSizeF& pageSizePt, QString* error = nullptr);
    // 导出到 OFD。
    static QByteArray exportOfd(const QVector<pdf::TextPage>& pages, const QSizeF& pageSizePt,
                                const doc::OfdExportOptions& options = {}, QString* error = nullptr);
    // PDF 转曲（视觉等价的栅格化版本）。
    static QByteArray flattenFonts(const QByteArray& pdfBytes,
                                   const doc::FlattenOptions& options = {},
                                   QString* error = nullptr);
};

}  // namespace aipdf::app
