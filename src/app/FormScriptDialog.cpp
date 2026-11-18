#include "app/FormScriptDialog.h"

#include "pdf/forms/FormJsEvaluator.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

// 把字段列表转成求值器要的上下文。
QVector<pdf::FormFieldValue> toValues(const QVector<FormScriptField>& fields) {
    QVector<pdf::FormFieldValue> values;
    values.reserve(fields.size());
    for (const FormScriptField& field : fields)
        values.append(pdf::FormFieldValue{field.name, field.value});
    return values;
}

QString valueOf(const QVector<FormScriptField>& fields, const QString& name) {
    for (const FormScriptField& field : fields) {
        if (field.name == name) return field.value;
    }
    return QString();
}

}  // namespace

FormScriptDialog::FormScriptDialog(const QVector<FormScriptField>& fields, QWidget* parent)
    : QDialog(parent), fields_(fields) {
    setWindowTitle(tr("运行表单脚本"));
    resize(560, 420);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    target_ = new QComboBox(this);
    // 第一项是"不写回"：脚本有时只是想看结果，不必落到字段上。
    target_->addItem(tr("（只算不写回）"), QString());
    for (const FormScriptField& field : fields_) {
        if (field.readOnly) continue;
        target_->addItem(field.name, field.name);
    }
    form->addRow(tr("结果写入字段"), target_);

    script_ = new QPlainTextEdit(this);
    script_->setObjectName(QStringLiteral("formScriptEditor"));
    script_->setPlaceholderText(
        tr("写一段脚本，例如：\nAFNumber_Format(2, event.value, 0, 0, \".\", false)"));
    form->addRow(tr("脚本"), script_);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    runButton_ = new QPushButton(tr("运行"), this);
    runButton_->setObjectName(QStringLiteral("formScriptRun"));
    buttons->addWidget(runButton_);
    buttons->addStretch(1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addWidget(box);
    layout->addLayout(buttons);

    output_ = new QPlainTextEdit(this);
    output_->setObjectName(QStringLiteral("formScriptOutput"));
    output_->setReadOnly(true);
    output_->setPlaceholderText(tr("运行结果会显示在这里。"));
    layout->addWidget(output_, 1);

    connect(runButton_, &QPushButton::clicked, this, &FormScriptDialog::runScript);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void FormScriptDialog::runScript() {
    const QString script = script_->toPlainText().trimmed();
    if (script.isEmpty()) {
        output_->setPlainText(tr("先写一段脚本再运行。"));
        result_.clear();
        return;
    }
    const QString target = target_->currentData().toString();
    const pdf::FormJsResult result =
        pdf::evaluateFormScript(script, toValues(fields_), target, valueOf(fields_, target));
    if (!result.ok) {
        output_->setPlainText(tr("脚本没跑通：%1").arg(result.error));
        result_.clear();
        return;
    }
    result_ = result.value;
    QString text = tr("结果：%1").arg(result.value);
    // 求值器把"引用了不存在的字段"这类提醒放在 error 里，但它不是失败。
    if (!result.error.isEmpty()) text += QLatin1Char('\n') + tr("提示：%1").arg(result.error);
    output_->setPlainText(text);
}

QString FormScriptDialog::targetField() const {
    return target_->currentData().toString();
}

QString FormScriptDialog::resultValue() const {
    return result_;
}

FormCalculationDialog::FormCalculationDialog(const QVector<FormScriptField>& fields,
                                             const QVector<pdf::FormFieldScript>& scripts,
                                             const QStringList& order, QWidget* parent)
    : QDialog(parent), fields_(fields), scripts_(scripts) {
    setWindowTitle(tr("计算次序"));
    resize(720, 460);

    auto* layout = new QVBoxLayout(this);
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("formCalculationTable"));
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        QStringList{ tr("顺序"), tr("字段"), tr("计算脚本"), tr("执行结果") });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);

    // 次序先按 PDF 里的 /CO 排，没写进 /CO 的字段接在后面（按字段顺序）。
    QStringList names;
    for (const QString& name : order) {
        if (!names.contains(name)) names.append(name);
    }
    for (const FormScriptField& field : fields_) {
        if (!names.contains(field.name)) names.append(field.name);
    }
    table_->setRowCount(names.size());
    for (int row = 0; row < names.size(); ++row) {
        const QString& name = names.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, nameItem);
        table_->setItem(row, 2, new QTableWidgetItem(scriptOf(name)));
        table_->setItem(row, 3, new QTableWidgetItem(QString()));
    }
    layout->addWidget(table_, 1);

    auto* buttons = new QHBoxLayout();
    upButton_ = new QPushButton(tr("上移"), this);
    upButton_->setObjectName(QStringLiteral("formCalculationUp"));
    downButton_ = new QPushButton(tr("下移"), this);
    downButton_->setObjectName(QStringLiteral("formCalculationDown"));
    runButton_ = new QPushButton(tr("执行全部"), this);
    runButton_->setObjectName(QStringLiteral("formCalculationRun"));
    buttons->addWidget(upButton_);
    buttons->addWidget(downButton_);
    buttons->addWidget(runButton_);
    buttons->addStretch(1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addWidget(box);
    layout->addLayout(buttons);

    connect(upButton_, &QPushButton::clicked, this, [this]() { moveRow(-1); });
    connect(downButton_, &QPushButton::clicked, this, [this]() { moveRow(1); });
    connect(runButton_, &QPushButton::clicked, this, &FormCalculationDialog::runAll);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void FormCalculationDialog::moveRow(int delta) {
    const int row = table_->currentRow();
    if (row < 0) return;
    const int target = row + delta;
    if (target < 0 || target >= table_->rowCount()) return;
    // 交换两行的内容：QTableWidget 没有"移动行"的接口。
    for (int column = 0; column < table_->columnCount(); ++column) {
        QTableWidgetItem* current = table_->takeItem(row, column);
        QTableWidgetItem* other = table_->takeItem(target, column);
        table_->setItem(row, column, other);
        table_->setItem(target, column, current);
    }
    table_->setCurrentCell(target, 0);
    renumber();
}

void FormCalculationDialog::renumber() {
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (auto* item = table_->item(row, 0)) item->setText(QString::number(row + 1));
    }
}

