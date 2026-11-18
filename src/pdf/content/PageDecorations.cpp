#include "pdf/content/PageDecorations.h"

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

bool allPages(PdfDocument& document,
              const std::function<bool(PdfPage&, int, int)>& action) {
    const int count = document.pageCount();
    if (count <= 0) {
        return false;
    }
    bool any = false;
    for (int index = 0; index < count; ++index) {
        auto page = document.page(index);
        if (!page.has_value()) {
            continue;
        }
        any = action(*page, index, count) || any;
    }
    return any;
}

// 粗略估算文本宽度：Helvetica 平均字符宽约 0.5 em。
double estimatedWidth(const QString& text, double fontSize) {
    return static_cast<double>(text.size()) * fontSize * 0.5;
}

}  // namespace

bool canRenderWithStandardFont(const QString& text) {
    for (const QChar character : text) {
        if (character.unicode() > 0x7E || character.unicode() < 0x20) {
            return false;
        }
    }
    return true;
}

bool applyWatermark(PdfDocument& document, const WatermarkOptions& options) {
    if (options.text.isEmpty() || !canRenderWithStandardFont(options.text)) {
        return false;
    }
    return allPages(document, [&](PdfPage& page, int, int) {
        const double width = page.widthPoints();
        const double height = page.heightPoints();
        const double textWidth = estimatedWidth(options.text, options.fontSize);
        // 把文字中心放到页面中心：先按文字宽度水平居中，再抬高约 1/3 字号。
        const double x = width / 2.0 - textWidth / 2.0;
        const double y = height / 2.0 - options.fontSize / 3.0;
        return page.addTextLayer(options.text, x, y, options.fontSize, options.color,
                                 options.rotationDegrees, options.opacity);
    });
}

bool applyHeaderFooter(PdfDocument& document, const HeaderFooterOptions& options) {
    if (options.header.isEmpty() && options.footer.isEmpty()) {
        return false;
    }
    if (!canRenderWithStandardFont(options.header) ||
        !canRenderWithStandardFont(options.footer)) {
        return false;
    }
    return allPages(document, [&](PdfPage& page, int, int) {
        const double width = page.widthPoints();
        const double height = page.heightPoints();
        bool ok = false;
        if (!options.header.isEmpty()) {
            const double x = width / 2.0 - estimatedWidth(options.header, options.fontSize) / 2.0;
            ok = page.addTextLayer(options.header, x, height - options.fontSize * 1.8,
                                   options.fontSize, options.color, 0.0, 1.0) ||
                 ok;
        }
        if (!options.footer.isEmpty()) {
            const double x = width / 2.0 - estimatedWidth(options.footer, options.fontSize) / 2.0;
            ok = page.addTextLayer(options.footer, x, options.fontSize * 0.9, options.fontSize,
                                   options.color, 0.0, 1.0) ||
                 ok;
        }
        return ok;
    });
}

bool applyPageNumbers(PdfDocument& document, const PageNumberOptions& options) {
    if (!canRenderWithStandardFont(options.format)) {
        return false;
    }
    return allPages(document, [&](PdfPage& page, int index, int count) {
        QString text = options.format;
        text.replace(QStringLiteral("%1"), QString::number(index + 1));
        text.replace(QStringLiteral("%2"), QString::number(count));
        const double width = page.widthPoints();
        const double x = width - estimatedWidth(text, options.fontSize) - options.fontSize;
        return page.addTextLayer(text, std::max(options.fontSize, x), options.fontSize * 0.9,
                                 options.fontSize, options.color, 0.0, 1.0);
    });
}


bool applyBatesNumbering(PdfDocument& document, const BatesOptions& options) {
    if (!canRenderWithStandardFont(options.prefix) || options.digits <= 0 || options.digits > 12) {
        return false;
    }
    return allPages(document, [&](PdfPage& page, int index, int) {
        const QString number = QString::number(options.start + index).rightJustified(
            options.digits, QLatin1Char('0'));
        const QString text = options.prefix + number;
        const double width = page.widthPoints();
        const double x = width - estimatedWidth(text, options.fontSize) - options.fontSize;
        return page.addTextLayer(text, std::max(options.fontSize, x),
                                 options.fontSize * 0.9, options.fontSize,
                                 options.color, 0.0, 1.0);
    });
}

}  // namespace aipdf::pdf
