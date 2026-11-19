#pragma once

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

// 页面图形提取（矢量路径 + 内嵌位图）——导出类功能共用的底座。
//
// 为什么单独抽一层：
//   「导出 HTML / 导出 DXF / 导出 OFD」这三条链路都要回答同一个问题——
//   这一页上有哪些矢量路径、有哪些位图、分别在哪、什么颜色、什么线宽。
//   以前每条链路自己解析一遍内容流，口径迟早会漂（同一个圆在 HTML 里是四段
//   折线、在 DXF 里是八个点），所以这里只保留**一份**解析实现，各导出只做格式映射。
//
// 口径约定：
//   - 坐标一律是 **PDF 用户空间**（原点左下、y 向上），单位点；
//   - 路径里的点已经把内容流里的变换矩阵（cm）算进去了，也就是"页面上真正在哪"；
//   - 不含文本：文本走各自的文字层（TextPage / FPDFText），不在这里混进来；
//   - 不含裁剪路径（clipping path）与内联图像（BI/ID/EI）的位图解码：
//     位图只从页面的 XObject 资源里取，已经解压成 PNG 或 JPEG 字节。
//
// 依赖：走 qpdf 取页面内容流与资源（与 ContentStream 同一套依赖口径）。
// 未启用 qpdf 的构建下，extractPageGraphics 返回 ok=false 并在 error 里说明原因，
// 调用方必须如实把这句话带给用户，不许拿别的路径冒充。

namespace aipdf::pdf {

// 一条路径里的一个绘图指令。
enum class GraphicsVerb {
    Move,    // 抬笔移动到 p1
    Line,    // 直线到 p1
    Curve,   // 三次贝塞尔：控制点 p1、p2，终点 p3
    Close    // 闭合当前子路径
};

struct GraphicsSegment {
    GraphicsVerb verb = GraphicsVerb::Move;
    QPointF p0;  // 当前点
    QPointF p1;  // Move/Line 的目标点
    QPointF p2;  // Curve 的第二个控制点
    QPointF p3;  // Curve 的终点
};

struct GraphicsRgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

// 一条矢量路径（一个子路径序列 + 它的填充/描边属性）。
struct GraphicsPath {
    QVector<GraphicsSegment> segments;
    bool filled = false;
    bool stroked = false;
    GraphicsRgb fillColor;
    GraphicsRgb strokeColor;
    double lineWidth = 1.0;
};

// 页面上的一张内嵌位图（XObject）。
struct GraphicsImage {
    QRectF rect;          // 页坐标里的摆放矩形（已含变换）
    QByteArray data;      // PNG 或 JPEG 字节
    QString mime;         // "image/png" 或 "image/jpeg"
    int pixelWidth = 0;
    int pixelHeight = 0;
};

// 一页的图形内容。
struct PageGraphics {
    QVector<GraphicsPath> paths;
    QVector<GraphicsImage> images;
    QSizeF sizePt;         // 页面尺寸（点）
    bool ok = false;
    QString error;         // 失败原因（中文）

    bool isEmpty() const { return paths.isEmpty() && images.isEmpty(); }
};

// 提取一页的矢量路径与内嵌位图。
//
// pageIndex 从 0 起。失败时返回 ok=false，error 里写中文原因。
// 「这一页确实没有图形」不是失败：ok=true 且 paths/images 都为空。
PageGraphics extractPageGraphics(const QByteArray& pdfBytes, int pageIndex);

// 提取整篇文档的页面图形，页序与文档页序一致。
QVector<PageGraphics> extractDocumentGraphics(const QByteArray& pdfBytes, QString* error = nullptr);

}  // namespace aipdf::pdf
