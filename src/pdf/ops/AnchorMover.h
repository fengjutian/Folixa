#pragma once

#include <QByteArray>
#include <QRectF>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 要求把一张图片按页坐标平移。
struct AnchorImageMove {
    int pageIndex = 0;
    // 图片当前在页面上的矩形（PDF user space，左下原点）。
    QRectF from;
    // 正数 = 往下推这么多点（PDF 坐标 y 向上，内部会转成 -deltaY）。
    double deltaY = 0.0;
};

// 要求把一条矢量路径 / 表格框按页坐标平移。
struct AnchorPathMove {
    int pageIndex = 0;
    // 路径当前在页面上的包围盒（曲线按控制点取保守包围盒）。
    QRectF from;
    double deltaY = 0.0;
};

struct AnchorMoveResult {
    QByteArray bytes;
    int moved = 0;
    int movedPaths = 0;
    // 非空表示有对象没找到或写回失败；调用方不应使用 bytes。
    QStringList errors;

    bool ok() const { return errors.isEmpty() && !bytes.isEmpty(); }
};

// 真正改写页内容流：
//   * 图片：在命中的 `Do` 前后包一层 `q 1 0 0 1 0 -deltaY cm ... Do Q`；
//   * 路径：把该路径所有点的坐标按当前 CTM 的逆变换平移。
//
// 为什么要包 q/Q：直接插 cm 会改掉后续操作的 CTM，把整页都带偏。
// 路径为什么按逆变换：路径坐标是用户空间，平移量要换回用户空间再改操作数。
//
// 匹配口径：按「当前 CTM 变换后的矩形/包围盒」与 from 比较（容差 2.5pt）。
// 找不到对应对象就整体失败并列出是哪一页的哪个矩形——不静默跳过。
AnchorMoveResult moveAnchoredGraphics(const QByteArray& pdfBytes,
                                      const QVector<AnchorImageMove>& images,
                                      const QVector<AnchorPathMove>& paths);

// 只搬图片的便捷入口（保留原有行为）。
AnchorMoveResult moveAnchoredImages(const QByteArray& pdfBytes,
                                    const QVector<AnchorImageMove>& moves);

}  // namespace aipdf::pdf
