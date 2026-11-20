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

// 按文件名后缀猜 MIME 类型的实现搬到了 pdf/kernel/MimeTypes.h ——
// 这个表现在被三层共用（附件批注写流、文档级附件补 /Subtype、适配层读回兜底），
// 放在 annot 层会让 kernel 反向依赖 annot。

}  // namespace aipdf::pdf
