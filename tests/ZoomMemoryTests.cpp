// 缩放记忆的单元测试。
//
// 这类"记住用户上次的样子"的功能最容易出的两个毛病：
//   1) 键不稳定——同一个文件换个写法（相对/绝对、大小写）就变成另一条记录；
//   2) 坏值不设防——设置文件被改坏、或者旧版本写进一个离谱的数值，
//      打开文档直接缩到 0.001，看起来像"文档打不开"。

#include "app/editor/ZoomMemory.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::rememberedZoomForFile;
using aipdf::app::rememberZoomForFile;
using aipdf::app::zoomSettingsKey;

class ZoomMemoryTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

private slots:
    void init();
    void emptyPathHasNoKey();
    void roundTripsPercentZoom();
    void rejectsOutOfRangeValues();
    void differentFilesGetDifferentKeys();
    void samePathSpelledDifferentlySharesKey();
};

void ZoomMemoryTests::init() {
    // 每个用例用独立设置文件，避免互相污染（也避免写进开发者真实的注册表）。
    QVERIFY(dir_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir_.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ZoomMemoryTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ZoomMemoryTests"));
    QSettings().clear();
}

void ZoomMemoryTests::emptyPathHasNoKey() {
    QVERIFY(zoomSettingsKey(QString()).isEmpty());
    rememberZoomForFile(QString(), 2.0);  // 不该崩
    QCOMPARE(rememberedZoomForFile(QString()), 0.0);
}

void ZoomMemoryTests::roundTripsPercentZoom() {
    const QString path = dir_.filePath(QStringLiteral("a.pdf"));
    QCOMPARE(rememberedZoomForFile(path), 0.0);  // 没记过
    rememberZoomForFile(path, 1.75);
    QCOMPARE(rememberedZoomForFile(path), 1.75);
}

void ZoomMemoryTests::rejectsOutOfRangeValues() {
    const QString path = dir_.filePath(QStringLiteral("b.pdf"));
    rememberZoomForFile(path, 0.0);
    rememberZoomForFile(path, 100.0);
    QCOMPARE(rememberedZoomForFile(path), 0.0);

    // 设置文件里被塞了坏值：读的时候也要挡掉。
    QSettings settings;
    settings.setValue(zoomSettingsKey(path), 0.0001);
    settings.sync();
    QCOMPARE(rememberedZoomForFile(path), 0.0);
}

void ZoomMemoryTests::differentFilesGetDifferentKeys() {
    const QString one = dir_.filePath(QStringLiteral("one.pdf"));
    const QString two = dir_.filePath(QStringLiteral("two.pdf"));
    QVERIFY(zoomSettingsKey(one) != zoomSettingsKey(two));

    rememberZoomForFile(one, 1.5);
    rememberZoomForFile(two, 2.5);
    QCOMPARE(rememberedZoomForFile(one), 1.5);
    QCOMPARE(rememberedZoomForFile(two), 2.5);
}

void ZoomMemoryTests::samePathSpelledDifferentlySharesKey() {
    const QString absolute = dir_.filePath(QStringLiteral("Mixed.PDF"));
    // 大小写与冗余分隔符不该产生第二条记录。
    const QString noisy = absolute.toUpper() + QStringLiteral("/../Mixed.PDF");
    const QString noisyKey = zoomSettingsKey(noisy);
    // 归一化之后指向同一个文件（大小写不敏感 + 去掉 ..）
    QCOMPARE(noisyKey, zoomSettingsKey(absolute));
}

QTEST_MAIN(ZoomMemoryTests)
#include "ZoomMemoryTests.moc"
