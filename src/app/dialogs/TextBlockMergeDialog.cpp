#include "app/dialogs/TextBlockMergeDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

// 列表里每一行的预览长度。
//
// 为什么要截断：文本块可能是一整页字，全塞进列表里会把对话框撑爆，
// 看的人也没法快速分辨哪几块是要合并的。
constexpr int kPreviewChars = 48;

}  // namespace

TextBlockMergeDialog::TextBlockMergeDialog(const QVector<pdf::ContentTextBlock>& blocks,
                                           QWidget* parent)
    : QDialog(parent), blocks_(blocks) {
    setWindowTitle(tr("跨文本块合并"));
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);

    hint_ = new QLabel(tr("勾选要合并成一段的文本块（至少两块）。块与块之间夹着图片或表格时会被拒绝。"),
                       this);
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const pdf::ContentTextBlock& block : blocks_) {
        QString preview = block.text.trimmed();
        preview.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (preview.size() > kPreviewChars) {
            preview = preview.left(kPreviewChars) + QStringLiteral("…");
        }
        const QString label =
            tr("块 %1：%2（位置 %3, %4）")
                .arg(block.blockId)
                .arg(preview.isEmpty() ? tr("（空）") : preview)
                .arg(QString::number(block.bounds.left(), 'f', 0))
                .arg(QString::number(block.bounds.top(), 'f', 0));
        QListWidgetItem* item = new QListWidgetItem(label, list_);
        item->setData(Qt::UserRole, block.blockId);
    }
    layout->addWidget(list_, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = box->button(QDialogButtonBox::Ok);
    okButton_->setText(tr("合并"));
    box->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QListWidget::itemSelectionChanged, this, &TextBlockMergeDialog::updateButtons);
    updateButtons();
}

QVector<int> TextBlockMergeDialog::selectedBlockIds() const {
    QVector<int> ids;
    for (QListWidgetItem* item : list_->selectedItems()) {
        ids.append(item->data(Qt::UserRole).toInt());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void TextBlockMergeDialog::updateButtons() {
    // 合并至少要两块——一块没有「合并」可言。
    okButton_->setEnabled(list_->selectedItems().size() >= 2);
}

}  // namespace aipdf::app
