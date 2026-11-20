#include "app/dialogs/SignatureWorkflowDialog.h"

#include "app/controllers/SignatureWorkflowController.h"
#include "pdf/security/SignatureWorkflow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

QString stateName(pdf::WorkflowState state) {
    switch (state) {
        case pdf::WorkflowState::Draft: return SignatureWorkflowDialog::tr("草稿");
        case pdf::WorkflowState::InProgress: return SignatureWorkflowDialog::tr("进行中");
        case pdf::WorkflowState::Completed: return SignatureWorkflowDialog::tr("已完成");
        case pdf::WorkflowState::Aborted: return SignatureWorkflowDialog::tr("已中止");
    }
    return SignatureWorkflowDialog::tr("草稿");
}

QString stepName(pdf::SignStepStatus status) {
    switch (status) {
        case pdf::SignStepStatus::Pending: return SignatureWorkflowDialog::tr("待签署");
        case pdf::SignStepStatus::Signed: return SignatureWorkflowDialog::tr("已签署");
        case pdf::SignStepStatus::Rejected: return SignatureWorkflowDialog::tr("已拒签");
        case pdf::SignStepStatus::Skipped: return SignatureWorkflowDialog::tr("已跳过");
    }
    return SignatureWorkflowDialog::tr("待签署");
}

QString actionName(const QString& action) {
    if (action == QLatin1String("created")) return SignatureWorkflowDialog::tr("创建流程");
    if (action == QLatin1String("signatory_added")) return SignatureWorkflowDialog::tr("新增签署人");
    if (action == QLatin1String("started")) return SignatureWorkflowDialog::tr("开始签署流程");
    if (action == QLatin1String("signed")) return SignatureWorkflowDialog::tr("签署完成");
    if (action == QLatin1String("rejected")) return SignatureWorkflowDialog::tr("拒签");
    if (action == QLatin1String("skipped")) return SignatureWorkflowDialog::tr("跳过签署");
    if (action == QLatin1String("completed")) return SignatureWorkflowDialog::tr("完成流程");
    if (action == QLatin1String("aborted")) return SignatureWorkflowDialog::tr("中止流程");
    return action;
}

