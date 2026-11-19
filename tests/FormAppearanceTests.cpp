#include "pdf/forms/FormAppearance.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QTest>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#include <optional>

using aipdf::pdf::FormAppearanceResult;
using aipdf::pdf::FormValueUpdate;
using aipdf::pdf::writeFormValues;

namespace {

QByteArray toPdfBytes(QPDF& pdf) {
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer) return {};
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<qsizetype>(buffer->getSize()));
}

// 一个文本域 + /DA + AcroForm /DR /Font /Helv。
QByteArray makeFormPdf(const QString& initialValue = QString(), int flags = 0) {
    QPDF pdf;
    pdf.emptyPDF();

    QPDFObjectHandle helvetica = QPDFObjectHandle::newDictionary();
    helvetica.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    helvetica.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    helvetica.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
    QPDFObjectHandle fontDict = QPDFObjectHandle::newDictionary();
    fontDict.replaceKey("/Helv", pdf.makeIndirectObject(helvetica));
    QPDFObjectHandle dr = QPDFObjectHandle::newDictionary();
    dr.replaceKey("/Font", fontDict);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle box = QPDFObjectHandle::newArray();
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(612));
    box.appendItem(QPDFObjectHandle::newInteger(792));
    page.replaceKey("/MediaBox", box);
    page.replaceKey("/Contents", pdf.makeIndirectObject(pdf.newStream("")));
    const QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);

    QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
    field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    field.replaceKey("/T", QPDFObjectHandle::newString("name"));
    field.replaceKey("/V", QPDFObjectHandle::newString(initialValue.toStdString()));
    field.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 12 Tf 0 g"));
    field.replaceKey("/Ff", QPDFObjectHandle::newInteger(flags));
    QPDFObjectHandle rect = QPDFObjectHandle::newArray();
    rect.appendItem(QPDFObjectHandle::newInteger(72));
    rect.appendItem(QPDFObjectHandle::newInteger(700));
    rect.appendItem(QPDFObjectHandle::newInteger(272));
    rect.appendItem(QPDFObjectHandle::newInteger(720));
    field.replaceKey("/Rect", rect);
    field.replaceKey("/P", pageRef);
    const QPDFObjectHandle fieldRef = pdf.makeIndirectObject(field);

    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(fieldRef);
    page.replaceKey("/Annots", annots);

    QPDFObjectHandle fields = QPDFObjectHandle::newArray();
    fields.appendItem(fieldRef);
    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    acroForm.replaceKey("/Fields", fields);
    acroForm.replaceKey("/DR", dr);
    pdf.getRoot().replaceKey("/AcroForm", pdf.makeIndirectObject(acroForm));

    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pdf.getRoot().replaceKey("/Pages", pdf.makeIndirectObject(pages));

    return toPdfBytes(pdf);
}

QString fieldValueViaPdfium(const QByteArray& bytes) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return {};
    for (const aipdf::pdf::FormField& field : opened.document.formFields()) {
        if (field.name == QStringLiteral("name")) return field.value;
    }
    return {};
}

}  // namespace

class FormAppearanceTests : public QObject {
    Q_OBJECT

private slots:
    void writesValueAndAppearance();
    void rejectsNonLatinText();
    void rejectsUnknownField();
    void rejectsReadOnlyField();
};

void FormAppearanceTests::writesValueAndAppearance() {
    const QByteArray pdf = makeFormPdf();
    QVERIFY(!pdf.isEmpty());

    const FormAppearanceResult result =
        writeFormValues(pdf, {{QStringLiteral("name"), QStringLiteral("Hello")}});
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QCOMPARE(result.report.fieldsUpdated, 1);
    QCOMPARE(result.report.appearancesWritten, 1);

    QCOMPARE(fieldValueViaPdfium(result.bytes), QStringLiteral("Hello"));

    // /AP /N 必须是 Form XObject，且 BBox 与 widget 尺寸一致。
    QPDF check;
    check.processMemoryFile("check.pdf", result.bytes.constData(),
                            static_cast<size_t>(result.bytes.size()));
    const QPDFObjectHandle page = check.getAllPages().at(0);
    const QPDFObjectHandle annot = page.getKey("/Annots").getArrayItem(0);
    const QPDFObjectHandle normal = annot.getKey("/AP").getKey("/N");
    QVERIFY(normal.isStream());
    QCOMPARE(normal.getDict().getKey("/Subtype").getName(), std::string("/Form"));
    const QPDFObjectHandle bbox = normal.getDict().getKey("/BBox");
    QCOMPARE(bbox.getArrayNItems(), 4);
    QCOMPARE(bbox.getArrayItem(2).getNumericValue(), 200.0);
    QCOMPARE(bbox.getArrayItem(3).getNumericValue(), 20.0);
}

void FormAppearanceTests::rejectsNonLatinText() {
    const QByteArray pdf = makeFormPdf();
    const FormAppearanceResult result =
        writeFormValues(pdf, {{QStringLiteral("name"), QStringLiteral("中文")}});
    QVERIFY(!result.ok());
    QVERIFY2(result.report.error.contains(QStringLiteral("非单字节")),
             qPrintable(result.report.error));
    QVERIFY(result.bytes.isEmpty());
}

void FormAppearanceTests::rejectsUnknownField() {
    const QByteArray pdf = makeFormPdf();
    const FormAppearanceResult result =
        writeFormValues(pdf, {{QStringLiteral("missing"), QStringLiteral("x")}});
    QVERIFY(!result.ok());
    QVERIFY(result.report.error.contains(QStringLiteral("找不到字段")));
}

void FormAppearanceTests::rejectsReadOnlyField() {
    const QByteArray pdf = makeFormPdf(QStringLiteral("fixed"), 1);
    const FormAppearanceResult result =
        writeFormValues(pdf, {{QStringLiteral("name"), QStringLiteral("changed")}});
    QVERIFY(!result.ok());
    QVERIFY(result.report.error.contains(QStringLiteral("只读")));
}

QTEST_MAIN(FormAppearanceTests)
#include "FormAppearanceTests.moc"
