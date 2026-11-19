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
namespace {

// 识别出来的类型 → 建域用的类型。
//
// 数字与日期在 AcroForm 里都还是文本域（数字靠格式、日期靠校验），
// 所以这里只多认一个勾选框，其余一律当文本域。
pdf::FormFieldKind kindFromDetected(pdf::DetectedFieldKind kind) {
    if (kind == pdf::DetectedFieldKind::CheckBox) return pdf::FormFieldKind::CheckBox;
    return pdf::FormFieldKind::Text;
}

}  // namespace

FormDesignAssistantDialog::FormDesignAssistantDialog(ipc::WorkerClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
    buildUi();
}

FormDesignAssistantDialog::FormDesignAssistantDialog(ipc::WorkerClient* client, QWidget* parent,
                                                     const QVector<QSizeF>& pageSizes,
                                                     int firstPageIndex)
    : QDialog(parent),
      client_(client),
      pageSizes_(pageSizes),
      firstPageIndex_(firstPageIndex) {
    buildUi();
}

void FormDesignAssistantDialog::buildUi() {
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
    if (hasLayout()) {
        // 有页面尺寸：显示算出来的版面（页号 + 绝对位置，点）。
        table_->setColumnCount(8);
        table_->setHorizontalHeaderLabels(QStringList{ tr("字段名"), tr("提示"), tr("类型"), tr("页"),
                                                       tr("左"), tr("上"), tr("宽"), tr("高") });
    } else {
        // 没有页面尺寸：退回老样子，只显示模型给的比例。
        table_->setColumnCount(7);
        table_->setHorizontalHeaderLabels(QStringList{ tr("字段名"), tr("提示"), tr("类型"), tr("左 %"),
                                                       tr("上 %"), tr("宽 %"), tr("高 %") });
    }
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
    createButton_ = new QPushButton(hasLayout() ? tr("创建字段") : tr("在当前页创建"), this);
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
        // 模型给的是清单，位置由这一层算：模型自己给的坐标不再直接用。
        detectedMode_ = false;
        fixedPlacements_.clear();
        rebuildPlacements();
        fillTable();
        if (extraPagesNeeded_ > 0) {
            status_->setText(tr("模型给出了 %1 个字段，要铺 %2 页（还差 %3 页），改完再创建。")
                                 .arg(fields_.size())
                                 .arg(placements_.isEmpty() ? 1 : placements_.last().pageOffset + 1)
                                 .arg(extraPagesNeeded_));
        } else {
            status_->setText(tr("模型给出了 %1 个字段，改完再创建。").arg(fields_.size()));
        }
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

void FormDesignAssistantDialog::rebuildPlacements() {
    placements_.clear();
    extraPagesNeeded_ = 0;
    // 没有页面尺寸就排不了版面：这时只有模型给的比例坐标可用（老行为）。
    if (!hasLayout()) return;
    if (detectedMode_) {
        // 识别出来的候选位置是固定的：重排会把"线框在哪"这条信息丢掉。
        placements_ = fixedPlacements_;
        return;
    }
    const pdf::FormLayoutResult layout = pdf::layoutDesignedFields(fields_, pageSizes_, firstPageIndex_);
    placements_ = layout.placements;
    extraPagesNeeded_ = layout.extraPagesNeeded;
}

QSizeF FormDesignAssistantDialog::pageSizeOf(int pageIndex) const {
    const int offset = pageIndex - firstPageIndex_;
    if (offset < 0 || offset >= pageSizes_.size()) return {};
    return pageSizes_.at(offset);
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

        if (hasLayout() && row < placements_.size()) {
            const pdf::PlacedFormField& placement = placements_.at(row);
            const QSizeF pageSize = pageSizeOf(placement.pageIndex);
            // 页号按人看的习惯从 1 起。
            auto* pageItem = new QTableWidgetItem(QString::number(placement.pageIndex + 1));
            pageItem->setFlags(pageItem->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, 3, pageItem);
            // 位置是算出来的，不让改：改了就不再是那个版面了。
            const double top = pageSize.isEmpty() ? placement.rect.bottom()
                                                  : pageSize.height() - placement.rect.bottom();
            const double left = placement.rect.left();
            const double values[4] = {left, top, placement.rect.width(), placement.rect.height()};
            for (int column = 0; column < 4; ++column) {
                auto* item = new QTableWidgetItem(QString::number(values[column], 'f', 1));
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table_->setItem(row, column + 4, item);
            }
            continue;
        }

        table_->setItem(row, 3, new QTableWidgetItem(QString::number(field.relX * 100.0, 'f', 1)));
        table_->setItem(row, 4, new QTableWidgetItem(QString::number(field.relY * 100.0, 'f', 1)));
        table_->setItem(row, 5, new QTableWidgetItem(QString::number(field.relWidth * 100.0, 'f', 1)));
        table_->setItem(row, 6, new QTableWidgetItem(QString::number(field.relHeight * 100.0, 'f', 1)));
    }
}

