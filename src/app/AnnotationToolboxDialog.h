#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "pdf/annot/AnnotationToolkit.h"

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace aipdf::app {

// 会计计算器：算一条算式，把结果整理成能贴进批注的文本。
//
// 这一层只做界面：求值、百分号规则、精度与「失败时不给 0 冒充成功」
// 全部由 pdf::evaluateExpression 决定，界面不自己算一遍。
class CalculatorDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CalculatorDialog(QWidget* parent = nullptr);

    // 输入框里的算式。
    QString expression() const;
    void setExpression(const QString& expression);

    // 求值结果（每次都重新算，不缓存）。
    pdf::CalculationResult evaluate() const;

    // 失败时给用户看的中文原因；成功时是空串。
    QString errorText() const;
    // 成功时规范化后的算式；失败时是空串。
    QString normalizedExpression() const;
    // 能贴进批注的文本。
    //
    // 失败时是**空串**：失败原因已经在 errorText 里，
    // 把错误塞进批注正文只会污染文档。
    QString annotationText() const;

signals:
    // 用户确认，要把这段文字插到当前页。
    void insertRequested(const QString& text);

private:
    void refreshResult();

    QLineEdit* expressionEdit_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QPushButton* insertButton_ = nullptr;
};

// 符号面板：按分类列出常用符号，点一下插到当前光标处。
//
// 「插完仍留在面板里」是这个面板存在的理由：符号往往要连着插几个，
// 每次插完就关面板等于白做。
class SymbolPanelDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SymbolPanelDialog(QWidget* parent = nullptr);

    // 当前分类。
    pdf::SymbolCategory currentCategory() const;
    void setCategory(pdf::SymbolCategory category);
    // 面板上列出的分类（顺序固定）。
    QVector<pdf::SymbolCategory> categories() const;

    // 当前分类下的符号（直接来自领域层的目录，不另存一份）。
    QVector<pdf::SymbolEntry> currentEntries() const;
    // 面板上显示的符号按钮数量。
    int symbolButtonCount() const;

    // 目标文字与光标位置。
    QString targetText() const;
    void setTargetText(const QString& text);
    int targetCursorPosition() const;
    void setTargetCursorPosition(int position);

    // 点第 index 个符号：按领域层的规则插到光标处，返回插完之后的文字。
    QString insertSymbolAt(int index);
    // 第 index 个符号整理成的批注文本（只放符号本身）。
    QString symbolAnnotationText(int index) const;

signals:
    // 用户确认，要把这段文字插到当前页。
    void insertRequested(const QString& text);

private:
    void rebuildSymbols();

    QComboBox* categoryBox_ = nullptr;
    QGridLayout* symbolGrid_ = nullptr;
    QPlainTextEdit* targetEdit_ = nullptr;
    QVector<pdf::SymbolEntry> entries_;
    QVector<QToolButton*> symbolButtons_;
};

}  // namespace aipdf::app
