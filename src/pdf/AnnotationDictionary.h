#pragma once

#include <QByteArray>

#include "pdf/Annotation.h"

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

}  // namespace aipdf::pdf
