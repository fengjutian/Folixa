#include "app/util/ConversionExports.h"

namespace aipdf::app {

QByteArray ConversionTools::exportHtml(const QVector<pdf::HtmlPage>& pages,
                                       pdf::HtmlExportResult* result, QString* error) {
    pdf::HtmlExportOptions options;
    const pdf::HtmlExportResult exported = pdf::exportToHtml(pages, options);
    if (!exported.ok) {
        if (error != nullptr) {
            *error = exported.error;
        }
        if (result != nullptr) {
            *result = exported;
        }
        return QByteArray();
    }
    if (result != nullptr) {
        *result = exported;
    }
    return exported.html.toUtf8();
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

pdf::CadDxfResult ConversionTools::exportCadDxf(const QByteArray& pdfBytes,
                                                 const pdf::CadDxfOptions& options) {
    return pdf::exportPdfToCadDxf(pdfBytes, options);
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

QByteArray ConversionTools::exportOfd(const QVector<doc::OfdPageContent>& pages, const QSizeF& pageSizePt,
                                      const doc::OfdExportOptions& options, doc::OfdExportResult* result,
                                      QString* error) {
    const doc::OfdExportResult exported = doc::exportPagesToOfd(pages, pageSizePt, options);
    if (result != nullptr) {
        *result = exported;
    }
    if (!exported.ok) {
        if (error != nullptr) {
            *error = exported.error;
        }
        return QByteArray();
    }
    return exported.bytes;
}

QByteArray ConversionTools::flattenFonts(const QByteArray& pdfBytes,
                                         const doc::FlattenOptions& options,
                                         doc::FlattenReport* report, QString* error) {
    if (report != nullptr) {
        return doc::flattenFontsToOutlines(pdfBytes, options, report, error);
    }
    return doc::flattenFontsToOutlines(pdfBytes, options, error);
}

}  // namespace aipdf::app
