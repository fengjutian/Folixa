#pragma once

#include <QColor>
#include <QString>

#include "pdf/kernel/PdfiumAdapter.h"

namespace aipdf::pdf {

// 页面装饰：水印、页眉页脚、页码。
//
// 它们本质上是同一件事——往页面内容流里插一段文字——所以共用一条通路：
// PdfPage::addTextLayer()。区别只在坐标与排版。
struct WatermarkOptions {
    QString text;
    double fontSize = 48.0;
    QColor color = QColor(200, 0, 0);
    double opacity = 0.15;
    // 倾斜角度（逆时针为正）。0 表示水平。
    double rotationDegrees = 45.0;
};

struct HeaderFooterOptions {
    QString header;
    QString footer;
    double fontSize = 9.0;
    QColor color = QColor(90, 96, 108);
};

struct PageNumberOptions {
    double fontSize = 9.0;
    QColor color = QColor(90, 96, 108);
    // 例如 "第 %1 页" 里的 %1 会被替换成页码。
    QString format = QStringLiteral("%1 / %2");
};

// 给每一页加同一个水印（居中、倾斜）。
bool applyWatermark(PdfDocument& document, const WatermarkOptions& options);

// 页眉在页顶居中，页脚在页底居中。
bool applyHeaderFooter(PdfDocument& document, const HeaderFooterOptions& options);

// 页码放在右下角。
bool applyPageNumbers(PdfDocument& document, const PageNumberOptions& options);

// 只支持标准 14 字体（Helvetica）能编码的字符：中文需要嵌入字体，暂不支持。
// 返回 false 表示这段文字里含有画不出来的字符。
bool canRenderWithStandardFont(const QString& text);
struct BatesOptions {
    QString prefix = QStringLiteral("BATES");
    int start = 1;
    int digits = 6;
    double fontSize = 8.0;
    QColor color = QColor(90, 96, 108);
};

// 贝茨编号：右下角逐页递增的档案编号（前缀 + 定长序号）。
bool applyBatesNumbering(PdfDocument& document, const BatesOptions& options);

}  // namespace aipdf::pdf
