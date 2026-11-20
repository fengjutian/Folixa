#pragma once

#include <QByteArray>

#include "pdf/annot/Annotation.h"

namespace aipdf::pdf {

// 生成要额外写进批注字典的条目（不含 /AP）。
//
// PDFium 的写接口能改矩形、颜色、正文，却写不了任意字典项。可这些键恰恰决定了
// 别人怎么理解这条批注：
//   /L        直线的两个端点（没有它，直线在福昕里就是一个空矩形）
//   /LE       线端样式（箭头靠它，否则只是一条线）
//   /Vertices 多边形的顶点
//   /BE       边框效果（云线的扇贝边）
//
// 返回的是可以直接拼进对象字典的 PDF 片段；不需要额外条目时返回空。
QByteArray annotationDictionaryExtras(const Annotation& annotation);

// 批注需要的额外条目与**额外对象**。
//
// 附件是个例外：它不只是往字典里加键，还要新建两个对象
// （嵌入式文件流 + Filespec 字典）。对象号在写入时才分配，所以这里用
// `@EXTRA0`、`@EXTRA1` 占位，由写入器替换。
struct AnnotationDictionaryParts {
    QByteArray entries;
    QVector<QByteArray> objects;
};

AnnotationDictionaryParts annotationDictionaryParts(const Annotation& annotation);

// 把一个 QString 写成合法的 PDF 文本串（含定界符）。
//
// 能塞进 ASCII 时用 `(...)` 并转义 `( ) \`；否则写成 UTF-16BE 的十六进制串。
// 手写 `/Contents (` + toLatin1() 会把中文变成 ?，反斜杠还会把右括号转义掉。
QByteArray annotationTextString(const QString& text);

// 按文件名后缀猜 MIME 类型（写入嵌入文件流的 /Subtype）。
//
// 为什么需要它：PDF 规范里嵌入文件流的 /Subtype 写的是 MIME 类型，
// 阅读器靠它决定「点开这个附件用什么程序打开」。缺了这一项，
// 挂进去的视频附件在别的阅读器里可能只被当成一坨二进制。
//
// 认不出来时返回 application/octet-stream —— 这是 PDF 规范里的默认值，
// 也是一个诚实的答案：不知道就说不知道，别编一个具体类型。
//
// 注意这条只管**页面上的附件批注**（/FileAttachment，嵌入文件流由本程序自己写）。
// 文档级附件（/Names → /EmbeddedFiles）走 PDFium 的嵌入文件接口，
// 那边写不进 /Subtype —— 那是实测出来的限制，不是漏写。
QByteArray mimeTypeForFileName(const QString& fileName);

}  // namespace aipdf::pdf
