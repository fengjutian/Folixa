#include "pdf/forms/QuickJsFormScript.h"

#include <QString>
#include <QTest>

using aipdf::pdf::FormFieldValue;
using aipdf::pdf::FormJsResult;
using aipdf::pdf::QuickJsFormScript;

namespace {

QVector<FormFieldValue> fields() {
    return {{QStringLiteral("A"), QStringLiteral("5")},
            {QStringLiteral("B"), QStringLiteral("hello")}};
}

}  // namespace

class QuickJsFormScriptTests : public QObject {
    Q_OBJECT

private slots:
    void arithmeticThroughGetField();
    void supportsVariablesAndLoops();
    void eventRcCanRejectInput();
    void thisValueAndChangeAreBound();
    void standardLibraryIsAvailable();
    void validationReturnsBoolean();
    void syntaxErrorIsReported();
    void infiniteLoopIsInterrupted();
};

void QuickJsFormScriptTests::arithmeticThroughGetField() {
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("event.value = this.getField(\"A\").value * 2 + 1;"), fields(),
        QStringLiteral("total"), QString());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.value, QStringLiteral("11"));
    QVERIFY(result.valueAssigned);
}

void QuickJsFormScriptTests::supportsVariablesAndLoops() {
    // 白名单子集不支持的写法：var + for 循环。
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("var s = 0; for (var i = 0; i < 5; i++) { s += i; } event.value = s;"),
        fields(), QStringLiteral("A"), QString());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.value, QStringLiteral("10"));
}

void QuickJsFormScriptTests::eventRcCanRejectInput() {
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("if (event.change === \"x\") event.rc = false;"), fields(),
        QStringLiteral("A"), QStringLiteral("abc"), QStringLiteral("x"));
    QVERIFY(result.ok);
    QVERIFY(!result.rc);
}

void QuickJsFormScriptTests::thisValueAndChangeAreBound() {
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("event.value = this.value + event.change;"), fields(),
        QStringLiteral("A"), QStringLiteral("ab"), QStringLiteral("c"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.value, QStringLiteral("abc"));
    QVERIFY(result.valueAssigned);
}

void QuickJsFormScriptTests::standardLibraryIsAvailable() {
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("event.value = Math.round(2.6) + \":\" + "
                       "this.getField(\"B\").value.toUpperCase();"),
        fields(), QStringLiteral("A"), QString());
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.value, QStringLiteral("3:HELLO"));
}

void QuickJsFormScriptTests::validationReturnsBoolean() {
    const FormJsResult rejected = QuickJsFormScript::evaluate(
        QStringLiteral("this.value >= 0 && this.value <= 10;"), fields(),
        QStringLiteral("A"), QStringLiteral("-1"));
    QVERIFY(rejected.ok);
    QVERIFY(!rejected.valid);

    const FormJsResult accepted = QuickJsFormScript::evaluate(
        QStringLiteral("this.value >= 0 && this.value <= 10;"), fields(),
        QStringLiteral("A"), QStringLiteral("5"));
    QVERIFY(accepted.ok);
    QVERIFY(accepted.valid);
}

void QuickJsFormScriptTests::syntaxErrorIsReported() {
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("event.value = ;"), fields(), QStringLiteral("A"), QString());
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void QuickJsFormScriptTests::infiniteLoopIsInterrupted() {
    aipdf::pdf::QuickJsLimits limits;
    limits.timeoutMs = 30;
    const FormJsResult result = QuickJsFormScript::evaluate(
        QStringLiteral("while (true) {}"), fields(), QStringLiteral("A"), QString(), QString(),
        0, 0, limits);
    QVERIFY(!result.ok);
    QVERIFY2(result.error.contains(QStringLiteral("超时")), qPrintable(result.error));
}

QTEST_MAIN(QuickJsFormScriptTests)
#include "QuickJsFormScriptTests.moc"
