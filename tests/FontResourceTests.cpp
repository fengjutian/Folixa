#include "pdf/content/FontResource.h"

#include <QHash>
#include <QString>
#include <QTest>
#include <QVector>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>

using aipdf::pdf::FontGlyph;
using aipdf::pdf::FontKind;
using aipdf::pdf::FontResource;
using aipdf::pdf::PageFontList;
using aipdf::pdf::findFontResource;
using aipdf::pdf::readPageFonts;

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

QPDFObjectHandle makeDescriptor() {
    QPDFObjectHandle descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey("/MissingWidth", QPDFObjectHandle::newInteger(0));
    return descriptor;
}

QPDFObjectHandle makeSimpleFont(QPDF& pdf, int firstChar, const QVector<int>& widths,
                                const QByteArray& cmap = {}, bool embedded = false) {
    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/ABCDEF+TestFont"));
    font.replaceKey("/FirstChar", QPDFObjectHandle::newInteger(firstChar));
    QPDFObjectHandle widthArray = QPDFObjectHandle::newArray();
    for (int width : widths) widthArray.appendItem(QPDFObjectHandle::newInteger(width));
    font.replaceKey("/Widths", widthArray);
    QPDFObjectHandle descriptor = makeDescriptor();
    if (embedded) {
        descriptor.replaceKey("/FontFile", pdf.makeIndirectObject(pdf.newStream("dummy")));
    }
    font.replaceKey("/FontDescriptor", descriptor);
    if (!cmap.isEmpty()) {
        font.replaceKey("/ToUnicode", pdf.makeIndirectObject(pdf.newStream(cmap.toStdString())));
    }
    return font;
}

QPDFObjectHandle makeType0Font(QPDF& pdf, const QByteArray& cmap, const QByteArray& encoding,
                               const QVector<int>& cidWidths, int dw = 1000) {
    QPDFObjectHandle cidFont = QPDFObjectHandle::newDictionary();
    cidFont.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    cidFont.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType2"));
    cidFont.replaceKey("/BaseFont", QPDFObjectHandle::newName("/TestCID"));
    cidFont.replaceKey("/DW", QPDFObjectHandle::newInteger(dw));
    QPDFObjectHandle w = QPDFObjectHandle::newArray();
    w.appendItem(QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle widths = QPDFObjectHandle::newArray();
    for (int width : cidWidths) widths.appendItem(QPDFObjectHandle::newInteger(width));
    w.appendItem(widths);
    cidFont.replaceKey("/W", w);
    cidFont.replaceKey("/FontDescriptor", makeDescriptor());

    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/TestCID"));
    font.replaceKey("/Encoding", QPDFObjectHandle::newName(encoding.toStdString()));
    QPDFObjectHandle descendants = QPDFObjectHandle::newArray();
    descendants.appendItem(pdf.makeIndirectObject(cidFont));
    font.replaceKey("/DescendantFonts", descendants);
    font.replaceKey("/ToUnicode", pdf.makeIndirectObject(pdf.newStream(cmap.toStdString())));
    return font;
}

QByteArray pdfWithFonts(QPDF& pdf,
                        const QVector<QPair<QString, QPDFObjectHandle>>& fonts) {
    QPDFObjectHandle fontDictionary = QPDFObjectHandle::newDictionary();
    for (const auto& font : fonts) {
        fontDictionary.replaceKey("/" + font.first.toStdString(),
                                  pdf.makeIndirectObject(font.second));
    }
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fontDictionary);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle mediaBox = QPDFObjectHandle::newArray();
    mediaBox.appendItem(QPDFObjectHandle::newInteger(0));
    mediaBox.appendItem(QPDFObjectHandle::newInteger(0));
    mediaBox.appendItem(QPDFObjectHandle::newInteger(612));
    mediaBox.appendItem(QPDFObjectHandle::newInteger(792));
    page.replaceKey("/MediaBox", mediaBox);
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents",
                    pdf.makeIndirectObject(pdf.newStream("BT /F1 12 Tf 72 720 Td (A) Tj ET")));

    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pdf.makeIndirectObject(page));
    pages.replaceKey("/Kids", kids);
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pdf.getRoot().replaceKey("/Pages", pdf.makeIndirectObject(pages));
    return toPdfBytes(pdf);
}

