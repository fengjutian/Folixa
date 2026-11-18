#include "app/editor/ViewFit.h"

#include <algorithm>
#include <cmath>

namespace aipdf::app {

FitResult fitPageToViewport(const QSizeF& pagePoints, const QSize& viewportPixels,
                            qint64 maxPixels) {
    FitResult result;
    if (pagePoints.width() <= 0.0 || pagePoints.height() <= 0.0 ||
        viewportPixels.width() <= 0 || viewportPixels.height() <= 0) {
        return result;
    }

    const double byWidth = viewportPixels.width() / pagePoints.width();
    const double byHeight = viewportPixels.height() / pagePoints.height();
    double zoom = std::min(byWidth, byHeight);

    // 像素预算：面积 = 页宽×高×zoom²，超预算就按比例缩回去。
    if (maxPixels > 0) {
        const double areaAtZoom = pagePoints.width() * pagePoints.height() * zoom * zoom;
        if (areaAtZoom > static_cast<double>(maxPixels)) {
            zoom *= std::sqrt(static_cast<double>(maxPixels) / areaAtZoom);
            result.clampedByPixels = true;
        }
    }

    zoom = std::clamp(zoom, 0.05, 8.0);
    result.zoom = zoom;
    // 向下取整而不是四舍五入：向上舍入会让实际像素数略微超过预算，
    // 恰好卡在适配层上限的时候就会从"勉强能画"变成"渲染失败"。
    result.pixelSize = QSize(std::max(1, static_cast<int>(std::floor(pagePoints.width() * zoom))),
                             std::max(1, static_cast<int>(std::floor(pagePoints.height() * zoom))));
    // 适配层还有单边上限：超大页面（A0 以上）在低缩放下也可能越界。
    if (result.pixelSize.width() > 8192 || result.pixelSize.height() > 8192) {
        const double shrink = 8192.0 / std::max(result.pixelSize.width(), result.pixelSize.height());
        result.zoom = std::clamp(zoom * shrink, 0.05, 8.0);
        result.pixelSize = QSize(
            std::max(1, static_cast<int>(std::floor(pagePoints.width() * result.zoom))),
            std::max(1, static_cast<int>(std::floor(pagePoints.height() * result.zoom))));
        result.clampedByPixels = true;
    }
    return result;
}

}  // namespace aipdf::app
