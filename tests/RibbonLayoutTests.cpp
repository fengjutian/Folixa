#include "app/shell/RibbonBar.h"
#include "app/ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

using aipdf::app::RibbonBar;

namespace {

// 复刻主窗口的目标结构：QWidget + QVBoxLayout(Ribbon, tabs)。
// Ribbon 固定 124，文档标签占满剩余空间。
class Harness {
public:
    Harness() {
        window_ = new QWidget;
        window_->setObjectName(QStringLiteral("ribbonHarness"));
        auto* layout = new QVBoxLayout(window_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        ribbon_ = new RibbonBar(window_);
        ribbon_->setFixedHeight(124);
        layout->addWidget(ribbon_);

        // 故意塞很宽的内容：验证 Ribbon 再宽也不会把窗口撑大。
        const char* tabs[] = {"文件", "主页", "注释", "页面管理", "视图", "保护", "AI 助手"};
        for (const char* title : tabs) {
            const int index = ribbon_->addTab(QString::fromUtf8(title));
            QHBoxLayout* group = ribbon_->addGroup(index, QString::fromUtf8(title));
            for (int i = 0; i < 8; ++i) {
                ribbon_->addCommand(group,
                                    QStringLiteral("命令 %1").arg(i + 1),
                                    QIcon(),
                                    QStringLiteral("很宽的命令按钮 %1").arg(i + 1));
            }
        }
        // 最后再加一页远超窗口宽度的命令：用来验证「只有当前页放不下才出滚动条」，
        // 而不是整摞页共用一条、停在窄页上也常驻。
        const int wide = ribbon_->addTab(QStringLiteral("宽页"));
        QHBoxLayout* wideGroup = ribbon_->addGroup(wide, QStringLiteral("宽页"));
        for (int i = 0; i < 30; ++i) {
            ribbon_->addCommand(wideGroup, QStringLiteral("命令 %1").arg(i + 1), QIcon(),
                                QStringLiteral("超宽命令 %1").arg(i + 1));
        }

        tabs_ = new QTabWidget(window_);
        tabs_->addTab(new QWidget(tabs_), QStringLiteral("multipage.pdf"));
        tabs_->addTab(new QWidget(tabs_), QStringLiteral("ap_plain.pdf"));
        layout->addWidget(tabs_, 1);
    }

    ~Harness() { delete window_; }

    QWidget* window() const { return window_; }
    RibbonBar* ribbon() const { return ribbon_; }
    QTabWidget* tabs() const { return tabs_; }

private:
    QWidget* window_ = nullptr;
    RibbonBar* ribbon_ = nullptr;
    QTabWidget* tabs_ = nullptr;
};

QString snapshotDir() {
    const QString fromEnv = qEnvironmentVariable("AIPDF_UI_SNAPSHOT_DIR");
    if (!fromEnv.isEmpty()) {
        return fromEnv;
    }
    return QDir::current().filePath(QStringLiteral("ribbon-layout"));
}

}  // namespace

class RibbonLayoutTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void ribbonFitsAtStandardSize();
    void ribbonFitsAtMinimumSize();
    void tabOrderKeepsContentAttached();

private:
    void verifySize(const QSize& requested, const QString& name);
};

void RibbonLayoutTests::initTestCase() {
    QDir().mkpath(snapshotDir());
    qApp->setStyleSheet(aipdf::app::theme::lightStyleSheet());
}

