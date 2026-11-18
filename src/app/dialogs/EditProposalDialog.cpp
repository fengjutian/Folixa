#include "app/dialogs/EditProposalDialog.h"

#include "app/ui/Language.h"
#include "i18n/Translate.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

EditProposalDialog::EditProposalDialog(const tools::Proposal& proposal, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("editProposalDialog"));
    texts_.bind([this] { setWindowTitle(tr("AI 提议的修改")); });
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(this);
    title->setObjectName(QStringLiteral("editProposalTitle"));
    const int operationCount = static_cast<int>(proposal.operations.size());
    texts_.bind([this, title, operationCount] {
        title->setText(tr("AI 想对文档做 %1 处修改").arg(operationCount));
    });
    layout->addWidget(title);

    auto* list = new QListWidget(this);
    list->setObjectName(QStringLiteral("editProposalList"));
    for (const tools::ProposedOperation& operation : proposal.operations) {
        list->addItem(operation.summary);
    }
    layout->addWidget(list, 1);

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("editProposalHint"));
    hint->setWordWrap(true);
    texts_.bind([this, hint] {
        hint->setText(tr("确认后这些修改会立即执行，并进入撤销栈（Ctrl+Z 可撤销）。") +
                      QStringLiteral("\n") +
                      tr("点「取消」则什么都不做。"));
    });
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(this);
    auto* cancel = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("editProposalCancel"));
    cancel->setDefault(true);
    auto* apply = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    apply->setObjectName(QStringLiteral("editProposalApply"));
    texts_.bind([this, cancel] { cancel->setText(tr("取消")); });
    texts_.bind([this, apply] { apply->setText(tr("执行")); });
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 切换语言后就地重译。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &EditProposalDialog::retranslateUi);
}

void EditProposalDialog::retranslateUi() {
    texts_.retranslate();
}

}  // namespace aipdf::app
