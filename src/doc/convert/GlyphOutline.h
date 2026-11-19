#pragma once

// 字形轮廓提取（PDFium 侧）。
//
// 为什么单独一个头文件：取字形轮廓要直接调 PDFium 的实验性 API
// （FPDFFont_GetGlyphPath），适配层没有对外暴露它；把这段调用单独放一个文件，
// 目的是让「哪些地方碰了 PDFium 原始 API」一眼可见。
//
// 为什么可以直接 include PDFium 的头：doc 层平时不碰 PDFium，
// 但这一处非碰不可——字形轮廓是 PDFium 内部算出来的，别的库拿不到。
// 代价是这一处必须自己上锁：PDFium 的全局状态不是线程安全的，
// 本仓库踩过「随机堆损坏」。下面这把锁与 pdf/kernel/PdfiumAdapter.cpp 里的
// 那把是**同一把**（见下面的说明），所以不会和渲染线程抢 PDFium。

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtMath>

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <mutex>

namespace aipdf::pdf::detail {

// 与 pdf/kernel/PdfiumAdapter.cpp 里的那把锁**逐字一致**。
//
// 为什么必须是同一把：inline 函数的局部静态在整个程序里只有一份，
// 两处定义会被链接器合并成同一个符号，于是两把「看起来不同」的锁其实是同一把。
// 改了适配层那一份，这里必须跟着改，否则就退化成两把锁，
// PDFium 的并发问题会原样回来（src/pdf/convert/ExportFormats.cpp 里也是这个写法）。
inline std::recursive_mutex& pdfiumMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

}  // namespace aipdf::pdf::detail

