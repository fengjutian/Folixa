#pragma once

#include "pdf/document/Navigation.h"

#include <QDialog>
#include <QPoint>
#include <QRect>
#include <QString>
#include <vector>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace aipdf::app {

// 拖放落点的语义：拖到条目的哪个位置，决定了书签会落到哪里。
//
// 为什么自己定义而不用 QTreeWidget 自带的指示线：
// 自带的那套只有「插到前面 / 插到后面 / 变成子节点」三种，
// 但它算出来的落点依赖控件内部对 ItemIsDropEnabled 的判断，
// 一旦某个条目不接受落点，整个拖放就会静默失灵。
// 这里改成「按条目矩形的三等分」自己算：位置决定语义，规则写在代码里，
// 界面提示、单元测试用的是同一份判定。
enum class BookmarkDropZone {
    Before,  // 排在它前面（同一层）
    On,      // 变成它的子节点
    After,   // 排在它后面（同一层）
    Root,    // 落到顶层（条目以外的空白处）
};

// 「编辑书签层级」对话框：把大纲按层级显示成一棵树，拖拽改层级。
//
// 为什么单独一个对话框，而不是把导航面板里那列扁平书签直接改成树：
// 导航面板那份扁���列表是面板识别与验收脚本的锚点（AutomationId、
// 条目名、条目条数都靠它），把它改成树会连带动到别处；
// 独立对话框只在这里做树，改完再整体写回文档，边界清楚。
class BookmarkTreeDialog final : public QDialog {
    Q_OBJECT

public:
    explicit BookmarkTreeDialog(const std::vector<pdf::OutlineItem>& outline, QWidget* parent = nullptr);

    // 搬一条书签：把 sourcePath 处的书签挪到 newParentPath 底下，
    // 在那一层里插到 insertIndex 的位置（-1 = 追加到末尾）。
    //
    // 搬的是**整条书签**（连同它自己的子树）。搬不动时返回 false，树保持原样。
    bool moveItem(const std::vector<int>& sourcePath,
                  const std::vector<int>& newParentPath,
                  int insertIndex);

    // 编辑后的大纲（还没写回文档）。
    const std::vector<pdf::OutlineItem>& outline() const;

    // 树控件。界面与测试都要用它来看层级。
    QTreeWidget* tree() const;

    // 落点几何 → 语义。条目矩形的上三分之一 = 排在它前面，
    // 中间三分之一 = 变成它的子节点，下三分之一 = 排在它后面。
    static BookmarkDropZone zoneFor(const QRect& itemRect, const QPoint& pos);

    // 鼠标位置 → (新父路径, 插入下标)。pos 是树控件的视口坐标。
    // 位置落在条目以外（空白处）时，新父路径为空、插入到顶层末尾。
    bool dropTargetFor(const QPoint& pos,
                       std::vector<int>* newParentPath,
                       int* insertIndex) const;

    // 把 sourcePath 处的书签"拖到" pos 这个位置：先算落点，再搬。
    //
    // 界面上的拖放事件走的是同一条路（落点判定 + 搬动），
    // 留这个入口是为了让「拖到哪儿会变成什么」能被单测覆盖，
    // 不必真的在桌面上拖一次。
    bool dropAt(const QPoint& pos, const std::vector<int>& sourcePath);

private:
    void rebuildTree();
    void appendItems(QTreeWidgetItem* parent, const std::vector<pdf::OutlineItem>& items);
    QString describeDrop(const QPoint& pos) const;

    std::vector<pdf::OutlineItem> outline_;
    QTreeWidget* tree_ = nullptr;
    QLabel* hint_ = nullptr;
};

}  // namespace aipdf::app
