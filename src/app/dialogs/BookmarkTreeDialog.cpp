#include "app/dialogs/BookmarkTreeDialog.h"

#include "i18n/Translate.h"
#include "pdf/ops/OutlineEditor.h"

#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace aipdf::app {

namespace {

// 条目在它所在那一层里的行号（顶层条目取顶层行号）。
int rowInParent(QTreeWidget* tree, QTreeWidgetItem* item) {
    if (tree == nullptr || item == nullptr) {
        return -1;
    }
    QTreeWidgetItem* parent = item->parent();
    if (parent == nullptr) {
        return tree->indexOfTopLevelItem(item);
    }
    return parent->indexOfChild(item);
}

// 条目 → 它在树里的路径（每层里的第几个，从顶层往下）。
std::vector<int> pathOfItem(QTreeWidget* tree, QTreeWidgetItem* item) {
    std::vector<int> path;
    QTreeWidgetItem* current = item;
    while (current != nullptr) {
        const int row = rowInParent(tree, current);
        if (row < 0) {
            return {};
        }
        path.push_back(row);
        current = current->parent();
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// 条目的父节点路径（顶层条目 → 空路径，也就是顶层）。
std::vector<int> parentPathOfItem(QTreeWidget* tree, QTreeWidgetItem* item) {
    if (item == nullptr) {
        return {};
    }
    QTreeWidgetItem* parent = item->parent();
    if (parent == nullptr) {
        return {};
    }
    return pathOfItem(tree, parent);
}

// 树控件：自己接管拖放。
//
// 为什么不让 QTreeWidget 自己搬：它搬的是「控件里的行」，
// 搬完控件里的树和文档里的大纲就分家了 —— 层级改在哪、写成什么样，
// 控件自己不知道。这里让它只负责"把落点算出来"，搬动交给 pdf 层，
// 搬完再从 pdf 层的结果把树重建一遍，两边永远一致。
class BookmarkTreeWidget final : public QTreeWidget {
public:
    // 落点 → 搬动，整条路径交给对话框（落点判定与搬动都在那儿，可被单测覆盖）。
    using DropHandler = std::function<bool(const QPoint& pos,
                                            const std::vector<int>& sourcePath)>;
    using HintHandler = std::function<QString(const QPoint& pos)>;

    explicit BookmarkTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {}

    void setDropHandler(DropHandler handler) { handler_ = std::move(handler); }
    void setHintHandler(HintHandler handler) { hint_ = std::move(handler); }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        pressedItem_ = itemAt(event->position().toPoint());
        QTreeWidget::mousePressEvent(event);
    }

    void startDrag(Qt::DropActions supportedActions) override {
        // 拖起来的到底是哪一条：按下时先记住，别等落点算完再猜。
        QTreeWidgetItem* item = pressedItem_ != nullptr ? pressedItem_ : currentItem();
        draggedPath_ = item != nullptr ? pathOfItem(this, item) : std::vector<int>{};
        QTreeWidget::startDrag(supportedActions);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        // 只接受本树自己拖出来的书签。以前不看来源直接 accept，加上
        // draggedPath_ 用后不清，从资源管理器拖个文件进来就会把上一次的
        // 书签搬走，点“应用”后大纲被静默改掉。
        if (event->source() != this || draggedPath_.empty()) {
            event->ignore();
            return;
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->source() != this || draggedPath_.empty()) {
            event->ignore();
            return;
        }
        if (hint_) {
            hint_(event->position().toPoint());
        }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dropEvent(QDropEvent* event) override {
        if (!handler_ || draggedPath_.empty() || event->source() != this) {
            event->ignore();
            return;
        }
        const bool moved = handler_(event->position().toPoint(), draggedPath_);
        // 一次拖动只处理一次：不清空的话，下一次外部拖放会复用这条旧路径。
        draggedPath_.clear();
        if (moved) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else {
            // 搬不动（比如搬进自己的子树）：这次拖放当作没发生。
            event->ignore();
        }
    }

private:
    DropHandler handler_;
    HintHandler hint_;
    QTreeWidgetItem* pressedItem_ = nullptr;
    std::vector<int> draggedPath_;
};

}  // namespace

BookmarkTreeDialog::BookmarkTreeDialog(const std::vector<pdf::OutlineItem>& outline, QWidget* parent)
    : QDialog(parent)
    , outline_(outline)
{
    setWindowTitle(i18n::tr("aipdf::app::BookmarkTreeDialog", "编辑书签层级"));
    setObjectName(QStringLiteral("bookmarkTreeDialog"));
    resize(520, 520);

    auto* layout = new QVBoxLayout(this);

    auto* tip = new QLabel(
        i18n::tr("aipdf::app::BookmarkTreeDialog",
                 "拖动书签可以改层级：拖到条目中间变成它的子节点，"
                 "拖到条目上缘或下缘排在它前面或后面，拖到空白处放到顶层。"),
        this);
    tip->setWordWrap(true);
    layout->addWidget(tip);

    auto* tree = new BookmarkTreeWidget(this);
    tree->setObjectName(QStringLiteral("bookmarkTreeDialog.tree"));
    tree->setColumnCount(2);
    tree->setHeaderLabels({i18n::tr("aipdf::app::BookmarkTreeDialog", "书签"),
                            i18n::tr("aipdf::app::BookmarkTreeDialog", "跳转到")});
    tree->header()->setStretchLastSection(true);
    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setDragDropMode(QAbstractItemView::DragDrop);
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(tree, 1);
    tree_ = tree;

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("bookmarkTreeHint"));
    hint_->setText(i18n::tr("aipdf::app::BookmarkTreeDialog", "把书签拖到新的位置即可改层级。"));
    layout->addWidget(hint_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(i18n::tr("aipdf::app::BookmarkTreeDialog", "应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(i18n::tr("aipdf::app::BookmarkTreeDialog", "取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    tree->setDropHandler([this](const QPoint& pos, const std::vector<int>& sourcePath) {
        return dropAt(pos, sourcePath);
    });
    tree->setHintHandler([this](const QPoint& pos) {
        const QString text = describeDrop(pos);
        if (hint_ != nullptr) {
            hint_->setText(text);
        }
        return text;
    });

    rebuildTree();
}

bool BookmarkTreeDialog::moveItem(const std::vector<int>& sourcePath,
                                   const std::vector<int>& newParentPath,
                                   int insertIndex) {
    if (sourcePath.empty()) {
        return false;
    }
    // 树操作用摊平行号：路径要先换算成行号（结构一改行号就变，所以每次都现算）。
    const int row = pdf::OutlineEditor::rowOfPath(&outline_, sourcePath);
    if (row < 0) {
        return false;
    }
    if (!pdf::OutlineEditor::reparent(&outline_, row, newParentPath, insertIndex)) {
        return false;
    }
    rebuildTree();
    return true;
}

const std::vector<pdf::OutlineItem>& BookmarkTreeDialog::outline() const {
    return outline_;
}

QTreeWidget* BookmarkTreeDialog::tree() const {
    return tree_;
}

BookmarkDropZone BookmarkTreeDialog::zoneFor(const QRect& itemRect, const QPoint& pos) {
    if (!itemRect.isValid() || itemRect.height() <= 0) {
        return BookmarkDropZone::On;
    }
    const int third = itemRect.height() / 3;
    const int offset = pos.y() - itemRect.top();
    if (offset < third) {
        return BookmarkDropZone::Before;
    }
    if (offset > itemRect.height() - third) {
        return BookmarkDropZone::After;
    }
    return BookmarkDropZone::On;
}

bool BookmarkTreeDialog::dropTargetFor(const QPoint& pos,
                                        std::vector<int>* newParentPath,
                                        int* insertIndex) const {
    if (newParentPath == nullptr || insertIndex == nullptr || tree_ == nullptr) {
        return false;
    }
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (item == nullptr) {
        // 条目以外的空白处：落到顶层末尾。
        newParentPath->clear();
        *insertIndex = -1;
        return true;
    }
    const BookmarkDropZone zone = zoneFor(tree_->visualItemRect(item), pos);
    if (zone == BookmarkDropZone::On) {
        // 变成它的子节点：追加到它自己的孩子末尾。
        *newParentPath = pathOfItem(tree_, item);
        *insertIndex = -1;
        return true;
    }
    const int row = rowInParent(tree_, item);
    if (row < 0) {
        return false;
    }
    *newParentPath = parentPathOfItem(tree_, item);
    *insertIndex = zone == BookmarkDropZone::Before ? row : row + 1;
    return true;
}

bool BookmarkTreeDialog::dropAt(const QPoint& pos, const std::vector<int>& sourcePath) {
    std::vector<int> parentPath;
    int insertIndex = -1;
    if (!dropTargetFor(pos, &parentPath, &insertIndex)) {
        return false;
    }
    return moveItem(sourcePath, parentPath, insertIndex);
}

void BookmarkTreeDialog::rebuildTree() {
    if (tree_ == nullptr) {
        return;
    }
    tree_->clear();
    appendItems(nullptr, outline_);
    // 顶层默认展开：一眼能看到层级，而不是一排收起来的箭头。
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        tree_->expandItem(tree_->topLevelItem(index));
    }
}

void BookmarkTreeDialog::appendItems(QTreeWidgetItem* parent,
                                     const std::vector<pdf::OutlineItem>& items) {
    if (tree_ == nullptr) {
        return;
    }
    for (const pdf::OutlineItem& item : items) {
        auto* node = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        node->setText(0, item.title);
        node->setText(1, item.pageIndex >= 0
                             ? i18n::tr("aipdf::app::BookmarkTreeDialog", "第 %1 页").arg(item.pageIndex + 1)
                             : i18n::tr("aipdf::app::BookmarkTreeDialog", "没有目标"));
        node->setFlags(node->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        appendItems(node, item.children);
    }
}

QString BookmarkTreeDialog::describeDrop(const QPoint& pos) const {
    if (tree_ == nullptr) {
        return {};
    }
    QTreeWidgetItem* item = tree_->itemAt(pos);
    if (item == nullptr) {
        return i18n::tr("aipdf::app::BookmarkTreeDialog", "放到顶层");
    }
    const QString title = item->text(0);
    switch (zoneFor(tree_->visualItemRect(item), pos)) {
    case BookmarkDropZone::On:
        return i18n::tr("aipdf::app::BookmarkTreeDialog", "变成「%1」的子节点").arg(title);
    case BookmarkDropZone::Before:
        return i18n::tr("aipdf::app::BookmarkTreeDialog", "排在「%1」前面").arg(title);
    case BookmarkDropZone::After:
        return i18n::tr("aipdf::app::BookmarkTreeDialog", "排在「%1」后面").arg(title);
    }
    return {};
}

}  // namespace aipdf::app
