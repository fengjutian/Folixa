// 界面主题（浅色 / 暗色 / 跟随系统）的单元测试。
//
// 盯的是四件事：
// 1. 暗色对照表必须覆盖浅色样式表里的每一个色值——漏一个，暗色下就留一条亮边。
// 2. 暗色样式表里不能残留浅色表里的亮色值（替换确实生效了）。
// 3. 两套主题下正文与主色按钮的对比度都要够（WCAG AA：4.5:1）。
// 4. 主题模式能存能读，未知值回落到浅色；切主题会真的换掉调色板。

#include "app/Theme.h"

#include <QApplication>
#include <QDir>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QTest>

using aipdf::app::theme::currentMode;
using aipdf::app::theme::darkStyleSheet;
using aipdf::app::theme::darkSurfaceColors;
using aipdf::app::theme::darkTextColors;
using aipdf::app::theme::darkPalette;
using aipdf::app::theme::interfaceTextColor;
using aipdf::app::theme::lightPalette;
using aipdf::app::theme::lightStyleSheet;
using aipdf::app::theme::modeFromKey;
using aipdf::app::theme::modeKey;
using aipdf::app::theme::setCurrentMode;
using aipdf::app::theme::ThemeMode;

namespace {

// 测试期间不要碰用户真实的设置。
class ScopedSettings {
public:
    ScopedSettings() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-theme-tests")));
        QSettings settings;
        settings.clear();
    }
    ~ScopedSettings() {
        QSettings settings;
        settings.clear();
    }
};

// 样式表里 `color:` 后面的色值：这些是文字色。
const QRegularExpression kTextColorPattern(
    QStringLiteral("(?<![\\w-])(color:\\s*)(#[0-9A-Fa-f]{6})"));
const QRegularExpression kHexPattern(QStringLiteral("#[0-9A-Fa-f]{6}"));
const QRegularExpression kRgbaPattern(QStringLiteral("rgba\\([^)]*\\)"));

// 相对亮度（WCAG 定义）。
double relativeLuminance(const QColor& color) {
    const auto channel = [](int value) {
        const double srgb = value / 255.0;
        return srgb <= 0.03928 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) +
           0.0722 * channel(color.blue());
}

// 对比度：1.0 = 完全一样，21.0 = 黑白。
double contrastRatio(const QColor& a, const QColor& b) {
    const double first = relativeLuminance(a);
    const double second = relativeLuminance(b);
    const double lighter = std::max(first, second);
    const double darker = std::min(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

// 浅色样式表里处在文字位置上的色值。
QStringList textPositionColors(const QString& sheet) {
    QStringList colors;
    QRegularExpressionMatchIterator matches = kTextColorPattern.globalMatch(sheet);
    while (matches.hasNext()) {
        colors.append(matches.next().captured(2));
    }
    return colors;
}

// 浅色样式表里不在文字位置上的色值（背景、描边、滚动条）。
QStringList surfacePositionColors(const QString& sheet) {
    // 先把文字位置挖掉，剩下的才是表面色。
    QString rest = sheet;
    rest.remove(kTextColorPattern);
    QStringList colors;
    QRegularExpressionMatchIterator hexes = kHexPattern.globalMatch(rest);
    while (hexes.hasNext()) {
        colors.append(hexes.next().captured());
    }
    QRegularExpressionMatchIterator rgbas = kRgbaPattern.globalMatch(rest);
    while (rgbas.hasNext()) {
        colors.append(rgbas.next().captured());
    }
    return colors;
}

}  // namespace

class ThemeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void everyStylesheetColorHasADarkCounterpart();
    void darkStylesheetHasNoLeftoverLightColors();
    void darkStylesheetKeepsEveryRule();
    void textIsReadableInBothThemes();
    void modeSurvivesSettingsRoundTrip();
    void unknownStoredModeFallsBackToLight();
    void applyingDarkModeSwapsThePalette();

private:
    ScopedSettings scopedSettings_;
};

void ThemeTests::initTestCase() {
    // 主题设置存在 QSettings 里，测试必须读写隔离的那份。
}

