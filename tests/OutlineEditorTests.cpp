// 大纲树编辑（书签排序与层级）的单元测试。
//
// 这些操作原来埋在 app/MainWindow.cpp 的文件内静态 helper 里，
// 搬进 pdf/OutlineEditor 之后才能被单独测。
//
// 判据不是"函数没崩"，而是**树真的变成了该有的形状**：
// 顺序对不对、层级对不对、该拒绝的边界有没有拒绝。
//
// 最后一例是落盘往返：改出来的层级必须真的能写进 PDF 并读回来，
// 否则界面上的"降级/升级"就只是个幻觉。

#include "pdf/OutlineEditor.h"
#include "pdf/OutlineWriter.h"
#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QStringList>
#include <QTest>

using aipdf::pdf::flattenOutline;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::OutlineEditor;
using aipdf::pdf::OutlineItem;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::PdfOpenResult;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

OutlineItem makeItem(const QString& title, int pageIndex = 0) {
    OutlineItem item;
    item.title = title;
    item.pageIndex = pageIndex;
    return item;
}

// 三个平级的顶层节点：A、B、C。
std::vector<OutlineItem> flatTree() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.push_back(makeItem(QStringLiteral("B"), 1));
    tree.push_back(makeItem(QStringLiteral("C"), 2));
    return tree;
}

// 摊平后的标题序列：断言顺序时用它，比逐个断言下标更贴近界面看到的东西。
QStringList titles(const std::vector<OutlineItem>& tree) {
    QStringList result;
    for (const auto& entry : flattenOutline(tree)) {
        result.append(entry.title);
    }
    return result;
}

}  // namespace

class OutlineEditorTests : public QObject {
    Q_OBJECT

private slots:
    void moveUpSwapsWithPreviousSibling();
    void moveDownSwapsWithNextSibling();
    void moveUpOnFirstItemIsRejected();
    void moveDownOnLastItemIsRejected();
    void indentMovesItemUnderPreviousSibling();
    void indentOnFirstTopLevelItemIsRejected();
    void indentKeepsSubtreeOfMovedItem();
    void outdentMakesItemSiblingAfterParent();
    void outdentWithoutParentIsRejected();
    void moveUpInsideSubtreeKeepsTopLevelOrder();
    void levelAtReportsDepth();
    void pathAtReportsIndexPath();
    void removeAtDeletesNodeAndKeepsOrder();
    void itemAtFindsFlattenedNode();
    void editedHierarchySurvivesPdfRoundTrip();
};

// ------------------------------------------------------------------ 换位 ----

void OutlineEditorTests::moveUpSwapsWithPreviousSibling() {
    auto tree = flatTree();
    QVERIFY(OutlineEditor::moveUp(&tree, 1));
    QCOMPARE(titles(tree), QStringList({QStringLiteral("B"), QStringLiteral("A"),
                                        QStringLiteral("C")}));
}

void OutlineEditorTests::moveDownSwapsWithNextSibling() {
    auto tree = flatTree();
    QVERIFY(OutlineEditor::moveDown(&tree, 0));
    QCOMPARE(titles(tree), QStringList({QStringLiteral("B"), QStringLiteral("A"),
                                        QStringLiteral("C")}));
}

void OutlineEditorTests::moveUpOnFirstItemIsRejected() {
    auto tree = flatTree();
    QVERIFY(!OutlineEditor::moveUp(&tree, 0));
    // 拒绝之后树必须原样不动。
    QCOMPARE(titles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("C")}));
}

void OutlineEditorTests::moveDownOnLastItemIsRejected() {
    auto tree = flatTree();
    QVERIFY(!OutlineEditor::moveDown(&tree, 2));
    QCOMPARE(titles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("C")}));
}

// ------------------------------------------------------------------ 层级 ----

void OutlineEditorTests::indentMovesItemUnderPreviousSibling() {
    auto tree = flatTree();
    QVERIFY(OutlineEditor::indent(&tree, 1));
    // B 变成了 A 的子节点，顶层只剩 A 和 C。
    QCOMPARE(tree.size(), size_t(2));
    QCOMPARE(tree.at(0).title, QStringLiteral("A"));
    QCOMPARE(tree.at(0).children.size(), size_t(1));
    QCOMPARE(tree.at(0).children.at(0).title, QStringLiteral("B"));
    QCOMPARE(tree.at(1).title, QStringLiteral("C"));
    // 摊平顺序没变：B 仍然紧跟在 A 后面。
    QCOMPARE(titles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("C")}));
}

