#include "app/FormDesignAssistantDialog.h"

#include "ipc/WorkerClient.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

// 模型看到的系统提示词。
//
// 为什么要求"只输出 JSON"：我们要的是一份能落盘的字段清单，不是散文。
// 让模型自己套代码块、加解释，解析就得写一堆容错——所以在提示词里就把格式钉死。
QString designSystemPrompt() {
    return QStringLiteral(
        "你是一个 PDF 表单设计助手。只输出一个 JSON 数组，不要任何解释文字，也不要用代码块包裹。\n"
        "数组的每一项形如：\n"
        "{\"name\":\"字段名（英文或中文均可，但不能为空）\","
        "\"tooltip\":\"给填表人看的提示\","
        "\"kind\":\"text 或 checkbox 或 radio 或 combo 或 list 或 button\","
        "\"x\":0.1,\"y\":0.2,\"width\":0.3,\"height\":0.03}\n"
        "x、y、width、height 都是相对页面尺寸的比例，取 0 到 1 之间的小数；"
        "y 从页面顶部往下算。同一行的字段 y 相同、x 依次增大。");
}

// 模型的种类名 → 建域用的类型。
pdf::FormFieldKind kindOf(const QString& name) {
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("checkbox")) return pdf::FormFieldKind::CheckBox;
    if (key == QLatin1String("radio")) return pdf::FormFieldKind::RadioButton;
    if (key == QLatin1String("combo")) return pdf::FormFieldKind::ComboBox;
    if (key == QLatin1String("list")) return pdf::FormFieldKind::ListBox;
    if (key == QLatin1String("button")) return pdf::FormFieldKind::PushButton;
    return pdf::FormFieldKind::Text;
}

QString kindName(pdf::FormFieldKind kind) {
    switch (kind) {
        case pdf::FormFieldKind::CheckBox:
            return QStringLiteral("checkbox");
        case pdf::FormFieldKind::RadioButton:
            return QStringLiteral("radio");
        case pdf::FormFieldKind::ComboBox:
            return QStringLiteral("combo");
        case pdf::FormFieldKind::ListBox:
            return QStringLiteral("list");
        case pdf::FormFieldKind::PushButton:
            return QStringLiteral("button");
        default:
            return QStringLiteral("text");
    }
}

// 把模型可能加上的代码块围栏去掉。
QString stripCodeFence(const QString& text) {
    QString result = text.trimmed();
    if (!result.startsWith(QStringLiteral("```"))) return result;
    const int firstNewline = result.indexOf(QLatin1Char('\n'));
    if (firstNewline < 0) return result;
    const int lastFence = result.lastIndexOf(QStringLiteral("```"));
    if (lastFence <= firstNewline) return result;
    return result.mid(firstNewline + 1, lastFence - firstNewline - 1).trimmed();
}

}  // namespace

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
    system.insert(QStringLiteral("content"), designSystemPrompt());
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
        if (!parseAnswer(answer_, &parsed, &error)) {
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

bool FormDesignAssistantDialog::parseAnswer(const QString& answer,
                                            QVector<DesignedFormField>* out, QString* error) {
    const QString text = stripCodeFence(answer);
    const int begin = text.indexOf(QLatin1Char('['));
    const int end = text.lastIndexOf(QLatin1Char(']'));
    if (begin < 0 || end <= begin) {
        if (error != nullptr) *error = tr("模型没有返回可用的 JSON 字段清单。");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(text.mid(begin, end - begin + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error != nullptr) *error = tr("模型返回的字段清单解析失败：%1").arg(parseError.errorString());
        return false;
    }
    QVector<DesignedFormField> fields;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        DesignedFormField field;
        field.name = name;
        field.toolTip = object.value(QStringLiteral("tooltip")).toString().trimmed();
        field.kind = kindOf(object.value(QStringLiteral("kind")).toString());
        field.relX = object.value(QStringLiteral("x")).toDouble(0.1);
        field.relY = object.value(QStringLiteral("y")).toDouble(0.1);
        field.relWidth = object.value(QStringLiteral("width")).toDouble(0.3);
        field.relHeight = object.value(QStringLiteral("height")).toDouble(0.03);
        // 比例必须落在 0..1：模型偶尔会给出 85 这种百分数或负数。
        auto clamp = [](double value, double fallback) {
            if (value < 0.0 || value > 1.0) return fallback;
            return value;
        };
        field.relX = clamp(field.relX, 0.1);
        field.relY = clamp(field.relY, 0.1);
        field.relWidth = clamp(field.relWidth, 0.3);
        field.relHeight = clamp(field.relHeight, 0.03);
        fields.append(field);
    }
    if (fields.isEmpty()) {
        if (error != nullptr) *error = tr("模型没有给出任何字段。");
        return false;
    }
    *out = fields;
    return true;
}

void FormDesignAssistantDialog::fillTable() {
    table_->setRowCount(fields_.size());
    for (int row = 0; row < fields_.size(); ++row) {
        const DesignedFormField& field = fields_.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(field.name));
        table_->setItem(row, 1, new QTableWidgetItem(field.toolTip));
        auto* kindItem = new QTableWidgetItem(kindName(field.kind));
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
