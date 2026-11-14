#include "app/EditProposalDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

EditProposalDialog::EditProposalDialog(const tools::Proposal& proposal, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("editProposalDialog"));
    setWindowTitle(tr("AI 提议的修改"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("AI 想对文档做 %1 处修改").arg(proposal.operations.size()), this);
    title->setObjectName(QStringLiteral("editProposalTitle"));
    layout->addWidget(title);

    auto* list = new QListWidget(this);
    list->setObjectName(QStringLiteral("editProposalList"));
    for (const tools::ProposedOperation& operation : proposal.operations) {
        list->addItem(operation.summary);
    }
    layout->addWidget(list, 1);

    auto* hint = new QLabel(
        tr("确认后这些修改会立即执行，并进入撤销栈（Ctrl+Z 可撤销）。\n"
           "点「取消」则什么都不做。"),
        this);
    hint->setObjectName(QStringLiteral("editProposalHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(this);
    auto* cancel = buttons->addButton(tr("取消"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("editProposalCancel"));
    cancel->setDefault(true);
    auto* apply = buttons->addButton(tr("执行"), QDialogButtonBox::AcceptRole);
    apply->setObjectName(QStringLiteral("editProposalApply"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

}  // namespace aipdf::app
