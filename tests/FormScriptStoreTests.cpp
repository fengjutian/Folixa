#include "pdf/forms/FormScriptStore.h"

#include <QByteArray>
#include <QTest>
#include <QVector>

using namespace aipdf::pdf;

namespace {

// 造一个最小的、带 AcroForm 的 PDF（经典 xref 表）。
//
// 为什么自己造而不是用夹具：这里要控制字段字典里的确切内容（/AA、/DV、/CO），
// 夹具里不会正好有这些。
QByteArray buildFormPdf(const QByteArray& fieldBody, const QByteArray& acroExtras = QByteArray()) {
    QVector<QByteArray> bodies;
    bodies << "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [3 0 R]"
           + acroExtras + " >> >>";
    bodies << "<< /Type /Pages /Kids [4 0 R] /Count 1 >>";
    bodies << fieldBody;
    bodies << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [3 0 R] >>";

    QByteArray pdf = "%PDF-1.7\n";
    QVector<int> offsets;
    for (int index = 0; index < bodies.size(); ++index) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(index + 1) + " 0 obj\n" + bodies.at(index) + "\nendobj\n";
    }
    const int xrefOffset = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(bodies.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (int offset : offsets)
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(bodies.size() + 1) + " /Root 1 0 R >>\n";
    pdf += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return pdf;
}

}  // namespace

class FormScriptStoreTests : public QObject {
    Q_OBJECT

private slots:
    void readsCalculateScriptAndDefaultValue();
    void readsFormatAndValidateScripts();
    void readsCalculationOrder();
    void skipsFieldsWithoutScripts();
    void readsHexEncodedFieldName();
#ifdef AIPDF_WITH_QPDF
    void writesScriptsAndOrderBack();
    void writesWithoutQpdfReportsReason();
#else
    void writesWithoutQpdfReportsReason();
#endif
};

void FormScriptStoreTests::readsCalculateScriptAndDefaultValue() {
    const QByteArray pdf = buildFormPdf(
        "<< /FT /Tx /T (total) /V (100) /DV (0) /AA << /C (event.value) >> >>");
    const QVector<FormFieldScript> scripts = readFormFieldScripts(pdf);
    QCOMPARE(scripts.size(), 1);
    QCOMPARE(scripts.first().fieldName, QStringLiteral("total"));
    QCOMPARE(scripts.first().calculate, QStringLiteral("event.value"));
    QCOMPARE(scripts.first().defaultValue, QStringLiteral("0"));
}

void FormScriptStoreTests::readsFormatAndValidateScripts() {
    const QByteArray pdf = buildFormPdf(
        "<< /FT /Tx /T (amount) /AA << /F (AFNumber_Format(2, event.value, 0, 0, \".\", false)) "
        "/V (event.value > 0) >> >>");
    const QVector<FormFieldScript> scripts = readFormFieldScripts(pdf);
    QCOMPARE(scripts.size(), 1);
    QVERIFY(scripts.first().format.startsWith(QStringLiteral("AFNumber_Format")));
    QCOMPARE(scripts.first().validate, QStringLiteral("event.value > 0"));
}

void FormScriptStoreTests::readsCalculationOrder() {
    const QByteArray pdf = buildFormPdf("<< /FT /Tx /T (total) >>", " /CO [3 0 R]");
    const QStringList order = readCalculationOrder(pdf);
    QCOMPARE(order.size(), 1);
    QCOMPARE(order.first(), QStringLiteral("total"));
}

void FormScriptStoreTests::skipsFieldsWithoutScripts() {
    // 没有任何脚本与默认值的字段不该出现在结果里。
    const QByteArray pdf = buildFormPdf("<< /FT /Tx /T (plain) /V (x) >>");
    QVERIFY(readFormFieldScripts(pdf).isEmpty());
}

void FormScriptStoreTests::readsHexEncodedFieldName() {
    // 字段名写成十六进制串（<FEFF…>）也要认出来。
    QByteArray utf16("\xFE\xFF", 2);
    for (QChar ch : QStringLiteral("名称")) {
        const ushort unit = ch.unicode();
        utf16.append(char((unit >> 8) & 0xff));
        utf16.append(char(unit & 0xff));
    }
    const QByteArray pdf = buildFormPdf(
        "<< /FT /Tx /T <" + utf16.toHex() + "> /AA << /C (1 + 1) >> >>");
    const QVector<FormFieldScript> scripts = readFormFieldScripts(pdf);
    QCOMPARE(scripts.size(), 1);
    QCOMPARE(scripts.first().fieldName, QStringLiteral("名称"));
    QCOMPARE(scripts.first().calculate, QStringLiteral("1 + 1"));
}

void FormScriptStoreTests::writesWithoutQpdfReportsReason() {
    FormFieldScript script;
    script.fieldName = QStringLiteral("total");
    script.calculate = QStringLiteral("event.value");
    const FormScriptWriteResult written =
        writeFormScripts(buildFormPdf("<< /FT /Tx /T (total) >>"), {script},
                         {QStringLiteral("total")});
#ifdef AIPDF_WITH_QPDF
    QVERIFY2(written.ok, qPrintable(written.error));
    // 写回去的内容必须能被自己读出来：这是"写生效"的唯一证据。
    const QVector<FormFieldScript> scripts = readFormFieldScripts(written.bytes);
    QCOMPARE(scripts.size(), 1);
    QCOMPARE(scripts.first().calculate, QStringLiteral("event.value"));
    const QStringList order = readCalculationOrder(written.bytes);
    QCOMPARE(order.size(), 1);
    QCOMPARE(order.first(), QStringLiteral("total"));
#else
    QVERIFY(!written.ok);
    QVERIFY(!written.error.isEmpty());
#endif
}

#ifdef AIPDF_WITH_QPDF
void FormScriptStoreTests::writesScriptsAndOrderBack() {
    const QByteArray pdf = buildFormPdf("<< /FT /Tx /T (total) /V (1) /DV (0) >>");
    FormFieldScript script;
    script.fieldName = QStringLiteral("total");
    script.calculate = QStringLiteral("event.value * 2");
    const FormScriptWriteResult written =
        writeFormScripts(pdf, {script}, {QStringLiteral("total")});
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY(!written.bytes.isEmpty());

    const QVector<FormFieldScript> scripts = readFormFieldScripts(written.bytes);
    QCOMPARE(scripts.size(), 1);
    QCOMPARE(scripts.first().calculate, QStringLiteral("event.value * 2"));
    const QStringList order = readCalculationOrder(written.bytes);
    QCOMPARE(order.size(), 1);
    QCOMPARE(order.first(), QStringLiteral("total"));
    // 写脚本不能把字段原来的默认值弄丢。
    QCOMPARE(scripts.first().defaultValue, QStringLiteral("0"));
}
#endif

QTEST_MAIN(FormScriptStoreTests)
#include "FormScriptStoreTests.moc"
