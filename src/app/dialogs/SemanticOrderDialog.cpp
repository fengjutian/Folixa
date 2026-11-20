#include "app/dialogs/SemanticOrderDialog.h"

#include "app/ui/Language.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <QSet>

namespace aipdf::app {
namespace {

// 列表项里记数据的角色。
//
// 为什么不用 Qt::UserRole 一个值塞完：页标题与元素项要能分开认，
// 三个角色各记一件事，读起来不用猜。
constexpr int kSlotRole = Qt::UserRole;        // 这一项属于第几个槽位
constexpr int kEntryIdRole = Qt::UserRole + 1;  // 这一项是第几个元素（拍平后的编号）
constexpr int kHeaderRole = Qt::UserRole + 2;   // 1 = 这一项是槽位标题

}  // namespace

SemanticOrderDialog::SemanticOrderDialog(const QVector<pdf::SemanticOrderSlot>& positions, QWidget* parent)
    : QDialog(parent), positions_(positions) {
    buildUi();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged, this,
            &SemanticOrderDialog::retranslateUi);
}

void SemanticOrderDialog::buildUi() {
    setObjectName(QStringLiteral("semanticOrderDialog"));
    setModal(true);
    resize(680, 560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("secondaryText"));
    hint->setWordWrap(true);
    texts_.bind([this, hint] {
        hint->setText(tr("下面是文档语义树里的内容元素（标题 / 段落 / 列表 / 表格 / 图）。"
                         "调整先后后写进 PDF 的结构树，读屏软件会按这个顺序念。"
                         "元素可以跨页移动：内容仍然印在原来那一页，只是念的先后换了。"));
    });
    layout->addWidget(hint);

    // 全部元素拍平：列表项靠编号认人，跨页移动后也不会认错。
    for (const pdf::SemanticOrderSlot& slot : positions_) {
        for (const pdf::SemanticOrderEntry& entry : slot.entries) {
            flatEntries_.append(entry);
        }
    }

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("semanticOrder.list"));
    int flatId = 0;
    for (int slot = 0; slot < positions_.size(); ++slot) {
        const pdf::SemanticOrderSlot& slotData = positions_.at(slot);
        QListWidgetItem* header = new QListWidgetItem(list_);
        header->setData(kSlotRole, slot);
        header->setData(kEntryIdRole, -1);
        header->setData(kHeaderRole, 1);
        // 槽位标题只是分隔，选不中：选中了也没法移动。
        header->setFlags(Qt::ItemIsEnabled);
        texts_.bind([this, header, slotData] {
            header->setText(tr("第 %1 页").arg(slotData.pageIndex + 1));
        });
        for (int entryIndex = 0; entryIndex < slotData.entries.size(); ++entryIndex) {
            const pdf::SemanticOrderEntry& entry = slotData.entries.at(entryIndex);
            QListWidgetItem* item = new QListWidgetItem(list_);
            item->setData(kSlotRole, slot);
            item->setData(kEntryIdRole, flatId);
            item->setData(kHeaderRole, 0);
            ++flatId;
            texts_.bind([this, item, entry] {
                // 标出内容所在的物理页：搬走了也不丢「它印在哪一页」。
                item->setText(tr("第 %1 页｜%2：%3")
                                  .arg(entry.pageIndex + 1)
                                  .arg(entry.role, entry.text));
            });
        }
    }
    layout->addWidget(list_);

    moveUpButton_ = new QPushButton(this);
    moveUpButton_->setObjectName(QStringLiteral("semanticOrder.moveUp"));
    moveDownButton_ = new QPushButton(this);
    moveDownButton_->setObjectName(QStringLiteral("semanticOrder.moveDown"));
    moveToTopButton_ = new QPushButton(this);
    moveToTopButton_->setObjectName(QStringLiteral("semanticOrder.moveToTop"));
    moveToBottomButton_ = new QPushButton(this);
    moveToBottomButton_->setObjectName(QStringLiteral("semanticOrder.moveToBottom"));
    moveToPreviousPageButton_ = new QPushButton(this);
    moveToPreviousPageButton_->setObjectName(QStringLiteral("semanticOrder.moveToPreviousPage"));
    moveToNextPageButton_ = new QPushButton(this);
    moveToNextPageButton_->setObjectName(QStringLiteral("semanticOrder.moveToNextPage"));
    auto* moveButtons = new QHBoxLayout;
    moveButtons->addWidget(moveUpButton_);
    moveButtons->addWidget(moveDownButton_);
    moveButtons->addWidget(moveToTopButton_);
    moveButtons->addWidget(moveToBottomButton_);
    moveButtons->addStretch(1);
    layout->addLayout(moveButtons);

    auto* pageButtons = new QHBoxLayout;
    pageButtons->addWidget(moveToPreviousPageButton_);
    pageButtons->addWidget(moveToNextPageButton_);
    pageButtons->addStretch(1);
    layout->addLayout(pageButtons);

    texts_.bind([this] { moveUpButton_->setText(tr("上移")); });
    texts_.bind([this] { moveDownButton_->setText(tr("下移")); });
    texts_.bind([this] { moveToTopButton_->setText(tr("移到最前")); });
    texts_.bind([this] { moveToBottomButton_->setText(tr("移到最后")); });
    texts_.bind([this] { moveToPreviousPageButton_->setText(tr("移到上一页末尾")); });
    texts_.bind([this] { moveToNextPageButton_->setText(tr("移到下一页开头")); });
    connect(moveUpButton_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(moveDownButton_, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(moveToTopButton_, &QPushButton::clicked, this, [this] { moveToEdge(true); });
    connect(moveToBottomButton_, &QPushButton::clicked, this, [this] { moveToEdge(false); });
    connect(moveToPreviousPageButton_, &QPushButton::clicked, this,
            [this] { moveToAdjacentPage(true); });
    connect(moveToNextPageButton_, &QPushButton::clicked, this, [this] { moveToAdjacentPage(false); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("semanticOrder.accept"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("semanticOrder.cancel"));
    texts_.bind([this, buttons] { buttons->button(QDialogButtonBox::Ok)->setText(tr("写入")); });
    texts_.bind([this, buttons] { buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消")); });
    connect(buttons, &QDialogButtonBox::accepted, this, &SemanticOrderDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SemanticOrderDialog::reject);
    layout->addWidget(buttons);

    texts_.bind([this] { setWindowTitle(tr("语义阅读顺序")); });
    retranslateUi();
}

void SemanticOrderDialog::retranslateUi() {
    texts_.retranslate();
}

bool SemanticOrderDialog::isEntryRow(int row) const {
    if (list_ == nullptr || row < 0 || row >= list_->count()) return false;
    return list_->item(row)->data(kHeaderRole).toInt() == 0;
}

int SemanticOrderDialog::entryIdOfRow(int row) const {
    if (!isEntryRow(row)) return -1;
    return list_->item(row)->data(kEntryIdRole).toInt();
}

void SemanticOrderDialog::refreshSlotOfRows() {
    // 页标题定边界：标题之后的那一段都属于这个槽位。
    if (list_ == nullptr) return;
    int slot = -1;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem* item = list_->item(row);
        if (item->data(kHeaderRole).toInt() == 1) {
            slot = item->data(kSlotRole).toInt();
            continue;
        }
        if (slot >= 0) item->setData(kSlotRole, slot);
    }
}

void SemanticOrderDialog::moveSelected(int delta) {
    const int row = list_->currentRow();
    if (!isEntryRow(row)) return;
    if (delta < 0) {
        if (row <= 0) return;
        QListWidgetItem* item = list_->takeItem(row);
        // 上面是页标题时，这一格是**上一个槽位**的末尾。
        list_->insertItem(row - 1, item);
    } else {
        if (row + 1 >= list_->count()) return;
        QListWidgetItem* item = list_->takeItem(row);
        // 往下挪一格：原来下面那一行（可能是页标题）跟着往前挪一位。
        list_->insertItem(row + 1, item);
    }
    list_->setCurrentRow(row + delta);
    refreshSlotOfRows();
}

void SemanticOrderDialog::moveToEdge(bool toFront) {
    const int row = list_->currentRow();
    if (!isEntryRow(row)) return;
    QListWidgetItem* item = list_->takeItem(row);
    if (toFront) {
        // 第一个槽位的标题在第 0 行，元素要排在它**后面**。
        const int firstEntry = list_->count() > 0 && list_->item(0)->data(kHeaderRole).toInt() == 1
                                   ? 1
                                   : 0;
        list_->insertItem(firstEntry, item);
        list_->setCurrentRow(firstEntry);
    } else {
        list_->addItem(item);
        list_->setCurrentRow(list_->count() - 1);
    }
    refreshSlotOfRows();
}

void SemanticOrderDialog::moveToAdjacentPage(bool previous) {
    const int row = list_->currentRow();
    if (!isEntryRow(row)) return;
    const int entryId = entryIdOfRow(row);
    if (entryId < 0 || entryId >= flatEntries_.size()) return;
    const int page = flatEntries_.at(entryId).pageIndex;

    // 找目标落点：内容在上一页的元素之后 / 下一页的元素之前。
    int target = -1;
    if (previous) {
        int bestPage = -1;
        int bestRow = -1;
        for (int r = 0; r < row; ++r) {
            const int id = entryIdOfRow(r);
            if (id < 0 || id >= flatEntries_.size()) continue;
            const int candidate = flatEntries_.at(id).pageIndex;
            if (candidate >= page) continue;
            if (candidate > bestPage) {
                bestPage = candidate;
                bestRow = r;
            }
        }
        if (bestRow < 0) return;
        target = bestRow + 1;
    } else {
        int bestPage = -1;
        int bestRow = -1;
        for (int r = row + 1; r < list_->count(); ++r) {
            const int id = entryIdOfRow(r);
            if (id < 0 || id >= flatEntries_.size()) continue;
            const int candidate = flatEntries_.at(id).pageIndex;
            if (candidate <= page) continue;
            if (bestPage < 0 || candidate < bestPage) {
                bestPage = candidate;
                bestRow = r;
            }
        }
        if (bestRow < 0) return;
        target = bestRow;
    }

    QListWidgetItem* item = list_->takeItem(row);
    // 摘掉之后，后面的行往前挪了一位。
    if (target > row) --target;
    list_->insertItem(target, item);
    list_->setCurrentRow(target);
    refreshSlotOfRows();
}

QVector<pdf::SemanticOrderSlot> SemanticOrderDialog::order() const {
    QVector<pdf::SemanticOrderSlot> result;
    result.resize(positions_.size());
    for (int slot = 0; slot < positions_.size(); ++slot) {
        result[slot].pageIndex = positions_.at(slot).pageIndex;
        result[slot].containerNodeIndex = positions_.at(slot).containerNodeIndex;
    }
    if (list_ == nullptr) return result;
    int slot = -1;
    for (int row = 0; row < list_->count(); ++row) {
        QListWidgetItem* item = list_->item(row);
        if (item->data(kHeaderRole).toInt() == 1) {
            slot = item->data(kSlotRole).toInt();
            continue;
        }
        const int id = item->data(kEntryIdRole).toInt();
        if (id < 0 || id >= flatEntries_.size()) continue;
        if (slot < 0 || slot >= result.size()) continue;
        result[slot].entries.append(flatEntries_.at(id));
    }
    return result;
}

bool SemanticOrderDialog::isOrderValid(QString* reason) const {
    // 语义元素一个都不能少、也不能多：少一个就是丢了一段内容，
    // 多一个就是凭空多出一段。
    int total = 0;
    for (const pdf::SemanticOrderSlot& slot : positions_) total += slot.entries.size();

    const QVector<pdf::SemanticOrderSlot> current = order();
    int count = 0;
    QSet<int> seen;
    for (const pdf::SemanticOrderSlot& slot : current) {
        for (const pdf::SemanticOrderEntry& entry : slot.entries) {
            ++count;
            seen.insert(entry.nodeIndex);
        }
    }
    if (count != total || seen.size() != count) {
        if (reason) {
            *reason = tr("语义元素的数量变了，不能写回（会丢掉内容）。");
        }
        return false;
    }
    return true;
}

void SemanticOrderDialog::accept() {
    QString reason;
    if (!isOrderValid(&reason)) {
        QMessageBox::warning(this, tr("语义阅读顺序"),
                             reason.isEmpty()
                                 ? tr("语义元素的数量变了，不能写回（会丢掉内容）。")
                                 : reason);
        return;
    }
    QDialog::accept();
}

}  // namespace aipdf::app
