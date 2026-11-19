#include "pdf/forms/FormRuntime.h"

#include <QHash>
#include <QRectF>
#include <QString>
#include <QTest>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>

using aipdf::pdf::FormEventType;
using aipdf::pdf::FormFieldType;
using aipdf::pdf::FormRuntime;

namespace {

struct FieldSpec {
    QString name;
    QString value;
    QRectF rect = QRectF(72, 700, 120, 20);
    QString fieldType = QStringLiteral("Tx");
    int flags = 0;
    QString calculate;
    QString validate;
    QString format;
    QString keystroke;
    QStringList options;
};

QByteArray toPdfBytes(QPDF& pdf) {
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer) return {};
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<qsizetype>(buffer->getSize()));
}

QPDFObjectHandle newRect(const QRectF& rect) {
    QPDFObjectHandle array = QPDFObjectHandle::newArray();
    array.appendItem(QPDFObjectHandle::newReal(rect.left()));
    array.appendItem(QPDFObjectHandle::newReal(rect.bottom()));
    array.appendItem(QPDFObjectHandle::newReal(rect.right()));
    array.appendItem(QPDFObjectHandle::newReal(rect.top()));
    return array;
}

QByteArray buildFormPdf(const QVector<FieldSpec>& specs, const QStringList& calculationOrder) {
    QPDF pdf;
    pdf.emptyPDF();

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    page.replaceKey("/MediaBox", [] {
        QPDFObjectHandle box = QPDFObjectHandle::newArray();
        box.appendItem(QPDFObjectHandle::newInteger(0));
        box.appendItem(QPDFObjectHandle::newInteger(0));
        box.appendItem(QPDFObjectHandle::newInteger(612));
        box.appendItem(QPDFObjectHandle::newInteger(792));
        return box;
    }());
    const QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);

    QPDFObjectHandle fields = QPDFObjectHandle::newArray();
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    QHash<QString, QPDFObjectHandle> byName;

    for (const FieldSpec& spec : specs) {
        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName(("/" + spec.fieldType).toStdString()));
        field.replaceKey("/T", QPDFObjectHandle::newString(spec.name.toStdString()));
        field.replaceKey("/Rect", newRect(spec.rect));
        field.replaceKey("/P", pageRef);
        if (spec.flags != 0) field.replaceKey("/Ff", QPDFObjectHandle::newInteger(spec.flags));
        if (!spec.value.isEmpty()) {
            field.replaceKey("/V", QPDFObjectHandle::newString(spec.value.toStdString()));
        }
        if (!spec.options.isEmpty()) {
            QPDFObjectHandle options = QPDFObjectHandle::newArray();
            for (const QString& option : spec.options) {
                options.appendItem(QPDFObjectHandle::newString(option.toStdString()));
            }
            field.replaceKey("/Opt", options);
        }
        QPDFObjectHandle actions = QPDFObjectHandle::newDictionary();
        bool hasAction = false;
        const auto putScript = [&](const char* key, const QString& script) {
            if (script.isEmpty()) return;
            QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
            action.replaceKey("/S", QPDFObjectHandle::newName("/JavaScript"));
            action.replaceKey("/JS", QPDFObjectHandle::newString(script.toStdString()));
            actions.replaceKey(key, action);
            hasAction = true;
        };
        putScript("/C", spec.calculate);
        putScript("/V", spec.validate);
        putScript("/F", spec.format);
        putScript("/K", spec.keystroke);
        if (hasAction) field.replaceKey("/AA", actions);

        const QPDFObjectHandle ref = pdf.makeIndirectObject(field);
        fields.appendItem(ref);
        annots.appendItem(ref);
        byName.insert(spec.name, ref);
    }

    page.replaceKey("/Annots", annots);
    page.replaceKey("/Contents", pdf.makeIndirectObject(pdf.newStream("")));

    QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
    acroForm.replaceKey("/Fields", fields);
    if (!calculationOrder.isEmpty()) {
        QPDFObjectHandle order = QPDFObjectHandle::newArray();
        for (const QString& name : calculationOrder) {
            order.appendItem(byName.value(name));
        }
        acroForm.replaceKey("/CO", order);
    }
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

