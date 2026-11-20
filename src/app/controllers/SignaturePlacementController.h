#pragma once

#include <QByteArray>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace aipdf::app {

class SignaturePlacementController final {
public:
    struct Placement {
        int pageIndex = -1;
        QRectF rect;
        QByteArray pngBytes;
    };
    struct Result {
        QVector<Placement> placements;
        QString error;
        [[nodiscard]] bool ok() const { return error.isEmpty() && !placements.isEmpty(); }
    };

    Result planRidingSeam(const QByteArray& imageBytes,
                          const QVector<QSizeF>& pageSizes) const;
};

}  // namespace aipdf::app
