// 国际化底座（v0.16.0）：.ts 运行时解析、语言解析与回退、切换与持久化。
//
// 这些用例只碰 QtCore：翻译器是纯字符串查找，语言解析是对 locale 列表的
// 纯函数，都不用起窗口。窗口级重译另由各面板自己的用例覆盖。

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <QXmlStreamReader>

#include "app/Language.h"
#include "i18n/Translate.h"

namespace {

using aipdf::i18n::LanguageManager;
using aipdf::i18n::TsTranslator;

constexpr auto kMainWindowContext = "aipdf::app::MainWindow";
// 目录里一定有这条：主窗口的欢迎页标签。
constexpr auto kSource = "欢迎页";
constexpr auto kEnglish = "Welcome";

QByteArray tsDocument(const QString& body) {
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<!DOCTYPE TS>\n"
               "<TS version=\"2.1\" language=\"en\" sourcelanguage=\"zh_CN\">%1</TS>\n")
        .arg(body)
        .toUtf8();
}

bool loadFromString(TsTranslator* translator, const QByteArray& document, QString* error = nullptr) {
    QBuffer buffer;
    buffer.setData(document);
    buffer.open(QIODevice::ReadOnly);
    return translator->loadTs(&buffer, error);
}

}  // namespace

class I18nTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void parsesStandardCatalog();
    void ignoresUnfinishedEntries();
    void supportsNumerusForms();
    void rejectsMalformedCatalog();
    void rejectsNonCatalogDocument();
    void resolveLanguage_data();
    void resolveLanguage();

    void embeddedCatalogLoads();
    void embeddedCatalogCoversKnownStrings();

    void switchToEnglishTranslates();
    void switchBackToChineseUsesSource();
    void unknownPreferenceIsRejected();
    void missingCatalogIsReported();
    void preferenceIsPersisted();
    void environmentOverrideWinsWithoutPersisting();
    void textBindingsRetranslate();

private:
    QTemporaryDir settingsDir_;
    // 语言包目录也指到临时目录：用例不碰用户真实 AppData 里那份。
    QTemporaryDir packDir_;
};