namespace aipdf::doc {

// 轮廓里的一段路径。
enum class OutlineSegmentType {
    Move,    // 起点（m）
    Line,    // 直线段（l）
    Bezier,  // 贝塞尔段的一个点（c 的三个点依次给出）
};

// 一个轮廓点。
//
// close 为真表示这个点同时闭合当前子路径（PDFium 在轮廓最后一个点上给这个标记），
// 写回 PDF 时对应 "h"（closepath）。
struct OutlineSegment {
    OutlineSegmentType type = OutlineSegmentType::Move;
    double x = 0.0;
    double y = 0.0;
    bool close = false;
};

// 一个字符转曲后的结果（坐标已经是 PDF 用户空间，左下原点）。
struct OutlinedChar {
    int pageIndex = 0;
    int charIndex = 0;
    unsigned int unicode = 0;
    QString text;
    // 轮廓路径（用户空间坐标）。
    QVector<OutlineSegment> segments;
    // 原来的填充色（0..255）。取不到时 hasFillColor 为假。
    QColor fillColor = QColor(Qt::black);
    bool hasFillColor = false;
    // 文字渲染模式：0 是填充，1 是描边，2 是填充+描边，3 是不可见，
    // 4..7 是裁剪模式。轮廓化只能还原填充，别的模式要在报告里说清楚。
    int renderMode = 0;
};

// 一页的转曲结果。
struct PageOutlineResult {
    int pageIndex = 0;
    int totalChars = 0;
    // 转成了路径的字符。
    QVector<OutlinedChar> outlined;
    // 转不了的字符：逐条中文说明（页码、字符、原因）。
    QStringList failures;
    // 没有内嵌字体程序、轮廓取自替代字体的字体名（去重）。
    QStringList substitutedFonts;
    // 渲染模式不是「填充」的字符数。
    int nonFillRenderModeChars = 0;
};

// 取一页里所有字符的字形轮廓。
//
// 坐标换算（为什么是这三步）：
//   1. PDFium 给的轮廓是 **em 相对值**（1.0 = 一个 em），原点在字形的基线原点；
//   2. FPDFText_GetCharOrigin 给的是该字符基线原点在**用户空间**的位置；
//   3. FPDFText_GetMatrix 给的是有效变换矩阵（不含字号，字号是另算的）。
//   于是：用户坐标 = 原点 + 字号 × (矩阵的线性部分 × 轮廓点)。
inline PageOutlineResult extractPageOutlines(FPDF_PAGE page, int pageIndex) {
    PageOutlineResult result;
    result.pageIndex = pageIndex;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (textPage == nullptr) {
        result.failures.append(
            QStringLiteral("第 %1 页：取不到文本层（页面可能已损坏）。").arg(pageIndex + 1));
        return result;
    }

    const int charCount = FPDFText_CountChars(textPage);
    result.totalChars = charCount;

    for (int i = 0; i < charCount; ++i) {
        const unsigned int unicode = FPDFText_GetUnicode(textPage, i);
        if (unicode == 0) {
            result.failures.append(
                QStringLiteral("第 %1 页第 %2 个字符：没有可用的 Unicode 映射，取不到字形轮廓。")
                    .arg(pageIndex + 1)
                    .arg(i + 1));
            continue;
        }

        const double fontSize = FPDFText_GetFontSize(textPage, i);
        if (!(fontSize > 0.0)) {
            result.failures.append(
                QStringLiteral("第 %1 页第 %2 个字符：字号异常（%3），取不到字形轮廓。")
                    .arg(pageIndex + 1)
                    .arg(i + 1)
                    .arg(fontSize));
            continue;
        }

        FPDF_PAGEOBJECT textObject = FPDFText_GetTextObject(textPage, i);
        if (textObject == nullptr) {
            result.failures.append(
                QStringLiteral("第 %1 页第 %2 个字符：取不到文字对象，取不到字形轮廓。")
                    .arg(pageIndex + 1)
                    .arg(i + 1));
            continue;
        }
        FPDF_FONT font = FPDFTextObj_GetFont(textObject);
        if (font == nullptr) {
            result.failures.append(
                QStringLiteral("第 %1 页第 %2 个字符：取不到字体对象，取不到字形轮廓。")
                    .arg(pageIndex + 1)
                    .arg(i + 1));
            continue;
        }

        // 没有内嵌字体程序时，PDFium 会用自己的替代字体出轮廓：
        // 形状可能与原文件里显示的略有差异，但轮廓本身是有效的。这要如实记进报告。
        if (FPDFFont_GetIsEmbedded(font) != 1) {
            char fontName[256] = {0};
            FPDFFont_GetBaseFontName(font, fontName, sizeof(fontName));
            const QString name = QString::fromUtf8(fontName);
            if (!name.isEmpty() && !result.substitutedFonts.contains(name)) {
                result.substitutedFonts.append(name);
            }
        }

        FPDF_GLYPHPATH glyphPath =
            FPDFFont_GetGlyphPath(font, unicode, static_cast<float>(fontSize));
        if (glyphPath == nullptr) {
            result.failures.append(
                QStringLiteral("第 %1 页第 %2 个字符（U+%3）：该字体取不到字形轮廓"
                               "（Type3 字体、损坏的字体程序或缺少字形都属于这一类）。")
                    .arg(pageIndex + 1)
                    .arg(i + 1)
                    .arg(unicode, 4, 16, QChar('0')));
            continue;
        }

        const int segmentCount = FPDFGlyphPath_CountGlyphSegments(glyphPath);
        if (segmentCount <= 0) {
            // 空格这类字符本来就没有轮廓，不算失败。
            continue;
        }

        double originX = 0.0;
        double originY = 0.0;
        FPDFText_GetCharOrigin(textPage, i, &originX, &originY);
        FS_MATRIX matrix = {};
        FPDFText_GetMatrix(textPage, i, &matrix);

        OutlinedChar outlined;
        outlined.pageIndex = pageIndex;
        outlined.charIndex = i;
        outlined.unicode = unicode;
        const char32_t char32Code = static_cast<char32_t>(unicode);
        outlined.text = QString::fromUcs4(&char32Code, 1);
        outlined.renderMode = FPDFTextObj_GetTextRenderMode(textObject);

        unsigned int r = 0;
        unsigned int g = 0;
        unsigned int b = 0;
        unsigned int a = 0;
        if (FPDFText_GetFillColor(textPage, i, &r, &g, &b, &a)) {
            outlined.fillColor = QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
            outlined.hasFillColor = true;
        }

        outlined.segments.reserve(segmentCount);
        for (int s = 0; s < segmentCount; ++s) {
            FPDF_PATHSEGMENT segment = FPDFGlyphPath_GetGlyphPathSegment(glyphPath, s);
            if (segment == nullptr) {
                continue;
            }
            float x = 0.0f;
            float y = 0.0f;
            if (!FPDFPathSegment_GetPoint(segment, &x, &y)) {
                continue;
            }
            OutlineSegment point;
            const int type = FPDFPathSegment_GetType(segment);
            if (type == FPDF_SEGMENT_LINETO) {
                point.type = OutlineSegmentType::Line;
            } else if (type == FPDF_SEGMENT_BEZIERTO) {
                point.type = OutlineSegmentType::Bezier;
            } else {
                point.type = OutlineSegmentType::Move;
            }
            point.close = FPDFPathSegment_GetClose(segment) != 0;
            // 轮廓是 em 相对值；乘上字号，再过矩阵的线性部分，最后加上基线原点。
            point.x = originX + fontSize * (matrix.a * x + matrix.c * y);
            point.y = originY + fontSize * (matrix.b * x + matrix.d * y);
            outlined.segments.append(point);
        }

        if (outlined.segments.isEmpty()) {
            continue;
        }
        // 轮廓的盒子必须与 PDFium 给的字符框对得上。
        //
        // 为什么必须对：CID 字体缺 /ToUnicode（或 ToUnicode 写坏）时，
        // 「Unicode → 字符码 → 字形」这条路会走到 .notdef，
        // 于是画出来是一个空方框而不是原来的字——页面看着「有东西」，
        // 其实内容是错的，而且不会有任何报错。对不上就当转不了，
        // 如实报出来，不许拿错字形冒充转曲成功。
        double boxLeft = 0.0;
        double boxRight = 0.0;
        double boxBottom = 0.0;
        double boxTop = 0.0;
        if (FPDFText_GetCharBox(textPage, i, &boxLeft, &boxRight, &boxBottom, &boxTop)) {
            double minX = 0.0;
            double minY = 0.0;
            double maxX = 0.0;
            double maxY = 0.0;
            for (int s = 0; s < outlined.segments.size(); ++s) {
                const OutlineSegment& point = outlined.segments.at(s);
                if (s == 0) {
                    minX = maxX = point.x;
                    minY = maxY = point.y;
                    continue;
                }
                minX = qMin(minX, point.x);
                minY = qMin(minY, point.y);
                maxX = qMax(maxX, point.x);
                maxY = qMax(maxY, point.y);
            }
            const double boxWidth = boxRight - boxLeft;
            const double boxHeight = boxTop - boxBottom;
            const double tolerance = qMax(0.75, 0.02 * qMax(boxWidth, boxHeight));
            const double delta = qMax(qMax(qAbs(minX - boxLeft), qAbs(maxX - boxRight)),
                                      qMax(qAbs(minY - boxBottom), qAbs(maxY - boxTop)));
            if (delta > tolerance) {
                result.failures.append(
                    QStringLiteral("第 %1 页第 %2 个字符（U+%3）：取到的字形与原字形对不上"
                                   "（多半是字体里没有这个字形，取到了 .notdef 方框），"
                                   "已按「转不了」处理。")
                        .arg(pageIndex + 1)
                        .arg(i + 1)
                        .arg(unicode, 4, 16, QChar('0')));
                continue;
            }
        }
        if (outlined.renderMode != 0) {
            ++result.nonFillRenderModeChars;
        }
        result.outlined.append(outlined);
    }

    FPDFText_ClosePage(textPage);
    return result;
}

// 取整份 PDF 每一页的文字轮廓。
//
// 注意：调用前 PDFium 必须已经初始化（本仓库由 pdf 适配层负责初始化，
// 所以这里不重复调 FPDF_InitLibraryWithConfig——那个函数一个进程只该调一次）。
inline bool extractOutlines(const QByteArray& pdfBytes, QVector<PageOutlineResult>* pages,
                            QString* error) {
    if (pages == nullptr) {
        return false;
    }
    pages->clear();

    const std::lock_guard<std::recursive_mutex> lock(aipdf::pdf::detail::pdfiumMutex());

    FPDF_DOCUMENT document = FPDF_LoadMemDocument(pdfBytes.constData(),
                                                   static_cast<int>(pdfBytes.size()), nullptr);
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("字体转曲失败：PDFium 打不开这份 PDF。");
        }
        return false;
    }

    const int pageCount = FPDF_GetPageCount(document);
    for (int i = 0; i < pageCount; ++i) {
        FPDF_PAGE page = FPDF_LoadPage(document, i);
        if (page == nullptr) {
            FPDF_CloseDocument(document);
            if (error != nullptr) {
                *error = QStringLiteral("字体转曲失败：第 %1 页打不开。").arg(i + 1);
            }
            return false;
        }
        pages->append(extractPageOutlines(page, i));
        FPDF_ClosePage(page);
    }

    FPDF_CloseDocument(document);
    return true;
}

}  // namespace aipdf::doc
