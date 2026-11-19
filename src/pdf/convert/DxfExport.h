#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// ============================================================ DXF（CAD）====
//
// 「转换｜输出｜到CAD」这条链路的**域层**实现。
//
// 与 ExportFormats.h 里那份 exportPdfToDxf 的分工：
//   那份是**老路径**（R12、只有折线与可选文字、没有图层/颜色/线宽、
//   曲线被打散成折线），保留它是为了不破坏既有调用方与既有回归；
//   这份是**深度路径**：三次贝塞尔保形（真曲线实体）、尺寸标注单独成层、
//   LAYER 表齐全、颜色与线宽都写进文件。
//
// 坐标系：与 PDF 用户空间一致（原点在左下、y 向上），**不翻转**；
//         多页按页码竖着摞，一页占一块高度。
//
// 真实边界（界面文案不要写成「已对齐」）：
//   - 位图**不导出**（DXF 这一版不嵌位图），只统计张数；
//   - PDF 里的 /Dim 批注对象**不识别**：共享底座不解析批注外观流，
//     PDFium 的批注子类型枚举里也没有 Dimension。尺寸标注只认
//     「内容流里画出来的箭头 + 尺寸线 + 数字」这一种；
//   - 渐变、图案填充、剪裁路径仍然丢失；填充色与描边色都能带过来。

// 图层名固定用英文：CAD 里图层名是接口的一部分，不能随界面语言变。
inline constexpr char kDxfLayerPath[] = "PATH";  // 矢量路径
inline constexpr char kDxfLayerText[] = "TEXT";  // 普通文字
inline constexpr char kDxfLayerDim[] = "DIM";    // 尺寸标注

// CAD 深度导出的选项。
struct CadDxfOptions {
    // 用户空间 → DXF 单位的缩放。1 = 点（PDF 的默认单位）。
    double scale = 1.0;
    // 普通文字要不要也导（作为 TEXT 实体）。
    //
    // 为什么尺寸标注的文字不受这个开关管：标注的数值是标注的一部分，
    // 只剩一条没有数值的尺寸线在 CAD 里没有意义，所以它始终导出。
    bool includeText = false;
    // 尺寸标注要不要导。
    bool includeDimensions = true;
};

// DXF LAYER 表里的一个图层。
struct CadDxfLayer {
    QString name;
    int colorIndex = 7;  // AutoCAD 索引色
    bool hasTrueColor = false;
    int red = 0;
    int green = 0;
    int blue = 0;
};

// CAD 深度导出的结果。
struct CadDxfResult {
    bool ok = false;
    QByteArray dxf;
    QString error;  // 中文硬编码（领域层不放 tr()）
    int lineCount = 0;
    int polylineCount = 0;
    int splineCount = 0;
    int textCount = 0;
    int dimensionCount = 0;
    int imageCount = 0;  // 只统计：本版不导位图
    QVector<CadDxfLayer> layers;
};

// 把整份 PDF 导成 CAD 能读的 DXF（AutoCAD R2000 / AC1015 ASCII）。
//
// 多页时按页码竖着摞；失败时 ok=false 且 error 里写中文原因。
CadDxfResult exportPdfToCadDxf(const QByteArray& pdfBytes,
                                const CadDxfOptions& options = CadDxfOptions{});

}  // namespace aipdf::pdf
