#include "pdf/AnnotationToolkit.h"

#include <QSet>
#include <QTest>

#include <cmath>

using namespace aipdf::pdf;

namespace {

// 浮点比较：金额类的数不该按位相等，用容差比。
bool nearlyEqual(double actual, double expected) {
    return std::abs(actual - expected) < 1e-9;
}

// 造一个已知条目，测格式化时不用去翻目录。
SymbolEntry checkEntry() {
    SymbolEntry entry;
    entry.symbol = QStringLiteral("✓");
    entry.name = QStringLiteral("通过");
    entry.category = SymbolCategory::Marks;
    return entry;
}

}  // namespace

class AnnotationToolkitTests final : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- 计算器 ----
    void calculatorAddsTwoNumbers();
    void calculatorKeepsOperatorPrecedence();
    void calculatorHonorsParentheses();
    void calculatorComputesFractionalDivision();
    void calculatorTreatsTrailingPercentAsHundredth();
    void calculatorTreatsPercentInAdditiveChainAsShareOfBase();
    void calculatorAcceptsThousandSeparators();
    void calculatorRejectsDivisionByZero();
    void calculatorRejectsIncompleteExpression();
    void calculatorRejectsUnclosedParenthesis();
    void calculatorRejectsMisplacedOperator();
    void calculatorRejectsEmptyExpression();
    void calculatorFormatsResultWithoutTrailingZeros();
    void calculatorRoundsToRequestedDecimals();
    void calculatorNormalizesExpression();
    void calculatorHandlesNegativeNumbers();
    void calculatorTreatsPercentInMultiplicativeChainAsHundredth();
    void calculatorRejectsBadThousandSeparator();
    void calculatorRejectsDoubleDecimalPoint();

    // ---------------------------------------------------------- 计算器输出 ----
    void formatCalculationProducesAnnotationText();
    void formatCalculationReturnsEmptyOnFailure();

    // ---------------------------------------------------------- 符号面板 ----
    void everySymbolCategoryIsPopulated();
    void symbolCatalogHasNoDuplicates();
    void symbolCatalogTotalIsLargeEnough();
    void formatSymbolWithoutNameKeepsSymbolOnly();
    void formatSymbolWithNameAppendsChineseName();
    void unknownSymbolCategoryReturnsEmpty();
    void wholeSymbolCatalogMatchesCategorySum();
    void symbolCategoryNamesAreNotEmpty();
    void insertSymbolIntoTextInsertsAtPosition();
    void insertSymbolIntoTextKeepsSurrogatePairsIntact();

    // ---------------------------------------------------------- 连续批注 ----
    void continuousModeIsInactiveByDefault();
    void continuousModeAfterPlacementIsTrueWhenActive();
    void continuousModeKeepsActivatedTool();
    void continuousModeDeactivateClearsState();
    void continuousModeExitsToolThatDoesNotRepeat();
    void continuousModeIgnoresEmptyToolId();
    void continuousModeReportsRepeatFlag();
};

// ---------------------------------------------------------- 计算器 ----

void AnnotationToolkitTests::calculatorAddsTwoNumbers() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1+2"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("3"));
    QVERIFY(nearlyEqual(result.numericValue, 3.0));
    QVERIFY(result.error.isEmpty());
}