int indexOfEvent(const QVector<aipdf::pdf::FormEventTrace>& trace, FormEventType type,
                 const QString& field) {
    for (int i = 0; i < trace.size(); ++i) {
        if (trace.at(i).type == type && trace.at(i).field == field) return i;
    }
    return -1;
}

// 用 qpdf 读回顶层字段的 /V。
QString pdfFieldValue(const QByteArray& bytes, const QString& name) {
    QPDF pdf;
    pdf.processMemoryFile("check.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const QPDFObjectHandle fields = pdf.getRoot().getKey("/AcroForm").getKey("/Fields");
    if (!fields.isArray()) return {};
    for (int i = 0; i < fields.getArrayNItems(); ++i) {
        const QPDFObjectHandle field = fields.getArrayItem(i);
        if (field.getKey("/T").isString() &&
            QString::fromStdString(field.getKey("/T").getStringValue()) == name) {
            const QPDFObjectHandle value = field.getKey("/V");
            return value.isString() ? QString::fromStdString(value.getStringValue()) : QString();
        }
    }
    return {};
}

}  // namespace

class FormRuntimeTests : public QObject {
    Q_OBJECT

private slots:
    void loadsFieldTree();
    void calculationRunsInDocumentOrder();
    void calculateThenFormatIsOrdered();
    void validationRejectsAndKeepsValue();
    void keystrokeScriptCanRejectInput();
    void readOnlyFieldCannotBeChanged();
    void calculationOrderFromCo();
    void afSimpleCalculateSumsFields();
    void appliesValuesToPdf();
    void appliesMultipleValuesInOneWriteBack();
    void runawayCalculationIsReported();
    void documentEventsAreRecorded();
};

void FormRuntimeTests::loadsFieldTree() {
    const QByteArray pdf = buildFormPdf(
        {{QStringLiteral("name"), QStringLiteral("Ada"), QRectF(72, 700, 120, 20)},
         {QStringLiteral("total"), QStringLiteral("0"), QRectF(72, 660, 120, 20)},
         {QStringLiteral("kind"), QStringLiteral("one"), QRectF(72, 620, 120, 20),
          QStringLiteral("Ch"), 1 << 17, {}, {}, {}, {}, {QStringLiteral("one"), QStringLiteral("two")}}},
        {QStringLiteral("total")});

    QString error;
    const FormRuntime runtime = FormRuntime::load(pdf, &error);
    QVERIFY2(runtime.isValid(), qPrintable(error));
    QCOMPARE(runtime.fields().size(), 3);
    QCOMPARE(runtime.calculationOrder(), QStringList{QStringLiteral("total")});

    const auto* name = runtime.field(QStringLiteral("name"));
    QVERIFY(name != nullptr);
    QCOMPARE(name->type, FormFieldType::Text);
    QCOMPARE(name->value, QStringLiteral("Ada"));
    QCOMPARE(name->pageIndex, 0);
    QVERIFY(name->rect == QRectF(72, 700, 120, 20));

    const auto* kind = runtime.field(QStringLiteral("kind"));
    QVERIFY(kind != nullptr);
    QCOMPARE(kind->type, FormFieldType::Choice);
    QVERIFY(kind->isCombo());
    QCOMPARE(kind->options, (QStringList{QStringLiteral("one"), QStringLiteral("two")}));
}

