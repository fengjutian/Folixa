#pragma once

#include <QString>
#include <QVector>

#include "pdf/Annotation.h"

namespace aipdf::pdf {

// XFDF 导入导出。
//
// 为什么单独做：批注一旦只能用本程序打开，就等于把用户锁死了。XFDF 是
// Acrobat / 福昕 / 大多数审阅工具都认的交换格式，导出它可以证明"批注不是我们的私货"。
//
// 只覆盖我们支持的字段：类型、页、矩形、颜色、正文、顶点（线/多边形）、
// 墨迹笔画、作者与状态。导入时遇到不认识的东西**跳过而不是失败**——
// 别的软件导出 XFDF 时会带一堆我们不认识的属性。
QString annotationsToXfdf(const QVector<Annotation>& annotations, int pageCount);

// 导入：返回解析出的批注。页号越界的条目不收（宁可少，不要错位）。
QVector<Annotation> annotationsFromXfdf(const QString& xml, int pageCount);

}  // namespace aipdf::pdf
