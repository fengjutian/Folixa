// 窗口级 i18n 用例：真正建一个控件，切换语言，看它的文字有没有就地变。
//
// 底座用例（I18nTests）只碰 QtCore；这里要证明「texts_.bind 登记 + languageChanged
// 信号」这条链路在真实控件上确实生效——不重建窗口、不重启程序。

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "app/ui/Language.h"
#include "app/shell/WelcomePage.h"
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
    void welcomePageRecentListRefreshes();

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

// 工作台的「最近打开」不能只在启动时铺一次：打开一份文档后叫 setRecentFiles，
// 列表、计数与空状态都要就地换掉（行控件可以重建，但不能留下旧行或旧数字）。
void I18nWidgetTests::welcomePageRecentListRefreshes() {
    const QString first = QStringLiteral("C:/docs/first.pdf");
    const QString second = QStringLiteral("C:/docs/second.pdf");
    const QString third = QStringLiteral("C:/docs/third.pdf");

    WelcomePage page(QStringList{first, second});
    auto* count = page.findChild<QLabel*>(QStringLiteral("welcomeSectionMeta"));
    QVERIFY2(count != nullptr, "最近文档的计数标签没建出来");
    QCOMPARE(count->text(), QStringLiteral("2 份文档"));
    QCOMPARE(page.findChildren<QPushButton*>(QStringLiteral("recentRow")).size(), 2);

    // 新打开一份：插到最前面，行数与计数跟着变，旧行不能残留。
    page.setRecentFiles(QStringList{third, first, second});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(count->text(), QStringLiteral("3 份文档"));
    const auto rows = page.findChildren<QPushButton*>(QStringLiteral("recentRow"));
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.first()->toolTip(), third);
    QCOMPARE(rows.last()->toolTip(), second);

    // 传同样的一份不该重建：行指针保持不变，避免每打开一次就闪一下。
    QPushButton* head = rows.first();
    page.setRecentFiles(QStringList{third, first, second});
    QCOMPARE(page.findChildren<QPushButton*>(QStringLiteral("recentRow")).first(), head);

    // 全部消失时回到空状态，计数收起而不是显示「0 份文档」。
    page.setRecentFiles(QStringList{});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(page.findChildren<QPushButton*>(QStringLiteral("recentRow")).isEmpty());
    QVERIFY(count->isHidden());
    auto* emptyTitle = page.findChild<QLabel*>(QStringLiteral("recentEmptyTitle"));
    QVERIFY(emptyTitle != nullptr);
    QCOMPARE(emptyTitle->text(), QStringLiteral("暂无最近文档"));
}

QTEST_MAIN(I18nWidgetTests)

#include "I18nWidgetTests.moc"
