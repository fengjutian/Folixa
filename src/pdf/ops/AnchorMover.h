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

struct AnchorMoveResult {
    QByteArray bytes;
    int moved = 0;
    // 非空表示有图片没找到或写回失败；调用方不应使用 bytes。
    QStringList errors;

    bool ok() const { return errors.isEmpty() && !bytes.isEmpty(); }
};

// 真正改写页内容流：在命中的图片 `Do` 前后包一层
// `q 1 0 0 1 0 -deltaY cm ... Do Q`，只移动这一张图，其它内容不动。
//
// 为什么要包 q/Q：直接插 cm 会改掉后续操作的 CTM，把整页都带偏。
//
// 匹配口径：按「当前 CTM 变换后的单位方块」算图片矩形，与 from 比较（容差 1pt）。
// 找不到对应图片就整体失败并列出是哪一页的哪个矩形——不静默跳过。
AnchorMoveResult moveAnchoredImages(const QByteArray& pdfBytes,
                                    const QVector<AnchorImageMove>& moves);

}  // namespace aipdf::pdf