void AnnotationToolkitTests::calculatorKeepsOperatorPrecedence() {
    // 2*3+4 = 10：先乘除后加减，不能算成 2*(3+4)。
    const CalculationResult result = evaluateExpression(QStringLiteral("2*3+4"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("10"));
}

void AnnotationToolkitTests::calculatorHonorsParentheses() {
    const CalculationResult result = evaluateExpression(QStringLiteral("(2+3)*4"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("20"));
}

void AnnotationToolkitTests::calculatorComputesFractionalDivision() {
    const CalculationResult result = evaluateExpression(QStringLiteral("10/4"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("2.5"));
}

void AnnotationToolkitTests::calculatorTreatsTrailingPercentAsHundredth() {
    // 单独一个百分号没有基数可参照，按除以 100 处理。
    const CalculationResult result = evaluateExpression(QStringLiteral("50%"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("0.5"));
}

void AnnotationToolkitTests::calculatorTreatsPercentInAdditiveChainAsShareOfBase() {
    // 加法链里的百分号是"基数的百分比"：100 + 10% × 100 = 110。
    const CalculationResult result = evaluateExpression(QStringLiteral("100+10%"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("110"));

    const CalculationResult minus = evaluateExpression(QStringLiteral("100-10%"));
    QVERIFY(minus.ok);
    QCOMPARE(minus.value, QStringLiteral("90"));

    const CalculationResult smaller = evaluateExpression(QStringLiteral("50+10%"));
    QVERIFY(smaller.ok);
    QCOMPARE(smaller.value, QStringLiteral("55"));
}

void AnnotationToolkitTests::calculatorAcceptsThousandSeparators() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1,234.56"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("1234.56"));
    QVERIFY(nearlyEqual(result.numericValue, 1234.56));
}

void AnnotationToolkitTests::calculatorRejectsDivisionByZero() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1/0"));
    // 关键：除以 0 不能返回 0 冒充成功。
    QVERIFY(!result.ok);
    QVERIFY(result.value.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("除数不能为零")));
}

void AnnotationToolkitTests::calculatorRejectsIncompleteExpression() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1+"));
    QVERIFY(!result.ok);
    QVERIFY(result.value.isEmpty());
    QVERIFY(!result.error.isEmpty());
}

void AnnotationToolkitTests::calculatorRejectsUnclosedParenthesis() {
    const CalculationResult result = evaluateExpression(QStringLiteral("(1+2"));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void AnnotationToolkitTests::calculatorRejectsMisplacedOperator() {
    const CalculationResult result = evaluateExpression(QStringLiteral("*/2"));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void AnnotationToolkitTests::calculatorRejectsEmptyExpression() {
    const CalculationResult result = evaluateExpression(QStringLiteral("   "));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void AnnotationToolkitTests::calculatorFormatsResultWithoutTrailingZeros() {
    const CalculationResult half = evaluateExpression(QStringLiteral("2.50"));
    QVERIFY(half.ok);
    QCOMPARE(half.value, QStringLiteral("2.5"));

    const CalculationResult whole = evaluateExpression(QStringLiteral("2.00"));
    QVERIFY(whole.ok);
    QCOMPARE(whole.value, QStringLiteral("2"));

    const CalculationResult zero = evaluateExpression(QStringLiteral("0.00"));
    QVERIFY(zero.ok);
    QCOMPARE(zero.value, QStringLiteral("0"));
}

void AnnotationToolkitTests::calculatorRoundsToRequestedDecimals() {
    const CalculationResult two = evaluateExpression(QStringLiteral("1/3"), 2);
    QVERIFY(two.ok);
    QCOMPARE(two.value, QStringLiteral("0.33"));

    const CalculationResult zero = evaluateExpression(QStringLiteral("1/3"), 0);
    QVERIFY(zero.ok);
    QCOMPARE(zero.value, QStringLiteral("0"));

    const CalculationResult four = evaluateExpression(QStringLiteral("1/3"), 4);
    QVERIFY(four.ok);
    QCOMPARE(four.value, QStringLiteral("0.3333"));
}

void AnnotationToolkitTests::calculatorNormalizesExpression() {
    // 规范化 = 去空白、去千分位逗号、去前导零。
    const CalculationResult spaced = evaluateExpression(QStringLiteral(" 1,234.56 "));
    QVERIFY(spaced.ok);
    QCOMPARE(spaced.expression, QStringLiteral("1234.56"));

    const CalculationResult padded = evaluateExpression(QStringLiteral("2 * 3 + 4"));
    QVERIFY(padded.ok);
    QCOMPARE(padded.expression, QStringLiteral("2*3+4"));

    const CalculationResult leadingZeros = evaluateExpression(QStringLiteral("007+1"));
    QVERIFY(leadingZeros.ok);
    QCOMPARE(leadingZeros.expression, QStringLiteral("7+1"));
}

void AnnotationToolkitTests::calculatorHandlesNegativeNumbers() {
    const CalculationResult leading = evaluateExpression(QStringLiteral("-5+3"));
    QVERIFY(leading.ok);
    QCOMPARE(leading.value, QStringLiteral("-2"));

    const CalculationResult multiplied = evaluateExpression(QStringLiteral("5*-2"));
    QVERIFY(multiplied.ok);
    QCOMPARE(multiplied.value, QStringLiteral("-10"));

    const CalculationResult doubled = evaluateExpression(QStringLiteral("5--3"));
    QVERIFY(doubled.ok);
    QCOMPARE(doubled.value, QStringLiteral("8"));
}

void AnnotationToolkitTests::calculatorTreatsPercentInMultiplicativeChainAsHundredth() {
    // 乘法链里的百分号是除以 100 的因子：100 × 10% = 10。
    const CalculationResult result = evaluateExpression(QStringLiteral("100*10%"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("10"));

    // 括号内重新起算：括号里的百分号够不着外层的基数。
    const CalculationResult nested = evaluateExpression(QStringLiteral("100+(10%)"));
    QVERIFY(nested.ok);
    QCOMPARE(nested.value, QStringLiteral("100.1"));
}

void AnnotationToolkitTests::calculatorRejectsBadThousandSeparator() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1,23"));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void AnnotationToolkitTests::calculatorRejectsDoubleDecimalPoint() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1.2.3"));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

// ---------------------------------------------------------- 计算器输出 ----

void AnnotationToolkitTests::formatCalculationProducesAnnotationText() {
    const CalculationResult result = evaluateExpression(QStringLiteral("100+10%"));
    QVERIFY(result.ok);
    QCOMPARE(formatCalculationForAnnotation(result), QStringLiteral("100+10% = 110"));
}

void AnnotationToolkitTests::formatCalculationReturnsEmptyOnFailure() {
    const CalculationResult result = evaluateExpression(QStringLiteral("1+"));
    QVERIFY(!result.ok);
    // 失败时不该把错误塞进批注正文。
    QVERIFY(formatCalculationForAnnotation(result).isEmpty());
}

// ---------------------------------------------------------- 符号面板 ----

void AnnotationToolkitTests::everySymbolCategoryIsPopulated() {
    const SymbolCategory categories[] = {
        SymbolCategory::Currency, SymbolCategory::Punctuation, SymbolCategory::Mathematics,
        SymbolCategory::Marks, SymbolCategory::Arrows,
    };
    for (SymbolCategory category : categories) {
        const QVector<SymbolEntry> entries = symbolCatalog(category);
        QVERIFY2(entries.size() >= 8, "每个类别的符号至少要 8 个，否则面板要翻页");
        for (const SymbolEntry& entry : entries) {
            QVERIFY(!entry.symbol.isEmpty());
            QVERIFY(!entry.name.isEmpty());
            QCOMPARE(entry.category, category);
        }
    }
}

void AnnotationToolkitTests::symbolCatalogHasNoDuplicates() {
    QSet<QString> seen;
    for (const SymbolEntry& entry : symbolCatalog()) {
        QVERIFY2(!seen.contains(entry.symbol), qPrintable(QStringLiteral("符号重复：%1").arg(entry.symbol)));
        seen.insert(entry.symbol);
    }
}

void AnnotationToolkitTests::symbolCatalogTotalIsLargeEnough() {
    QVERIFY(symbolCatalog().size() >= 32);
}

void AnnotationToolkitTests::formatSymbolWithoutNameKeepsSymbolOnly() {
    QCOMPARE(formatSymbolForAnnotation(checkEntry()), QStringLiteral("✓"));
    QCOMPARE(formatSymbolForAnnotation(checkEntry(), false), QStringLiteral("✓"));
}

void AnnotationToolkitTests::formatSymbolWithNameAppendsChineseName() {
    QCOMPARE(formatSymbolForAnnotation(checkEntry(), true), QStringLiteral("✓ 通过"));
}

void AnnotationToolkitTests::unknownSymbolCategoryReturnsEmpty() {
    const QVector<SymbolEntry> entries = symbolCatalog(static_cast<SymbolCategory>(999));
    QVERIFY(entries.isEmpty());
}

void AnnotationToolkitTests::wholeSymbolCatalogMatchesCategorySum() {
    const SymbolCategory categories[] = {
        SymbolCategory::Currency, SymbolCategory::Punctuation, SymbolCategory::Mathematics,
        SymbolCategory::Marks, SymbolCategory::Arrows,
    };
    int sum = 0;
    for (SymbolCategory category : categories) {
        sum += symbolCatalog(category).size();
    }
    QCOMPARE(symbolCatalog().size(), sum);
}

void AnnotationToolkitTests::symbolCategoryNamesAreNotEmpty() {
    const SymbolCategory categories[] = {
        SymbolCategory::Currency, SymbolCategory::Punctuation, SymbolCategory::Mathematics,
        SymbolCategory::Marks, SymbolCategory::Arrows,
    };
    for (SymbolCategory category : categories) {
        QVERIFY(!symbolCategoryName(category).isEmpty());
    }
}

void AnnotationToolkitTests::insertSymbolIntoTextInsertsAtPosition() {
    QCOMPARE(insertSymbolIntoText(QStringLiteral("通过"), 0, checkEntry()), QStringLiteral("✓通过"));
    // QString 的位置按 UTF-16 码元算，"审核通过" 里第 2 个字前面就是位置 2。
    QCOMPARE(insertSymbolIntoText(QStringLiteral("审核通过"), 2, checkEntry()), QStringLiteral("审核✓通过"));
    // 越界按追加处理，不能把用户写的内容弄丢。
    QCOMPARE(insertSymbolIntoText(QStringLiteral("通过"), 99, checkEntry()), QStringLiteral("通过✓"));
    QCOMPARE(insertSymbolIntoText(QStringLiteral("通过"), -5, checkEntry()), QStringLiteral("✓通过"));
}

void AnnotationToolkitTests::insertSymbolIntoTextKeepsSurrogatePairsIntact() {
    // emoji 是一个代理对（两个 UTF-16 码元）。位置落在代理对中间时不能把它劈开。
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    QCOMPARE(insertSymbolIntoText(emoji, 1, checkEntry()), QStringLiteral("✓") + emoji);
}

// ---------------------------------------------------------- 连续批注 ----

void AnnotationToolkitTests::continuousModeIsInactiveByDefault() {
    ContinuousAnnotationMode mode;
    QVERIFY(!mode.isActive());
    QVERIFY(mode.activeTool().isEmpty());
    // 没激活时放完批注要回到选择态。
    QVERIFY(!mode.afterPlacement());
}

void AnnotationToolkitTests::continuousModeAfterPlacementIsTrueWhenActive() {
    ContinuousAnnotationMode mode;
    mode.activate(QStringLiteral("highlight"));
    QVERIFY(mode.isActive());
    // 工具保持激活，调用方不要回到选择态。
    QVERIFY(mode.afterPlacement());
    QVERIFY(mode.afterPlacement());
    QVERIFY(mode.isActive());
}

void AnnotationToolkitTests::continuousModeKeepsActivatedTool() {
    ContinuousAnnotationMode mode;
    mode.activate(QStringLiteral("underline"));
    QCOMPARE(mode.activeTool(), QStringLiteral("underline"));
    mode.activate(QStringLiteral("highlight"));
    QCOMPARE(mode.activeTool(), QStringLiteral("highlight"));
}

void AnnotationToolkitTests::continuousModeDeactivateClearsState() {
    ContinuousAnnotationMode mode;
    mode.activate(QStringLiteral("highlight"));
    QVERIFY(mode.isActive());
    mode.deactivate();
    QVERIFY(!mode.isActive());
    QVERIFY(mode.activeTool().isEmpty());
    QVERIFY(!mode.afterPlacement());
}

void AnnotationToolkitTests::continuousModeExitsToolThatDoesNotRepeat() {
    ContinuousAnnotationMode mode;
    mode.activate(QStringLiteral("ink"), false);
    QVERIFY(mode.isActive());
    QVERIFY(!mode.repeatsLastPlacement());
    // 这种工具放完就退出：afterPlacement 返回 false 并关掉连续模式。
    QVERIFY(!mode.afterPlacement());
    QVERIFY(!mode.isActive());
}

void AnnotationToolkitTests::continuousModeIgnoresEmptyToolId() {
    ContinuousAnnotationMode mode;
    mode.activate(QStringLiteral(""));
    QVERIFY(!mode.isActive());
    QVERIFY(mode.activeTool().isEmpty());
    QVERIFY(!mode.afterPlacement());
}

void AnnotationToolkitTests::continuousModeReportsRepeatFlag() {
    ContinuousAnnotationMode mode;
    QVERIFY(!mode.repeatsLastPlacement());
    mode.activate(QStringLiteral("highlight"), true);
    QVERIFY(mode.repeatsLastPlacement());
    mode.activate(QStringLiteral("ink"), false);
    QVERIFY(!mode.repeatsLastPlacement());
    mode.deactivate();
    QVERIFY(!mode.repeatsLastPlacement());
}

QTEST_GUILESS_MAIN(AnnotationToolkitTests)
#include "AnnotationToolkitTests.moc"
