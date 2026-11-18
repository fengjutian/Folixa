#include "app/editor/MouseShortcuts.h"

#include <algorithm>
#include <cmath>

namespace aipdf::app::mouse {
namespace {

constexpr int kWheelUnitsPerNotch = 120;
constexpr int kMaxNotchesPerEvent = 4;

}  // namespace

WheelGesture wheelGesture(Qt::KeyboardModifiers modifiers, const QPoint& angleDelta) {
    if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier)) {
        return WheelGesture::Zoom;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        return WheelGesture::HorizontalScroll;
    }
    // 只有横向增量（部分鼠标的倾斜滚轮 / 触摸板横滑）也走横向滚动。
    if (angleDelta.y() == 0 && angleDelta.x() != 0) {
        return WheelGesture::HorizontalScroll;
    }
    return WheelGesture::VerticalScroll;
}

double wheelZoomFactor(int angleDeltaY, double stepPerNotch) {
    if (angleDeltaY == 0) {
        return 1.0;
    }
    if (!std::isfinite(stepPerNotch) || stepPerNotch <= 0.0) {
        stepPerNotch = 1.1;
    }
    double notches = static_cast<double>(angleDeltaY) / kWheelUnitsPerNotch;
    notches = std::clamp(notches, -static_cast<double>(kMaxNotchesPerEvent),
                         static_cast<double>(kMaxNotchesPerEvent));
    const double factor = std::pow(stepPerNotch, notches);
    return std::clamp(factor, 0.5, 2.0);
}

int nextClickCount(int previousCount,
                   qint64 elapsedMs,
                   int distancePx,
                   int intervalMs,
                   int maxDistancePx) {
    if (previousCount <= 0) {
        return 1;
    }
    if (elapsedMs < 0 || elapsedMs > intervalMs) {
        return 1;
    }
    if (distancePx < 0 || distancePx > maxDistancePx) {
        return 1;
    }
    // 四击之后从头数：第五下是新的单击，不是第五击。
    if (previousCount >= 4) {
        return 1;
    }
    return previousCount + 1;
}

int anchoredScrollValue(double contentPosition, int viewportPosition, int maxScroll) {
    if (!std::isfinite(contentPosition)) {
        return 0;
    }
    const int raw = static_cast<int>(std::lround(contentPosition - viewportPosition));
    return std::clamp(raw, 0, std::max(0, maxScroll));
}

}  // namespace aipdf::app::mouse
