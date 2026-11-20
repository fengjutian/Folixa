// 帮助中心（站内帮助）的控件测试。
//
// 盯的不是「窗口能打开」，而是「它真的是站内帮助」：
//   1) 章节列表非空、章节数不少于 5（几件事说清楚才叫帮助中心）；
//   2) 每一章都有正文，不是占位文字；
//   3) **切换章节，正文会跟着变**——这是「站内帮助」与「一页静态说明」的分界；
//   4) 标题、章节列表、正文三个控件都拿 objectName 找得到
//      （界面文案会随语言变，名字不会）。
//
// 为什么用 WA_DontShowOnScreen：控件照样参与布局计算（几何是真的），
// 但不会在桌面上弹窗抢前台——本地跑测试不该干扰用户正在做的事。

#include "app/dialogs/HelpCenterDialog.h"

#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTest>
#include <QTextBrowser>

#include <memory>

using aipdf::app::HelpCenterDialog;

namespace {

// 造一个**不弹窗**的对话框。
//
// WA_DontShowOnScreen 必须在第一次显示之前设上；这里从头到尾都不 show()，
// 设它只是明确表态：这个测试不许在桌面上抢前台。
std::unique_ptr<HelpCenterDialog> makeDialog() {
    auto* dialog = new HelpCenterDialog;
    dialog->setAttribute(Qt::WA_DontShowOnScreen, true);
    return std::unique_ptr<HelpCenterDialog>(dialog);
}

}  // namespace

class HelpCenterDialogTests final : public QObject {
    Q_OBJECT

private slots:
    void exposesTheThreeNamedWidgets();
    void listsAtLeastFiveChapters();
    void firstChapterHasContent();
    void switchingChapterChangesContent();
    void everyChapterHasContent();
    void hasNonEmptyWindowTitle();
};

void HelpCenterDialogTests::exposesTheThreeNamedWidgets() {
    auto dialog = makeDialog();

    // findChild 显式把**被测对象**当根：不在测试类里另写一个同名包装函数。
    auto* chapterList = dialog->findChild<QListWidget*>(QStringLiteral("helpCenter.chapters"));
    QVERIFY(chapterList != nullptr);
    auto* content = dialog->findChild<QTextBrowser*>(QStringLiteral("helpCenter.content"));
    QVERIFY(content != nullptr);

    QCOMPARE(dialog->objectName(), QStringLiteral("helpCenterDialog"));
    // 两个控件都挂在对话框上，不是游离的。
    QCOMPARE(chapterList->parentWidget(), static_cast<QWidget*>(dialog.get()));
    QCOMPARE(content->parentWidget(), static_cast<QWidget*>(dialog.get()));
}

void HelpCenterDialogTests::listsAtLeastFiveChapters() {
    auto dialog = makeDialog();
    auto* chapterList = dialog->findChild<QListWidget*>(QStringLiteral("helpCenter.chapters"));
    QVERIFY(chapterList != nullptr);

    QVERIFY(chapterList->count() > 0);
    QVERIFY2(chapterList->count() >= 5, "帮助中心至少要有 5 个章节。");

    // 每一行都要有名字：列表里一行空白等于没写。
    for (int row = 0; row < chapterList->count(); ++row) {
        const QListWidgetItem* item = chapterList->item(row);
        QVERIFY(item != nullptr);
        QVERIFY2(!item->text().trimmed().isEmpty(),
                 qPrintable(QStringLiteral("第 %1 章没有标题").arg(row + 1)));
    }

    // 对话框自己也要知道有几章（将来做「跳到某章」要用）。
    QCOMPARE(dialog->chapterCount(), chapterList->count());
}

void HelpCenterDialogTests::firstChapterHasContent() {
    auto dialog = makeDialog();
    auto* chapterList = dialog->findChild<QListWidget*>(QStringLiteral("helpCenter.chapters"));
    QVERIFY(chapterList != nullptr);
    auto* content = dialog->findChild<QTextBrowser*>(QStringLiteral("helpCenter.content"));
    QVERIFY(content != nullptr);

    // 一打开就该停在第一章，右侧有正文，而不是一片空白。
    QVERIFY(chapterList->currentRow() >= 0);
    QVERIFY2(!content->toPlainText().trimmed().isEmpty(), "初始章节的正文是空的。");
    QVERIFY(!dialog->chapterBodyHtml(chapterList->currentRow()).trimmed().isEmpty());
}

void HelpCenterDialogTests::switchingChapterChangesContent() {
    auto dialog = makeDialog();
    auto* chapterList = dialog->findChild<QListWidget*>(QStringLiteral("helpCenter.chapters"));
    QVERIFY(chapterList != nullptr);
    auto* content = dialog->findChild<QTextBrowser*>(QStringLiteral("helpCenter.content"));
    QVERIFY(content != nullptr);
    QVERIFY(chapterList->count() >= 2);

    const QString first = content->toHtml();
    QVERIFY(!first.isEmpty());

    // 核心不变量：**切换章节，正文必须跟着换**。
    // 换不了就说明它只是一页静态说明，不是站内帮助。
    chapterList->setCurrentRow(1);
    const QString second = content->toHtml();
    QVERIFY2(first != second, "切换章节后正文没变：这不是站内帮助，是一页静态文字。");

    // 切回第一章，正文也得跟着回去。
    chapterList->setCurrentRow(0);
    QCOMPARE(content->toHtml(), first);
}

void HelpCenterDialogTests::everyChapterHasContent() {
    auto dialog = makeDialog();
    auto* chapterList = dialog->findChild<QListWidget*>(QStringLiteral("helpCenter.chapters"));
    QVERIFY(chapterList != nullptr);
    auto* content = dialog->findChild<QTextBrowser*>(QStringLiteral("helpCenter.content"));
    QVERIFY(content != nullptr);
    QVERIFY(chapterList->count() >= 5);

    // 逐章翻一遍，每一章都得有正文。
    for (int row = 0; row < chapterList->count(); ++row) {
        chapterList->setCurrentRow(row);
        QVERIFY2(!content->toPlainText().trimmed().isEmpty(),
                 qPrintable(QStringLiteral("第 %1 章的正文是空的").arg(row + 1)));
        QVERIFY2(!dialog->chapterBodyHtml(row).trimmed().isEmpty(),
                 qPrintable(QStringLiteral("第 %1 章的正文 HTML 是空的").arg(row + 1)));
    }
}

void HelpCenterDialogTests::hasNonEmptyWindowTitle() {
    auto dialog = makeDialog();
    QVERIFY2(!dialog->windowTitle().trimmed().isEmpty(), "窗口标题不能是空的。");
}

QTEST_MAIN(HelpCenterDialogTests)
#include "HelpCenterDialogTests.moc"
