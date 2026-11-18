// 无障碍配置向导的单元测试。
//
// 这层测两件事：
//   1. 向导的控件真的按用户选的收成一份计划（控件 → 数据这一段）；
//   2. 计划的规则（字号夹取、朗读归一、差异判断）是对的（纯函数那一段）。
//
// 音色列表是注入进去的：查系统音色要起一个 PowerShell 子进程，
// 单测里跑那个既慢又不稳，界面层应该能在没有系统依赖的情况下被测。
//
// 对话框只构造、不 show：不弹窗、不抢前台，控件的几何与取值照样能测。

#include "app/ui/AccessibilityPlan.h"
#include "app/dialogs/AccessibilityWizard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTest>

using aipdf::app::AccessibilityPlan;
using aipdf::app::AccessibilityWizard;
using aipdf::app::accessibilityPlanDiffers;
using aipdf::app::clampAccessibilityFontSize;
using aipdf::app::normalizeAccessibilityPlan;

namespace {

// 测试用的两个音色。
QVector<aipdf::speech::SpeechVoice> testVoices() {
    QVector<aipdf::speech::SpeechVoice> voices;
    aipdf::speech::SpeechVoice first;
    first.name = QStringLiteral("Test Voice One");
    first.culture = QStringLiteral("zh-CN");
    voices.append(first);
    aipdf::speech::SpeechVoice second;
    second.name = QStringLiteral("Test Voice Two");
    second.culture = QStringLiteral("en-US");
    voices.append(second);
    return voices;
}

// 造一个向导：音色是注入的，不会去问系统。
AccessibilityWizard* makeWizard(QWidget* parent = nullptr) {
    aipdf::speech::SpeechSettings speech;
    speech.voice = QStringLiteral("Test Voice One");
    speech.rate = 2;
    speech.volume = 80;
    return new AccessibilityWizard(speech, testVoices(), parent);
}

}  // namespace

class AccessibilityWizardTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void wizardHasThreeSteps();
    void planKeepsChosenValues();
    void planKeepsGivenSpeechSettingsByDefault();
    void planClampsOutOfRangeFontSize();
    void planNormalizesSpeechSettings();
    void differsDetectsEachField();
    void voiceListIsInjected();
    void summaryLinesCoverEverySetting();
};

// QSettings 默认在 Windows 上写注册表：把默认格式与路径指到临时目录，
// 免得测试动到用户自己的设置。
void AccessibilityWizardTests::initTestCase() {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir::tempPath() + QStringLiteral("/aipdf-accessibility-wizard"));
}

// 向导是三步：显示与阅读 → 朗读 → 摘要。
void AccessibilityWizardTests::wizardHasThreeSteps() {
    QScopedPointer<AccessibilityWizard> wizard(makeWizard());
    auto* pages = wizard->findChild<QStackedWidget*>();
    QVERIFY(pages != nullptr);
    QCOMPARE(pages->count(), 3);
}

