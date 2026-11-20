#include "app/dialogs/SemanticOrderDialog.h"

#include "app/ui/Language.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

// 列表项里记数据的角色。
//
// 为什么不用 Qt::UserRole 一个值塞完：页标题与元素项要能分开认，
// 三个角色各记一件事，读起来不用猜。
constexpr int kPageSlotRole = Qt::UserRole;      // 这一项属于第几个「页槽」
constexpr int kEntryRole = Qt::UserRole + 1;      // 这一项是该页里的第几个元素
constexpr int kHeaderRole = Qt::UserRole + 2;     // 1 = 这一项是页标题

}  // namespace

SemanticOrderDialog::SemanticOrderDialog(const QVector<pdf::SemanticOrderPage>& pages, QWidget* parent)
    : QDialog(parent), pages_(pages) {
    buildUi();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged, this,
            &SemanticOrderDialog::retranslateUi);
}

void SemanticOrderDialog::buildUi() {
    setObjectName(QStringLiteral("semanticOrderDialog"));
    setModal(true);
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("secondaryText"));
    hint->setWordWrap(true);
    texts_.bind([this, hint] {
        hint->setText(tr("下面是文档语义树里的内容元素（标题 / 段落 / 列表 / 表格 / 图）。"
                         "调整先后后写进 PDF 的结构树，读屏软件会按这个顺序念。"
                         "跨页移动不支持，只能在同一页内调整。"));
    });
    layout->addWidget(hint);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("semanticOrder.list"));
    for (int slot = 0; slot < pages_.size(); ++slot) {
        const pdf::SemanticOrderPage& page = pages_.at(slot);
        QListWidgetItem* header = new QListWidgetItem(list_);
        header->setData(kPageSlotRole, -1);
        header->setData(kEntryRole, -1);
        header->setData(kHeaderRole, 1);
        // 页标题只是分隔，选不中：选中了也没法移动。
        header->setFlags(Qt::ItemIsEnabled);
        texts_.bind([this, header, page] {
            header->setText(tr("第 %1 页").arg(page.pageIndex + 1));
        });
        for (int entryIndex = 0; entryIndex < page.entries.size(); ++entryIndex) {
            const pdf::SemanticOrderEntry& entry = page.entries.at(entryIndex);
            QListWidgetItem* item = new QListWidgetItem(list_);
            item->setData(kPageSlotRole, slot);
            item->setData(kEntryRole, entryIndex);
            item->setData(kHeaderRole, 0);
            texts_.bind([this, item, entry] {
                item->setText(tr("%1：%2").arg(entry.role, entry.text));
            });
        }
    }
    layout->addWidget(list_);

    moveUpButton_ = new QPushButton(this);
    moveUpButton_->setObjectName(QStringLiteral("semanticOrder.moveUp"));
    moveDownButton_ = new QPushButton(this);
    moveDownButton_->setObjectName(QStringLiteral("semanticOrder.moveDown"));
    auto* moveButtons = new QHBoxLayout;
    moveButtons->addWidget(moveUpButton_);
    moveButtons->addWidget(moveDownButton_);
    moveButtons->addStretch(1);
    layout->addLayout(moveButtons);

    texts_.bind([this] { moveUpButton_->setText(tr("上移")); });
    texts_.bind([this] { moveDownButton_->setText(tr("下移")); });
    connect(moveUpButton_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(moveDownButton_, &QPushButton::clicked, this, [this] { moveSelected(1); });

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

void SemanticOrderDialog::moveSelected(int delta) {
    const int row = list_->currentRow();
    if (!isEntryRow(row)) return;
    const int target = row + delta;
    if (!isEntryRow(target)) return;
    // 跨页不动：语义元素的顺序只在页内说得通。
    if (list_->item(row)->data(kPageSlotRole).toInt() !=
        list_->item(target)->data(kPageSlotRole).toInt()) {
        return;
    }
    QListWidgetItem* item = list_->takeItem(row);
    list_->insertItem(target, item);
    list_->setCurrentRow(target);
}

QVector<pdf::SemanticOrderPage> SemanticOrderDialog::order() const {
    QVector<pdf::SemanticOrderPage> result;
    result.resize(pages_.size());
    for (int slot = 0; slot < pages_.size(); ++slot) {
        result[slot].pageIndex = pages_.at(slot).pageIndex;
    }
    if (list_ == nullptr) return result;
    for (int row = 0; row < list_->count(); ++row) {
        if (!isEntryRow(row)) continue;
        QListWidgetItem* item = list_->item(row);
        const int slot = item->data(kPageSlotRole).toInt();
        const int entryIndex = item->data(kEntryRole).toInt();
        if (slot < 0 || slot >= pages_.size()) continue;
        if (entryIndex < 0 || entryIndex >= pages_.at(slot).entries.size()) continue;
        result[slot].entries.append(pages_.at(slot).entries.at(entryIndex));
    }
    return result;
}

void SemanticOrderDialog::accept() {
    // 元素个数必须与传进来的一致：少一个就是丢了一段内容，不能写。
    const QVector<pdf::SemanticOrderPage> current = order();
    for (int slot = 0; slot < pages_.size() && slot < current.size(); ++slot) {
        if (current.at(slot).entries.size() != pages_.at(slot).entries.size()) {
            QMessageBox::warning(this, tr("语义阅读顺序"),
                                 tr("语义元素的数量变了，不能写回（会丢掉内容）。"));
            return;
        }
    }
    QDialog::accept();
}

}  // namespace aipdf::app