void RibbonLayoutTests::verifySize(const QSize& requested, const QString& name) {
    Harness harness;
    QWidget* window = harness.window();
    window->resize(requested);
    window->show();
    QTest::qWait(250);

    // 1) 窗口保持请求尺寸：Ribbon 的宽内容不能把窗口撑大。
    QCOMPARE(window->width(), requested.width());
    // 高度可能被屏幕工作区夹小（测试机窗口最大高度不够 900），只校验不被撑大。
    QVERIFY(window->height() > 600);
    QVERIFY(window->height() <= requested.height());

    // 2) Ribbon 高度锁在 124，标签条必须可见。
    QCOMPARE(harness.ribbon()->height(), 124);
    auto* tabBar = harness.ribbon()->findChild<QTabBar*>(QStringLiteral("ribbonTabs"));
    QVERIFY(tabBar != nullptr);
    QVERIFY2(tabBar->height() >= 24, qPrintable(QStringLiteral("标签条高度 %1").arg(tabBar->height())));
    QCOMPARE(tabBar->count(), 8);
    QCOMPARE(tabBar->currentIndex(), 0);
    QVERIFY(tabBar->isVisible());

    // 3) 分组标题必须真的占到高度：被按钮挤成 0 高是最隐蔽的回归。
    const QList<QLabel*> groupTitles =
        harness.ribbon()->findChildren<QLabel*>(QStringLiteral("ribbonGroupTitle"));
    QVERIFY(!groupTitles.isEmpty());
    bool titleVisible = false;
    for (QLabel* title : groupTitles) {
        if (title->isVisible() && title->height() >= 10) {
            titleVisible = true;
            break;
        }
    }
    QVERIFY2(titleVisible, "Ribbon 分组标题没有被画出来");

    // 4) 停在窄标签页时不该有横向滚动条；切到超宽页才允许出现。
    auto activeScroll = [&harness]() -> QScrollArea* {
        for (QScrollArea* area : harness.ribbon()->findChildren<QScrollArea*>()) {
            if (area->isVisible()) {
                return area;
            }
        }
        return nullptr;
    };
    QScrollArea* narrowScroll = activeScroll();
    QVERIFY(narrowScroll != nullptr);
    QCOMPARE(narrowScroll->horizontalScrollBar()->maximum(), 0);
    harness.ribbon()->setCurrentTab(7);
    QTest::qWait(50);
    QScrollArea* wideScroll = activeScroll();
    QVERIFY(wideScroll != nullptr);
    QVERIFY2(wideScroll->horizontalScrollBar()->maximum() > 0,
             "超宽标签页没有出现横向滚动条");
    harness.ribbon()->setCurrentTab(0);
    QTest::qWait(50);

    // 3) 文档标签仍在 Ribbon 下方且可见。
    QVERIFY(harness.tabs()->isVisible());
    QVERIFY(harness.tabs()->height() > 100);

    // 5) 命令按钮必须真的画出来：96px 高度下被压成 0 高是最隐蔽的失败。
    const QList<QToolButton*> buttons = harness.ribbon()->findChildren<QToolButton*>();
    QVERIFY(!buttons.isEmpty());
    int visibleButtons = 0;
    for (QToolButton* button : buttons) {
        if (button->isVisible() && button->height() >= 30) {
            ++visibleButtons;
        }
    }
    QVERIFY2(visibleButtons > 0, "Ribbon 命令按钮没有被画出来");

    // 4) 截图留证，供人工比对 1440 与 1024 两档。
    const QString path = QDir(snapshotDir()).filePath(name + QStringLiteral(".png"));
    QVERIFY2(window->grab().save(path), qPrintable(path));
}

void RibbonLayoutTests::ribbonFitsAtStandardSize() {
    verifySize(QSize(1440, 900), QStringLiteral("ribbon-1440x900"));
}

void RibbonLayoutTests::ribbonFitsAtMinimumSize() {
    verifySize(QSize(1024, 680), QStringLiteral("ribbon-1024x680"));
}

void RibbonLayoutTests::tabOrderKeepsContentAttached() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* ribbon = new RibbonBar(&window);
    ribbon->setFixedHeight(124);
    layout->addWidget(ribbon);

    const QStringList original = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    for (const QString& title : original) {
        const int tab = ribbon->addTab(title);
        QHBoxLayout* group = ribbon->addGroup(tab, title);
        ribbon->addCommand(group, title, QIcon(), title, true, QStringLiteral("command") + title);
    }
    ribbon->setTabOrder({QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")});

    window.resize(800, 180);
    window.show();
    QTest::qWait(50);
    auto* tabBar = ribbon->findChild<QTabBar*>(QStringLiteral("ribbonTabs"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->tabText(0), QStringLiteral("C"));
    QCOMPARE(tabBar->tabText(1), QStringLiteral("A"));
    QCOMPARE(tabBar->tabText(2), QStringLiteral("B"));

    for (int index = 0; index < 3; ++index) {
        ribbon->setCurrentTab(index);
        QTest::qWait(10);
        const QString expected = QStringLiteral("command") + tabBar->tabText(index);
        auto* button = ribbon->findChild<QToolButton*>(expected);
        QVERIFY2(button != nullptr, qPrintable(expected));
        QVERIFY2(button->isVisible(), qPrintable(expected + QStringLiteral(" is detached")));
        QVERIFY(button->parentWidget() != nullptr);
        QVERIFY(button->parentWidget()->parentWidget() != nullptr);
    }
}

QTEST_MAIN(RibbonLayoutTests)
#include "RibbonLayoutTests.moc"
