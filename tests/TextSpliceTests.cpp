#include "pdf/content/TextSplice.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QFile>
#include <QRectF>
#include <QString>
#include <QTest>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <memory>
#include <optional>

using aipdf::pdf::TextSpliceResult;
using aipdf::pdf::TextSpliceTarget;
using aipdf::pdf::splicePageText;

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

// 一页 + 一个非嵌入 Type1 字体（/Widths 全是 600，位置断言才有确定值）。
QByteArray makePdfWithContent(const QByteArray& content) {
    QPDF pdf;
    pdf.emptyPDF();

    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
    font.replaceKey("/FirstChar", QPDFObjectHandle::newInteger(32));
    font.replaceKey("/LastChar", QPDFObjectHandle::newInteger(255));
    QPDFObjectHandle widths = QPDFObjectHandle::newArray();
    for (int i = 32; i <= 255; ++i) widths.appendItem(QPDFObjectHandle::newInteger(600));
    font.replaceKey("/Widths", widths);

    QPDFObjectHandle fontDict = QPDFObjectHandle::newDictionary();
    fontDict.replaceKey("/F1", pdf.makeIndirectObject(font));
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fontDict);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle box = QPDFObjectHandle::newArray();
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(612));
    box.appendItem(QPDFObjectHandle::newInteger(792));
    page.replaceKey("/MediaBox", box);
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", pdf.makeIndirectObject(pdf.newStream(content.toStdString())));
    const QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);

    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pdf.getRoot().replaceKey("/Pages", pdf.makeIndirectObject(pages));

    return toPdfBytes(pdf);
}

// 一页 + 一个 Form XObject（文字藏在 XObject 里）。
QByteArray makePdfWithXObject(const QByteArray& pageContent, const QByteArray& xobjectContent) {
    QPDF pdf;
    pdf.emptyPDF();
    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
    const QPDFObjectHandle fontRef = pdf.makeIndirectObject(font);

    QPDFObjectHandle xobject = pdf.newStream(xobjectContent.toStdString());
    QPDFObjectHandle xdict = xobject.getDict();
    xdict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    xdict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
    bbox.appendItem(QPDFObjectHandle::newInteger(0));
    bbox.appendItem(QPDFObjectHandle::newInteger(0));
    bbox.appendItem(QPDFObjectHandle::newInteger(200));
    bbox.appendItem(QPDFObjectHandle::newInteger(20));
    xdict.replaceKey("/BBox", bbox);
    QPDFObjectHandle innerFonts = QPDFObjectHandle::newDictionary();
    innerFonts.replaceKey("/F1", fontRef);
    QPDFObjectHandle innerResources = QPDFObjectHandle::newDictionary();
    innerResources.replaceKey("/Font", innerFonts);
    xdict.replaceKey("/Resources", innerResources);
    const QPDFObjectHandle xobjectRef = pdf.makeIndirectObject(xobject);

    QPDFObjectHandle fontDict = QPDFObjectHandle::newDictionary();
    fontDict.replaceKey("/F1", fontRef);
    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/Fm1", xobjectRef);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fontDict);
    resources.replaceKey("/XObject", xobjects);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle box = QPDFObjectHandle::newArray();
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(612));
    box.appendItem(QPDFObjectHandle::newInteger(792));
    page.replaceKey("/MediaBox", box);
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", pdf.makeIndirectObject(pdf.newStream(pageContent.toStdString())));
    const QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);

    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pdf.getRoot().replaceKey("/Pages", pdf.makeIndirectObject(pages));
    return toPdfBytes(pdf);
}
QString pageText(const QByteArray& bytes) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return {};
    const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(0);
    if (!page.has_value()) return {};
    const std::optional<aipdf::pdf::TextPage> text = page->extractText();
    return text.has_value() ? text->plainText() : QString();
}

std::optional<QRectF> charBox(const QByteArray& bytes, char16_t character) {
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!opened.document.isValid()) return std::nullopt;
    const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(0);
    if (!page.has_value()) return std::nullopt;
    const std::optional<aipdf::pdf::TextPage> text = page->extractText();
    if (!text.has_value()) return std::nullopt;
    for (const aipdf::pdf::TextChar& item : text->chars) {
        if (item.text == QString(QChar(character))) return item.box;
    }
    return std::nullopt;
}

TextSpliceTarget makeTarget(const QString& find, const QString& replace) {
    TextSpliceTarget target;
    target.find = find;
    target.replace = replace;
    return target;
}

}  // namespace

class TextSpliceTests : public QObject {
    Q_OBJECT

private slots:
    void replacesTextInTjAndKeepsBytes();
    void replacementKeepsFollowingGlyphPosition();
    void replacesOnlySelectedOccurrence();
    void replacesAllOccurrencesWhenRequested();
    void replacesAllOccurrencesCaseInsensitively();
    void deletesText();
    void refusesGlyphsMissingFromFont();
    void replacesInsideTjArray();
    void reportsMissingMatch();
    void rejectsPageOutOfRange();
    void replacesTextInsideFormXObject();
};

