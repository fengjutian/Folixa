#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 页面上的一个「锚点对象」：图片、表格、矢量图之类。
struct AnchorBody {
    QRectF bounds;
    QString kind;
};

// 一个段落的位移：paragraph 是它原来的位置，deltaY 是它变高/变矮的量
// （正数 = 底边往下推了这么多点，PDF 坐标 y 向上，所以对象要向下移动）。
struct AnchorShift {
    QRectF paragraph;
    double deltaY = 0.0;
};

// 一个锚点对象的结果。
struct AnchorDecision {
    QRectF from;
    QRectF to;
    double deltaY = 0.0;
    // 非空表示这次搬动有问题（越界 / 压到新文字 / 与别的对象重叠）。
    QString conflict;
};

struct AnchorPlan {
    QVector<AnchorDecision> decisions;
    QStringList conflicts;
    int movedCount = 0;

    bool ok() const { return conflicts.isEmpty(); }
};

// 计算锚点对象的位移，并做冲突检测（不写回，只报告）。
//
// 规则：
//   * 每个对象跟随「它上方最近的、有位移的段落」；
//   * 目标矩形 = 原矩形向下平移 deltaY；
//   * 目标越出页面、压到任何 avoidRects（重排后的文字区域）、
//     或与其它对象的目标矩形相交，都记为冲突。
//
// 这是重排锚定搬迁的第一层：先让「能不能搬」可判定，再谈改写内容流。
AnchorPlan planAnchorMoves(const QVector<AnchorBody>& bodies,
                           const QVector<AnchorShift>& shifts,
                           const QRectF& pageBox,
                           const QVector<QRectF>& avoidRects);

}  // namespace aipdf::pdf
