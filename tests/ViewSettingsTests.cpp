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

namespace {

// 按 objectName 找控件。找不到时给一条明确的提示，
// 免得后面的解引用变成一个看不懂的访问违例。
//
// 注意 root 要显式传进来：findChild 是 QObject 的成员函数，
// 写在测试类里会搜**测试对象**的孩子（一个都没有），而不是对话框的。
template <typename T>
T* mustFind(QWidget* root, const char* objectName) {
    T* widget = root->findChild<T*>(QString::fromLatin1(objectName));
    if (widget == nullptr) {
        qWarning("ViewSettingsTests: 找不到控件 %s", objectName);
    }
    return widget;
}

}  // namespace

class ViewSettingsTests final : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

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

    auto* fitWidth = mustFind<QRadioButton>(&dialog, "viewSettings.fitWidth");
    auto* actualSize = mustFind<QRadioButton>(&dialog, "viewSettings.actualSize");
    auto* fitPage = mustFind<QRadioButton>(&dialog, "viewSettings.fitPage");
    auto* customZoom = mustFind<QRadioButton>(&dialog, "viewSettings.customZoom");
    auto* facingCover = mustFind<QRadioButton>(&dialog, "viewSettings.facingCover");
    auto* continuous = mustFind<QRadioButton>(&dialog, "viewSettings.continuous");
    auto* navigation = mustFind<QCheckBox>(&dialog, "viewSettings.navigation");
    auto* aiPanel = mustFind<QCheckBox>(&dialog, "viewSettings.aiPanel");
    auto* ruler = mustFind<QCheckBox>(&dialog, "viewSettings.ruler");
    auto* rotation270 = mustFind<QRadioButton>(&dialog, "viewSettings.rotation270");
    auto* spin = mustFind<QSpinBox>(&dialog, "viewSettings.customZoomSpin");
    QVERIFY(fitWidth != nullptr);
    QVERIFY(actualSize != nullptr);
    QVERIFY(fitPage != nullptr);
    QVERIFY(customZoom != nullptr);
    QVERIFY(facingCover != nullptr);
    QVERIFY(continuous != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(aiPanel != nullptr);
    QVERIFY(ruler != nullptr);
    QVERIFY(rotation270 != nullptr);
    QVERIFY(spin != nullptr);

    QVERIFY(fitWidth->isChecked());
    QVERIFY(!actualSize->isChecked());
    QVERIFY(!fitPage->isChecked());
    QVERIFY(!customZoom->isChecked());
    QVERIFY(facingCover->isChecked());
    QVERIFY(!continuous->isChecked());
    QVERIFY(!navigation->isChecked());
    QVERIFY(aiPanel->isChecked());
    QVERIFY(ruler->isChecked());
    QVERIFY(rotation270->isChecked());
    QCOMPARE(spin->value(), 175);
}

void ViewSettingsTests::settingsReflectUserChoices() {
    ViewSettingsDialog dialog(ViewSettings{});
    auto* customZoom = mustFind<QRadioButton>(&dialog, "viewSettings.customZoom");
    auto* spin = mustFind<QSpinBox>(&dialog, "viewSettings.customZoomSpin");
    auto* singlePage = mustFind<QRadioButton>(&dialog, "viewSettings.singlePage");
    auto* navigation = mustFind<QCheckBox>(&dialog, "viewSettings.navigation");
    auto* ruler = mustFind<QCheckBox>(&dialog, "viewSettings.ruler");
    auto* rotation90 = mustFind<QRadioButton>(&dialog, "viewSettings.rotation90");
    QVERIFY(customZoom != nullptr);
    QVERIFY(spin != nullptr);
    QVERIFY(singlePage != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(ruler != nullptr);
    QVERIFY(rotation90 != nullptr);

    customZoom->setChecked(true);
    spin->setValue(150);
    singlePage->setChecked(true);
    navigation->setChecked(false);
    ruler->setChecked(true);
    rotation90->setChecked(true);

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
    auto* spin = mustFind<QSpinBox>(&dialog, "viewSettings.customZoomSpin");
    auto* customZoom = mustFind<QRadioButton>(&dialog, "viewSettings.customZoom");
    auto* actualSize = mustFind<QRadioButton>(&dialog, "viewSettings.actualSize");
    QVERIFY(spin != nullptr);
    QVERIFY(customZoom != nullptr);
    QVERIFY(actualSize != nullptr);
    // 默认是「实际大小」，那个百分比没有意义，不能让用户去改。
    QVERIFY(!spin->isEnabled());

    customZoom->setChecked(true);
    QVERIFY(spin->isEnabled());

    // 换回别的模式，百分比框要跟着灰掉——不然界面上留着一个能改的数字，
    // 改完又不生效，是最容易让人以为程序坏了的地方。
    actualSize->setChecked(true);
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
    // 只比「两者相等」不够：两个函数一起改就又相等了。
    // 所以再钉住键的**格式**——版本前缀一变，老用户的配置就再也读不出来。
    QVERIFY(viewSettingsKey(path, QStringLiteral("zoom")).startsWith(
        QStringLiteral("view/zoom/v1/")));
    QVERIFY(viewSettingsKey(path, QStringLiteral("layout")).startsWith(
        QStringLiteral("view/layout/v1/")));
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