void FormDesignAssistantDialog::loadCandidates(const QVector<pdf::DetectedFormField>& candidates) {
    fields_.clear();
    fixedPlacements_.clear();
    detectedMode_ = true;
    for (const pdf::DetectedFormField& candidate : candidates) {
        DesignedFormField field;
        field.name = candidate.name;
        field.toolTip = candidate.label;
        field.kind = kindFromDetected(candidate.kind);
        fields_.append(field);

        pdf::PlacedFormField placement;
        placement.pageIndex = candidate.pageIndex;
        placement.pageOffset = candidate.pageIndex - firstPageIndex_;
        placement.name = candidate.name;
        placement.toolTip = candidate.label;
        placement.kind = field.kind;
        placement.rect = candidate.rect;
        fixedPlacements_.append(placement);
    }
    rebuildPlacements();
    fillTable();
    createButton_->setEnabled(!fields_.isEmpty());
    status_->setText(tr("识别出 %1 个候选字段，改完再创建。").arg(fields_.size()));
}

QVector<DesignedFormField> FormDesignAssistantDialog::fields() const {
    // 表格里的改动优先：用户在确认前改过的名字与提示要算数。
    QVector<DesignedFormField> result = fields_;
    for (int row = 0; row < table_->rowCount() && row < result.size(); ++row) {
        if (auto* item = table_->item(row, 0)) result[row].name = item->text().trimmed();
        if (auto* item = table_->item(row, 1)) result[row].toolTip = item->text().trimmed();
    }
    if (!hasLayout()) {
        // 老路径：位置就是模型给的比例，表格里改过就按改后的算。
        for (int row = 0; row < table_->rowCount() && row < result.size(); ++row) {
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
    // 排了版：把版面折算回相对坐标，老的调用点（按页面尺寸换算）照样能用。
    // 跨页的字段要用 placements() 拿页号——比例换算只对所在页成立。
    if (placements_.size() == result.size()) {
        for (int index = 0; index < result.size(); ++index) {
            const pdf::PlacedFormField& placement = placements_.at(index);
            const QSizeF pageSize = pageSizeOf(placement.pageIndex);
            if (pageSize.isEmpty() || pageSize.width() <= 0.0 || pageSize.height() <= 0.0) continue;
            result[index].relX = placement.rect.left() / pageSize.width();
            result[index].relWidth = placement.rect.width() / pageSize.width();
            result[index].relHeight = placement.rect.height() / pageSize.height();
            result[index].relY = (pageSize.height() - placement.rect.bottom()) / pageSize.height();
        }
    }
    return result;
}

QVector<pdf::PlacedFormField> FormDesignAssistantDialog::placements() const {
    return placements_;
}

int FormDesignAssistantDialog::extraPagesNeeded() const {
    return extraPagesNeeded_;
}

}  // namespace aipdf::app
