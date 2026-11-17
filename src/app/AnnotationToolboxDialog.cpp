#include "app/AnnotationToolboxDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

// 跨层文案统一走字面量 context：翻译目录里的 context 不依赖文件路径，
// 搬家或改名都不会丢词条。
}  // namespace

// ------------------------------------------------------------ 会计计算器 ----

CalculatorDialog::CalculatorDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("annotation.calculatorDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "会计计算器"));
    resize(420, 220);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    expressionEdit_ = new QLineEdit(this);
    expressionEdit_->setObjectName(QStringLiteral("annotation.calculator.expression"));
    expressionEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "算式"));
    expressionEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "例如 100+10%"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "算式:"), expressionEdit_);
    layout->addLayout(form);

    resultLabel_ = new QLabel(this);
    resultLabel_->setObjectName(QStringLiteral("annotation.calculator.result"));
    resultLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "计算结果"));
    resultLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(resultLabel_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("annotation.calculator.error"));
    errorLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "计算错误"));
    errorLabel_->setWordWrap(true);
    layout->addWidget(errorLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    insertButton_ = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "插入到当前页"), QDialogButtonBox::ActionRole);
    insertButton_->setObjectName(QStringLiteral("annotation.calculator.insert"));
    insertButton_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "插入到当前页"));
    layout->addWidget(buttons);

    connect(expressionEdit_, &QLineEdit::textChanged, this, &CalculatorDialog::refreshResult);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(insertButton_, &QPushButton::clicked, this, [this] {
        const QString text = annotationText();
        if (text.isEmpty()) {
            // 失败时不给批注：原因已经在界面上了。
            return;
        }
        emit insertRequested(text);
    });

    refreshResult();
}

QString CalculatorDialog::expression() const {
    return expressionEdit_ != nullptr ? expressionEdit_->text() : QString();
}

void CalculatorDialog::setExpression(const QString& expression) {
    if (expressionEdit_ != nullptr) {
        expressionEdit_->setText(expression);
    }
}

pdf::CalculationResult CalculatorDialog::evaluate() const {
    return pdf::evaluateExpression(expression());
}

QString CalculatorDialog::errorText() const {
    const pdf::CalculationResult result = evaluate();
    return result.ok ? QString() : result.error;
}

QString CalculatorDialog::normalizedExpression() const {
    const pdf::CalculationResult result = evaluate();
    return result.ok ? result.expression : QString();
}

QString CalculatorDialog::annotationText() const {
    const pdf::CalculationResult result = evaluate();
    if (!result.ok) {
        return QString();
    }
    return pdf::formatCalculationForAnnotation(result);
}

void CalculatorDialog::refreshResult() {
    if (resultLabel_ == nullptr || errorLabel_ == nullptr) {
        return;
    }
    const pdf::CalculationResult result = evaluate();
    if (result.ok) {
        resultLabel_->setText(i18n::tr("aipdf::app::MainWindow", "结果: %1 = %2").arg(result.expression, result.value));
        errorLabel_->clear();
        errorLabel_->setVisible(false);
    } else {
        resultLabel_->clear();
        errorLabel_->setText(result.error);
        errorLabel_->setVisible(true);
    }
    if (insertButton_ != nullptr) {
        // 算不出来时不让「插入」变成一条往文档里写错误的路。
        insertButton_->setEnabled(result.ok);
    }
}

// -------------------------------------------------------------- 符号面板 ----

SymbolPanelDialog::SymbolPanelDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("annotation.symbolPanelDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "符号面板"));
    resize(520, 420);

    auto* layout = new QVBoxLayout(this);

    auto* categoryRow = new QHBoxLayout;
    categoryRow->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "分类:"), this));
    categoryBox_ = new QComboBox(this);
    categoryBox_->setObjectName(QStringLiteral("annotation.symbols.category"));
    categoryBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "符号分类"));
    categoryRow->addWidget(categoryBox_, 1);
    layout->addLayout(categoryRow);

    symbolGrid_ = new QGridLayout;
    layout->addLayout(symbolGrid_);

    layout->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "插入位置（下面的文字就是当前的批注内容）:"), this));
    targetEdit_ = new QPlainTextEdit(this);
    targetEdit_->setObjectName(QStringLiteral("annotation.symbols.target"));
    targetEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "符号插入位置"));
    targetEdit_->setFixedHeight(90);
    layout->addWidget(targetEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* insertButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "插入到当前页"), QDialogButtonBox::ActionRole);
    insertButton->setObjectName(QStringLiteral("annotation.symbols.insert"));
    insertButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "插入到当前页"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(insertButton, &QPushButton::clicked, this, [this] {
        const QString content = targetEdit_->toPlainText();
        if (content.isEmpty()) {
            return;
        }
        emit insertRequested(content);
    });

    for (pdf::SymbolCategory category : categories()) {
        categoryBox_->addItem(pdf::symbolCategoryName(category),
                              static_cast<int>(category));
    }
    connect(categoryBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        setCategory(static_cast<pdf::SymbolCategory>(categoryBox_->itemData(index).toInt()));
    });
    setCategory(pdf::SymbolCategory::Currency);
}

