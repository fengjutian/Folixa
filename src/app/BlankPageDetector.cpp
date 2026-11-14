#include "app/BlankPageDetector.h"

namespace aipdf::app {

namespace {

// 比这个灰度更暗才算墨迹：扫描件的纸面通常在 240 以上，
// 直接拿「非纯白」当墨迹会把阴影和噪点全算进去。
constexpr int kInkLumaThreshold = 200;

}  // namespace

double inkRatio(const QImage& pageImage) {
    if (pageImage.isNull()) {
        return 0.0;
    }
    const QImage gray = pageImage.convertToFormat(QImage::Format_Grayscale8);
    const int width = gray.width();
    const int height = gray.height();
    if (width <= 0 || height <= 0) {
        return 0.0;
    }

    qint64 ink = 0;
    for (int y = 0; y < height; ++y) {
        const uchar* line = gray.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            if (line[x] < kInkLumaThreshold) {
                ++ink;
            }
        }
    }
    return static_cast<double>(ink) / (static_cast<double>(width) * height);
}

bool looksBlank(const QImage& pageImage, double maxInkRatio) {
    if (pageImage.isNull()) {
        // 渲染失败的页不该被当成空白页删掉。
        return false;
    }
    return inkRatio(pageImage) < maxInkRatio;
}

}  // namespace aipdf::app
