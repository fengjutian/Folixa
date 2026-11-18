#include "app/dialogs/FormDesignAssistantDialog.h"

#include "ipc/WorkerClient.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace aipdf::app {

FormDesignAssistantDialog::FormDesignAssistantDialog(ipc::WorkerClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
    setWindowTitle(tr("表单设计助手"));
    resize(760, 560);

    auto* layout = new QVBoxLayout(this);
    description_ = new QPlainTextEdit(this);
    description_->setObjectName(QStringLiteral("formDesignDescription"));
    description_->setPlaceholderText(
        tr("写一句描述，例如：一张增值税发票，需要购买方名称、纳税人识别号、开票日期、金额、税率。"));
    layout->addWidget(description_);

    auto* buttonsRow = new QHBoxLayout();
    generateButton_ = new QPushButton(tr("生成设计"), this);
    generateButton_->setObjectName(QStringLiteral("formDesignGenerate"));
    buttonsRow->addWidget(generateButton_);
    buttonsRow->addStretch(1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("formDesignStatus"));
    buttonsRow->addWidget(status_);
    layout->addLayout(buttonsRow);

    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("formDesignTable"));
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(QStringList{ tr("字段名"), tr("提示"), tr("类型"), tr("左 %"),
                                                   tr("上 %"), tr("宽 %"), tr("高 %") });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    auto* rawBox = new QGroupBox(tr("模型原始回答"), this);
    auto* rawLayout = new QVBoxLayout(rawBox);
    raw_ = new QPlainTextEdit(rawBox);
    raw_->setReadOnly(true);
    rawLayout->addWidget(raw_);
    layout->addWidget(rawBox);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    createButton_ = new QPushButton(tr("在当前页创建"), this);
    createButton_->setObjectName(QStringLiteral("formDesignCreate"));
    box->addButton(createButton_, QDialogButtonBox::AcceptRole);
    layout->addWidget(box);

    connect(generateButton_, &QPushButton::clicked, this, &FormDesignAssistantDialog::requestDesign);
    connect(createButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (client_ != nullptr) {
        connect(client_, &ipc::WorkerClient::eventReceived, this,
                &FormDesignAssistantDialog::handleEvent);
        connect(client_, &ipc::WorkerClient::requestFailed, this,
                &FormDesignAssistantDialog::handleFailed);
    }
    createButton_->setEnabled(false);
}

void FormDesignAssistantDialog::setBusy(bool busy) {
    generateButton_->setEnabled(!busy);
    createButton_->setEnabled(!busy && !fields_.isEmpty());
    if (busy) status_->setText(tr("模型正在设计…"));
}

void FormDesignAssistantDialog::requestDesign() {
    const QString description = description_->toPlainText().trimmed();
    if (description.isEmpty()) {
        status_->setText(tr("先写一句描述。"));
        return;
    }
    if (client_ == nullptr) {
        status_->setText(tr("AI 服务还没连上。"));
        return;
    }
    // 只送用户写的这句描述，不送文档内容：设计表单不需要把原文发给模型。
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), pdf::designSystemPrompt());
    messages.append(system);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), description);
    messages.append(user);

    QJsonObject payload;
    payload.insert(QStringLiteral("task"), QStringLiteral("chat"));
    payload.insert(QStringLiteral("data_scope"), tr("表单设计描述"));
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("temperature"), 0.2);
    payload.insert(QStringLiteral("max_tokens"), 1200);

    answer_.clear();
    raw_->clear();
    setBusy(true);
    activeRequestId_ = client_->sendStreamRequest(QStringLiteral("ai.chat.stream"), payload);
    if (activeRequestId_.isEmpty()) {
        status_->setText(tr("AI 服务还没连上。"));
        setBusy(false);
    }
}

void FormDesignAssistantDialog::handleEvent(const QString& requestId, const QString& event,
                                             const QJsonObject& payload) {
    if (requestId != activeRequestId_) return;
    if (event == QLatin1String("ai.stream.delta")) {
        const QString text = payload.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) return;
        answer_ += text;
        raw_->setPlainText(answer_);
        return;
    }
    if (event == QLatin1String("ai.stream.done")) {
        setBusy(false);
        QString error;
        QVector<DesignedFormField> parsed;
        if (!pdf::parseDesignedFields(answer_, &parsed, &error)) {
            status_->setText(error);
            return;
        }
        fields_ = parsed;
        fillTable();
        status_->setText(tr("模型给出了 %1 个字段，改完再创建。").arg(fields_.size()));
        createButton_->setEnabled(true);
        return;
    }
    if (event == QLatin1String("ai.stream.error")) {
        setBusy(false);
        status_->setText(tr("模型调用失败：%1").arg(payload.value(QStringLiteral("message")).toString()));
        return;
    }
    if (event == QLatin1String("ai.stream.cancelled")) {
        setBusy(false);
        status_->setText(tr("（已停止）"));
    }
}

void FormDesignAssistantDialog::handleFailed(const QString& requestId, const QString& message) {
    if (requestId != activeRequestId_) return;
    setBusy(false);
    status_->setText(tr("AI 服务调用失败：%1").arg(message));
}

void FormDesignAssistantDialog::fillTable() {
    table_->setRowCount(fields_.size());
    for (int row = 0; row < fields_.size(); ++row) {
        const DesignedFormField& field = fields_.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(field.name));
        table_->setItem(row, 1, new QTableWidgetItem(field.toolTip));
        auto* kindItem = new QTableWidgetItem(pdf::kindName(field.kind));
        kindItem->setFlags(kindItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 2, kindItem);
        table_->setItem(row, 3, new QTableWidgetItem(QString::number(field.relX * 100.0, 'f', 1)));
        table_->setItem(row, 4, new QTableWidgetItem(QString::number(field.relY * 100.0, 'f', 1)));
        table_->setItem(row, 5, new QTableWidgetItem(QString::number(field.relWidth * 100.0, 'f', 1)));
        table_->setItem(row, 6, new QTableWidgetItem(QString::number(field.relHeight * 100.0, 'f', 1)));
    }
}

QVector<DesignedFormField> FormDesignAssistantDialog::fields() const {
    // 表格里的改动优先：用户在确认前改过的位置与提示要算数。
    QVector<DesignedFormField> result = fields_;
    for (int row = 0; row < table_->rowCount() && row < result.size(); ++row) {
        if (auto* item = table_->item(row, 0)) result[row].name = item->text().trimmed();
        if (auto* item = table_->item(row, 1)) result[row].toolTip = item->text().trimmed();
        auto percent = [this, row](int column, double fallback) {
            if (!table_->item(row, column)) return fallback;
            bool ok = false;
            const double value = table_->item(row, column)->text().trimmed().toDouble(&ok);
            if (!ok) return fallback;
            const double ratio = value / 100.0;
            if (ratio < 0.0 || ratio > 1.0) return fallback;
            return ratio;
        };
        result[row].relX = percent(3, result[row].relX);
        result[row].relY = percent(4, result[row].relY);
        result[row].relWidth = percent(5, result[row].relWidth);
        result[row].relHeight = percent(6, result[row].relHeight);
    }
    return result;
}

}  // namespace aipdf::app
