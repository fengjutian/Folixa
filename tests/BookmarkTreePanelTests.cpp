// 「编辑书签层级」对话框的控件测试。
//
// 判据不是「窗口能打开」，而是三件事：
//   1. 层级真的显示成了一棵树（父子关系看得见）；
//   2. 拖拽改完层级之后，**模型和树控件两边一致**；
//   3. 落点位置算出来的语义是对的（中间=变子节点、上缘=排在前面、下缘=排在后面）。
//
// 为什么用 WA_DontShowOnScreen：控件照样参与布局计算（几何是真的），
// 但不会在桌面上弹窗抢前台。

#include "app/dialogs/BookmarkTreeDialog.h"

#include <QApplication>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTest>

using aipdf::app::BookmarkDropZone;
using aipdf::app::BookmarkTreeDialog;
using aipdf::pdf::OutlineItem;

namespace {

OutlineItem makeItem(const QString& title, int pageIndex = 0) {
    OutlineItem item;
    item.title = title;
    item.pageIndex = pageIndex;
    return item;
}

// A（带一个子节点 A.1）、B、C —— 三层够用，够看出拖拽改的是哪一层。
std::vector<OutlineItem> sampleTree() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.at(0).children.push_back(makeItem(QStringLiteral("A.1"), 1));
    tree.push_back(makeItem(QStringLiteral("B"), 2));
    tree.push_back(makeItem(QStringLiteral("C"), 3));
    return tree;
}

// 树控件里的标题（按摊平顺序），用来断言"界面上看到的层级"。
QStringList treeTitles(const QTreeWidget* tree) {
    QStringList titles;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        titles.append(item->text(0));
        for (int i = 0; i < item->childCount(); ++i) {
            walk(item->child(i));
        }
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        walk(tree->topLevelItem(i));
    }
    return titles;
}

// 顶层条目的标题（只看顶层，断言"谁还在顶层"）。
QStringList topLevelTitles(const QTreeWidget* tree) {
    QStringList titles;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        titles.append(tree->topLevelItem(i)->text(0));
    }
    return titles;
}

}  // namespace

class BookmarkTreePanelTests : public QObject {
    Q_OBJECT

private slots:
    void treeShowsHierarchy();
    void movingItemIntoAnotherParentUpdatesModelAndTree();
    void movingItemIntoOwnSubtreeIsRejected();
    void dropZoneSplitsItemRectIntoThreeParts();
    void dropOnItemBecomesItsChild();
    void dropAboveItemStaysOnSameLevel();
    void dropOnEmptyAreaGoesToTopLevel();
    void droppingOnItemMovesItUnderThatItem();
    void droppingAboveItemKeepsItOnSameLevel();
};

void BookmarkTreePanelTests::treeShowsHierarchy() {
    BookmarkTreeDialog dialog(sampleTree());
    QTreeWidget* tree = dialog.tree();
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 3);
    QCOMPARE(topLevelTitles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                                QStringLiteral("C")}));
    // A 的子节点必须真的挂在 A 下面，而不是平铺在顶层。
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("A.1"));
    QCOMPARE(treeTitles(tree), QStringList({QStringLiteral("A"), QStringLiteral("A.1"),
                                            QStringLiteral("B"), QStringLiteral("C")}));
}

void BookmarkTreePanelTests::movingItemIntoAnotherParentUpdatesModelAndTree() {
    BookmarkTreeDialog dialog(sampleTree());
    QTreeWidget* tree = dialog.tree();

    // 把 B 拖到 A 底下（源路径 {1}，新父路径 {0}）。
    QVERIFY(dialog.moveItem({1}, {0}, -1));
    // 模型：B 成了 A 的孩子，顶层因此少了一个。
    QCOMPARE(dialog.outline().size(), size_t(2));
    QCOMPARE(dialog.outline().at(0).title, QStringLiteral("A"));
    QCOMPARE(dialog.outline().at(0).children.size(), size_t(2));
    QCOMPARE(dialog.outline().at(0).children.at(0).title, QStringLiteral("A.1"));
    QCOMPARE(dialog.outline().at(0).children.at(1).title, QStringLiteral("B"));
    QCOMPARE(dialog.outline().at(1).title, QStringLiteral("C"));
    // 树控件：必须跟着模型一起变，不能停在旧样子。
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(topLevelTitles(tree), QStringList({QStringLiteral("A"), QStringLiteral("C")}));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->child(1)->text(0), QStringLiteral("B"));
}

void BookmarkTreePanelTests::movingItemIntoOwnSubtreeIsRejected() {
    BookmarkTreeDialog dialog(sampleTree());
    const QStringList before = treeTitles(dialog.tree());
    // 把 A 拖到它自己的子节点 A.1 底下：会成环，必须拒绝。
    QVERIFY(!dialog.moveItem({0}, {0, 0}, -1));
    QCOMPARE(treeTitles(dialog.tree()), before);
    QCOMPARE(dialog.outline().size(), size_t(3));
    QCOMPARE(dialog.outline().at(0).children.size(), size_t(1));
}

