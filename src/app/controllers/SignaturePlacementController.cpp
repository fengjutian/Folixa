#include "app/controllers/SignaturePlacementController.h"

#include <QBuffer>
#include <QImage>

namespace aipdf::app {

SignaturePlacementController::Result SignaturePlacementController::planRidingSeam(
    const QByteArray& imageBytes, const QVector<QSizeF>& pageSizes) const {
    Result result;
    QImage image;
    if (pageSizes.isEmpty() || !image.loadFromData(imageBytes)) {
        result.error = QStringLiteral("Invalid signature image or empty document.");
        return result;
    }

    constexpr double kSliceWidth = 18.0;
    for (int pageIndex = 0; pageIndex < pageSizes.size(); ++pageIndex) {
        const int left = image.width() * pageIndex / pageSizes.size();
        const int right = image.width() * (pageIndex + 1) / pageSizes.size();
        const QImage slice = image.copy(left, 0, qMax(1, right - left), image.height());
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !slice.save(&buffer, "PNG")) {
            result.error = QStringLiteral("Unable to encode a signature slice.");
            result.placements.clear();
            return result;
        }
        const QSizeF page = pageSizes.at(pageIndex);
        const double sliceHeight =
            qMin(page.height() * 0.45, kSliceWidth * slice.height() / qMax(1, slice.width()));
        result.placements.append({
            pageIndex,
            QRectF(page.width() - kSliceWidth,
                   page.height() / 2.0 - sliceHeight / 2.0,
                   kSliceWidth, sliceHeight),
            std::move(png)});
    }
    return result;
}

}  // namespace aipdf::app
