#pragma once

#include <QByteArray>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 注释形状（callout 箭头框 + 区域高亮）的领域层。
//
// 这一层只做两件事：算出几何、算好外观流（/AP 的内容）。
// 不碰文档、不碰界面：界面把用户画出来的东西换算成页面坐标传进来，
// 这里把结果算成批注可以直接用的坐标与内容流。
//
// ---------------------------------------------------------------
// 坐标系（全文件统一，踩错一次就整体错位）
// ---------------------------------------------------------------
// 1. 入参一律是**页面 user space**：原点在页面左下角，y 向上。
//    与 Annotation.h 里的批注矩形、PageTransform 的页面坐标是同一套。
// 2. 外观流里的坐标是**批注自身坐标系**：原点在批注矩形的左下角，
//    y 仍然向上（PDF 规范：/AP 是 Form XObject，BBox 为 [0 0 w h]，
//    坐标从左下角起算）。
//    直接拿页面坐标去画，整个图形会平移一个批注矩形的位置 ——
//    这是批注外观最容易踩的坑，所以对外的几何函数一律返回批注自身坐标。
//
// QRectF 在本文件里的含义（沿用 AppearanceStream.cpp 的约定）：
// rect.y() 是下边界、rect.y() + rect.height() 是上边界。
// 用 Qt 的 bottom()（= y + height）当下边界会把整个图形翻过来。

// callout 箭头框：一个方框 + 一条指向目标点的折线箭头。
struct CalloutSpec {
    // 批注框（页面 user space）。同时也是批注矩形（/Rect）：
    // 外观流的 BBox 就是它，画到框外会被阅读器裁掉。
    QRectF box;
    // 箭头尖指向的位置（页面 user space）。
    QPointF tip;
    // 折点（页面 user space）。与 tip 相同 = 直箭头。
    QPointF knee;
    // 描边宽度（PDF 点）。
    double strokeWidth = 1.0;
    // 颜色。无效（默认构造）时用箭头家族的红。
    QColor color;
};

// 一个任意多边形区域（区域高亮）。
struct RegionHighlightSpec {
    // 多边形顶点（页面 user space），至少 3 个。
    QVector<QPointF> polygon;
    // 不透明度（0..1）。半透明才能看见底下的正文。
    double opacity = 0.35;
    // 颜色。无效时用高亮家族的黄。
    QColor color;
};

// 算出箭头折线在**批注自身坐标系**里的路径点。
//
// 为什么要在批注坐标系里算：外观流的原点是批注矩形左下角，
// 拿页面坐标直接去画，整体会错位一个批注矩形的位置。
//
// 返回 2 个点（直箭头：引出点、箭头尖）或 3 个点（折线：引出点、折点、箭头尖）。
// 输入非法（坐标非有限、批注框退化等）时返回空数组。
QVector<QPointF> calloutArrowPoints(const CalloutSpec& spec);

// 生成 callout 的外观流内容（Form XObject 的内容部分）。
// 坐标原点在批注框左下角；箭头必须完全落在批注框内 ——
// 出框的箭头会被 BBox 裁成半截，与其交半成品不如直接失败。
// 失败返回空 QByteArray，并把中文原因写进 error。
QByteArray buildCalloutAppearanceStream(const CalloutSpec& spec, QString* error = nullptr);

// 生成"箭头可以伸出注释框"的 callout 外观流。
//
// 与 buildCalloutAppearanceStream 的区别：
//   * 坐标原点变成 calloutBounds() 的左下角（批注矩形 = 注释框 ∪ 箭头）；
//   * 注释框仍然画在它在页面上的位置；
//   * 箭头允许指向框外的正文（福昕那种"注释框在别处、箭头指向正文"的形状）。
// 批注的 /Rect 必须用 calloutBounds()，否则出框的箭头还是会被裁掉。
QByteArray buildCalloutAppearanceStreamExtended(const CalloutSpec& spec, QString* error = nullptr);

// callout 的批注矩形（/Rect）应该覆盖的范围：注释框与箭头的并集。
// 箭头指向框外正文时，/Rect 必须用这个矩形。
QRectF calloutBounds(const CalloutSpec& spec);

// 多边形是否自交。
//
// 自交的高亮画出来会翻车（填充按非零环绕规则会把交叉区域也算进去），
// 必须在生成外观流之前拒掉。
// 算法：两两检查非相邻边是否相交，O(n²)；顶点很少的区域高亮够用。
bool polygonIsSelfIntersecting(const QVector<QPointF>& polygon);

// 生成区域高亮的外观流内容（Form XObject 的内容部分）。
// 坐标原点在多边形包围盒的左下角。
// 失败返回空 QByteArray，并把中文原因写进 error。
QByteArray buildRegionHighlightAppearanceStream(const RegionHighlightSpec& spec, QString* error = nullptr);

// 多边形的包围盒（页面 user space）。
QRectF polygonBounds(const QVector<QPointF>& polygon);

// 区域高亮要半透明必须用 ExtGState，这个片段要放进外观流的 /Resources。
// 少了它，/GS0 gs 不生效，高亮会变成不透明的色块把正文盖死。
QByteArray regionHighlightResources(double opacity);

}  // namespace aipdf::pdf
