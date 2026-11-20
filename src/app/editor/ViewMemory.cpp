#include "app/editor/ViewMemory.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace aipdf::app {

namespace {

QString normalizedPath(const QString& filePath) {
    if (filePath.isEmpty()) {
        return {};
    }
    // 用绝对路径做键：相对路径在不同工作目录下指向同一个文件，但字符串不同，
    // 不归一化就会出现"同一个文件记住两套视图设置"。
    const QFileInfo info(filePath);
    const QString absolute = info.absoluteFilePath();
    return QDir::cleanPath(absolute).toLower();
}

}  // namespace

QString viewSettingsKey(const QString& filePath, const QString& group) {
    const QString normalized = normalizedPath(filePath);
    if (normalized.isEmpty() || group.isEmpty()) {
        return {};
    }
    // qHash 是进程内稳定的（Qt 的散列种子固定），不同版本之间不做兼容承诺，
    // 所以键里带上版本前缀：将来换算法时旧值自然失效。
    return QStringLiteral("view/%1/v1/%2").arg(group, QString::number(qHash(normalized), 16));
}

void rememberLayoutForFile(const QString& filePath, PageLayout::LayoutMode mode) {
    const QString key = viewSettingsKey(filePath, QStringLiteral("layout"));
    if (key.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(key, static_cast<int>(mode));
}

std::optional<PageLayout::LayoutMode> rememberedLayoutForFile(const QString& filePath) {
    const QString key = viewSettingsKey(filePath, QStringLiteral("layout"));
    if (key.isEmpty()) {
        return std::nullopt;
    }
    QSettings settings;
    if (!settings.contains(key)) {
        return std::nullopt;
    }
    const int value = settings.value(key, -1).toInt();
    switch (value) {
        case static_cast<int>(PageLayout::LayoutMode::Continuous):
        case static_cast<int>(PageLayout::LayoutMode::SinglePage):
        case static_cast<int>(PageLayout::LayoutMode::Facing):
        case static_cast<int>(PageLayout::LayoutMode::FacingCover):
            return static_cast<PageLayout::LayoutMode>(value);
        default:
            // 记着但认不出来的值：按「没记过」处理，不去猜。
            return std::nullopt;
    }
}

}  // namespace aipdf::app
