// 表单域 JavaScript 子集求值器的单元测试。
//
// 这些脚本是 PDF 文件里带来的，既不能崩，也不能悄悄算错：
// 语法不认识要报错，除零要报错，"1"+1 和 "1"-1 的差别要保住。

#include "pdf/FormJsEvaluator.h"

#include <QTest>

using aipdf::pdf::evaluateFormScript;
using aipdf::pdf::formatFormNumber;
using aipdf::pdf::FormFieldValue;
using aipdf::pdf::FormJsResult;

namespace {

QVector<FormFieldValue> sampleFields() {
    return {
        FormFieldValue{ QStringLiteral("数量"), QStringLiteral("3") },
        FormFieldValue{ QStringLiteral("单价"), QStringLiteral("12.5") },
    };
}

QVector<FormFieldValue> noFields() {
    return QVector<FormFieldValue>();
}

}  // namespace

class FormJsEvaluatorTests : public QObject {
    Q_OBJECT

private slots:
    void formatPadsDecimals();
    void formatAddsThousandsSeparator();
    void calculateMultipliesFieldValue();
    void calculateFallsBackForUnknownField();
    void eventValueAssignment();
    void ifElseBranches();
    void localVariableAndIf();
    void stringConcatVersusSubtraction();
    void unsupportedSyntaxFails();
    void emptyScriptIsNotAnError();
    void divisionByZeroFails();
    void validationScriptReportsValidity();
    void unknownFieldIsOnlyAWarning();
    void truncatedScriptDoesNotCrash();
    void deepNestingIsRejected();
    void longOperatorChainIsRejected();
};

void FormJsEvaluatorTests::formatPadsDecimals() {
    QCOMPARE(formatFormNumber(QStringLiteral("AFNumber_Format(2, 0, 0, 0, \".\", false)"),
                              QStringLiteral("1234.5")),
             QStringLiteral("1234.50"));
    // 脚本里的 AFNumber_Format 格式化的是 event.value。
    const FormJsResult result = evaluateFormScript(
        QStringLiteral("AFNumber_Format(2, 0, 0, 0, \".\", false)"), noFields(), QStringLiteral("total"),
        QStringLiteral("1234.5"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("1234.50"));
}

void FormJsEvaluatorTests::formatAddsThousandsSeparator() {
    QCOMPARE(formatFormNumber(QStringLiteral("AFNumber_Format(2, 1, 0, 0, \".\", false)"),
                              QStringLiteral("1234.5")),
             QStringLiteral("1,234.50"));
    const FormJsResult result = evaluateFormScript(
        QStringLiteral("AFNumber_Format(2, 1, 0, 0, \".\", false)"), noFields(), QStringLiteral("total"),
        QStringLiteral("1234.5"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("1,234.50"));
}

void FormJsEvaluatorTests::calculateMultipliesFieldValue() {
    const FormJsResult result =
        evaluateFormScript(QStringLiteral("AFSimple_Calculate(\"数量\", 0) * 2"), sampleFields());
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("6"));
}

void FormJsEvaluatorTests::calculateFallsBackForUnknownField() {
    const FormJsResult result =
        evaluateFormScript(QStringLiteral("AFSimple_Calculate(\"没有这个字段\", 7)"), sampleFields());
    // 取不到就用 fallback：这是脚本作者预期的行为，不该报错。
    QVERIFY(result.ok);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.value, QStringLiteral("7"));
}

void FormJsEvaluatorTests::eventValueAssignment() {
    const FormJsResult result = evaluateFormScript(QStringLiteral("event.value = 1 + 2"), noFields(),
                                                   QStringLiteral("total"), QStringLiteral("0"));
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("3"));
}

void FormJsEvaluatorTests::ifElseBranches() {
    const QString script = QStringLiteral(
        "if (event.value > 10) { event.value = \"big\"; } else { event.value = \"small\"; }");
    const FormJsResult small =
        evaluateFormScript(script, noFields(), QStringLiteral("n"), QStringLiteral("5"));
    QVERIFY(small.ok);
    QCOMPARE(small.value, QStringLiteral("small"));
    const FormJsResult big =
        evaluateFormScript(script, noFields(), QStringLiteral("n"), QStringLiteral("50"));
    QVERIFY(big.ok);
    QCOMPARE(big.value, QStringLiteral("big"));
}