void I18nTests::initTestCase() {
    // 设置落到临时目录：用例不碰真实用户的那份配置。
    QVERIFY(settingsDir_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir_.path());
    QCoreApplication::setOrganizationName(QStringLiteral("AiPdfEditorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("I18nTests"));
    // 用户语言包目录同理：语言管理器现在会先扫用户目录，
    // 用例不覆盖的话，这台机器上装过的语言包会漏进断言里。
    QVERIFY(packDir_.isValid());
    aipdf::i18n::setLanguagePackDirectoryOverride(packDir_.path());
    LanguageManager::instance().loadSavedPreference();
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void I18nTests::cleanupTestCase() {
    LanguageManager::instance().setPreference(QStringLiteral("zh_CN"));
    aipdf::i18n::setLanguagePackDirectoryOverride(QString());
    QSettings().clear();
}

void I18nTests::parsesStandardCatalog() {
    const QByteArray document = tsDocument(QStringLiteral(R"(
        <context>
            <name>aipdf::app::MainWindow</name>
            <message>
                <location filename="../app/MainWindow.cpp" line="236"/>
                <source>文件(&amp;F)</source>
                <translation>File(&amp;F)</translation>
            </message>
            <message>
                <source>已保存 %1</source>
                <translation>Saved %1</translation>
            </message>
        </context>)"));
    TsTranslator translator;
    QString error;
    QVERIFY2(loadFromString(&translator, document, &error), qPrintable(error));
    QCOMPARE(translator.messageCount(), 2);
    QCOMPARE(translator.catalogLanguage(), QStringLiteral("en"));
    QCOMPARE(translator.translate(kMainWindowContext, "文件(&F)"), QStringLiteral("File(&F)"));
    QCOMPARE(translator.translate(kMainWindowContext, "已保存 %1"), QStringLiteral("Saved %1"));
    // 没登记的词条返回 null：Qt 据此继续找下一个翻译器，最终回退源文案。
    QVERIFY(translator.translate(kMainWindowContext, "未登记").isNull());
    QVERIFY(translator.translate("别的上下文", "文件(&F)").isNull());
}

void I18nTests::ignoresUnfinishedEntries() {
    const QByteArray document = tsDocument(QStringLiteral(R"(
        <context>
            <name>aipdf::app::MainWindow</name>
            <message>
                <source>翻译到一半</source>
                <translation type="unfinished">half done</translation>
            </message>
            <message>
                <source>已完成</source>
                <translation>done</translation>
            </message>
        </context>)"));
    TsTranslator translator;
    QVERIFY(loadFromString(&translator, document));
    // 未完成的不进目录：界面上继续显示中文，覆盖率脚本也会报出来。
    QCOMPARE(translator.messageCount(), 1);
    QVERIFY(translator.translate(kMainWindowContext, "翻译到一半").isNull());
    QCOMPARE(translator.translate(kMainWindowContext, "已完成"), QStringLiteral("done"));
    QVERIFY(!translator.isEmpty());
}

void I18nTests::supportsNumerusForms() {
    const QByteArray document = tsDocument(QStringLiteral(R"(
        <context>
            <name>aipdf::app::MainWindow</name>
            <message numerus="yes">
                <source>%n 页</source>
                <translation>
                    <numerusform>%n page</numerusform>
                    <numerusform>%n pages</numerusform>
                </translation>
            </message>
        </context>)"));
    TsTranslator translator;
    QVERIFY(loadFromString(&translator, document));
    QCOMPARE(translator.translate(kMainWindowContext, "%n 页", nullptr, 1), QStringLiteral("%n page"));
    QCOMPARE(translator.translate(kMainWindowContext, "%n 页", nullptr, 2), QStringLiteral("%n pages"));
}

void I18nTests::rejectsMalformedCatalog() {
    TsTranslator translator;
    QString error;
    QVERIFY(!loadFromString(&translator, QByteArray("<TS><context><name>坏掉了"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(translator.isEmpty());
}

void I18nTests::rejectsNonCatalogDocument() {
    TsTranslator translator;
    QString error;
    QVERIFY(!loadFromString(&translator, QByteArray("<html><body>不是目录</body></html>"), &error));
    QVERIFY(!error.isEmpty());
}

void I18nTests::resolveLanguage_data() {
    QTest::addColumn<QStringList>("uiLanguages");
    QTest::addColumn<QString>("expected");

    QTest::newRow("简体中文") << QStringList{QStringLiteral("zh-Hans-CN")} << QStringLiteral("zh_CN");
    QTest::newRow("繁体中文归到中文") << QStringList{QStringLiteral("zh-Hant-TW")} << QStringLiteral("zh_CN");
    QTest::newRow("英文") << QStringList{QStringLiteral("en-US")} << QStringLiteral("en");
    QTest::newRow("未知语言回退英文") << QStringList{QStringLiteral("ja-JP")} << QStringLiteral("en");
    QTest::newRow("空列表回退英文") << QStringList{} << QStringLiteral("en");
    QTest::newRow("跳过不认识的语言") << (QStringList{QStringLiteral("fr-FR"), QStringLiteral("zh-CN")})
                                     << QStringLiteral("zh_CN");
}

void I18nTests::resolveLanguage() {
    QFETCH(QStringList, uiLanguages);
    QFETCH(QString, expected);
    QCOMPARE(aipdf::i18n::resolveLanguage(uiLanguages), expected);
}

void I18nTests::embeddedCatalogLoads() {
    const QString path = LanguageManager::catalogPath(QStringLiteral("en"));
    QVERIFY(path.startsWith(QStringLiteral(":/i18n/")));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    TsTranslator translator;
    QString error;
    QVERIFY2(translator.loadTs(&file, &error), qPrintable(error));
    QCOMPARE(translator.catalogLanguage(), QStringLiteral("en"));
    QVERIFY(translator.messageCount() > 0);
    // 源语言（中文）没有目录：界面文案本身就是中文。
    QVERIFY(LanguageManager::catalogPath(QStringLiteral("zh_CN")).isEmpty());
}

void I18nTests::embeddedCatalogCoversKnownStrings() {
    QFile file(LanguageManager::catalogPath(QStringLiteral("en")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    TsTranslator translator;
    QVERIFY(translator.loadTs(&file));

    // 关键界面文案的守卫：目录被误删/改空时这里先红。
    QVERIFY2(!translator.translate(kMainWindowContext, kSource).isEmpty(),
             "英文目录里缺少主窗口欢迎页标签；重新导出 .ts 或补上翻译");
    // 带 & 快捷键的条目：XML 里写的是 &amp;，解析回来必须是单个 &。
    QCOMPARE(translator.translate(kMainWindowContext, "文件(&F)"),
             QStringLiteral("File(&F)"));
}

void I18nTests::switchToEnglishTranslates() {
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    QCOMPARE(LanguageManager::instance().code(), QStringLiteral("en"));
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource), QString::fromUtf8(kEnglish));
}

void I18nTests::switchBackToChineseUsesSource() {
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
    QCOMPARE(LanguageManager::instance().code(), QStringLiteral("zh_CN"));
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource), QString::fromUtf8(kSource));
}

void I18nTests::unknownPreferenceIsRejected() {
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    QVERIFY(!LanguageManager::instance().setPreference(QStringLiteral("tlh")));
    // 不认识的值不改状态，界面保持原样。
    QCOMPARE(LanguageManager::instance().code(), QStringLiteral("en"));
    QCOMPARE(LanguageManager::instance().preference(), QStringLiteral("en"));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void I18nTests::missingCatalogIsReported() {
    // 先把当前语言定住（英文），后面要确认失败之后它没被改掉。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    const QString before = LanguageManager::instance().code();

    // "zz_ZZ" 这个语言包在资源里根本不存在：apply() 必须失败并说清原因。
    QVERIFY(!LanguageManager::instance().apply(QStringLiteral("zz_ZZ")));
    // 不许静默劣化：失败必须留下原因。
    QVERIFY(!LanguageManager::instance().lastError().isEmpty());
    // 也不许谎报「已切换」：界面语言必须保持原样。
    QCOMPARE(LanguageManager::instance().code(), before);

    // 切回中文收尾，别影响后面的用例。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void I18nTests::preferenceIsPersisted() {
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(), QStringLiteral("en"));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
    QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(), QStringLiteral("zh_CN"));
}

void I18nTests::environmentOverrideWinsWithoutPersisting() {
    // 验收/CI 用 AIPDF_UI_LANGUAGE 钉死界面语言：生效，但不写进用户设置。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("ui/language")).toString();

    qputenv("AIPDF_UI_LANGUAGE", "en");
    LanguageManager::instance().loadSavedPreference();
    QCOMPARE(LanguageManager::instance().code(), QStringLiteral("en"));
    QCOMPARE(LanguageManager::instance().preference(), QStringLiteral("en"));
    QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(), saved);
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource),
             QString::fromUtf8(kEnglish));

    // 不认识的值不当回事：回到设置里记的那份选择。
    qputenv("AIPDF_UI_LANGUAGE", "tlh");
    LanguageManager::instance().loadSavedPreference();
    QCOMPARE(LanguageManager::instance().code(), QStringLiteral("zh_CN"));

    qunsetenv("AIPDF_UI_LANGUAGE");
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void I18nTests::textBindingsRetranslate() {
    // 界面文案的「应用 + 登记」：切换语言后重跑一遍就换成新语言。
    QString text;
    aipdf::i18n::TextBindings bindings;
    bindings.bind([&text] { text = QCoreApplication::translate(kMainWindowContext, kSource); });
    QCOMPARE(text, QString::fromUtf8(kSource));
    QCOMPARE(bindings.size(), std::size_t(1));

    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    bindings.retranslate();
    QCOMPARE(text, QString::fromUtf8(kEnglish));

    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
    bindings.retranslate();
    QCOMPARE(text, QString::fromUtf8(kSource));
}

QTEST_GUILESS_MAIN(I18nTests)

#include "I18nTests.moc"