void OutlineEditorTests::indentOnFirstTopLevelItemIsRejected() {
    auto tree = flatTree();
    QVERIFY(!OutlineEditor::indent(&tree, 0));
    QCOMPARE(tree.size(), size_t(3));
    QVERIFY(tree.at(0).children.empty());
}

void OutlineEditorTests::indentKeepsSubtreeOfMovedItem() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.push_back(makeItem(QStringLiteral("B"), 1));
    tree.push_back(makeItem(QStringLiteral("C"), 2));
    tree.at(1).children.push_back(makeItem(QStringLiteral("D"), 3));

    QVERIFY(OutlineEditor::indent(&tree, 1));
    // 降级搬的是整条书签：B 下面那个 D 得跟着一起走。
    QCOMPARE(tree.at(0).children.size(), size_t(1));
    QCOMPARE(tree.at(0).children.at(0).title, QStringLiteral("B"));
    QCOMPARE(tree.at(0).children.at(0).children.size(), size_t(1));
    QCOMPARE(tree.at(0).children.at(0).children.at(0).title, QStringLiteral("D"));
    QCOMPARE(titles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("D"), QStringLiteral("C")}));
}

void OutlineEditorTests::outdentMakesItemSiblingAfterParent() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.push_back(makeItem(QStringLiteral("C"), 2));
    tree.at(0).children.push_back(makeItem(QStringLiteral("B"), 1));
    // 摊平：A(0) B(1) C(2)。

    QVERIFY(OutlineEditor::outdent(&tree, 1));
    // B 升到顶层，排在父节点 A 的后面。
    QCOMPARE(tree.size(), size_t(3));
    QCOMPARE(tree.at(0).title, QStringLiteral("A"));
    QCOMPARE(tree.at(1).title, QStringLiteral("B"));
    QCOMPARE(tree.at(2).title, QStringLiteral("C"));
    QVERIFY(tree.at(0).children.empty());
}

void OutlineEditorTests::outdentWithoutParentIsRejected() {
    auto tree = flatTree();
    QVERIFY(!OutlineEditor::outdent(&tree, 1));
    QCOMPARE(tree.size(), size_t(3));
    QCOMPARE(titles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("C")}));
}

void OutlineEditorTests::moveUpInsideSubtreeKeepsTopLevelOrder() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.push_back(makeItem(QStringLiteral("B"), 1));
    tree.push_back(makeItem(QStringLiteral("C"), 2));
    tree.at(1).children.push_back(makeItem(QStringLiteral("D"), 3));
    tree.at(1).children.push_back(makeItem(QStringLiteral("E"), 4));
    // 摊平：A(0) B(1) D(2) E(3) C(4)。

    QVERIFY(OutlineEditor::moveUp(&tree, 3));
    // E 换到了 D 前面，但只在 B 的子树内部换位，顶层顺序不能动。
    QCOMPARE(tree.at(1).children.at(0).title, QStringLiteral("E"));
    QCOMPARE(tree.at(1).children.at(1).title, QStringLiteral("D"));
    QCOMPARE(titles(tree), QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("E"), QStringLiteral("D"),
                                        QStringLiteral("C")}));
    // E 现在是 B 的第一个子节点，上面没有兄弟可换。
    QVERIFY(!OutlineEditor::moveUp(&tree, 2));
}

// ------------------------------------------------------------ 路径与层级 ----

void OutlineEditorTests::levelAtReportsDepth() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.at(0).children.push_back(makeItem(QStringLiteral("B"), 1));
    tree.at(0).children.at(0).children.push_back(makeItem(QStringLiteral("C"), 2));
    // 摊平：A(0) B(1) C(2)。

    QCOMPARE(OutlineEditor::levelAt(tree, 0), 0);
    QCOMPARE(OutlineEditor::levelAt(tree, 1), 1);
    QCOMPARE(OutlineEditor::levelAt(tree, 2), 2);
    // 越界返回 -1，而不是 0（0 是"顶层"，混起来会当成顶层节点）。
    QCOMPARE(OutlineEditor::levelAt(tree, 3), -1);
}