QVector<pdf::SymbolCategory> SymbolPanelDialog::categories() const {
    return {pdf::SymbolCategory::Currency, pdf::SymbolCategory::Punctuation,
            pdf::SymbolCategory::Mathematics, pdf::SymbolCategory::Marks,
            pdf::SymbolCategory::Arrows};
}

pdf::SymbolCategory SymbolPanelDialog::currentCategory() const {
    if (categoryBox_ == nullptr || categoryBox_->currentIndex() < 0) {
        return pdf::SymbolCategory::Currency;
    }
    return static_cast<pdf::SymbolCategory>(categoryBox_->currentData().toInt());
}

void SymbolPanelDialog::setCategory(pdf::SymbolCategory category) {
    if (categoryBox_ == nullptr) {
        return;
    }
    const int index = categoryBox_->findData(static_cast<int>(category));
    if (index >= 0 && index != categoryBox_->currentIndex()) {
        categoryBox_->setCurrentIndex(index);
        return;
    }
    rebuildSymbols();
}

QVector<pdf::SymbolEntry> SymbolPanelDialog::currentEntries() const {
    return pdf::symbolCatalog(currentCategory());
}

int SymbolPanelDialog::symbolButtonCount() const {
    return symbolButtons_.size();
}

QString SymbolPanelDialog::targetText() const {
    return targetEdit_ != nullptr ? targetEdit_->toPlainText() : QString();
}

void SymbolPanelDialog::setTargetText(const QString& text) {
    if (targetEdit_ != nullptr) {
        targetEdit_->setPlainText(text);
    }
}

int SymbolPanelDialog::targetCursorPosition() const {
    return targetEdit_ != nullptr ? targetEdit_->textCursor().position() : 0;
}

void SymbolPanelDialog::setTargetCursorPosition(int position) {
    if (targetEdit_ == nullptr) {
        return;
    }
    QTextCursor cursor = targetEdit_->textCursor();
    cursor.setPosition(qBound(0, position, targetEdit_->toPlainText().size()));
    targetEdit_->setTextCursor(cursor);
}

QString SymbolPanelDialog::insertSymbolAt(int index) {
    if (index < 0 || index >= entries_.size()) {
        return targetText();
    }
    const pdf::SymbolEntry& entry = entries_.at(index);
    const QString updated = pdf::insertSymbolIntoText(targetText(), targetCursorPosition(), entry);
    setTargetText(updated);
    // 光标跟着插入点往后走：连着点两个符号，第二个要排在第一个后面。
    setTargetCursorPosition(targetCursorPosition() + entry.symbol.size());
    return updated;
}

QString SymbolPanelDialog::symbolAnnotationText(int index) const {
    if (index < 0 || index >= entries_.size()) {
        return QString();
    }
    return pdf::formatSymbolForAnnotation(entries_.at(index));
}

void SymbolPanelDialog::rebuildSymbols() {
    if (symbolGrid_ == nullptr) {
        return;
    }
    // 先清掉上一批按钮：分类切换后条目数会变，旧的按钮必须一起走。
    for (QToolButton* button : symbolButtons_) {
        symbolGrid_->removeWidget(button);
        button->deleteLater();
    }
    symbolButtons_.clear();
    entries_ = pdf::symbolCatalog(currentCategory());

    int column = 0;
    int row = 0;
    for (int index = 0; index < entries_.size(); ++index) {
        const pdf::SymbolEntry& entry = entries_.at(index);
        auto* button = new QToolButton(symbolGrid_->parentWidget());
        button->setText(entry.symbol);
        button->setToolTip(entry.name);
        button->setAccessibleName(entry.name);
        button->setObjectName(QStringLiteral("annotation.symbols.entry.%1").arg(index));
        button->setMinimumSize(44, 36);
        connect(button, &QToolButton::clicked, this, [this, index] {
            insertSymbolAt(index);
            // 插完仍留在面板里：符号面板就是要连着插的。
        });
        symbolGrid_->addWidget(button, row, column);
        symbolButtons_.append(button);
        ++column;
        if (column >= 6) {
            column = 0;
            ++row;
        }
    }
}

}  // namespace aipdf::app
