// 「清理隐藏信息」对话框的勾选行为测试。
//
// 盯的是这一轮新增的「全选 / 全不选」开关的三条不变量：
//   1) **默认勾选不变** —— 开关不能悄悄把保守默认换成激进默认；
//   2) 全选 = 勾上这份文档里**真实存在**的每一类（含签名、嵌入文件本体），
//      但 0 条目的那一类不勾（勾了也没得清，复验时会让人以为自己漏勾）；
//   3) 界面上的按钮真的接上了 —— 按钮不是死的。
//
// 为什么单独建一个目标：这个对话框在 app 层（依赖 Widgets），而 HiddenDataInventoryTests
// 是 guiless 的领域层测试，两者不能塞进同一个可执行文件。

#include "app/dialogs/HiddenDataDialog.h"

#include <QPushButton>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

using aipdf::app::HiddenDataDialog;

namespace {

// 造一份清单：每一类的 id 与条数。
//
// 用固定的 id 顺序而不是真实文档：这一层要验的是**勾选行为**，
// 领域层的「枚举准不准」由 HiddenDataInventoryTests 负责，两边不重叠。
QVector<aipdf::pdf::HiddenDataCategory> makeCategories()
{
    const struct Entry {
        const char* id;
        int count;
    } kEntries[] = {
        {"metadata", 3},
        {"metadata-streams", 1},
        {"incremental-history", 2},
        {"scripts", 1},
        {"actions", 1},
        {"annotation-actions", 4},
        {"thumbnails", 2},
        // 默认不勾的那几类（签名、表单已填值、嵌入文件本体……）
        {"signatures", 1},
        {"form-data", 5},
        {"embedded-files", 1},
        {"file-attachments", 2},
        // 这份文档里没有的东西
        {"xfa", 0},
        {"layers", 0},
    };
    QVector<aipdf::pdf::HiddenDataCategory> categories;
    for (const auto& entry : kEntries) {
        aipdf::pdf::HiddenDataCategory category;
        category.id = QString::fromLatin1(entry.id);
        category.itemCount = entry.count;
        category.detail = QStringLiteral("test");
        categories.append(category);
    }
    return categories;
}

// 按 id 找界面上的那一行。
//
// root 要显式传进来：findChild 是 QObject 的成员函数，
// 写成测试类的成员就会搜**测试对象**的孩子（一个都没有），返回 nullptr，
// 后面的解引用会变成看不懂的访问违例。
QTreeWidgetItem* findRow(QWidget* root, const QString& id)
{
    auto* tree = root->findChild<QTreeWidget*>(QStringLiteral("hiddenDataDialog.tree"));
    if (tree == nullptr) {
        return nullptr;
    }
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = tree->topLevelItem(row);
        if (item != nullptr && item->data(0, Qt::UserRole).toString() == id) {
            return item;
        }
    }
    return nullptr;
}

bool isChecked(QWidget* root, const QString& id)
{
    QTreeWidgetItem* item = findRow(root, id);
    return item != nullptr && item->checkState(0) == Qt::Checked;
}

}  // namespace

class HiddenDataDialogTests final : public QObject {
    Q_OBJECT

private slots:
    void defaultSelectionStaysConservative();
    void selectAllChecksEveryExistingCategory();
    void selectAllSkipsEmptyCategories();
    void clearAllUnchecksEverything();
    void selectAllDoesNotChangeTheDefaultList();
    void toolbarButtonsAreWiredToTheActions();
    void emptyCategoriesAreDisabled();
};

void HiddenDataDialogTests::defaultSelectionStaysConservative()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());

    // 默认勾的仍然是那份保守白名单。
    QCOMPARE(dialog.selectedCategoryIds(), HiddenDataDialog::defaultCleanupCategoryIds());

    // 签名与嵌入文件本体默认不勾 —— 它们有法律效力与数据价值。
    QVERIFY(!isChecked(&dialog, QStringLiteral("signatures")));
    QVERIFY(!isChecked(&dialog, QStringLiteral("embedded-files")));
    QVERIFY(!isChecked(&dialog, QStringLiteral("form-data")));
}

