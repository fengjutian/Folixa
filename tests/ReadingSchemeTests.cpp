// 阅读配色（夜间 / 护眼 / 高对比）的单元测试。
//
// 为什么单独一个文件：这一层只管「配色方案的取值与持久化」，
// 不碰文档也不碰渲染，测试要快、要稳。

#include "app/ReadingScheme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QTest>

using aipdf::app::theme::backdropColor;
using aipdf::app::theme::currentScheme;
using aipdf::app::theme::isDarkScheme;
using aipdf::app::theme::pageOutlineColor;
using aipdf::app::theme::ReadingScheme;
using aipdf::app::theme::setCurrentScheme;

namespace {

// 测试期间不要碰用户真实的设置：用一个独立的组织名，
// 免得测试跑完把用户保存的阅读配色改掉。
class ScopedSettings {
public:
    ScopedSettings() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-scheme-tests")));
        QSettings settings;
        settings.clear();
    }
    ~ScopedSettings() {
        QSettings settings;
        settings.clear();
    }
};

}  // namespace

class ReadingSchemeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void everySchemeHasItsOwnBackdrop();
    void highContrastIsBlackBackdropWithWhiteOutline();
    void nightAndHighContrastAreDarkSchemes();
    void schemeSurvivesSettingsRoundTrip();
    void unknownStoredValueFallsBackToLight();
    void applySchemePaintsTheBackdropIntoThePalette();

private:
    ScopedSettings scopedSettings_;
};

void ReadingSchemeTests::initTestCase() {
    // 配色方案存进设置里，测试必须读写隔离的那份，不能污染用户配置。
}

void ReadingSchemeTests::everySchemeHasItsOwnBackdrop() {
    const QList<ReadingScheme> schemes{ReadingScheme::Light, ReadingScheme::Night,
                                       ReadingScheme::EyeCare, ReadingScheme::HighContrast};
    QList<QColor> backdrops;
    for (const ReadingScheme scheme : schemes) {
        const QColor backdrop = backdropColor(scheme);
        QVERIFY2(backdrop.isValid(), "底色必须是有效颜色");
        QVERIFY2(!backdrops.contains(backdrop), "每个方案都要有自己的底色，不能撞色");
        backdrops.append(backdrop);
    }
}

void ReadingSchemeTests::highContrastIsBlackBackdropWithWhiteOutline() {
    QCOMPARE(backdropColor(ReadingScheme::HighContrast), QColor(0x00, 0x00, 0x00));
    QCOMPARE(pageOutlineColor(ReadingScheme::HighContrast), QColor(0xFF, 0xFF, 0xFF));
}

void ReadingSchemeTests::nightAndHighContrastAreDarkSchemes() {
    QVERIFY(isDarkScheme(ReadingScheme::Night));
    QVERIFY(isDarkScheme(ReadingScheme::HighContrast));
    QVERIFY(!isDarkScheme(ReadingScheme::Light));
    QVERIFY(!isDarkScheme(ReadingScheme::EyeCare));
}

void ReadingSchemeTests::schemeSurvivesSettingsRoundTrip() {
    const QList<ReadingScheme> schemes{ReadingScheme::Light, ReadingScheme::Night,
                                       ReadingScheme::EyeCare, ReadingScheme::HighContrast};
    for (const ReadingScheme scheme : schemes) {
        setCurrentScheme(scheme);
        QCOMPARE(currentScheme(), scheme);
    }
}

void ReadingSchemeTests::unknownStoredValueFallsBackToLight() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("reading"));
    settings.setValue(QStringLiteral("scheme"), 99);
    settings.endGroup();
    QCOMPARE(currentScheme(), ReadingScheme::Light);
}

void ReadingSchemeTests::applySchemePaintsTheBackdropIntoThePalette() {
    aipdf::app::theme::applyScheme(*qApp, ReadingScheme::Night);
    QCOMPARE(qApp->palette().color(QPalette::Mid), backdropColor(ReadingScheme::Night));
    aipdf::app::theme::applyScheme(*qApp, ReadingScheme::Light);
    QCOMPARE(qApp->palette().color(QPalette::Mid), backdropColor(ReadingScheme::Light));
}

QTEST_MAIN(ReadingSchemeTests)
#include "ReadingSchemeTests.moc"
