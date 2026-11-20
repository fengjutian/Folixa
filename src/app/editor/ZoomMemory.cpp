#include "app/editor/ZoomMemory.h"

#include "app/editor/ViewMemory.h"

#include <QSettings>

#include <cmath>

namespace aipdf::app {

QString zoomSettingsKey(const QString& filePath) {
    // 键算法搬到 ViewMemory（缩放与页面布局是同一族视图状态）。
    // 键的格式没变（view/zoom/v1/<hash>），老配置继续有效。
    return viewSettingsKey(filePath, QStringLiteral("zoom"));
}

void rememberZoomForFile(const QString& filePath, double zoom) {
    const QString key = zoomSettingsKey(filePath);
    if (key.isEmpty() || !std::isfinite(zoom) || zoom < kMinRememberedZoom ||
        zoom > kMaxRememberedZoom) {
        return;
    }
    QSettings settings;
    settings.setValue(key, zoom);
}

double rememberedZoomForFile(const QString& filePath) {
    const QString key = zoomSettingsKey(filePath);
    if (key.isEmpty()) {
        return 0.0;
    }
    QSettings settings;
    const double value = settings.value(key, 0.0).toDouble();
    if (!std::isfinite(value) || value < kMinRememberedZoom || value > kMaxRememberedZoom) {
        return 0.0;
    }
    return value;
}

}  // namespace aipdf::app