QString FormCalculationDialog::scriptOf(const QString& name) const {
    for (const pdf::FormFieldScript& script : scripts_) {
        if (script.fieldName == name) return script.calculate;
    }
    return QString();
}

void FormCalculationDialog::runAll() {
    results_.clear();
    QVector<pdf::FormFieldValue> values = toValues(fields_);
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString name = table_->item(row, 1) != nullptr ? table_->item(row, 1)->text()
                                                             : QString();
        const QString script =
            table_->item(row, 2) != nullptr ? table_->item(row, 2)->text() : QString();
        if (name.isEmpty() || script.trimmed().isEmpty()) {
            if (auto* item = table_->item(row, 3)) item->setText(QString());
            continue;
        }
        // 逐个算：后面的字段要用到前面刚算出来的值，所以边算边更新上下文。
        const pdf::FormJsResult result =
            pdf::evaluateFormScript(script, values, name, valueOf(fields_, name));
        if (!result.ok) {
            if (auto* item = table_->item(row, 3)) item->setText(result.error);
            continue;
        }
        for (pdf::FormFieldValue& value : values) {
            if (value.name == name) value.value = result.value;
        }
        results_.append({name, result.value});
        if (auto* item = table_->item(row, 3)) {
            item->setText(result.error.isEmpty() ? result.value
                                                 : result.value + QLatin1Char('\n') + result.error);
        }
    }
}

QVector<QPair<QString, QString>> FormCalculationDialog::results() const {
    return results_;
}

QStringList FormCalculationDialog::newOrder() const {
    QStringList order;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (auto* item = table_->item(row, 1)) order.append(item->text());
    }
    return order;
}

QVector<pdf::FormFieldScript> FormCalculationDialog::scripts() const {
    QVector<pdf::FormFieldScript> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString name = table_->item(row, 1) != nullptr ? table_->item(row, 1)->text()
                                                             : QString();
        if (name.isEmpty()) continue;
        pdf::FormFieldScript script;
        script.fieldName = name;
        script.calculate = table_->item(row, 2) != nullptr ? table_->item(row, 2)->text()
                                                            : QString();
        result.append(script);
    }
    return result;
}

}  // namespace aipdf::app
