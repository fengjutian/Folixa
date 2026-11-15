// 窗口级 i18n 用例：真正建一个控件，切换语言，看它的文字有没有就地变。
//
// 底座用例（I18nTests）只碰 QtCore；这里要证明「texts_.bind 登记 + languageChanged
// 信号」这条链路在真实控件上确实生效——不重建窗口、不重启程序。

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "app/Language.h"
#include "app/WelcomePage.h"
#include "i18n/Translate.h"

namespace {
using aipdf::app::WelcomePage;
using aipdf::i18n::LanguageManager;
}

class I18nWidgetTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void welcomePageRetranslatesLive();

private:
    QTemporaryDir settingsDir_;
};

void I18nWidgetTests::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir_.path());
    QCoreApplication::setOrganizationName(QStringLiteral("AiPdfEditorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("I18nWidgetTests"));
    LanguageManager::instance().loadSavedPreference();
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void I18nWidgetTests::cleanupTestCase() {
    LanguageManager::instance().setPreference(QStringLiteral("zh_CN"));
    QSettings().clear();
}

void I18nWidgetTests::welcomePageRetranslatesLive() {
    WelcomePage page(QStringList{});

    auto* openButton = page.findChild<QPushButton*>(QStringLiteral("primaryButton"));
    QVERIFY2(openButton != nullptr, "欢迎页上没有 primaryButton，文案断言无从谈起");
    auto* title = page.findChild<QLabel*>(QStringLiteral("dropZoneTitle"));
    QVERIFY(title != nullptr);

    // 中文是源语言：不装翻译目录时看到的就是源码里的中文。
    QCOMPARE(openButton->text(), QStringLiteral("打开 PDF"));
    QCOMPARE(title->text(), QStringLiteral("把 PDF 拖到这里"));

    // 切到英文：同一个控件、同一块内存，文字就地换成英文。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    QCOMPARE(openButton->text(), QStringLiteral("Open PDF"));
    QCOMPARE(title->text(), QStringLiteral("Drop a PDF here"));

    // 切回中文同样就地生效。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
    QCOMPARE(openButton->text(), QStringLiteral("打开 PDF"));
    QCOMPARE(title->text(), QStringLiteral("把 PDF 拖到这里"));
}

QTEST_MAIN(I18nWidgetTests)

#include "I18nWidgetTests.moc"
