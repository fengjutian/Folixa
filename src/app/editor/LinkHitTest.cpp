#include "app/LinkHitTest.h"

namespace aipdf::app {

int hitTestLink(const QVector<pdf::PageLink>& links,
                const PageTransform& transform,
                const QPointF& displayPoint) {
    int found = -1;
    for (int index = 0; index < links.size(); ++index) {
        const pdf::PageLink& link = links.at(index);
        if (!link.isUsable()) {
            // 目标损坏或指向文档之外的链接不可用，不参与命中。
            continue;
        }
        const QRectF rect = transform.toDisplay(link.rect);
        if (rect.width() <= 0.0 || rect.height() <= 0.0) {
            // 退化矩形不构成可点击区域。
            continue;
        }
        if (rect.contains(displayPoint)) {
            // 靠后的覆盖靠前的：PDFium 按 Z 序枚举，后枚举的画在上面。
            found = index;
        }
    }
    return found;
}

}  // namespace aipdf::app
