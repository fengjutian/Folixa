// 视图设置面板（对话框 + 按文件记忆）的单元测试。
//
// 这一层最容易出的三个毛病：
//   1) 对话框初值没接上——打开面板显示的还是默认值，不是用户当前的样子；
//   2) 读回的值和界面上的选择对不上——面板显示改了，存下去的却是别的；
//   3) 记忆串了——A 文件的页面布局跑到 B 文件上。
//
// 缩放记忆的老键不能变：viewSettingsKey(path, "zoom") 必须与旧的
// zoomSettingsKey(path) 完全一致，否则升级后所有文件都回到 100%。

#include "app/dialogs/ViewSettingsDialog.h"
#include "app/editor/ViewMemory.h"
#include "app/editor/ZoomMemory.h"

#include <QCheckBox>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::ViewSettings;
using aipdf::app::ViewSettingsDialog;
using aipdf::app::viewSettingsKey;
using aipdf::app::zoomSettingsKey;

class ViewSettingsTests final : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

    template <typename T>
    T* find(const char* objectName) {
        return findChild<T*>(QString::fromLatin1(objectName));
    }

private slots:
    void init();
    void dialogStartsWithCurrentValues();
    void settingsReflectUserChoices();
    void customZoomEnabledOnlyWhenCustomSelected();
    void layoutMemoryRoundTripsPerFile();
    void layoutMemoryKeepsFilesApart();
    void zoomKeyUnchangedSoOldSettingsStillApply();
    void invalidStoredLayoutCountsAsUnset();
};

void ViewSettingsTests::init() {
    // 每个用例用独立设置文件，避免互相污染（也避免写进开发者真实的注册表）。
    QVERIFY(dir_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir_.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ViewSettingsTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ViewSettingsTests"));
    QSettings().clear();
}

void ViewSettingsTests::dialogStartsWithCurrentValues() {
    ViewSettings current;
    current.zoomMode = ViewSettings::ZoomMode::FitWidth;
    current.customZoom = 1.75;
    current.layoutMode = aipdf::app::PageLayout::LayoutMode::FacingCover;
    current.navigationVisible = false;
    current.aiVisible = true;
    current.rulerVisible = true;
    current.rotation = 270;

    ViewSettingsDialog dialog(current);
    QVERIFY(find<QRadioButton>("viewSettings.fitWidth")->isChecked());
    QVERIFY(!find<QRadioButton>("viewSettings.actualSize")->isChecked());
    QVERIFY(!find<QRadioButton>("viewSettings.fitPage")->isChecked());
    QVERIFY(!find<QRadioButton>("viewSettings.customZoom")->isChecked());
    QVERIFY(find<QRadioButton>("viewSettings.facingCover")->isChecked());
    QVERIFY(!find<QRadioButton>("viewSettings.continuous")->isChecked());
    QVERIFY(!find<QCheckBox>("viewSettings.navigation")->isChecked());
    QVERIFY(find<QCheckBox>("viewSettings.aiPanel")->isChecked());
    QVERIFY(find<QCheckBox>("viewSettings.ruler")->isChecked());
    QVERIFY(find<QRadioButton>("viewSettings.rotation270")->isChecked());
    QCOMPARE(find<QSpinBox>("viewSettings.customZoomSpin")->value(), 175);
}

void ViewSettingsTests::settingsReflectUserChoices() {
    ViewSettingsDialog dialog(ViewSettings{});
    find<QRadioButton>("viewSettings.customZoom")->setChecked(true);
    find<QSpinBox>("viewSettings.customZoomSpin")->setValue(150);
    find<QRadioButton>("viewSettings.singlePage")->setChecked(true);
    find<QCheckBox>("viewSettings.navigation")->setChecked(false);
    find<QCheckBox>("viewSettings.ruler")->setChecked(true);
    find<QRadioButton>("viewSettings.rotation90")->setChecked(true);

    const ViewSettings result = dialog.settings();
    QCOMPARE(result.zoomMode, ViewSettings::ZoomMode::Custom);
    QCOMPARE(result.customZoom, 1.5);
    QCOMPARE(result.layoutMode, aipdf::app::PageLayout::LayoutMode::SinglePage);
    QVERIFY(!result.navigationVisible);
    QVERIFY(result.rulerVisible);
    QVERIFY(!result.aiVisible);
    QCOMPARE(result.rotation, 90);
}

void ViewSettingsTests::customZoomEnabledOnlyWhenCustomSelected() {
    ViewSettingsDialog dialog(ViewSettings{});
    auto* spin = find<QSpinBox>("viewSettings.customZoomSpin");
    QVERIFY(spin != nullptr);
    // 默认是「实际大小」，那个百分比没有意义，不能让用户去改。
    QVERIFY(!spin->isEnabled());

    find<QRadioButton>("viewSettings.customZoom")->setChecked(true);
    QVERIFY(spin->isEnabled());

    // 换回别的模式，百分比框要跟着灰掉——不然界面上留着一个能改的数字，
    // 改完又不生效，是最容易让人以为程序坏了的地方。
    find<QRadioButton>("viewSettings.actualSize")->setChecked(true);
    QVERIFY(!spin->isEnabled());
}

void ViewSettingsTests::layoutMemoryRoundTripsPerFile() {
    const QString path = dir_.filePath(QStringLiteral("a.pdf"));
    QVERIFY(!aipdf::app::rememberedLayoutForFile(path).has_value());
    aipdf::app::rememberLayoutForFile(path, aipdf::app::PageLayout::LayoutMode::SinglePage);
    const auto remembered = aipdf::app::rememberedLayoutForFile(path);
    QVERIFY(remembered.has_value());
    QCOMPARE(*remembered, aipdf::app::PageLayout::LayoutMode::SinglePage);
}

void ViewSettingsTests::layoutMemoryKeepsFilesApart() {
    const QString one = dir_.filePath(QStringLiteral("one.pdf"));
    const QString two = dir_.filePath(QStringLiteral("two.pdf"));
    aipdf::app::rememberLayoutForFile(one, aipdf::app::PageLayout::LayoutMode::Facing);
    aipdf::app::rememberLayoutForFile(two, aipdf::app::PageLayout::LayoutMode::Continuous);

    QCOMPARE(*aipdf::app::rememberedLayoutForFile(one),
             aipdf::app::PageLayout::LayoutMode::Facing);
    QCOMPARE(*aipdf::app::rememberedLayoutForFile(two),
             aipdf::app::PageLayout::LayoutMode::Continuous);
}

void ViewSettingsTests::zoomKeyUnchangedSoOldSettingsStillApply() {
    // 缩放记忆的键算法搬到了 ViewMemory，但键**不能变**：
    // 变了的话所有文件在升级后都会回到 100%（用户看不出原因，只觉得"记忆丢了"）。
    const QString path = dir_.filePath(QStringLiteral("legacy.pdf"));
    QCOMPARE(viewSettingsKey(path, QStringLiteral("zoom")), zoomSettingsKey(path));
}

void ViewSettingsTests::invalidStoredLayoutCountsAsUnset() {
    const QString path = dir_.filePath(QStringLiteral("broken.pdf"));
    // 设置文件被改坏：塞一个不存在的布局值。
    QSettings settings;
    settings.setValue(viewSettingsKey(path, QStringLiteral("layout")), 99);
    settings.sync();
    // 认不出来的值按「没记过」处理，不去猜——猜错了会让文档打开就排布错乱。
    QVERIFY(!aipdf::app::rememberedLayoutForFile(path).has_value());
}

QTEST_MAIN(ViewSettingsTests)
#include "ViewSettingsTests.moc"
