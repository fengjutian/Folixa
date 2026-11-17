#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::app {

// 往指定页追加一段内容流。
//
// 为什么放在 app 层：领域层（src/pdf/**）已定稿，里面没有「往既有页面追加
// 任意内容流」这个能力；而 PDFium 的公开接口只能加文字对象、加图片，
// 写不了任意内容流（要写就得直接改对象图）。所以这里用 qpdf 做一次
// 对象级追加：把新内容流挂进页面的 /Contents 数组，并保证页面资源里有
// 内容流用到的字体资源名。
//
// 坐标约定：内容流里的坐标是 **PDF user space**（原点在左下、y 向上），
// 与 pdf::ParagraphFormat / pdf::TextPage 一致。
//
// 失败时返回空字节，并把中文原因写进 error（不抛异常、不返回半成品）。
QByteArray appendContentStreamToPage(const QByteArray& pdfBytes, int pageIndex,
                                      const QByteArray& content,
                                      const QString& fontResourceName,
                                      QString* error = nullptr);

}  // namespace aipdf::app
