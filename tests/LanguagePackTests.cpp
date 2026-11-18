// 语言包的用户侧安装与切换（评分卡 I3）。
//
// 这一层只碰 QtCore：语言包就是一份 .ts 文件，校验、复制、加载、
// 切换都不需要窗口。窗口级重译另由 I18nWidgetTests 覆盖。
//
// 最重要的一条是「不许静默劣化」：找不到语言包时必须报错并且**保持原样**，
// 旧实现会谎报「已切换为 English」而界面一个字没变。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "app/ui/Language.h"

namespace {

using aipdf::i18n::LanguageManager;
using aipdf::i18n::TsTranslator;

constexpr auto kMainWindowContext = "aipdf::app::MainWindow";
// 主窗口的欢迎页标签：内置英文目录里一定翻译了这条。
constexpr auto kSource = "欢迎页";
constexpr auto kBuiltinEnglish = "Welcome";

// 一份 .ts 目录：language 是目录自己声明的语言，body 是 <context> 部分。
QByteArray packDocument(const QString& language, const QString& body) {
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<!DOCTYPE TS>\n"
               "<TS version=\"2.1\" language=\"%1\" sourcelanguage=\"zh_CN\">%2</TS>\n")
        .arg(language, body)
        .toUtf8();
}

// 「本应用」的语言包：上下文是 aipdf::app::MainWindow。
QByteArray appPack(const QString& language, const QString& translation) {
    return packDocument(language, QStringLiteral(R"(
        <context>
            <name>aipdf::app::MainWindow</name>
            <message>
                <source>%1</source>
                <translation>%2</translation>
            </message>
        </context>)")
                            .arg(QString::fromUtf8(kSource), translation));
}

// 别人的语言包：上下文不是本应用的。
QByteArray foreignPack(const QString& language) {
    return packDocument(language, QStringLiteral(R"(
        <context>
            <name>OtherApplication</name>
            <message>
                <source>Hello</source>
                <translation>Bonjour</translation>
            </message>
        </context>)"));
}

// 往临时目录写一份文件，返回路径。
QString writeFile(const QString& directory, const QString& name, const QByteArray& content) {
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(content);
    file.close();
    return path;
}

}  // namespace

class LanguagePackTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    // 每个用例都从「用户目录里没有语言包」开始：
    // 否则上一条用例装上的包会漏进这一条的断言里（用例之间不互相依赖顺序）。
    void init();

    void declaresItsOwnLanguage();
    void installsPackIntoUserDirectory();
    void installedLanguageBecomesSelectable();
    void reinstallReplacesPreviousPack();

    void rejectsPackOfAnotherLanguage();
    void rejectsForeignCatalog();
    void rejectsEmptyCatalog();
    void rejectsBrokenFile();
    void rejectsSourceLanguage();
    void rejectsMalformedLanguageCode();

    void guidanceTellsUserWhereToPutTheFile();
    void missingPackDoesNotSilentlyDegrade();
    void builtinPackIsUsedWithoutUserPack();
    void userPackOverridesBuiltinPack();

private:
    QTemporaryDir settingsDir_;
    // 「用户语言包目录」：程序装包时往这里写。
    QTemporaryDir packDir_;
    // 「待安装的源包目录」：**必须**和上面那个分开 ——
    // 放在一起的话，源文件本身就会被扫成「已安装的语言包」，
    // 装没装都分不出来。
    QTemporaryDir sourceDir_;
};

