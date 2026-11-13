#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

#include "app/PageTransform.h"
#include "pdf/Navigation.h"

namespace aipdf::app {

// 链接命中判定：纯几何，不碰 PDFium，也不碰界面。
//
// 链接矩形在 PDF user space（左下原点），鼠标位置在页面显示空间（左上原点）；
// 用同一个 PageTransform 把矩形搬到显示空间，再判断点是否落在里面。
// 字符框命中用的是同一套变换，所以链接热区和文字位置必然对齐。
//
// 多条链接重叠时取**枚举顺序靠后**的那条：PDFium 按 Z 序枚举链接注释，
// 越靠后越画在上层，也就越该先被点中。
int hitTestLink(const QVector<pdf::PageLink>& links,
                const PageTransform& transform,
                const QPointF& displayPoint);

}  // namespace aipdf::app
