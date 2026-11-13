#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 批注类型。
//
// v0.7.0 只做六种：三种基于文字选区的标记（高亮、下划线、删除线），
// 三种基于页面区域的标记（便签、矩形、墨迹）。
// 协作批注（评论回复）与高级图章不在本版本范围内。
enum class AnnotationType {
    Highlight,  // 高亮
    Underline,  // 下划线
    StrikeOut,  // 删除线
    Note,       // 便签（文本备注）
    Square,     // 矩形
    Ink,        // 墨迹
};

// 一条批注。
//
// 坐标一律是 **PDF user space**（左下原点），与字符框、链接矩形同一坐标系：
// 这样批注可以直接交给 PDFium 写进文档，中间不用换算。
// 要画到屏幕上时，由上层用 PageTransform 转到显示空间。
struct Annotation {
    // 模型内唯一标识：撤销栈与文档同步都靠它认批注。
    quint64 id = 0;
    int pageIndex = 0;
    AnnotationType type = AnnotationType::Highlight;
    // 包围盒（user space）。高亮是所有行矩形的并集；墨迹是所有笔画的并集。
    QRectF rect;
    // 逐行矩形（user space）：高亮、下划线、删除线按行着色，画出来才是一行一条。
    QVector<QRectF> quads;
    // 墨迹笔画（user space）：每条笔画是一串点。
    QVector<QVector<QPointF>> strokes;
    QColor color;
    // 便签正文与备注。
    QString contents;
    QString author;
};

// 类型的显示名（界面与轨迹用）。
QString annotationTypeName(AnnotationType type);

// 各类型的默认颜色。
QColor defaultAnnotationColor(AnnotationType type);

// 批注 ↔ JSON：可序列化命令用它落盘，撤销栈也靠它做往返校验。
QJsonObject annotationToJson(const Annotation& annotation);
bool annotationFromJson(const QJsonObject& object, Annotation* annotation);

}  // namespace aipdf::pdf