void LanguagePackTests::initTestCase() {
    // 设置落到临时目录：用例不碰真实用户的那份配置。
    QVERIFY(settingsDir_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir_.path());
    QCoreApplication::setOrganizationName(QStringLiteral("AiPdfEditorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("LanguagePackTests"));

    // 语言包目录也指到临时目录：否则跑一次测试就会往用户真实的
    // AppData 里写语言包文件。
    QVERIFY(packDir_.isValid());
    QVERIFY(sourceDir_.isValid());
    aipdf::i18n::setLanguagePackDirectoryOverride(packDir_.path());
    QCOMPARE(aipdf::i18n::languagePackDirectory(), packDir_.path());
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void LanguagePackTests::init() {
    QDir directory(packDir_.path());
    const QStringList files =
        directory.entryList({QStringLiteral("*.ts")}, QDir::Files);
    for (const QString& file : files) {
        QFile::remove(directory.filePath(file));
    }
}

void LanguagePackTests::cleanupTestCase() {
    LanguageManager::instance().setPreference(QStringLiteral("zh_CN"));
    aipdf::i18n::setLanguagePackDirectoryOverride(QString());
    QSettings().clear();
}

void LanguagePackTests::declaresItsOwnLanguage() {
    const QString path = writeFile(sourceDir_.path(), QStringLiteral("probe.ts"),
                                   appPack(QStringLiteral("ja"), QStringLiteral("ようこそ")));
    QVERIFY(!path.isEmpty());
    QCOMPARE(aipdf::i18n::declaredLanguageOf(path), QStringLiteral("ja"));
    // 不是 .ts 的文件读不出语言代码。
    const QString broken = writeFile(sourceDir_.path(), QStringLiteral("broken.ts"),
                                     QByteArray("not a catalog"));
    QCOMPARE(aipdf::i18n::declaredLanguageOf(broken), QString());
}

void LanguagePackTests::installsPackIntoUserDirectory() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("ja-source.ts"),
                                     appPack(QStringLiteral("ja"), QStringLiteral("ようこそ")));
    QVERIFY(!source.isEmpty());

    QString error;
    QVERIFY2(aipdf::i18n::installLanguagePack(source, QStringLiteral("ja"), &error),
             qPrintable(error));
    QVERIFY(error.isEmpty());

    // 装完就在用户目录里，文件名按语言代码。
    const QString installed = aipdf::i18n::installedLanguagePackPath(QStringLiteral("ja"));
    QVERIFY(!installed.isEmpty());
    QCOMPARE(QFileInfo(installed).fileName(),
             aipdf::i18n::languagePackFileName(QStringLiteral("ja")));
    QVERIFY(QFile::exists(installed));
    QCOMPARE(aipdf::i18n::installedLanguageCodes(), QStringList{QStringLiteral("ja")});
}

void LanguagePackTests::installedLanguageBecomesSelectable() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("ja-select.ts"),
                                     appPack(QStringLiteral("ja"), QStringLiteral("ようこそ")));
    QVERIFY(aipdf::i18n::installLanguagePack(source, QStringLiteral("ja")));

    // 装了就得能选：availableLanguages() 里要出现它。
    bool found = false;
    for (const aipdf::i18n::Language& language : aipdf::i18n::availableLanguages()) {
        if (language.code == QLatin1String("ja")) {
            found = true;
        }
    }
    QVERIFY2(found, "装了语言包却不能选，安装这一半就没有意义");

    // 切过去：界面文案要真的变。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("ja")));
    QCOMPARE(LanguageManager::instance().code(), QStringLiteral("ja"));
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource),
             QString::fromUtf8("ようこそ"));
    QCOMPARE(LanguageManager::instance().languagePackSource(QStringLiteral("ja")),
             QStringLiteral("user"));

    // 切回中文收尾。
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void LanguagePackTests::reinstallReplacesPreviousPack() {
    const QString first = writeFile(sourceDir_.path(), QStringLiteral("ko-1.ts"),
                                    appPack(QStringLiteral("ko"), QStringLiteral("첫 번째")));
    const QString second = writeFile(sourceDir_.path(), QStringLiteral("ko-2.ts"),
                                     appPack(QStringLiteral("ko"), QStringLiteral("두 번째")));
    QVERIFY(aipdf::i18n::installLanguagePack(first, QStringLiteral("ko")));
    QVERIFY(aipdf::i18n::installLanguagePack(second, QStringLiteral("ko")));

    // 重装要换掉旧的那份，不能两份都留在目录里。
    QCOMPARE(aipdf::i18n::installedLanguageCodes(), QStringList{QStringLiteral("ko")});
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("ko")));
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource),
             QString::fromUtf8("두 번째"));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void LanguagePackTests::rejectsPackOfAnotherLanguage() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("ja-wrong.ts"),
                                     appPack(QStringLiteral("ja"), QStringLiteral("ようこそ")));
    QString error;
    // 目录声明 ja，却按 ko 装：装错语言包比不装更糟。
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QStringLiteral("ko"), &error));
    QVERIFY(!error.isEmpty());
    // 没装上：用户目录里不该出现 ko。
    QVERIFY(aipdf::i18n::installedLanguagePackPath(QStringLiteral("ko")).isEmpty());
}

void LanguagePackTests::rejectsForeignCatalog() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("foreign.ts"),
                                     foreignPack(QStringLiteral("de")));
    QString error;
    // 别人的 .ts 装进来界面一个字都不会变 —— 装之前就该拦住。
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QStringLiteral("de"), &error));
    QVERIFY2(error.contains(QString::fromUtf8("本应用")), qPrintable(error));
    QVERIFY(aipdf::i18n::installedLanguagePackPath(QStringLiteral("de")).isEmpty());
}

