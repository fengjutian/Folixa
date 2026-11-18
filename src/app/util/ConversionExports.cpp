#include "app/util/ConversionExports.h"

namespace aipdf::app {

QByteArray ConversionTools::exportHtml(const QVector<pdf::HtmlPage>& pages, QString* error) {
    pdf::HtmlExportOptions options;
    const pdf::HtmlExportResult result = pdf::exportToHtml(pages, options);
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return QByteArray();
    }
    return result.html.toUtf8();
}

QByteArray ConversionTools::exportImageBasedPdf(const QByteArray& pdfBytes,
                                                const pdf::RasterizeOptions& options,
                                                QString* error) {
    return pdf::exportToImageBasedPdf(pdfBytes, options, error);
}

QByteArray ConversionTools::exportDxf(const QByteArray& pdfBytes,
                                       const pdf::DxfExportOptions& options,
                                       QString* error) {
    return pdf::exportPdfToDxf(pdfBytes, options, error);
}

QByteArray ConversionTools::exportOffice(doc::ExportFormat format, const pdf::TextPage& text,
                                         const QSizeF& pageSizePt, QString* error) {
    const doc::ExportResult result = doc::exportPageTo(format, text, pageSizePt);
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return QByteArray();
    }
    return result.bytes;
}

QByteArray ConversionTools::exportOfd(const QVector<pdf::TextPage>& pages, const QSizeF& pageSizePt,
                                      const doc::OfdExportOptions& options, QString* error) {
    const doc::OfdExportResult result = doc::exportPagesToOfd(pages, pageSizePt, options);
    if (!result.ok) {
        if (error != nullptr) {
            *error = result.error;
        }
        return QByteArray();
    }
    return result.bytes;
}

QByteArray ConversionTools::flattenFonts(const QByteArray& pdfBytes,
                                         const doc::FlattenOptions& options, QString* error) {
    return doc::flattenFontsToOutlines(pdfBytes, options, error);
}

}  // namespace aipdf::app