void TextSpliceTests::replacesTextInTjAndKeepsBytes() {
    const QByteArray pdf = makePdfWithContent(
        "BT /F1 12 Tf 72 720 Td (Hello world) Tj ET");
    QVERIFY(!pdf.isEmpty());
    QCOMPARE(pageText(pdf), QStringLiteral("Hello world"));

    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral("world"), QStringLiteral("there")));
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QCOMPARE(result.report.occurrences, 1);
    QCOMPARE(result.report.fontResource, QStringLiteral("F1"));
    QCOMPARE(result.report.baseFont, QStringLiteral("Helvetica"));
    QCOMPARE(pageText(result.bytes), QStringLiteral("Hello there"));
}

void TextSpliceTests::replacementKeepsFollowingGlyphPosition() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (ABCD) Tj ET");
    const std::optional<QRectF> before = charBox(pdf, u'D');
    QVERIFY(before.has_value());

    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral("BC"), QStringLiteral("X")));
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    // 宽度变了，PDFium 会把补偿位移读成一个空格；位置断言才是关键。
    const QString text = pageText(result.bytes);
    QVERIFY2(text.startsWith(QStringLiteral("AX")) && text.endsWith(QStringLiteral("D")),
             qPrintable(text));

    const std::optional<QRectF> after = charBox(result.bytes, u'D');
    QVERIFY(after.has_value());
    QVERIFY2(std::abs(after->left() - before->left()) < 0.5,
             qPrintable(QStringLiteral("D 的 x 从 %1 变到 %2")
                            .arg(before->left())
                            .arg(after->left())));
}

void TextSpliceTests::replacesOnlySelectedOccurrence() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (foo foo) Tj ET");
    TextSpliceTarget target = makeTarget(QStringLiteral("foo"), QStringLiteral("bar"));
    target.occurrence = 1;
    const TextSpliceResult result = splicePageText(pdf, target);
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QCOMPARE(pageText(result.bytes), QStringLiteral("foo bar"));
}

void TextSpliceTests::replacesAllOccurrencesWhenRequested() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (foo foo) Tj ET");
    TextSpliceTarget target = makeTarget(QStringLiteral("foo"), QStringLiteral("bar"));
    target.occurrence = -1;
    const TextSpliceResult result = splicePageText(pdf, target);
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QCOMPARE(result.report.occurrences, 2);
    QCOMPARE(pageText(result.bytes), QStringLiteral("bar bar"));
}

void TextSpliceTests::replacesAllOccurrencesCaseInsensitively() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (Foo foo) Tj ET");
    TextSpliceTarget target = makeTarget(QStringLiteral("foo"), QStringLiteral("bar"));
    target.occurrence = -1;
    target.sensitivity = Qt::CaseInsensitive;
    const TextSpliceResult result = splicePageText(pdf, target);
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QCOMPARE(result.report.occurrences, 2);
    QCOMPARE(pageText(result.bytes), QStringLiteral("bar bar"));
}
void TextSpliceTests::deletesText() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (Hello world) Tj ET");
    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral(" world"), QString()));
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QCOMPARE(result.report.glyphsInserted, 0);
    QCOMPARE(pageText(result.bytes), QStringLiteral("Hello"));
}

void TextSpliceTests::refusesGlyphsMissingFromFont() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (Hello) Tj ET");
    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral("Hello"), QStringLiteral("Hello中")));
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY2(result.report.error.contains(QStringLiteral("写不出")),
             qPrintable(result.report.error));
}

void TextSpliceTests::replacesInsideTjArray() {
    const QByteArray pdf = makePdfWithContent(
        "BT /F1 12 Tf 72 720 Td [(AB) -120 (CD)] TJ ET");
    QVERIFY(pageText(pdf).contains(QStringLiteral("AB")));

    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral("AB"), QStringLiteral("XY")));
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    const QString text = pageText(result.bytes);
    QVERIFY2(text.contains(QStringLiteral("XY")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("CD")), qPrintable(text));
}

void TextSpliceTests::reportsMissingMatch() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (Hello) Tj ET");
    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral("missing"), QStringLiteral("x")));
    QVERIFY(!result.ok());
    QVERIFY(result.report.error.contains(QStringLiteral("没有找到")));
}

void TextSpliceTests::replacesTextInsideFormXObject() {
    const QByteArray pdf = makePdfWithXObject(
        "q 1 0 0 1 72 700 cm /Fm1 Do Q",
        "BT /F1 12 Tf 0 0 Td (Inside) Tj ET");
    QVERIFY(pageText(pdf).contains(QStringLiteral("Inside")));

    const TextSpliceResult result =
        splicePageText(pdf, makeTarget(QStringLiteral("Inside"), QStringLiteral("Outside")));
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    const QString text = pageText(result.bytes);
    QVERIFY2(text.contains(QStringLiteral("Outside")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("Inside")), qPrintable(text));
}
void TextSpliceTests::rejectsPageOutOfRange() {
    const QByteArray pdf = makePdfWithContent("BT /F1 12 Tf 72 720 Td (Hello) Tj ET");
    TextSpliceTarget target = makeTarget(QStringLiteral("Hello"), QStringLiteral("Hi"));
    target.pageIndex = 3;
    const TextSpliceResult result = splicePageText(pdf, target);
    QVERIFY(!result.ok());
    QVERIFY(result.report.error.contains(QStringLiteral("越界")));
}

QTEST_MAIN(TextSpliceTests)
#include "TextSpliceTests.moc"