void BookmarkTreePanelTests::dropZoneSplitsItemRectIntoThreeParts() {
    const QRect rect(0, 100, 200, 30);
    // 上缘 → 排在它前面；中间 → 变成它的子节点；下缘 → 排在它后面。
    QCOMPARE(BookmarkTreeDialog::zoneFor(rect, QPoint(10, 102)), BookmarkDropZone::Before);
    QCOMPARE(BookmarkTreeDialog::zoneFor(rect, QPoint(10, 115)), BookmarkDropZone::On);
    QCOMPARE(BookmarkTreeDialog::zoneFor(rect, QPoint(10, 128)), BookmarkDropZone::After);
}

void BookmarkTreePanelTests::dropOnItemBecomesItsChild() {
    BookmarkTreeDialog dialog(sampleTree());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QCoreApplication::processEvents();

    QTreeWidget* tree = dialog.tree();
    QTreeWidgetItem* target = tree->topLevelItem(1);  // B
    const QRect rect = tree->visualItemRect(target);
    QVERIFY(rect.isValid());

    std::vector<int> parentPath;
    int insertIndex = -1;
    QVERIFY(dialog.dropTargetFor(rect.center(), &parentPath, &insertIndex));
    // 落在条目中间 = 变成它的子节点。
    QCOMPARE(parentPath, (std::vector<int>{1}));
    QCOMPARE(insertIndex, -1);
}

void BookmarkTreePanelTests::dropAboveItemStaysOnSameLevel() {
    BookmarkTreeDialog dialog(sampleTree());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QCoreApplication::processEvents();

    QTreeWidget* tree = dialog.tree();
    QTreeWidgetItem* target = tree->topLevelItem(2);  // C
    const QRect rect = tree->visualItemRect(target);
    QVERIFY(rect.isValid());

    std::vector<int> parentPath;
    int insertIndex = -1;
    // 落在上缘 = 排在它前面，仍在同一层（顶层）。
    QVERIFY(dialog.dropTargetFor(QPoint(rect.center().x(), rect.top() + 1),
                                 &parentPath, &insertIndex));
    QVERIFY(parentPath.empty());
    QCOMPARE(insertIndex, 2);
}

void BookmarkTreePanelTests::dropOnEmptyAreaGoesToTopLevel() {
    BookmarkTreeDialog dialog(sampleTree());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QCoreApplication::processEvents();

    QTreeWidget* tree = dialog.tree();
    std::vector<int> parentPath;
    int insertIndex = -1;
    // 条目以外的空白处 = 落到顶层末尾。
    const QPoint empty = QPoint(10, tree->viewport()->height() - 2);
    QVERIFY(tree->itemAt(empty) == nullptr);
    QVERIFY(dialog.dropTargetFor(empty, &parentPath, &insertIndex));
    QVERIFY(parentPath.empty());
    QCOMPARE(insertIndex, -1);
}

// 整条拖放路径（落点判定 + 搬动）走一遍：把 C 拖到 B 的条目中间。
void BookmarkTreePanelTests::droppingOnItemMovesItUnderThatItem() {
    BookmarkTreeDialog dialog(sampleTree());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QCoreApplication::processEvents();

    QTreeWidget* tree = dialog.tree();
    QTreeWidgetItem* target = tree->topLevelItem(1);  // B
    const QRect rect = tree->visualItemRect(target);
    QVERIFY(rect.isValid());

    QVERIFY(dialog.dropAt(rect.center(), {2}));
    // C 成了 B 的孩子。
    QCOMPARE(dialog.outline().size(), size_t(2));
    QCOMPARE(dialog.outline().at(1).title, QStringLiteral("B"));
    QCOMPARE(dialog.outline().at(1).children.size(), size_t(1));
    QCOMPARE(dialog.outline().at(1).children.at(0).title, QStringLiteral("C"));
    // 树控件也跟着变。
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(1)->childCount(), 1);
    QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("C"));
}

// 拖到条目上缘 = 排在它前面，层级不变。
void BookmarkTreePanelTests::droppingAboveItemKeepsItOnSameLevel() {
    BookmarkTreeDialog dialog(sampleTree());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QCoreApplication::processEvents();

    QTreeWidget* tree = dialog.tree();
    QTreeWidgetItem* target = tree->topLevelItem(1);  // B
    const QRect rect = tree->visualItemRect(target);
    QVERIFY(rect.isValid());

    // 把 C 拖到 B 的上缘 → C 排到 B 前面，仍在顶层。
    QVERIFY(dialog.dropAt(QPoint(rect.center().x(), rect.top() + 1), {2}));
    QCOMPARE(topLevelTitles(tree), QStringList({QStringLiteral("A"), QStringLiteral("C"),
                                                QStringLiteral("B")}));
    QCOMPARE(tree->topLevelItem(1)->childCount(), 0);
    QCOMPARE(dialog.outline().size(), size_t(3));
    QCOMPARE(dialog.outline().at(1).title, QStringLiteral("C"));
    QCOMPARE(dialog.outline().at(2).title, QStringLiteral("B"));
}

QTEST_MAIN(BookmarkTreePanelTests)
#include "BookmarkTreePanelTests.moc"