void FormRuntimeTests::calculationRunsInDocumentOrder() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("2")};
    FieldSpec b{QStringLiteral("B"), QStringLiteral("0")};
    b.calculate = QStringLiteral("event.value = this.getField(\"A\").value * 2");
    const QByteArray pdf = buildFormPdf({a, b}, {QStringLiteral("B")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    const auto result = runtime.setValue(QStringLiteral("A"), QStringLiteral("5"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(runtime.value(QStringLiteral("B")), QStringLiteral("10"));
    QVERIFY(runtime.isDirty());
}

void FormRuntimeTests::calculateThenFormatIsOrdered() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("7")};
    FieldSpec b{QStringLiteral("B"), QStringLiteral("0")};
    b.calculate = QStringLiteral("event.value = this.getField(\"A\").value * 2");
    b.format = QStringLiteral("AFNumber_Format(2, 0, 0, 0, \".\", false)");
    const QByteArray pdf = buildFormPdf({a, b}, {QStringLiteral("B")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    runtime.clearTrace();
    runtime.setValue(QStringLiteral("A"), QStringLiteral("8"));

    QCOMPARE(runtime.value(QStringLiteral("B")), QStringLiteral("16"));
    QCOMPARE(runtime.formattedValue(QStringLiteral("B")), QStringLiteral("16.00"));
    const int calculate = indexOfEvent(runtime.trace(), FormEventType::Calculate, QStringLiteral("B"));
    const int format = indexOfEvent(runtime.trace(), FormEventType::Format, QStringLiteral("B"));
    QVERIFY(calculate >= 0);
    QVERIFY(format > calculate);
}

void FormRuntimeTests::validationRejectsAndKeepsValue() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("ok")};
    a.validate = QStringLiteral("this.value != \"bad\"");
    const QByteArray pdf = buildFormPdf({a}, {});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    const auto rejected = runtime.setValue(QStringLiteral("A"), QStringLiteral("bad"));
    QVERIFY(!rejected.ok);
    QCOMPARE(runtime.value(QStringLiteral("A")), QStringLiteral("ok"));

    const auto accepted = runtime.setValue(QStringLiteral("A"), QStringLiteral("good"));
    QVERIFY2(accepted.ok, qPrintable(accepted.error));
    QCOMPARE(runtime.value(QStringLiteral("A")), QStringLiteral("good"));
}

void FormRuntimeTests::keystrokeScriptCanRejectInput() {
    FieldSpec a{QStringLiteral("A"), QString()};
    a.keystroke = QStringLiteral("event.rc = event.change != \"x\"");
    const QByteArray pdf = buildFormPdf({a}, {});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.typeText(QStringLiteral("A"), QStringLiteral("ab")).ok);
    QCOMPARE(runtime.value(QStringLiteral("A")), QStringLiteral("ab"));

    const auto rejected = runtime.typeText(QStringLiteral("A"), QStringLiteral("x"));
    QVERIFY(!rejected.ok);
    QVERIFY(!rejected.accepted);
    QCOMPARE(runtime.value(QStringLiteral("A")), QStringLiteral("ab"));

    const int keystroke = indexOfEvent(runtime.trace(), FormEventType::Keystroke, QStringLiteral("A"));
    QVERIFY(keystroke >= 0);
}

void FormRuntimeTests::readOnlyFieldCannotBeChanged() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("fixed")};
    a.flags = 1;  // ReadOnly
    const QByteArray pdf = buildFormPdf({a}, {});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.field(QStringLiteral("A"))->readOnly);
    const auto result = runtime.setValue(QStringLiteral("A"), QStringLiteral("changed"));
    QVERIFY(!result.ok);
    QCOMPARE(runtime.value(QStringLiteral("A")), QStringLiteral("fixed"));
}

