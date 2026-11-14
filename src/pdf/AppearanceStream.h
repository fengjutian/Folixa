#pragma once

#include <QByteArray>
#include <QRectF>

#include "pdf/Annotation.h"

namespace aipdf::pdf {

// 一条批注的外观流（Form XObject 的内容部分）。
//
// 为什么要自己生成：PDFium 只会渲染**自带 /AP 外观流**的注释，而它的公开 API
// 不能创建 XObject（FPDFAnnot_SetAP 只写字典项）。不补这一层，我们写进文件的
// 批注在福昕、Acrobat 里就是不可见的。
//
// 坐标约定：content 以 bbox 的左下角为原点。写入层把它包成
//   << /Type /XObject /Subtype /Form /BBox [0 0 w h] /Resources ... >>
// 之后，阅读器会把这块外观拉伸到注释的 /Rect 上。
struct Appearance {
    // content stream 字节；note 这类没有图形的类型为空。
    QByteArray content;
    // 外观的包围盒（PDF user space），与注释的 /Rect 同尺寸。
    QRectF bbox;
    // 资源字典片段（只在需要透明时非空），由写入层塞进 /Resources。
    QByteArray resources;
    // 用了 ExtGState 就必须写 /Resources，否则透明度会被忽略。
    bool usesTransparency = false;

    bool isEmpty() const { return content.isEmpty(); }
};

// 生成批注外观。只依赖批注数据，不碰 PDFium，因此可以脱离文档单测。
//
// 透明度取批注类型的惯例值：文字标记类半透明，线条类不透明。
Appearance buildAnnotationAppearance(const Annotation& annotation);

// 单个类型的默认透明度（0..1）。界面绘制与外观流共用，避免"屏幕一个样、别人打开又一个样"。
double appearanceOpacity(AnnotationType type);

// 单个类型的默认描边宽度（PDF 点）。同上，屏幕与外观流共用。
double appearanceLineWidth(AnnotationType type);

}  // namespace aipdf::pdf
