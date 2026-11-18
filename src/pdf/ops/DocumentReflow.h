#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/content/ParagraphFormat.h"
#include "pdf/content/ReflowPlan.h"
#include "pdf/kernel/PageObjectModel.h"

namespace aipdf::pdf {

// ============================================================================
// 这个模块做什么、不做什么
// ============================================================================
//
// 做：把**带结构标签**的 PDF 按一个新的页宽重新排一遍，产出一份新的 PDF。
//     流程是：
//       1. 用 pdf::readStructTree 读出既有的标签树与各结构元素的文字；
//       2. 用 pdf::readPageObjectModel 把每一页的对象结构化读出来
//          （文字块 / 图片 / 矢量 / 表格 / 页眉 / 页脚），并给出稳定身份；
//       3. 按新页宽排版：正文重新断行，表格按行列重建，
//          图片与矢量按原尺寸搬过去，页眉页脚每页重复；
//       4. 用 PDFium 就地改页写进新文档，产出新的 PDF 字节。
//
// 不做——这是**取舍**，不是缺陷：
//   * **不保留原版式**。重排后页数会变，图形与表格的位置会变。
//     界面上必须把这句话讲清楚，否则用户会以为版面没变。
//   * **不保留批注、书签与内部链接的目标**。页码变了，那些指向就旧了；
//     报告里会写明这一点，不假装它还在。
//   * **不处理无标签的文档**。扫描件、纯排版文档没有标签树，
//     就没有可靠依据判断哪些是正文、哪些是页眉页脚、哪些该属于同一段。
//     这类文档**直接拒绝**并写明原因，不用启发式硬凑一个看着像的版面。
//
// **门禁**：写回之前会把重排结果重新打开、重新统计一遍，
// 和原文档的统计逐类对比。少了一类对象（文字块 / 图片 / 矢量 / 表格），
// 就**不写回**，并在 report.error 里写明少了什么。
// 门禁在领域层（这个函数里），界面那条路径同样被它拦住。

// 重排参数。
struct ReflowOptions {
    // 新页宽（点）。必须大于 0。
    //
    // 为什么只有宽度也要能干活：屏幕阅读、手机竖屏这些场景最常改的就是宽度，
    // 高度按比例跟着走就够用了。
    double pageWidth = 0.0;
    // 新页高（点）。<= 0 表示按页宽的 √2 倍自动算（与 A4 的长宽比一致）。
    double pageHeight = 0.0;
    // 页边距（点）。
    double marginLeft = 36.0;
    double marginRight = 36.0;
    double marginTop = 36.0;
    double marginBottom = 36.0;
    // 段落排版参数：字号、字体、行距、首行缩进、对齐都走仓库既有的
    // pdf::ParagraphFormat，排版本身也由 pdf::layoutParagraph 完成，
    // 这样「重排出来的样子」与界面上其它地方用同一套排版口径。
    ParagraphFormat paragraph;
    // 段落之间的间距（点）。
    double paragraphSpacing = 6.0;
};

// 重排时**没有搬过去**的内容。
struct ReflowDropped {
    // 结构元素的标签（/S），例如 "Figure"。
    QString tag;
    // 中文原因：为什么没搬。
    QString reason;
    // 原文档里的页码（0 基）。-1 表示文件里没写。
    int pageIndex = -1;
    // 补充说明（例如图片的替代文字）。
    QString detail;
};

// 重排报告。
struct ReflowReport {
    // 原文档的页数。
    int sourcePageCount = 0;
    // 新文档的页数。
    int outputPageCount = 0;
    // 参与重排的正文块（段落）总数。
    int blockCount = 0;
    // 实际排进新 PDF 的段落数。
    int reflowedBlockCount = 0;
    // 没有搬过去的内容数量。**永远等于 dropped.size()**。
    int droppedCount = 0;
    // 没有搬过去的内容清单。
    QVector<ReflowDropped> dropped;
    // 文字里有标准 14 号字体表示不了的字符、被写成 '?' 占位符的段落数。
    //
    // 这不是「丢掉」，但也不能装作没发生：排出来的字比原文少。
    int degradedBlockCount = 0;
    // 重排**前**的页面对象计数。
    PageObjectCounts sourceCounts;
    // 重排**后**的页面对象计数（门禁就是拿它和 sourceCounts 对比的）。
    PageObjectCounts outputCounts;
    // 锚点计划：源对象 → 目标页号与目标矩形。
    ReflowPlan plan;
    // 中文说明（做了什么、有什么限制）。
    QStringList notes;
    // 失败原因（中文）。为空表示成功。
    QString error;

    bool ok() const { return error.isEmpty(); }
};

// 重排结果。
//
// 名字带 Document 前缀是必须的：pdf::ReflowResult 这个名字已经被
// pdf::TextReflow 占了（那是单段文字的排版结果，不是整篇重排的结果）。
struct DocumentReflowResult {
    // 新 PDF 的字节。失败时为空。
    QByteArray bytes;
    // 报告。
    ReflowReport report;

    bool ok() const { return report.ok(); }
};

// 把一份带标签的 PDF 按新页宽重排一遍。
//
// 失败（report.error 非空、bytes 为空）的情况：
//   * 输入不是 PDF（或内容为空）；
//   * 这份文档**没有结构标签**：没有可靠依据判断正文与版式，直接拒绝，
//     并在 error 里写明「没有标签树」这个事实与后果；
//   * 新页宽不大于 0；
//   * 页边距把可用宽度或可用高度吃光了；
//   * 标签树里一个字的正文都没有；
//   * **门禁没过**：重排后的对象计数比原文档少（文字块 / 图片 / 矢量 / 表格），
//     说明有内容没搬过去。这种情况下**不写回**，error 里写明少了什么。
//
// 成功时 bytes 是新的 PDF，report 给出重排了多少段、搬走了多少对象、
// 少了什么（少了就是门禁拦住了，不会出现在成功结果里）。
DocumentReflowResult reflowTaggedDocument(const QByteArray& pdfBytes, const ReflowOptions& options);

}  // namespace aipdf::pdf