void FormRuntimeTests::calculationOrderFromCo() {
    FieldSpec c{QStringLiteral("C"), QStringLiteral("1")};
    FieldSpec b{QStringLiteral("B"), QStringLiteral("0")};
    b.calculate = QStringLiteral("event.value = this.getField(\"C\").value * 1 + 1");
    FieldSpec a{QStringLiteral("A"), QStringLiteral("0")};
    a.calculate = QStringLiteral("event.value = this.getField(\"B\").value * 1 + 1");
    const QByteArray pdf = buildFormPdf({a, b, c}, {QStringLiteral("B"), QStringLiteral("A")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    QCOMPARE(runtime.calculationOrder(),
             (QStringList{QStringLiteral("B"), QStringLiteral("A")}));
    const auto result = runtime.setValue(QStringLiteral("C"), QStringLiteral("5"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(runtime.value(QStringLiteral("B")), QStringLiteral("6"));
    QCOMPARE(runtime.value(QStringLiteral("A")), QStringLiteral("7"));
}

void FormRuntimeTests::afSimpleCalculateSumsFields() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("1")};
    FieldSpec b{QStringLiteral("B"), QStringLiteral("2")};
    FieldSpec total{QStringLiteral("T"), QStringLiteral("0")};
    total.calculate = QStringLiteral("AFSimple_Calculate(\"SUM\", new Array(\"A\", \"B\"))");
    const QByteArray pdf = buildFormPdf({a, b, total}, {QStringLiteral("T")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    const auto result = runtime.setValue(QStringLiteral("A"), QStringLiteral("3"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(runtime.value(QStringLiteral("T")), QStringLiteral("5"));
}

void FormRuntimeTests::appliesValuesToPdf() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("2")};
    FieldSpec b{QStringLiteral("B"), QStringLiteral("0")};
    b.calculate = QStringLiteral("event.value = this.getField(\"A\").value * 2");
    const QByteArray pdf = buildFormPdf({a, b}, {QStringLiteral("B")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.setValue(QStringLiteral("A"), QStringLiteral("5")).ok);

    const aipdf::pdf::FormAppearanceResult applied = runtime.applyTo(pdf);
    QVERIFY2(applied.ok(), qPrintable(applied.report.error));
    QCOMPARE(pdfFieldValue(applied.bytes, QStringLiteral("A")), QStringLiteral("5"));
    QCOMPARE(pdfFieldValue(applied.bytes, QStringLiteral("B")), QStringLiteral("10"));
    QVERIFY(applied.report.appearancesWritten >= 2);
}

void FormRuntimeTests::appliesMultipleValuesInOneWriteBack() {
    FieldSpec a{QStringLiteral("A"), QStringLiteral("1")};
    FieldSpec b{QStringLiteral("B"), QStringLiteral("2")};
    FieldSpec total{QStringLiteral("T"), QStringLiteral("0")};
    total.calculate = QStringLiteral("AFSimple_Calculate(\"SUM\", new Array(\"A\", \"B\"))");
    const QByteArray pdf = buildFormPdf({a, b, total}, {QStringLiteral("T")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.setValue(QStringLiteral("A"), QStringLiteral("3")).ok);
    QVERIFY(runtime.setValue(QStringLiteral("B"), QStringLiteral("4")).ok);

    const aipdf::pdf::FormAppearanceResult applied = runtime.applyTo(pdf);
    QVERIFY2(applied.ok(), qPrintable(applied.report.error));
    // 一次写回要把两个手填域和计算域都带上。
    QCOMPARE(pdfFieldValue(applied.bytes, QStringLiteral("A")), QStringLiteral("3"));
    QCOMPARE(pdfFieldValue(applied.bytes, QStringLiteral("B")), QStringLiteral("4"));
    QCOMPARE(pdfFieldValue(applied.bytes, QStringLiteral("T")), QStringLiteral("7"));
}

void FormRuntimeTests::runawayCalculationIsReported() {    FieldSpec a{QStringLiteral("A"), QStringLiteral("0")};
    a.calculate = QStringLiteral("event.value = this.getField(\"B\").value * 1 + 1");
    FieldSpec b{QStringLiteral("B"), QStringLiteral("0")};
    b.calculate = QStringLiteral("event.value = this.getField(\"A\").value * 1 + 1");
    const QByteArray pdf = buildFormPdf({a, b}, {QStringLiteral("A"), QStringLiteral("B")});

    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    runtime.clearTrace();
    runtime.setValue(QStringLiteral("A"), QStringLiteral("1"));

    bool reported = false;
    for (const auto& entry : runtime.trace()) {
        if (entry.type == FormEventType::Calculate && !entry.ok &&
            entry.detail.contains(QStringLiteral("收敛"))) {
            reported = true;
        }
    }
    QVERIFY(reported);
}

void FormRuntimeTests::documentEventsAreRecorded() {
    const QByteArray pdf = buildFormPdf(
        {{QStringLiteral("A"), QStringLiteral("x")}}, {});
    FormRuntime runtime = FormRuntime::load(pdf);
    QVERIFY(runtime.isValid());
    runtime.clearTrace();
    runtime.dispatchDocumentEvent(FormEventType::DocOpen);
    runtime.dispatchDocumentEvent(FormEventType::WillSave);
    QCOMPARE(runtime.trace().size(), 2);
    QCOMPARE(runtime.trace().at(0).type, FormEventType::DocOpen);
    QCOMPARE(runtime.trace().at(1).type, FormEventType::WillSave);
}

QTEST_MAIN(FormRuntimeTests)
#include "FormRuntimeTests.moc"
