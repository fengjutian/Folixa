#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "pdf/forms/FormScriptStore.h"

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace aipdf::app {

// 参与脚本运算的字段。
//
// 为什么只传纯数据：对话框不依赖 PdfView，也就不用把整个视图拖进来；
// 主窗口负责取数、取回结果并写回文档。
struct FormScriptField {
    QString name;
    int pageIndex = 0;
    QString value;
    bool readOnly = false;
};

// 「JavaScript」对话框：写一段脚本，跑一次，把结果写回目标字段。
class FormScriptDialog final : public QDialog {
    Q_OBJECT

public:
    explicit FormScriptDialog(const QVector<FormScriptField>& fields, QWidget* parent = nullptr);

    // 目标字段名（空串表示"只看结果，不写回"）。
    QString targetField() const;
    // 最近一次「运行」算出来的值。
    QString resultValue() const;

private:
    void runScript();

    QVector<FormScriptField> fields_;
    QComboBox* target_ = nullptr;
    QPlainTextEdit* script_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QString result_;
};

// 「计算次序」对话框：列出各字段的计算脚本，调整次序，按次序执行一遍。
//
// 次序与脚本都来自 PDF 本身（AcroForm /CO 与字段的 /AA /C），
// 用户改完可以写回文件，让别的阅读器也按同样的次序算。
class FormCalculationDialog final : public QDialog {
    Q_OBJECT

public:
    FormCalculationDialog(const QVector<FormScriptField>& fields,
                           const QVector<pdf::FormFieldScript>& scripts,
                           const QStringList& order,
                           QWidget* parent = nullptr);

    // 执行结果：字段名 → 新值。
    QVector<QPair<QString, QString>> results() const;
    // 用户排好的次序（字段名）。
    QStringList newOrder() const;
    // 用户改过的脚本。
    QVector<pdf::FormFieldScript> scripts() const;

private:
    void moveRow(int delta);
    void runAll();
    void renumber();
    // 某个字段当前的计算脚本（来自 PDF 里的 /AA /C）。
    QString scriptOf(const QString& name) const;

    QVector<FormScriptField> fields_;
    QVector<pdf::FormFieldScript> scripts_;
    QTableWidget* table_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QPushButton* downButton_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QVector<QPair<QString, QString>> results_;
};

}  // namespace aipdf::app
