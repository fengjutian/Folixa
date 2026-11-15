// 表单数据交换（CSV / FDF）的单元测试。
//
// 这两条通路的失败很难被用户察觉：CSV 少一列、引号没转义、FDF 反斜杠没还原，
// 结果都是"值悄悄变了"，而不是报错。

#include "pdf/FormData.h"

#include <QTest>

using aipdf::pdf::formValuesFromCsv;
using aipdf::pdf::formValuesFromFdf;
using aipdf::pdf::formValuesToCsv;
using aipdf::pdf::formValuesToFdf;
using aipdf::pdf::FormField;

namespace {

QVector<FormField> sample() {
    FormField a;
    a.name = QStringLiteral("fullName");
    a.value = QStringLiteral("张三");
    FormField b;
    b.name = QStringLiteral("country");
    b.value = QStringLiteral("China, \"CN\"");
    FormField c;
    c.name = QStringLiteral("note");
    c.value = QStringLiteral("line1\nline2");
    return { a, b, c };
}

}  // namespace

class FormDataTests : public QObject {
    Q_OBJECT

private slots:
    void csvRoundTripsSpecialCharacters();
    void csvSkipsHeaderRow();
    void fdfRoundTripsValues();
    void fdfUnescapesParentheses();
};

void FormDataTests::csvRoundTripsSpecialCharacters() {
    const QByteArray csv = formValuesToCsv(sample());
    const auto values = formValuesFromCsv(csv);
    QCOMPARE(values.size(), 3);
    QCOMPARE(values.at(0).name, QStringLiteral("fullName"));
    QCOMPARE(values.at(0).value, QStringLiteral("张三"));
    // 逗号与引号必须原样回来。
    QCOMPARE(values.at(1).value, QStringLiteral("China, \"CN\""));
    QCOMPARE(values.at(2).value, QStringLiteral("line1\nline2"));
}

void FormDataTests::csvSkipsHeaderRow() {
    const auto values = formValuesFromCsv(QByteArray("name,value\na,1\n"));
    QCOMPARE(values.size(), 1);
    QCOMPARE(values.first().name, QStringLiteral("a"));
}

void FormDataTests::fdfRoundTripsValues() {
    const QByteArray fdf = formValuesToFdf(sample());
    QVERIFY(fdf.startsWith("%FDF-1.2"));
    const auto values = formValuesFromFdf(fdf);
    QCOMPARE(values.size(), 3);
    QCOMPARE(values.at(0).value, QStringLiteral("张三"));
    QCOMPARE(values.at(1).value, QStringLiteral("China, \"CN\""));
}

void FormDataTests::fdfUnescapesParentheses() {
    const auto values = formValuesFromFdf(QByteArray(
        "%FDF-1.2\n1 0 obj\n<< /FDF << /Fields [ << /T (title) /V (a \\(b\\) c) >> ] >> >>\n"));
    QCOMPARE(values.size(), 1);
    QCOMPARE(values.first().value, QStringLiteral("a (b) c"));
}

QTEST_MAIN(FormDataTests)
#include "FormDataTests.moc"
