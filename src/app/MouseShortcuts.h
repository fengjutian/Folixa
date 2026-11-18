#pragma once

#include <QPoint>
#include <Qt>

namespace aipdf::app::mouse {

// 鼠标快捷操作：滚轮手势、缩放倍率、连击计数与锚点滚动。
//
// 这一层只做纯计算，不碰任何 QWidget：判定规则要能被单测钉死，
// 界面层（PageCanvas / PdfView）只负责把结果接到事件与滚动条上。

// 一次滚轮事件应该做什么。
enum class WheelGesture {
    VerticalScroll,    // 普通滚轮：上下滚
    HorizontalScroll,  // Shift+滚轮或横向滚轮：左右滚
    Zoom,              // Ctrl+滚轮：以指针为中心缩放
};

// 按修饰键与滚轮方向判定手势。
//
// 优先级是 Ctrl > Shift > 方向：Ctrl+Shift+滚轮仍然缩放，
// 因为缩放是明确意图，横向滚动只是辅助。
WheelGesture wheelGesture(Qt::KeyboardModifiers modifiers, const QPoint& angleDelta);

// 滚轮增量 → 缩放倍率。
//
// 一格（120）放大 10%，连续滚动按格数累乘；单次事件最多按 4 格算，
// 触摸板偶尔会给出很大的 delta，不夹住会一下从 100% 跳到 800%。
// 结果再夹到 [0.5, 2.0]，反向滚动与正向滚动严格互逆。
double wheelZoomFactor(int angleDeltaY, double stepPerNotch = 1.1);

// 连击计数：返回 1..4。
//
// previousCount 是上一次点击的连击数；超过时间间隔或移动太远就重新从 1 数。
// 双击由 Qt 的 mouseDoubleClickEvent 直接给到 2，三击 / 四击要靠这里数。
int nextClickCount(int previousCount,
                   qint64 elapsedMs,
                   int distancePx,
                   int intervalMs,
                   int maxDistancePx);

// 缩放锚点：让内容里的某个点缩放后仍落在视口的同一位置。
//
// contentPosition 是该点在内容坐标（缩放后）里的位置，viewportPosition 是
// 指针在视口里的位置；返回滚动条应取的像素值（已夹到 [0, maxScroll]）。
int anchoredScrollValue(double contentPosition, int viewportPosition, int maxScroll);

}  // namespace aipdf::app::mouse