// 控件上选了什么，计划里就是什么。
void AccessibilityWizardTests::planKeepsChosenValues() {
    QScopedPointer<AccessibilityWizard> wizard(makeWizard());
    auto* fontSize = wizard->findChild<QSpinBox*>(QStringLiteral("accessibility.wizard.fontSize"));
    auto* scheme = wizard->findChild<QComboBox*>(QStringLiteral("accessibility.wizard.scheme"));
    auto* navigation =
        wizard->findChild<QCheckBox*>(QStringLiteral("accessibility.wizard.navigation"));
    auto* voice = wizard->findChild<QComboBox*>(QStringLiteral("accessibility.wizard.voice"));
    auto* rate = wizard->findChild<QSlider*>(QStringLiteral("accessibility.wizard.rate"));
    auto* volume = wizard->findChild<QSlider*>(QStringLiteral("accessibility.wizard.volume"));
    QVERIFY(fontSize != nullptr);
    QVERIFY(scheme != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(voice != nullptr);
    QVERIFY(rate != nullptr);
    QVERIFY(volume != nullptr);

    fontSize->setValue(16);
    scheme->setCurrentIndex(scheme->findData(static_cast<int>(aipdf::app::theme::ReadingScheme::Night)));
    navigation->setChecked(false);
    voice->setCurrentIndex(voice->findData(QStringLiteral("Test Voice Two")));
    rate->setValue(-4);
    volume->setValue(35);

    const AccessibilityPlan plan = wizard->plan();
    QCOMPARE(plan.interfaceFontSize, 16);
    QCOMPARE(plan.readingScheme, aipdf::app::theme::ReadingScheme::Night);
    QCOMPARE(plan.navigationVisible, false);
    QCOMPARE(plan.speech.voice, QStringLiteral("Test Voice Two"));
    QCOMPARE(plan.speech.rate, -4);
    QCOMPARE(plan.speech.volume, 35);
}

// 什么都不动时，向导要给出传进去的那份朗读设置。
void AccessibilityWizardTests::planKeepsGivenSpeechSettingsByDefault() {
    QScopedPointer<AccessibilityWizard> wizard(makeWizard());
    const AccessibilityPlan plan = wizard->plan();
    QCOMPARE(plan.speech.voice, QStringLiteral("Test Voice One"));
    QCOMPARE(plan.speech.rate, 2);
    QCOMPARE(plan.speech.volume, 80);
}

// 字号越界要夹住：太小看不清，太大装不下。
void AccessibilityWizardTests::planClampsOutOfRangeFontSize() {
    QCOMPARE(clampAccessibilityFontSize(1), aipdf::app::kAccessibilityFontSizeMin);
    QCOMPARE(clampAccessibilityFontSize(100), aipdf::app::kAccessibilityFontSizeMax);
    QCOMPARE(clampAccessibilityFontSize(12), 12);

    AccessibilityPlan plan;
    plan.interfaceFontSize = 999;
    QCOMPARE(normalizeAccessibilityPlan(plan).interfaceFontSize,
             aipdf::app::kAccessibilityFontSizeMax);
    plan.interfaceFontSize = -5;
    QCOMPARE(normalizeAccessibilityPlan(plan).interfaceFontSize,
             aipdf::app::kAccessibilityFontSizeMin);
}

// 朗读参数也要归一：语速 [-10, 10]，音量 [0, 100]。
void AccessibilityWizardTests::planNormalizesSpeechSettings() {
    AccessibilityPlan plan;
    plan.speech.rate = 99;
    plan.speech.volume = -20;
    const AccessibilityPlan normalized = normalizeAccessibilityPlan(plan);
    QCOMPARE(normalized.speech.rate, 10);
    QCOMPARE(normalized.speech.volume, 0);
}

// 「改了哪些」要能看出每一项：字号、配色、导航、朗读各自算一处改动。
void AccessibilityWizardTests::differsDetectsEachField() {
    AccessibilityPlan base;
    base.interfaceFontSize = 12;
    base.readingScheme = aipdf::app::theme::ReadingScheme::Light;
    base.navigationVisible = true;
    base.speech.voice = QStringLiteral("A");
    base.speech.rate = 0;
    base.speech.volume = 100;

    QCOMPARE(accessibilityPlanDiffers(base, base), false);

    AccessibilityPlan bigger = base;
    bigger.interfaceFontSize = 14;
    QCOMPARE(accessibilityPlanDiffers(bigger, base), true);

    AccessibilityPlan night = base;
    night.readingScheme = aipdf::app::theme::ReadingScheme::Night;
    QCOMPARE(accessibilityPlanDiffers(night, base), true);

    AccessibilityPlan hidden = base;
    hidden.navigationVisible = false;
    QCOMPARE(accessibilityPlanDiffers(hidden, base), true);

    AccessibilityPlan voice = base;
    voice.speech.voice = QStringLiteral("B");
    QCOMPARE(accessibilityPlanDiffers(voice, base), true);

    AccessibilityPlan rate = base;
    rate.speech.rate = 3;
    QCOMPARE(accessibilityPlanDiffers(rate, base), true);

    AccessibilityPlan volume = base;
    volume.speech.volume = 50;
    QCOMPARE(accessibilityPlanDiffers(volume, base), true);
}

// 音色下拉里要是注入的那几个，外加一个「自动」。
void AccessibilityWizardTests::voiceListIsInjected() {
    QScopedPointer<AccessibilityWizard> wizard(makeWizard());
    auto* voice = wizard->findChild<QComboBox*>(QStringLiteral("accessibility.wizard.voice"));
    QVERIFY(voice != nullptr);
    QCOMPARE(voice->count(), 3);
    QCOMPARE(voice->itemData(0).toString(), QString());
    QCOMPARE(voice->itemData(1).toString(), QStringLiteral("Test Voice One"));
    QCOMPARE(voice->itemData(2).toString(), QStringLiteral("Test Voice Two"));
}

// 摘要页要把每一项都说一遍，用户才知道自己改了什么。
void AccessibilityWizardTests::summaryLinesCoverEverySetting() {
    QScopedPointer<AccessibilityWizard> wizard(makeWizard());
    const QStringList lines = wizard->summaryLines();
    QVERIFY(lines.size() >= 5);
    bool hasFont = false;
    bool hasNavigation = false;
    bool hasVoice = false;
    bool hasRate = false;
    bool hasVolume = false;
    for (const QString& line : lines) {
        hasFont = hasFont || line.contains(QStringLiteral("字号"));
        hasNavigation = hasNavigation || line.contains(QStringLiteral("导航"));
        hasVoice = hasVoice || line.contains(QStringLiteral("音色"));
        hasRate = hasRate || line.contains(QStringLiteral("语速"));
        hasVolume = hasVolume || line.contains(QStringLiteral("音量"));
    }
    QVERIFY(hasFont);
    QVERIFY(hasNavigation);
    QVERIFY(hasVoice);
    QVERIFY(hasRate);
    QVERIFY(hasVolume);
}

QTEST_MAIN(AccessibilityWizardTests)
#include "AccessibilityWizardTests.moc"
