#include "app/ZoomMemory.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <cmath>

namespace aipdf::app {

namespace {

QString normalizedPath(const QString& filePath) {
    if (filePath.isEmpty()) {
        return {};
    }
    // 用绝对路径做键：相对路径在不同工作目录下指向同一个文件，但字符串不同，
    // 不归一化就会出现"同一个文件记住两套缩放"。
    const QFileInfo info(filePath);
    const QString absolute = info.absoluteFilePath();
    return QDir::cleanPath(absolute).toLower();
}

}  // namespace

QString zoomSettingsKey(const QString& filePath) {
    const QString normalized = normalizedPath(filePath);
    if (normalized.isEmpty()) {
        return {};
    }
    // qHash 是进程内稳定的（Qt 的散列种子固定），不同版本之间不做兼容承诺，
    // 所以键里带上版本前缀：将来换算法时旧值自然失效，不会读出乱七八糟的数字。
    return QStringLiteral("view/zoom/v1/%1").arg(qHash(normalized), 0, 16);
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