void LanguagePackTests::rejectsEmptyCatalog() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("empty.ts"),
                                     packDocument(QStringLiteral("sv"),
                                                  QStringLiteral("<context><name>x</name></context>")));
    QString error;
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QStringLiteral("sv"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(aipdf::i18n::installedLanguagePackPath(QStringLiteral("sv")).isEmpty());
}

void LanguagePackTests::rejectsBrokenFile() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("broken2.ts"),
                                     QByteArray("<TS><context><name>坏掉了"));
    QString error;
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QStringLiteral("fr"), &error));
    QVERIFY(!error.isEmpty());
    // 文件根本打不开也是失败，不是「装成功」。
    QVERIFY(!aipdf::i18n::installLanguagePack(
        QDir(packDir_.path()).filePath(QStringLiteral("不存在.ts")),
        QStringLiteral("fr"), &error));
    QVERIFY(!error.isEmpty());
}

void LanguagePackTests::rejectsSourceLanguage() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("zh.ts"),
                                     appPack(QStringLiteral("zh_CN"), QStringLiteral("欢迎")));
    QString error;
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QStringLiteral("zh_CN"), &error));
    QVERIFY(!error.isEmpty());
}

void LanguagePackTests::rejectsMalformedLanguageCode() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("weird.ts"),
                                     appPack(QStringLiteral("my pack"), QStringLiteral("Hi")));
    QString error;
    // 目录里的 language 属性可以随便填，填出来的东西不能当语言代码用。
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QStringLiteral("my pack"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!aipdf::i18n::installLanguagePack(source, QString(), &error));
    QVERIFY(!error.isEmpty());
}

void LanguagePackTests::guidanceTellsUserWhereToPutTheFile() {
    const QString guidance = LanguageManager::instance().missingPackGuidance(QStringLiteral("ja"));
    // 引导要能照着做：目录、文件名、界面上哪儿能装，三样都要有。
    QVERIFY2(guidance.contains(packDir_.path()), qPrintable(guidance));
    QVERIFY2(guidance.contains(aipdf::i18n::languagePackFileName(QStringLiteral("ja"))),
             qPrintable(guidance));
    QVERIFY2(guidance.contains(QString::fromUtf8("安装语言包")), qPrintable(guidance));
}

void LanguagePackTests::missingPackDoesNotSilentlyDegrade() {
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    const QString before = LanguageManager::instance().code();

    // 没装的语言：必须失败、必须说原因、必须保持原样。
    QVERIFY(!LanguageManager::instance().apply(QStringLiteral("da")));
    QVERIFY(!LanguageManager::instance().lastError().isEmpty());
    QCOMPARE(LanguageManager::instance().code(), before);
    // 界面文案仍然是原来那门语言。
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource),
             QString::fromUtf8(kBuiltinEnglish));

    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void LanguagePackTests::builtinPackIsUsedWithoutUserPack() {
    // 用户目录里没有 en 的包：走程序自带的。
    QCOMPARE(LanguageManager::instance().languagePackSource(QStringLiteral("en")),
             QStringLiteral("builtin"));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource),
             QString::fromUtf8(kBuiltinEnglish));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));
}

void LanguagePackTests::userPackOverridesBuiltinPack() {
    const QString source = writeFile(sourceDir_.path(), QStringLiteral("en-user.ts"),
                                     appPack(QStringLiteral("en"), QStringLiteral("User English")));
    QVERIFY(aipdf::i18n::installLanguagePack(source, QStringLiteral("en")));
    QCOMPARE(LanguageManager::instance().languagePackSource(QStringLiteral("en")),
             QStringLiteral("user"));

    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("en")));
    // 用户目录优先：装的那份压过程序自带的。
    QCOMPARE(QCoreApplication::translate(kMainWindowContext, kSource),
             QString::fromUtf8("User English"));
    QVERIFY(LanguageManager::instance().setPreference(QStringLiteral("zh_CN")));

    // 把用户目录里的 en 包删掉：后面的用例不受这条影响
    // （用例之间不该互相依赖顺序）。
    QFile::remove(aipdf::i18n::installedLanguagePackPath(QStringLiteral("en")));
    QCOMPARE(LanguageManager::instance().languagePackSource(QStringLiteral("en")),
             QStringLiteral("builtin"));
}

QTEST_GUILESS_MAIN(LanguagePackTests)

#include "LanguagePackTests.moc"
