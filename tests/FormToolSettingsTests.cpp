// 「表单工具设置」的单元测试。
//
// 这层决定写进每个域的 /DA 外观串：格式一变，已经生成的 PDF 外观就变了。
// 所以断言的是**逐字符的**外观串，不是"看起来差不多"的那种。
//
// 设置读写用 QSettings(path, IniFormat) 落在临时目录：默认 QSettings 在 Windows 上
// 写的是用户注册表，测试绝不能去动用户的真实设置。

#include "pdf/FormToolSettings.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTest>

using aipdf::pdf::FormToolSettings;

namespace {

// 每个用例一份独立的临时设置文件：QSettings 在同一进程里会缓存同名文件。
QString tempSettingsPath(const QString& name) {
    return QDir::tempPath() +
           QStringLiteral("/aipdf-form-tool-settings-") + name + QStringLiteral(".ini");
}

}  // namespace

class FormToolSettingsTests : public QObject {
    Q_OBJECT

private slots:
    void defaultsMatchStandardFont();
    void appearanceUsesBlackFillOperator();
    void appearanceFollowsFontAndSize();
    void appearanceUsesRgbOperatorForColor();
    void roundTripsThroughSettings();
    void unknownFontFallsBackToDefault();
    void outOfRangeSizeFallsBackToDefault();
    void invalidColorFallsBackToDefault();
    void emptySettingsUseDefaults();
};

// 默认就该是标准 14 字体、10 号、黑字。
void FormToolSettingsTests::defaultsMatchStandardFont() {
    const FormToolSettings settings;
    QCOMPARE(settings.fontName, QStringLiteral("Helv"));
    QCOMPARE(settings.fontSize, 10.0);
    QCOMPARE(settings.textColor, QColor(0, 0, 0));
    QVERIFY(FormToolSettings::fontChoices().contains(QStringLiteral("Helv")));
}

// 黑色走填充算子 "0 g"（PDF 里最常见的写法）。字号按 %f 保留一位小数。
void FormToolSettingsTests::appearanceUsesBlackFillOperator() {
    QCOMPARE(FormToolSettings().appearance(), QStringLiteral("(/Helv 10.0 Tf 0 g)"));
}

// 换字体或字号，外观串要跟着变。
void FormToolSettingsTests::appearanceFollowsFontAndSize() {
    FormToolSettings settings;
    settings.fontName = QStringLiteral("Cour");
    settings.fontSize = 14.0;
    QCOMPARE(settings.appearance(), QStringLiteral("(/Cour 14.0 Tf 0 g)"));
}

// 非黑色走 "r g b rg"，且不能再出现 "0 g"。
void FormToolSettingsTests::appearanceUsesRgbOperatorForColor() {
    FormToolSettings settings;
    settings.textColor = QColor(255, 0, 0);
    const QString appearance = settings.appearance();
    QCOMPARE(appearance, QStringLiteral("(/Helv 10.0 Tf 1.000 0.000 0.000 rg)"));
    QVERIFY(appearance.contains(QStringLiteral(" rg")));
    QVERIFY(!appearance.contains(QStringLiteral("0 g")));
}

// 存进去再读出来要一模一样：读的是另一个 QSettings 对象，证明真落盘了。
void FormToolSettingsTests::roundTripsThroughSettings() {
    const QString path = tempSettingsPath(QStringLiteral("roundtrip"));
    QFile::remove(path);
    {
        QSettings values(path, QSettings::IniFormat);
        FormToolSettings saved;
        saved.fontName = QStringLiteral("TiRo");
        saved.fontSize = 12.0;
        saved.textColor = QColor(0, 0, 255);
        saved.saveTo(values);
        values.sync();
    }
    QSettings reopened(path, QSettings::IniFormat);
    const FormToolSettings loaded = FormToolSettings::loadFrom(reopened);
    QCOMPARE(loaded.fontName, QStringLiteral("TiRo"));
    QCOMPARE(loaded.fontSize, 12.0);
    QCOMPARE(loaded.textColor, QColor(0, 0, 255));
    QFile::remove(path);
}

// 不认识的字体名不能写进 /DA：PDF 里没有这个字体，画出来是空的。
void FormToolSettingsTests::unknownFontFallsBackToDefault() {
    const QString path = tempSettingsPath(QStringLiteral("badfont"));
    QFile::remove(path);
    {
        QSettings values(path, QSettings::IniFormat);
        values.setValue(QStringLiteral("formTools/fontName"), QStringLiteral("Comic"));
        values.sync();
    }
    QSettings reopened(path, QSettings::IniFormat);
    const FormToolSettings loaded = FormToolSettings::loadFrom(reopened);
    QCOMPARE(loaded.fontName, QStringLiteral("Helv"));
    QFile::remove(path);
}

// 字号越界回落默认：0 号字画不出东西，超过 72 也没有意义。
void FormToolSettingsTests::outOfRangeSizeFallsBackToDefault() {
    const QString path = tempSettingsPath(QStringLiteral("badsize"));
    QFile::remove(path);
    for (const double size : {2.0, 100.0}) {
        {
            QSettings values(path, QSettings::IniFormat);
            values.setValue(QStringLiteral("formTools/fontSize"), size);
            values.sync();
        }
        QSettings reopened(path, QSettings::IniFormat);
        const FormToolSettings loaded = FormToolSettings::loadFrom(reopened);
        QCOMPARE(loaded.fontSize, 10.0);
        QFile::remove(path);
    }
}

// 颜色串认不出来就保持默认黑色，不能落一个无效颜色进 /DA。
void FormToolSettingsTests::invalidColorFallsBackToDefault() {
    const QString path = tempSettingsPath(QStringLiteral("badcolor"));
    QFile::remove(path);
    {
        QSettings values(path, QSettings::IniFormat);
        values.setValue(QStringLiteral("formTools/textColor"), QStringLiteral("不是颜色"));
        values.sync();
    }
    QSettings reopened(path, QSettings::IniFormat);
    const FormToolSettings loaded = FormToolSettings::loadFrom(reopened);
    QCOMPARE(loaded.textColor, QColor(0, 0, 0));
    QFile::remove(path);
}

// 什么都没有的设置文件：三项都回默认值。
void FormToolSettingsTests::emptySettingsUseDefaults() {
    const QString path = tempSettingsPath(QStringLiteral("empty"));
    QFile::remove(path);
    QSettings values(path, QSettings::IniFormat);
    const FormToolSettings loaded = FormToolSettings::loadFrom(values);
    QCOMPARE(loaded.fontName, QStringLiteral("Helv"));
    QCOMPARE(loaded.fontSize, 10.0);
    QCOMPARE(loaded.textColor, QColor(0, 0, 0));
    QFile::remove(path);
}

// 用 QTEST_GUILESS_MAIN 而不是 QTEST_MAIN：这组测试链了 Qt6::Gui（QColor 在里面），
// 但 QTEST_MAIN 会因此去建 QGuiApplication，无头环境下多一个平台插件的依赖。
QTEST_GUILESS_MAIN(FormToolSettingsTests)
#include "FormToolSettingsTests.moc"