// 暗色对照表漏登记一个色值，暗色主题下那一处就会留一条亮边。
void ThemeTests::everyStylesheetColorHasADarkCounterpart() {
    const QString sheet = lightStyleSheet();
    const QMap<QString, QString> textColors = darkTextColors();
    const QMap<QString, QString> surfaceColors = darkSurfaceColors();

    for (const QString& color : textPositionColors(sheet)) {
        QVERIFY2(textColors.contains(color),
                 qPrintable(QStringLiteral("文字色漏登记：%1").arg(color)));
    }
    for (const QString& color : surfacePositionColors(sheet)) {
        QVERIFY2(surfaceColors.contains(color),
                 qPrintable(QStringLiteral("表面色漏登记：%1").arg(color)));
    }
}

// 替换必须真的发生：浅色表里的亮色值不能原样留在暗色表里。
void ThemeTests::darkStylesheetHasNoLeftoverLightColors() {
    const QString light = lightStyleSheet();
    const QString dark = darkStyleSheet();
    for (const QString& color : surfacePositionColors(light)) {
        // #FFFFFF 在暗色下仍然是白字（主色按钮上的字），
        // 它作为「表面色」已经换成深色，这里单独放过。
        if (color == QLatin1String("#FFFFFF")) {
            continue;
        }
        QVERIFY2(!dark.contains(color),
                 qPrintable(QStringLiteral("暗色表里还留着浅色：%1").arg(color)));
    }
    // 白字必须还在：主色按钮上的字不能变成深色。
    QVERIFY(dark.contains(QStringLiteral("color: #FFFFFF")));
}

// 替换不能改变样式表的结构：规则一条都不能少。
void ThemeTests::darkStylesheetKeepsEveryRule() {
    const QStringList lightLines = lightStyleSheet().split(QLatin1Char('\n'));
    const QStringList darkLines = darkStyleSheet().split(QLatin1Char('\n'));
    QCOMPARE(darkLines.size(), lightLines.size());
}

// 文字压在底色上要看得清：正文 4.5:1，白字压主色按钮 4.5:1。
void ThemeTests::textIsReadableInBothThemes() {
    // 正文色取样式表里 `QWidget { color: ... }` 用的那个色值，
    // 暗色版本从文字色对照表里取——这样测的就是真实用的那一对。
    const QString lightTextHex = QStringLiteral("#202A3A");
    const QString darkTextHex = darkTextColors().value(lightTextHex);
    QVERIFY(!darkTextHex.isEmpty());

    QVERIFY2(contrastRatio(QColor(lightTextHex), lightPalette().color(QPalette::Window)) >= 4.5,
             "浅色主题的正文压窗口底不够清楚");
    QVERIFY2(contrastRatio(QColor(darkTextHex), darkPalette().color(QPalette::Window)) >= 4.5,
             "暗色主题的正文压窗口底不够清楚");

    // 主色按钮：白字压主色底。
    const QString accent = darkSurfaceColors().value(QStringLiteral("#4169C1"));
    QVERIFY2(contrastRatio(QColor(Qt::white), QColor(accent)) >= 4.5,
             "暗色主题的主色按钮压不住白字");

    // 主色文字压深底。
    const QString accentText = darkTextColors().value(QStringLiteral("#365AA8"));
    QVERIFY2(contrastRatio(QColor(accentText), darkPalette().color(QPalette::Window)) >= 4.5,
             "暗色主题的主色文字压深底不够清楚");
}

void ThemeTests::modeSurvivesSettingsRoundTrip() {
    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::System}) {
        setCurrentMode(mode);
        QCOMPARE(currentMode(), mode);
        QCOMPARE(modeFromKey(modeKey(mode)), mode);
    }
}

void ThemeTests::unknownStoredModeFallsBackToLight() {
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("neon"));
    QCOMPARE(currentMode(), ThemeMode::Light);
}

// 切主题要真的换掉调色板：否则界面一半深一半浅。
void ThemeTests::applyingDarkModeSwapsThePalette() {
    aipdf::app::theme::applyMode(*qApp, ThemeMode::Dark);
    QVERIFY(aipdf::app::theme::isDarkInterface());
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 80);
    QCOMPARE(interfaceTextColor(), QColor(0xED, 0xF2, 0xF8));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("#3E63BE")));

    aipdf::app::theme::applyMode(*qApp, ThemeMode::Light);
    QVERIFY(!aipdf::app::theme::isDarkInterface());
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 200);
    QCOMPARE(interfaceTextColor(), QColor(0x20, 0x2A, 0x3A));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("#4169C1")));
}

QTEST_MAIN(ThemeTests)
#include "ThemeTests.moc"