void HiddenDataDialogTests::selectAllChecksEveryExistingCategory()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());
    dialog.selectAll();

    // 默认不勾的三类，这一刻必须勾上了。
    QVERIFY(isChecked(&dialog, QStringLiteral("signatures")));
    QVERIFY(isChecked(&dialog, QStringLiteral("embedded-files")));
    QVERIFY(isChecked(&dialog, QStringLiteral("form-data")));
    QVERIFY(isChecked(&dialog, QStringLiteral("file-attachments")));

    // 原本就勾着的那些还在。
    QVERIFY(isChecked(&dialog, QStringLiteral("metadata")));
    QVERIFY(isChecked(&dialog, QStringLiteral("scripts")));

    // 选中集合 = 所有有内容的类别，一个不多一个不少。
    const QStringList selected = dialog.selectedCategoryIds();
    QCOMPARE(selected.size(), 11);
    QVERIFY(selected.contains(QStringLiteral("signatures")));
    QVERIFY(selected.contains(QStringLiteral("embedded-files")));
}

void HiddenDataDialogTests::selectAllSkipsEmptyCategories()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());
    dialog.selectAll();

    // 这份文档里没有的东西不该被勾上：勾了也没得清，
    // 复验时那一行仍然是 0，用户只会以为自己漏勾了。
    QVERIFY(!isChecked(&dialog, QStringLiteral("xfa")));
    QVERIFY(!isChecked(&dialog, QStringLiteral("layers")));
    QVERIFY(!dialog.selectedCategoryIds().contains(QStringLiteral("xfa")));
    QVERIFY(!dialog.selectedCategoryIds().contains(QStringLiteral("layers")));
}

void HiddenDataDialogTests::clearAllUnchecksEverything()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());
    dialog.selectAll();
    QVERIFY(!dialog.selectedCategoryIds().isEmpty());

    dialog.clearAll();

    // 空集合 = 什么都不清。调用方必须自己处理这个情况，
    // 不能把「没勾」当成「全清」。
    QVERIFY(dialog.selectedCategoryIds().isEmpty());
}

void HiddenDataDialogTests::selectAllDoesNotChangeTheDefaultList()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());
    const QStringList defaults = HiddenDataDialog::defaultCleanupCategoryIds();

    dialog.selectAll();
    dialog.clearAll();

    // 开关是「这一次的选择」，不是「改掉默认口径」。
    // 下次打开对话框还是那份保守白名单。
    QCOMPARE(HiddenDataDialog::defaultCleanupCategoryIds(), defaults);
    QCOMPARE(dialog.selectedCategoryIds(), QStringList());
}

void HiddenDataDialogTests::toolbarButtonsAreWiredToTheActions()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());

    auto* selectAllButton =
        dialog.findChild<QPushButton*>(QStringLiteral("hiddenDataDialog.selectAll"));
    auto* clearAllButton =
        dialog.findChild<QPushButton*>(QStringLiteral("hiddenDataDialog.clearAll"));
    QVERIFY(selectAllButton != nullptr);
    QVERIFY(clearAllButton != nullptr);

    // 按钮不是死的：点一下界面上就真的变了。
    QTest::mouseClick(selectAllButton, Qt::LeftButton);
    QVERIFY(isChecked(&dialog, QStringLiteral("signatures")));

    QTest::mouseClick(clearAllButton, Qt::LeftButton);
    QVERIFY(dialog.selectedCategoryIds().isEmpty());
}

void HiddenDataDialogTests::emptyCategoriesAreDisabled()
{
    HiddenDataDialog dialog;
    dialog.setCategories(makeCategories());

    QTreeWidgetItem* xfa = findRow(&dialog, QStringLiteral("xfa"));
    QVERIFY(xfa != nullptr);
    // 这份文档里没有的类别是禁用的：点不动，也没法勾。
    QVERIFY(xfa->isDisabled());
    QVERIFY(!(xfa->flags() & Qt::ItemIsUserCheckable));
}

QTEST_MAIN(HiddenDataDialogTests)
#include "HiddenDataDialogTests.moc"
