#include "app/ui/ReadingScheme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>

namespace aipdf::app::theme {

namespace {

// 设置里存方案的地方。
//
// 为什么单独一个键而不是塞进「外观」那一堆：换配色和换字体是两件事，
// 用户可能只换配色不动字体。
constexpr char kSettingsGroup[] = "reading";
constexpr char kSettingsKey[] = "scheme";

QColor colorOr(const QColor& value, const QColor& fallback) {
    return value.isValid() ? value : fallback;
}

}  // namespace

QColor backdropColor(ReadingScheme scheme) {
    switch (scheme) {
        case ReadingScheme::Night:
            return QColor(0x1B, 0x1D, 0x22);
        case ReadingScheme::EyeCare:
            return QColor(0xC8, 0xD3, 0xA9);
        case ReadingScheme::HighContrast:
            return QColor(0x00, 0x00, 0x00);
        case ReadingScheme::Light:
            break;
    }
    return QColor(0xF3, 0xF4, 0xF6);
}

QColor pageOutlineColor(ReadingScheme scheme) {
    switch (scheme) {
        case ReadingScheme::Night:
            return QColor(0x3A, 0x40, 0x4C);
        case ReadingScheme::EyeCare:
            return QColor(0x9F, 0xAE, 0x82);
        case ReadingScheme::HighContrast:
            // 高对比下用纯白描边：页面边界必须在任何底色上都一眼可辨。
            return QColor(0xFF, 0xFF, 0xFF);
        case ReadingScheme::Light:
            break;
    }
    return QColor(0xD8, 0xDC, 0xE4);
}

bool isDarkScheme(ReadingScheme scheme) {
    return scheme == ReadingScheme::Night || scheme == ReadingScheme::HighContrast;
}

void applyScheme(QApplication& application, ReadingScheme scheme) {
    QPalette palette = application.palette();
    const QColor backdrop = backdropColor(scheme);
    // 画布底色走 QPalette::Mid：PageCanvas 直接用它画页面周围的底色，
    // 不必在画布里再写一套配色判断。
    //
    // 这里只动 Mid，不动 Window / Text / Base：
    // 那些是**界面**的颜色，归「界面主题」管。两边都写的话，
    // 深色界面 + 浅色阅读配色会把窗口底冲回浅灰，
    // 深色界面 + 夜间阅读配色则会把控件底冲成另一套深灰。
    palette.setColor(QPalette::Mid, backdrop);
    application.setPalette(palette);
}

ReadingScheme currentScheme() {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const int stored = settings.value(QString::fromLatin1(kSettingsKey), 0).toInt();
    settings.endGroup();
    switch (stored) {
        case 1:
            return ReadingScheme::Night;
        case 2:
            return ReadingScheme::EyeCare;
        case 3:
            return ReadingScheme::HighContrast;
        default:
            return ReadingScheme::Light;
    }
}

void setCurrentScheme(ReadingScheme scheme) {
    int stored = 0;
    switch (scheme) {
        case ReadingScheme::Night:
            stored = 1;
            break;
        case ReadingScheme::EyeCare:
            stored = 2;
            break;
        case ReadingScheme::HighContrast:
            stored = 3;
            break;
        case ReadingScheme::Light:
            stored = 0;
            break;
    }
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QString::fromLatin1(kSettingsKey), stored);
    settings.endGroup();
}

}  // namespace aipdf::app::theme
