#include "pdf/ops/AnchorPlanner.h"

#include <algorithm>

namespace aipdf::pdf {
namespace {

bool overlaps(const QRectF& left, const QRectF& right) {
    const QRectF intersection = left.intersected(right);
    return intersection.width() > 0.5 && intersection.height() > 0.5;
}

}  // namespace

AnchorPlan planAnchorMoves(const QVector<AnchorBody>& bodies,
                           const QVector<AnchorShift>& shifts, const QRectF& pageBox,
                           const QVector<QRectF>& avoidRects) {
    AnchorPlan plan;
    for (int i = 0; i < bodies.size(); ++i) {
        const AnchorBody& body = bodies.at(i);
        if (body.bounds.isEmpty()) continue;

        // 找对象上方最近的一个有位移的段落。
        const AnchorShift* owner = nullptr;
        for (const AnchorShift& shift : shifts) {
            if (shift.deltaY == 0.0) continue;
            // 「上方」：段落底边不低于对象底边（PDF y 向上）。
            if (shift.paragraph.bottom() < body.bounds.bottom() - 0.5) continue;
            if (owner == nullptr || shift.paragraph.bottom() < owner->paragraph.bottom()) {
                owner = &shift;
            }
        }
        if (owner == nullptr) continue;

        AnchorDecision decision;
        decision.bodyIndex = i;
        decision.kind = body.kind;
        decision.from = body.bounds;
        decision.deltaY = owner->deltaY;
        decision.to = body.bounds.translated(0.0, -owner->deltaY);

        if (decision.to.left() < pageBox.left() - 0.5 ||
            decision.to.right() > pageBox.right() + 0.5 ||
            decision.to.top() < pageBox.top() - 0.5 ||
            decision.to.bottom() > pageBox.bottom() + 0.5) {
            decision.conflict = QStringLiteral("移出页面");
        }
        if (decision.conflict.isEmpty()) {
            for (const QRectF& avoid : avoidRects) {
                if (overlaps(decision.to, avoid)) {
                    decision.conflict = QStringLiteral("压到重排后的文字");
                    break;
                }
            }
        }
        if (decision.conflict.isEmpty()) {
            for (const AnchorDecision& other : plan.decisions) {
                if (overlaps(decision.to, other.to)) {
                    decision.conflict = QStringLiteral("与其它对象重叠");
                    break;
                }
            }
        }
        if (!decision.conflict.isEmpty()) {
            plan.conflicts.append(
                QStringLiteral("%1：%2").arg(body.kind, decision.conflict));
        } else {
            ++plan.movedCount;
        }
        plan.decisions.append(decision);
    }
    return plan;
}

}  // namespace aipdf::pdf