void FormJsEvaluatorTests::localVariableAndIf() {
    const FormJsResult result = evaluateFormScript(
        QStringLiteral("var x = 2; if (x > 1) { event.value = x * 5; }"), noFields(),
        QStringLiteral("n"), QString());
    QVERIFY(result.ok);
    QCOMPARE(result.value, QStringLiteral("10"));
}

void FormJsEvaluatorTests::stringConcatVersusSubtraction() {
    const FormJsResult concat = evaluateFormScript(QStringLiteral("\"1\" + 1"), noFields());
    QVERIFY(concat.ok);
    QCOMPARE(concat.value, QStringLiteral("11"));
    const FormJsResult subtract = evaluateFormScript(QStringLiteral("\"1\" - 1"), noFields());
    QVERIFY(subtract.ok);
    QCOMPARE(subtract.value, QStringLiteral("0"));
}

void FormJsEvaluatorTests::unsupportedSyntaxFails() {
    const FormJsResult loop =
        evaluateFormScript(QStringLiteral("for (var i = 0; i < 3; i++) { }"), noFields());
    QVERIFY(!loop.ok);
    QVERIFY(!loop.error.isEmpty());
    QVERIFY(loop.error.contains(QStringLiteral("不支持的语法")));
    // 字符串方法也不支持。
    const FormJsResult method =
        evaluateFormScript(QStringLiteral("event.value.substring(0, 2)"), noFields(),
                           QStringLiteral("n"), QStringLiteral("abcd"));
    QVERIFY(!method.ok);
    QVERIFY(method.error.contains(QStringLiteral("不支持的语法")));
    // 数组字面量同样不支持。
    const FormJsResult array = evaluateFormScript(QStringLiteral("[1, 2]"), noFields());
    QVERIFY(!array.ok);
    QVERIFY(array.error.contains(QStringLiteral("不支持的语法")));
}

void FormJsEvaluatorTests::emptyScriptIsNotAnError() {
    const FormJsResult result = evaluateFormScript(QStringLiteral("   "), noFields());
    QVERIFY(result.ok);
    QVERIFY(result.value.isEmpty());
    QVERIFY(result.valid);
}

void FormJsEvaluatorTests::divisionByZeroFails() {
    const FormJsResult result = evaluateFormScript(QStringLiteral("1 / 0"), noFields());
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void FormJsEvaluatorTests::validationScriptReportsValidity() {
    const QString script = QStringLiteral("return event.value >= 18;");
    const FormJsResult tooYoung = evaluateFormScript(script, noFields(), QStringLiteral("age"),
                                                     QStringLiteral("17"));
    QVERIFY(tooYoung.ok);
    QVERIFY(!tooYoung.valid);
    const FormJsResult oldEnough = evaluateFormScript(script, noFields(), QStringLiteral("age"),
                                                       QStringLiteral("20"));
    QVERIFY(oldEnough.ok);
    QVERIFY(oldEnough.valid);
}

void FormJsEvaluatorTests::unknownFieldIsOnlyAWarning() {
    const FormJsResult result =
        evaluateFormScript(QStringLiteral("event.value = 未知字段 + 1"), noFields());
    // 找不到字段不是错误：按空串继续算。
    QVERIFY(result.ok);
    QVERIFY(result.error.contains(QStringLiteral("未知字段")));
    QCOMPARE(result.value, QStringLiteral("1"));
}

void FormJsEvaluatorTests::truncatedScriptDoesNotCrash() {
    const FormJsResult result = evaluateFormScript(QStringLiteral("1 +"), noFields());
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void FormJsEvaluatorTests::deepNestingIsRejected() {
    QString script(200, QLatin1Char('('));
    script += QStringLiteral("1");
    script += QString(200, QLatin1Char(')'));
    const FormJsResult result = evaluateFormScript(script, noFields());
    // 深度上限挡住就行，绝不能把栈跑飞。
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void FormJsEvaluatorTests::longOperatorChainIsRejected() {
    // 源码没有嵌套，但 "1+1+…" 会让表达式树很深；求值递归扛不住，必须提前拒。
    QString script;
    for (int i = 0; i < 5000; ++i) {
        script += QStringLiteral("1+");
    }
    script += QStringLiteral("1");
    const FormJsResult result = evaluateFormScript(script, noFields());
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

QTEST_MAIN(FormJsEvaluatorTests)
#include "FormJsEvaluatorTests.moc"