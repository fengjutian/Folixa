#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::doc {

// ---------------------------------------------------------------------------
// PDF → Excel / PowerPoint / Word（整篇）
//
// 为什么要有这一层：矩阵里「转换｜输出｜到 MS Office」一直记着降级——
// Excel 与 PPT 零实现，Word 只导一页文字层。命令叫「导出到 MS Office」，
// 产出却只有一页，那是**命令名与产出不符**，不是「少一个功能」。
//
// 三条约定：
//
//  1. **只导文本层**。图片、批注、精确版式进不去——这一点在界面上要说明白，
//     不许让用户以为导出的东西与原文一模一样。
//  2. **整篇，不是一页**。页与页之间用分页符（Word）或换页（PPT）隔开，
//     表格按页拆成工作表（Excel）。
//  3. **表格靠启发式识别**（`pdf::TableStructure::detect`）：认得出就写成表格，
//     认不出就写成文字行——**不猜**。猜错比不认更糟：用户会拿到一张对不上的表。
// ---------------------------------------------------------------------------

// PDF → Excel 工作簿（.xlsx）。
struct ExcelExportResult {
    bool ok = false;
    // 工作簿字节。失败时为空。
    QByteArray bytes;
    // 处理了几页。
    int pageCount = 0;
    // 写了几张工作表。
    int sheetCount = 0;
    // 认出几个表格（每页最多一个）。
    int tableCount = 0;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 把 PDF 导成 Excel 工作簿。
//
// 每页至少一张工作表：认出表格的页写「表格 + 文字」两张，没认出表格的页写一张文字表。
// 工作表名形如「第 1 页 表」「第 1 页 文字」。
ExcelExportResult exportPdfToExcel(const QByteArray& pdfBytes);

// PDF → PowerPoint（.pptx）：一页一张幻灯片。
struct PptxExportResult {
    bool ok = false;
    // 演示文稿字节。失败时为空。
    QByteArray bytes;
    // 幻灯片张数（一页 PDF 至少一张；文字太多的页会拆成「（续）」）。
    int slideCount = 0;
    // 导出了几张图片（幻灯片里的 <p:pic> 个数）。
    int imageCount = 0;
    // 认出几个表格（幻灯片里的 <a:tbl> 个数）。
    int tableCount = 0;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 把 PDF 导成演示文稿：一页一张幻灯片，标题是「第 N 页」，正文是这一页的文字。
// 一页文字太多时拆成多张（第 N 页（续）），**不丢字**。
//
// 图片按页面上的纵向位置分散到对应的幻灯片上（一页多图时按位置分），
// 表格写成真正的表格对象（<a:tbl>）。
PptxExportResult exportPdfToPptx(const QByteArray& pdfBytes);

// PDF → Word（.docx）：**整篇**。
struct WordExportResult {
    bool ok = false;
    // 文档字节。失败时为空。
    QByteArray bytes;
    // 导出了几页。
    int pageCount = 0;
    // 导出了几张图片（正文里 <w:drawing> 的个数）。
    int imageCount = 0;
    // 认出几个表格（正文里 <w:tbl> 的个数）。
    int tableCount = 0;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 把整篇 PDF 导成 Word 文档（页与页之间插分页符）。
//
// 图片是包里的独立部件（word/media/imageN.png），由正文里的 <w:drawing>
// 引用；表格写成真正的表格对象（<w:tbl>）。
WordExportResult exportPdfToWord(const QByteArray& pdfBytes);

}  // namespace aipdf::doc
