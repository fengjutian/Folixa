#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/kernel/TextPage.h"

namespace aipdf::pdf {

class PdfPage;

// 版面块的种类。
//
// 为什么要有种类：文字块与图片块的「内容一样」判据完全不同——
// 文字看文本，图片看像素。把它们混成一种块，换图就永远查不出来。
enum class LayoutBlockKind {
    // 文字（已经合并到段落级）。
    Text,
    // 图片。
    Image,
    // 表格（行列网格）。
    Table,
    // 矢量图形（填充块、渐变这类有面积的图形）。
    Graphic,
};

// 种类的 ASCII 名字（技术备注里用；不要拿它拼界面文案）。
QString layoutBlockKindName(LayoutBlockKind kind);

// 一个表格单元格。
struct LayoutCell {
    // 行号 / 列号（0 基）。
    int row = -1;
    int column = -1;
    // 单元格矩形（PDF user space，左下原点）。
    QRectF bounds;
    // 单元格里的文字。
    QString text;
};

// 一个版面块。
//
// 「版面块」与 DocumentComparer 里的 TextBlock 不是一回事：
// TextBlock 是「同一行里连续的一段文字」，只用来做文字比对；
// LayoutBlock 是「一块可以独立指认的版面内容」（一段、一个图、一张表），
// 用来做结构归因——「这一块从第 3 页挪到了第 5 页」。
struct LayoutBlock {
    // 页序号（0 基）。
    int pageIndex = -1;
    // 页内序号（按阅读顺序：从上到下、从左到右）。
    int blockIndex = -1;
    // 块的种类。
    LayoutBlockKind kind = LayoutBlockKind::Text;
    // 块的包围盒（PDF user space，左下原点，**不含**页面 /Rotate）。
    // 与字符框、对象框同一坐标系，可以直接拿去高亮。
    QRectF bounds;
    // 归一化包围盒：相对本页 contentBox 的宽高归一化到 [0,1]。
    //
    // 为什么必须归一化：两份文档的页面尺寸可以不同，
    // 绝对坐标下的 IoU 在「612 宽的页」与「800 宽的页」之间没有意义。
    QRectF normalizedBounds;
    // 块里的文字（文字块 / 表格块有值；图片与图形块为空）。
    QString text;
    // 内容指纹。
    //
    // 文字块：归一化文本的哈希。
    // 表格块：行列数 + 各单元格文本的哈希。
    // 图片 / 图形块：所在区域渲染出来的像素指纹（8×8 灰度网格）。
    //
    // 为什么图片不能用对象号：两份文档里同一张图的对象号不一样，
    // 用对象号做指纹会把「同一张图」判成「两张不同的图」。
    quint64 contentHash = 0;
    // 像素指纹是否算出来了（图片 / 图形块用）。
    // false 时 contentHash 只反映几何位置，不能用来判断「图有没有被换掉」。
    bool pixelFingerprintValid = false;
    // 表格专用：行数 / 列数。
    int rowCount = 0;
    int columnCount = 0;
    // 表格专用：单元格。
    QVector<LayoutCell> cells;
};

// 抽取版面块时的选项。
struct LayoutBlockOptions {
    // 单页最多取多少个块：匹配是 O(n×m) 的，不设上限的话
    // 一页塞满文字的畸形文档能把内存吃光。
    int maxBlocksPerPage = 400;
    // 是否检测表格。
    bool detectTables = true;
    // 是否检测图片。
    bool detectImages = true;
    // 是否检测矢量图形。
    bool detectGraphics = true;
    // 图形块的最小面积占比（相对页面内容区）。
    //
    // 为什么要有：表格的边框线、装饰线都是「零面积的路径」，
    // 不按面积过滤的话一张表会变成几十条「图形块」，把差异列表淹掉。
    double minimumGraphicAreaRatio = 0.01;
    // 表格单元格文字长度的上限（中位数）。
    //
    // 为什么要有：双栏正文按行列聚起来也像一张表，
    // 但它的「单元格」是一整行几百个字的正文。要求单元格足够短，
    // 才能把「表格」与「双栏排版」分开。
    int maximumCellTextLength = 30;
    // 像素指纹网格的边长（8 表示 8×8 的灰度网格）。
    int pixelGridSize = 8;
    // 最多给多少页计算像素指纹：渲染是重活，不设上限的话几百页的文档
    // 会让「比较」看起来像卡死了。超出的部分记进 note。
    int maxFingerprintPages = 200;
    // 本页要不要算像素指纹。
    //
    // 为什么要有这个开关：像素指纹要渲染整页，是版面抽取里最重的一步。
    // 超过 maxFingerprintPages 之后，调用方把这里置 false，
    // 剩下的页只给出几何指纹（能配上，但认不出「图被换掉」）。
    bool computeFingerprint = true;
    // 本次是否真的渲染了页面（算出了像素指纹）。
    //
    // 为什么单独给一个出口：调用方要按「真正渲染过的页数」计数，
    // 光数处理过的页会把「本来就没有图的页」也算进去，
    // 很快就把渲染预算耗光了。
    bool* fingerprintComputed = nullptr;
};

// 抽出一页的版面块。
//
// 失败时返回 false 并写明原因（中文）。
bool extractLayoutBlocks(const PdfPage& page,
                         int pageIndex,
                         const LayoutBlockOptions& options,
                         QVector<LayoutBlock>* blocks,
                         QString* reason = nullptr);

// 把矩形归一化到页面内容区的 [0,1]。
//
// 注意 y 的方向：仍然沿用 PDF user space 的「y 向上」，
// 也就是矩形的下沿落在归一化坐标的下方。
QRectF normalizeRect(const QRectF& rect, const MediaBox& box);

// 两个归一化矩形的 IoU（0..1）。
double normalizedRectIou(const QRectF& left, const QRectF& right);

// ------------------------------------------------- 像素坐标 ↔ PDF user space ----
//
// 为什么单独放这一节：PdfPage::render 出来的图是**应用过页面 /Rotate 之后**的，
// 而字符框与对象框都在未旋转的用户空间里。两者之间差一个旋转，
// 不换算就会把差异框报在页面的错误位置。
//
// 这一套换算只有一份实现：像素比对（DocumentComparer）与版面块指纹
// （LayoutBlocks）都必须用同一套，否则两边的框对不上。

// 一页的「像素坐标 → 用户空间」换算表。
struct PagePixelMap {
    // 页面的有效盒子（PDF user space，左下原点）：PDFium 渲染的就是它。
    MediaBox box;
    // 页面自带的 /Rotate（顺时针度数）：0 / 90 / 180 / 270。
    int rotation = 0;
    // 一个用户空间单位对应多少像素（沿用户空间的 x / y 轴）。
    double pixelsPerUnitX = 1.0;
    double pixelsPerUnitY = 1.0;
};

// 按渲染出来的图建立换算表。
bool buildPagePixelMap(const PdfPage& page,
                       const QImage& rendered,
                       PagePixelMap* map,
                       QString* reason = nullptr);

// 图像上的一个点（像素坐标，左上原点）换算回 PDF user space。
QPointF pixelToUserSpace(const PagePixelMap& map, double px, double py);

// 图像上的一个矩形换算回 PDF user space 的矩形。
//
// 差异像素的包围盒用的是「最后一个不同像素」的下标（闭区间），
// 所以右边界与下边界要 +1 像素才是真正的外沿。
QRectF pixelRectToUserSpace(const PagePixelMap& map, const QRect& pixelRect);

// 用户空间矩形换算成图像上的矩形（取四个角的包围盒）。
QRect userRectToPixelRect(const PagePixelMap& map, const QRectF& userRect);

// 一块区域的像素指纹：把区域缩成 grid×grid 的灰度网格再哈希。
//
// 为什么用渲染像素而不是图片对象本身：仓库里没有「取出图片原始数据」的接口，
// 渲染是唯一能拿到像素的路径。已知代价：压在图上的文字会混进指纹里。
quint64 pixelFingerprint(const QImage& pageImage,
                         const PagePixelMap& map,
                         const QRectF& userRect,
                         int gridSize);

}  // namespace aipdf::pdf