QByteArray latinCmap() {
    return "/CIDInit /ProcSet findresource begin\n"
           "12 dict begin\n"
           "begincmap\n"
           "1 begincodespacerange\n"
           "<00> <FF>\n"
           "endcodespacerange\n"
           "2 beginbfchar\n"
           "<41> <0041>\n"
           "<42> <0042>\n"
           "endbfchar\n"
           "endcmap\n"
           "end\n"
           "end\n";
}

QByteArray cjkCmap() {
    return "/CIDInit /ProcSet findresource begin\n"
           "12 dict begin\n"
           "begincmap\n"
           "1 begincodespacerange\n"
           "<0000> <FFFF>\n"
           "endcodespacerange\n"
           "2 beginbfchar\n"
           "<0001> <4E2D>\n"
           "<0002> <6587>\n"
           "endbfchar\n"
           "endcmap\n"
           "end\n"
           "end\n";
}

}  // namespace

class FontResourceTests : public QObject {
    Q_OBJECT

private slots:
    void simpleFontDecodesAndEncodes();
    void simpleFontReportsUncoveredGlyphs();
    void type0FontUsesTwoByteCodesAndCidWidths();
    void verticalEncodingIsReported();
    void differencesProvideCoverageWithoutToUnicode();
    void embeddedFontWithoutToUnicodeDoesNotGuess();
    void missingWidthsAreMarkedApproximate();
    void invalidInputFails();
    void missingResourceReturnsNull();
};

void FontResourceTests::simpleFontDecodesAndEncodes() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes =
        pdfWithFonts(pdf, {{QStringLiteral("F1"), makeSimpleFont(pdf, 65, {600, 700}, latinCmap())}});
    QVERIFY(!bytes.isEmpty());

    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY2(list.ok, qPrintable(list.error));
    QCOMPARE(list.fonts.size(), 1);
    const FontResource& font = list.fonts.first();
    QCOMPARE(font.resourceName, QStringLiteral("F1"));
    QCOMPARE(font.baseFont, QStringLiteral("TestFont"));
    QCOMPARE(font.kind, FontKind::Type1);
    QCOMPARE(font.codeBytes, 1);
    QVERIFY(font.toUnicodePresent);
    QVERIFY(font.covers(QStringLiteral("AB")));
    QVERIFY(!font.covers(QStringLiteral("AZ")));

    QCOMPARE(font.decode(QByteArray("AB")), QStringLiteral("AB"));
    const QVector<FontGlyph> glyphs = font.encode(QStringLiteral("ABZ"));
    QCOMPARE(glyphs.size(), 3);
    QCOMPARE(glyphs.at(0).code, 65u);
    QCOMPARE(glyphs.at(0).width, 600.0);
    QCOMPARE(glyphs.at(1).code, 66u);
    QCOMPARE(glyphs.at(1).width, 700.0);
    QVERIFY(glyphs.at(0).covered);
    QVERIFY(glyphs.at(1).covered);
    QVERIFY(!glyphs.at(2).covered);
    QCOMPARE(font.measure(QStringLiteral("AB"), 10.0), 13.0);
}