void OutlineEditorTests::pathAtReportsIndexPath() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.push_back(makeItem(QStringLiteral("C"), 2));
    tree.at(0).children.push_back(makeItem(QStringLiteral("B"), 1));
    tree.at(1).children.push_back(makeItem(QStringLiteral("D"), 3));
    // 摊平：A(0) B(1) C(2) D(3)。

    std::vector<int> path;
    QVERIFY(OutlineEditor::pathAt(tree, 1, &path));
    QCOMPARE(path, (std::vector<int>{0, 0}));
    QVERIFY(OutlineEditor::pathAt(tree, 3, &path));
    QCOMPARE(path, (std::vector<int>{1, 0}));
    QVERIFY(!OutlineEditor::pathAt(tree, 9, &path));
}

// ---------------------------------------------------------------- 定位 ----

void OutlineEditorTests::removeAtDeletesNodeAndKeepsOrder() {
    auto tree = flatTree();
    QVERIFY(OutlineEditor::removeAt(&tree, 1));
    QCOMPARE(tree.size(), size_t(2));
    QCOMPARE(tree.at(0).title, QStringLiteral("A"));
    QCOMPARE(tree.at(1).title, QStringLiteral("C"));
    // 越界删除必须失败，而且不能顺手删掉别的。
    QVERIFY(!OutlineEditor::removeAt(&tree, 5));
    QCOMPARE(tree.size(), size_t(2));
}

void OutlineEditorTests::itemAtFindsFlattenedNode() {
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("A"), 0));
    tree.push_back(makeItem(QStringLiteral("C"), 2));
    tree.at(0).children.push_back(makeItem(QStringLiteral("B"), 1));

    OutlineItem* item = OutlineEditor::itemAt(&tree, 1);
    QVERIFY(item != nullptr);
    QCOMPARE(item->title, QStringLiteral("B"));
    QVERIFY(OutlineEditor::itemAt(&tree, 9) == nullptr);
}

// ------------------------------------------------------------ 落盘往返 ----

void OutlineEditorTests::editedHierarchySurvivesPdfRoundTrip() {
    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(opened.document.isValid());
    const PdfDocument document = opened.document;

    // 用编辑器改出一棵有层级的树：
    // A（带子节点 B）、C；然后把 B 降级到 A 下面，B 自己还带着子节点 D。
    std::vector<OutlineItem> tree;
    tree.push_back(makeItem(QStringLiteral("Chapter A"), 0));
    tree.push_back(makeItem(QStringLiteral("Chapter B"), 0));
    tree.push_back(makeItem(QStringLiteral("Chapter C"), 0));
    tree.at(1).children.push_back(makeItem(QStringLiteral("Section B.1"), 0));
    QVERIFY(OutlineEditor::indent(&tree, 1));

    // rewriteOutline 要经典交叉引用表的 PDF：这里用 PdfDocument 重新序列化一份。
    const QByteArray base = document.serialize();
    QVERIFY(!base.isEmpty());
    const auto written = aipdf::pdf::rewriteOutline(base, tree);
    QVERIFY2(written.ok, qPrintable(written.error));

    // 读回来验证：标题、顺序、父子层级都要对。
    const PdfOpenResult reopened = openDocumentFromBytes(written.bytes);
    QVERIFY(reopened.document.isValid());
    const std::vector<OutlineItem> parsed = reopened.document.outline();
    QCOMPARE(parsed.size(), size_t(2));
    QCOMPARE(parsed.at(0).title, QStringLiteral("Chapter A"));
    QCOMPARE(parsed.at(1).title, QStringLiteral("Chapter C"));
    QCOMPARE(parsed.at(0).children.size(), size_t(1));
    QCOMPARE(parsed.at(0).children.at(0).title, QStringLiteral("Chapter B"));
    QCOMPARE(parsed.at(0).children.at(0).children.size(), size_t(1));
    QCOMPARE(parsed.at(0).children.at(0).children.at(0).title,
             QStringLiteral("Section B.1"));
    QVERIFY(parsed.at(1).children.empty());
}

QTEST_MAIN(OutlineEditorTests)
#include "OutlineEditorTests.moc"