void showReport(const pdf::SignatureWorkflow& workflow, QWidget* parent) {
    const QString text = workflow.buildReport().toPlainText();
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("signature.workflow.reportDialog"));
    dialog.setWindowTitle(SignatureWorkflowDialog::tr("签署报告"));
    dialog.resize(680, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setObjectName(QStringLiteral("signature.workflow.reportText"));
    editor->setReadOnly(true);
    editor->setPlainText(text);
    layout->addWidget(editor);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* copy = buttons->addButton(SignatureWorkflowDialog::tr("复制到剪贴板"),
                                    QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("signature.workflow.reportCopy"));
    auto* exportButton = buttons->addButton(SignatureWorkflowDialog::tr("导出为文本文件"),
                                            QDialogButtonBox::ActionRole);
    exportButton->setObjectName(QStringLiteral("signature.workflow.reportExport"));
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    QObject::connect(copy, &QPushButton::clicked, &dialog,
                     [text] { QApplication::clipboard()->setText(text); });
    QObject::connect(exportButton, &QPushButton::clicked, &dialog, [text, &dialog] {
        const QString path = QFileDialog::getSaveFileName(
            &dialog, SignatureWorkflowDialog::tr("导出签署报告"), QString(),
            SignatureWorkflowDialog::tr("文本文件 (*.txt)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(&dialog, SignatureWorkflowDialog::tr("导出签署报告"),
                                 SignatureWorkflowDialog::tr("写不了这个文件。"));
            return;
        }
        file.write(text.toUtf8());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

}  // namespace

SignatureWorkflowDialog::SignatureWorkflowDialog(SignatureWorkflowController& controller,
                                                 QWidget* parent)
    : QDialog(parent) {
    const pdf::SignatureWorkflow& workflow = controller.model();
    setObjectName(QStringLiteral("signature.workflowDialog"));
    setWindowTitle(tr("签署流程"));
    resize(880, 660);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* title = new QLineEdit(this);
    title->setObjectName(QStringLiteral("signature.workflow.title"));
    auto* initiator = new QLineEdit(this);
    initiator->setObjectName(QStringLiteral("signature.workflow.initiator"));
    auto* state = new QLabel(this);
    state->setObjectName(QStringLiteral("signature.workflow.state"));
    form->addRow(tr("标题"), title);
    form->addRow(tr("发起人"), initiator);
    form->addRow(tr("状态"), state);
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("签署人名单"), this));
    auto* signatories = new QTableWidget(0, 4, this);
    signatories->setObjectName(QStringLiteral("signature.workflow.signatories"));
    signatories->setHorizontalHeaderLabels({tr("姓名"), tr("联系方式"), tr("顺序"), tr("状态")});
    signatories->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    signatories->setEditTriggers(QAbstractItemView::NoEditTriggers);
    signatories->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(signatories);
    auto* noSignatories = new QLabel(tr("（还没有签署人）"), this);
    noSignatories->setObjectName(QStringLiteral("signature.workflow.signatories.empty"));
    layout->addWidget(noSignatories);

    auto* addRow = new QHBoxLayout;
    auto* name = new QLineEdit(this);
    name->setObjectName(QStringLiteral("signature.workflow.newName"));
    name->setPlaceholderText(tr("姓名"));
    auto* contact = new QLineEdit(this);
    contact->setObjectName(QStringLiteral("signature.workflow.newContact"));
    contact->setPlaceholderText(tr("联系方式"));
    auto* order = new QSpinBox(this);
    order->setObjectName(QStringLiteral("signature.workflow.newOrder"));
    order->setRange(0, 999);
    order->setSpecialValueText(tr("自动"));
    auto* add = new QPushButton(tr("添加签署人"), this);
    add->setObjectName(QStringLiteral("signature.workflow.addSignatory"));
    auto* remove = new QPushButton(tr("删除签署人"), this);
    remove->setObjectName(QStringLiteral("signature.workflow.removeSignatory"));
    addRow->addWidget(name);
    addRow->addWidget(contact);
    addRow->addWidget(order);
    addRow->addWidget(add);
    addRow->addWidget(remove);
    layout->addLayout(addRow);

    auto* actionRow = new QHBoxLayout;
    const auto actionButton = [this, actionRow](const QString& text, const char* objectName) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(QString::fromLatin1(objectName));
        actionRow->addWidget(button);
        return button;
    };
    auto* start = actionButton(tr("开始签署"), "signature.workflow.start");
    auto* signedButton = actionButton(tr("标记已签"), "signature.workflow.signed");
    auto* reject = actionButton(tr("标记拒签"), "signature.workflow.rejected");
    auto* skip = actionButton(tr("跳过"), "signature.workflow.skip");
    auto* complete = actionButton(tr("完成"), "signature.workflow.complete");
    auto* abort = actionButton(tr("中止"), "signature.workflow.abort");
    layout->addLayout(actionRow);

    layout->addWidget(new QLabel(tr("签署记录"), this));
    auto* events = new QTableWidget(0, 4, this);
    events->setObjectName(QStringLiteral("signature.workflow.events"));
    events->setHorizontalHeaderLabels({tr("时间"), tr("谁"), tr("做了什么"), tr("说明")});
    events->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    events->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(events);
    auto* noEvents = new QLabel(tr("（还没有签署记录）"), this);
    noEvents->setObjectName(QStringLiteral("signature.workflow.events.empty"));
    layout->addWidget(noEvents);
    auto* report = new QPushButton(tr("查看签署报告"), this);
    report->setObjectName(QStringLiteral("signature.workflow.report"));
    layout->addWidget(report, 0, Qt::AlignLeft);

    const auto refresh = [=, &workflow] {
        state->setText(stateName(workflow.state()));
        if (!workflow.id().isEmpty()) {
            title->setText(workflow.title());
            initiator->setText(workflow.initiator());
            title->setEnabled(false);
            initiator->setEnabled(false);
        }
        const auto people = workflow.signatories();
        signatories->setRowCount(people.size());
        for (int row = 0; row < people.size(); ++row) {
            const auto& person = people.at(row);
            signatories->setItem(row, 0, new QTableWidgetItem(person.name));
            signatories->setItem(row, 1, new QTableWidgetItem(person.contact));
            signatories->setItem(row, 2, new QTableWidgetItem(QString::number(person.order)));
            auto* item = new QTableWidgetItem(stepName(person.status));
            if (person.status == pdf::SignStepStatus::Signed) item->setForeground(QColor(46, 160, 67));
            if (person.status == pdf::SignStepStatus::Rejected) item->setForeground(QColor(198, 40, 40));
            if (!person.rejectReason.isEmpty()) item->setToolTip(person.rejectReason);
            signatories->setItem(row, 3, item);
        }
        noSignatories->setVisible(people.isEmpty());
        const auto history = workflow.events();
        events->setRowCount(history.size());
        for (int row = 0; row < history.size(); ++row) {
            const auto& event = history.at(row);
            events->setItem(row, 0, new QTableWidgetItem(event.at.toString(Qt::ISODate)));
            events->setItem(row, 1, new QTableWidgetItem(event.actor));
            events->setItem(row, 2, new QTableWidgetItem(actionName(event.action)));
            events->setItem(row, 3, new QTableWidgetItem(event.detail));
        }
        noEvents->setVisible(history.isEmpty());
        const bool finished = workflow.state() == pdf::WorkflowState::Completed ||
                              workflow.state() == pdf::WorkflowState::Aborted;
        start->setEnabled(!finished && workflow.state() == pdf::WorkflowState::Draft &&
                          !workflow.id().isEmpty() && !people.isEmpty());
        for (QPushButton* button : {signedButton, reject, skip, complete, abort}) button->setEnabled(!finished);
        report->setEnabled(!workflow.id().isEmpty());
    };
    const auto showError = [this](const QString& error) {
        QMessageBox::warning(this, tr("签署流程"), error.isEmpty() ? tr("操作没有成功。") : error);
    };
    const auto ensureCreated = [&, this] {
        if (!workflow.id().isEmpty()) return true;
        QString error;
        if (controller.ensureCreated(title->text().trimmed(), initiator->text().trimmed(), &error)) return true;
        QMessageBox::warning(this, tr("签署流程"), error.isEmpty() ? tr("创建签署流程失败。") : error);
        return false;
    };

    connect(add, &QPushButton::clicked, this, [&, this] {
        if (!ensureCreated()) return;
        pdf::Signatory person{name->text(), contact->text(), order->value()};
        QString error;
        if (!controller.addSignatory(person, &error)) return showError(error);
        name->clear(); contact->clear(); order->setValue(0); refresh();
    });
    connect(remove, &QPushButton::clicked, this, [&, this] {
        const int row = signatories->currentRow();
        if (row < 0 || row >= workflow.signatories().size()) {
            QMessageBox::information(this, tr("签署流程"), tr("先选中要删除的签署人。"));
            return;
        }
        QMessageBox::information(this, tr("签署流程"),
            tr("签署人 %1 不能删除：签署记录要保留完整名单。").arg(workflow.signatories().at(row).name));
    });
    connect(start, &QPushButton::clicked, this, [&, this] { QString e; if (ensureCreated() && controller.start(&e)) refresh(); else if (!e.isEmpty()) showError(e); });
    connect(signedButton, &QPushButton::clicked, this, [&, this] { QString e; if (controller.signNext(QDateTime::currentDateTime(), &e)) refresh(); else showError(e); });
    connect(skip, &QPushButton::clicked, this, [&, this] { QString e; if (controller.skipNext(&e)) refresh(); else showError(e); });
    connect(complete, &QPushButton::clicked, this, [&, this] { QString e; if (controller.complete(&e)) refresh(); else showError(e); });
    connect(reject, &QPushButton::clicked, this, [&, this] {
        bool ok = false;
        const QString reason = QInputDialog::getText(this, tr("标记拒签"), tr("拒签理由（必填）"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        if (reason.trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("标记拒签"), tr("拒签必须写理由。"));
            return;
        }
        QString error;
        if (controller.rejectNext(reason, QDateTime::currentDateTime(), &error)) refresh(); else showError(error);
    });
    connect(abort, &QPushButton::clicked, this, [&, this] {
        bool ok = false;
        const QString reason = QInputDialog::getText(this, tr("中止流程"), tr("中止理由（必填）"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        if (reason.trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("中止流程"), tr("中止必须写理由。"));
            return;
        }
        QString error;
        if (controller.abort(reason, &error)) refresh(); else showError(error);
    });
    connect(report, &QPushButton::clicked, this, [&workflow, this] { showReport(workflow, this); });
    refresh();
}

}  // namespace aipdf::app
