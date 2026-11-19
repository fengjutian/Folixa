#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一页上「不能被重排弄丢」的东西的计数。
struct PageObjectInventory {
    int pageIndex = 0;
    int textChars = 0;
    int images = 0;
    int paths = 0;
};

// 统计每页的文本字数、图片数、矢量路径数。
//
// 为什么需要：重排（改栏宽、改字号）只应该动文字；图片、表格线、矢量图形
// 一旦在写回时被顺手删掉，用户看到的是「重排完好，图没了」。
// 这个清单就是写回前后的对照物。
//
// 依赖 PDFium（取文字）与 qpdf（取页面图形）。取不出来时 error 非空，
// 调用方应当把它当成「无法证明没丢对象」，不要当成「没丢」。
QVector<PageObjectInventory> pageObjectInventory(const QByteArray& pdfBytes,
                                                 QString* error = nullptr);

// 对比两份清单：after 比 before 少了任何一类对象，就返回中文原因（列出页码与数量）；
// 没少返回空串。页数变化也算失败。
QString objectInventoryLoss(const QVector<PageObjectInventory>& before,
                            const QVector<PageObjectInventory>& after);

}  // namespace aipdf::pdf
