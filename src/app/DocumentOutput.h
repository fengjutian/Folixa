#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include "pdf/PdfiumAdapter.h"

class QPrinter;

namespace aipdf::app {

// 页码范围（0 基、闭区间）；lastPage 为 -1 表示"到最后一页"。
struct OutputRange {
    int firstPage = 0;
    int lastPage = -1;

    bool includes(int pageIndex) const {
        return pageIndex >= firstPage && (lastPage < 0 || pageIndex <= lastPage);
    }
};

// 导出 / 打印的统一结果。
struct OutputResult {
    bool ok = false;
    QString error;
    QStringList files;
};

// 把文档转成"带外观流"的副本用于输出。
//
// 为什么不能直接画原文档：本程序写的批注在内存里没有 /AP，PDFium 不画它，
// 而导出/打印的代码没有界面那套自绘逻辑。先走一次保存路径（serializeForSave）
// 再重开，批注就跟着 /AP 一起进画面——顺带保证"打印出来的样子 == 存盘后别人看到的样子"。
pdf::PdfDocument documentForOutput(const pdf::PdfDocument& document, QString* warning);

// 导出页面图片：每页一个文件，命名 <baseName>-p001.<ext>。
struct ImageExportOptions {
    int dpi = 150;
    OutputRange range;
    QString format = QStringLiteral("png");  // png / jpg
    int jpegQuality = 90;
};

OutputResult exportPageImages(const pdf::PdfDocument& document,
                              const QString& directory,
                              const QString& baseName,
                              const ImageExportOptions& options);

// 导出纯文本：按页拼接，页间用换页符分隔（UTF-8，带 BOM，Windows 记事本能认）。
struct TextExportOptions {
    OutputRange range;
};

OutputResult exportPlainText(const pdf::PdfDocument& document,
                             const QString& filePath,
                             const TextExportOptions& options);

// 打印：每页一张图，按纸张可打印区域等比缩放居中。
//
// 走"渲染成图"而不是系统 PDF 打印栈：批注是我们自己在页面图上合成的，
// 交给系统再解释一遍 PDF 会丢掉它们，也会让"屏幕所见"和"纸上所得"对不上。
struct PrintOptions {
    OutputRange range;
    int copies = 1;
    // 关掉后只打印文档内容（批注不出现）。
    bool includeAnnotations = true;
    // 每张纸排几页：1 / 2 / 4 / 6 / 9（2×1、2×2、3×2、3×3）。
    int pagesPerSheet = 1;
};

bool printDocument(const pdf::PdfDocument& document,
                   QPrinter* printer,
                   const PrintOptions& options,
                   QString* error);

}  // namespace aipdf::app