void FontResourceTests::simpleFontReportsUncoveredGlyphs() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes =
        pdfWithFonts(pdf, {{QStringLiteral("F1"), makeSimpleFont(pdf, 65, {600}, latinCmap())}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY(list.ok);
    const FontResource& font = list.fonts.first();
    const FontGlyph glyph = font.glyphFor(0x4E2D);  // 中
    QVERIFY(!glyph.covered);
    QCOMPARE(glyph.codeBytes, 1);
    QCOMPARE(font.measure(QStringLiteral("中"), 10.0), 5.0);  // 缺省 500
}

void FontResourceTests::type0FontUsesTwoByteCodesAndCidWidths() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes = pdfWithFonts(
        pdf, {{QStringLiteral("F1"), makeType0Font(pdf, cjkCmap(), "/Identity-H", {500, 600})}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY2(list.ok, qPrintable(list.error));
    QCOMPARE(list.fonts.size(), 1);
    const FontResource& font = list.fonts.first();
    QCOMPARE(font.kind, FontKind::Type0);
    QVERIFY(font.composite);
    QCOMPARE(font.codeBytes, 2);
    QVERIFY(!font.vertical);

    QCOMPARE(font.decode(QByteArray("\x00\x01\x00\x02", 4)), QStringLiteral("中文"));
    const QVector<FontGlyph> glyphs = font.encode(QStringLiteral("中文A"));
    QCOMPARE(glyphs.size(), 3);
    QVERIFY(glyphs.at(0).covered);
    QVERIFY(glyphs.at(1).covered);
    QVERIFY(!glyphs.at(2).covered);
    QCOMPARE(glyphs.at(0).width, 500.0);
    QCOMPARE(glyphs.at(1).width, 600.0);
    QCOMPARE(font.measure(QStringLiteral("中文"), 10.0), 11.0);
}

void FontResourceTests::verticalEncodingIsReported() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes = pdfWithFonts(
        pdf, {{QStringLiteral("F1"), makeType0Font(pdf, cjkCmap(), "/Identity-V", {500, 600})}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY(list.ok);
    QVERIFY(list.fonts.first().vertical);
}

void FontResourceTests::differencesProvideCoverageWithoutToUnicode() {
    QPDF pdf;
    pdf.emptyPDF();
    QPDFObjectHandle font = makeSimpleFont(pdf, 65, {1000});
    QPDFObjectHandle encoding = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle differences = QPDFObjectHandle::newArray();
    differences.appendItem(QPDFObjectHandle::newInteger(65));
    differences.appendItem(QPDFObjectHandle::newName("/uni4E2D"));
    encoding.replaceKey("/Differences", differences);
    font.replaceKey("/Encoding", encoding);

    const QByteArray bytes = pdfWithFonts(pdf, {{QStringLiteral("F1"), font}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY(list.ok);
    const FontResource& parsed = list.fonts.first();
    QVERIFY(!parsed.toUnicodePresent);
    QVERIFY(parsed.covers(QStringLiteral("中")));
    QCOMPARE(parsed.glyphFor(0x4E2D).code, 65u);
    QCOMPARE(parsed.decode(QByteArray("\x41")), QStringLiteral("中"));
}

void FontResourceTests::embeddedFontWithoutToUnicodeDoesNotGuess() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes = pdfWithFonts(
        pdf, {{QStringLiteral("F1"), makeSimpleFont(pdf, 65, {600}, {}, /*embedded=*/true)}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY(list.ok);
    const FontResource& font = list.fonts.first();
    QVERIFY(font.embedded);
    QVERIFY(!font.toUnicodePresent);
    // 嵌入又缺 ToUnicode：不猜字形，宁可报「写不出来」。
    QVERIFY(!font.covers(QStringLiteral("Hello")));
    QVERIFY(!font.covers(QStringLiteral("中")));
}

void FontResourceTests::missingWidthsAreMarkedApproximate() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes =
        pdfWithFonts(pdf, {{QStringLiteral("F1"), makeSimpleFont(pdf, 65, {}, latinCmap())}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY(list.ok);
    const FontResource& parsed = list.fonts.first();
    QVERIFY(parsed.widthsApproximate);
    QCOMPARE(parsed.glyphFor(0x41).width, 500.0);
}

void FontResourceTests::invalidInputFails() {
    const PageFontList empty = readPageFonts({}, 0);
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());

    const PageFontList garbage = readPageFonts(QByteArray("not a pdf"), 0);
    QVERIFY(!garbage.ok);
    QVERIFY(!garbage.error.isEmpty());
}

void FontResourceTests::missingResourceReturnsNull() {
    QPDF pdf;
    pdf.emptyPDF();
    const QByteArray bytes =
        pdfWithFonts(pdf, {{QStringLiteral("F1"), makeSimpleFont(pdf, 65, {600}, latinCmap())}});
    const PageFontList list = readPageFonts(bytes, 0);
    QVERIFY(list.ok);
    QVERIFY(findFontResource(list, QStringLiteral("F1")) != nullptr);
    QVERIFY(findFontResource(list, QStringLiteral("F9")) == nullptr);
}

QTEST_MAIN(FontResourceTests)
#include "FontResourceTests.moc"
