// 导出到 OFD（中国版式标准，GB/T 33190-2016）的测试。
//
// 测的是包结构本身：OFD 是个 ZIP，里面必须有 OFD.xml、Doc_0/Document.xml
// 和每页的 Content.xml，坐标单位是毫米。

#include <QtTest>

#include <QRegularExpression>
#include <QXmlStreamReader>

#include "doc/OfdExport.h"
#include "doc/ZipWriter.h"

using aipdf::doc::OfdExportOptions;
using aipdf::doc::OfdExportResult;
using aipdf::pdf::TextChar;
using aipdf::pdf::TextPage;

namespace {

// XML 能不能被解析器读进去（OFD 的每一份 XML 都必须格式正确）。
bool isWellFormedXml(const QByteArray& xml) {
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    return !reader.hasError();
}

TextChar makeChar(const QString& text, double x, double bottom, double width = 10.0,
                   double height = 12.0) {
    TextChar character;
    character.text = text;
    character.box = QRectF(x, bottom, width, height);
    character.index = 0;
    return character;
}

TextPage makePage(const QVector<TextChar>& chars, int pageIndex = 0) {
    TextPage page;
    page.pageIndex = pageIndex;
    page.chars = chars;
    page.contentBox = aipdf::pdf::MediaBox{0.0, 0.0, 595.0, 842.0};
    return page;
}

TextPage samplePage() {
    return makePage({
        makeChar(QStringLiteral("Hello"), 50.0, 700.0, 50.0, 12.0),
        makeChar(QStringLiteral("World"), 140.0, 700.0, 50.0, 12.0),
    });
}

QByteArray entryOf(const QByteArray& package, const QString& name) {
    return aipdf::doc::zip::readEntry(package, name);
}

}  // namespace

class OfdExportTests : public QObject {
    Q_OBJECT

private slots:
    void exportSucceeds();
    void packageIsZip();
    void packageContainsOfdXml();
    void packageContainsDocumentXml();
    void packageContainsPageContent();
    void pageContentHasTextObject();
    void multiplePagesGetTheirOwnContentFiles();
    void emptyTextPageDoesNotCrash();
    void pageSizeIsConvertedToMillimeters();
    void chineseTextIsStoredAsUtf8();
    void metadataReachesDocInfo();
    void textObjectCarriesRealHeight();
};

void OfdExportTests::exportSucceeds() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.bytes.isEmpty());
    QVERIFY(result.error.isEmpty());

    // 包里的每一份 XML 都必须能被解析器读进去。
    for (const QString& name : aipdf::doc::zip::listEntryNames(result.bytes)) {
        const QByteArray content = entryOf(result.bytes, name);
        QVERIFY2(isWellFormedXml(content),
                 qPrintable(QStringLiteral("%1 不是合法 XML").arg(name)));
    }
}

void OfdExportTests::packageIsZip() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    QVERIFY(result.bytes.startsWith(QByteArrayLiteral("PK\x03\x04")));
    // 中央目录结束记录也要在，说明包是完整的。
    QVERIFY(result.bytes.contains(QByteArrayLiteral("PK\x05\x06")));
}

void OfdExportTests::packageContainsOfdXml() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("OFD.xml")));

    const QByteArray ofd = entryOf(result.bytes, QStringLiteral("OFD.xml"));
    QVERIFY(ofd.contains("<ofd:OFD"));
    QVERIFY(ofd.contains("Doc_0/Document.xml"));
}

void OfdExportTests::packageContainsDocumentXml() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("Doc_0/Document.xml")));

    const QByteArray document = entryOf(result.bytes, QStringLiteral("Doc_0/Document.xml"));
    QVERIFY(document.contains("<ofd:Document"));
    QVERIFY(document.contains("Pages/Page_0/Content.xml"));
}

void OfdExportTests::packageContainsPageContent() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("Doc_0/Pages/Page_0/Content.xml")));

    const QByteArray content =
        entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
    QVERIFY(content.contains("<ofd:Page"));
    QVERIFY(content.contains("PhysicalBox"));
}

void OfdExportTests::pageContentHasTextObject() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    const QByteArray content =
        entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
    QVERIFY(content.contains("TextObject"));
    QVERIFY(content.contains("TextCode"));
    QVERIFY(content.contains("Boundary="));
    QVERIFY(content.contains("Hello World"));
}

void OfdExportTests::multiplePagesGetTheirOwnContentFiles() {
    QVector<TextPage> pages;
    pages.append(samplePage());
    pages.append(makePage({makeChar(QStringLiteral("Second"), 50.0, 700.0)}, 1));
    pages.append(makePage({makeChar(QStringLiteral("Third"), 50.0, 700.0)}, 2));

    const OfdExportResult result =
        aipdf::doc::exportPagesToOfd(pages, QSizeF(595.0, 842.0));
    QVERIFY2(result.ok, qPrintable(result.error));

    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("Doc_0/Pages/Page_0/Content.xml")));
    QVERIFY(names.contains(QStringLiteral("Doc_0/Pages/Page_1/Content.xml")));
    QVERIFY(names.contains(QStringLiteral("Doc_0/Pages/Page_2/Content.xml")));

    const QByteArray document = entryOf(result.bytes, QStringLiteral("Doc_0/Document.xml"));
    QVERIFY(document.contains("Pages/Page_0/Content.xml"));
    QVERIFY(document.contains("Pages/Page_1/Content.xml"));
    QVERIFY(document.contains("Pages/Page_2/Content.xml"));

    const QByteArray second = entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_1/Content.xml"));
    QVERIFY(second.contains("Second"));
}

void OfdExportTests::emptyTextPageDoesNotCrash() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(makePage({}), QSizeF(595.0, 842.0));
    QVERIFY2(result.ok, qPrintable(result.error));
    const QByteArray content =
        entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
    QVERIFY(content.contains("<ofd:Page"));
    QVERIFY(!content.contains("TextObject"));
}

void OfdExportTests::pageSizeIsConvertedToMillimeters() {
    // 72 点 = 1 英寸 = 25.4 毫米。
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(72.0, 72.0));
    QVERIFY(result.ok);
    const QByteArray content =
        entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
    QVERIFY2(content.contains("25.4 25.4"),
             qPrintable(QStringLiteral("页面尺寸没换算成毫米：%1")
                            .arg(QString::fromUtf8(content.left(400)))));

    // A4（595 × 842 点）≈ 209.9 × 297 毫米。
    const OfdExportResult a4 = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(a4.ok);
    const QByteArray a4Content =
        entryOf(a4.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
    QVERIFY2(a4Content.contains("0 0 209.9"),
             qPrintable(QStringLiteral("页面宽度没换算成毫米：%1")
                            .arg(QString::fromUtf8(a4Content.left(400)))));
    QVERIFY(a4Content.contains("297.03"));
}

void OfdExportTests::chineseTextIsStoredAsUtf8() {
    const TextPage page = makePage({
        makeChar(QStringLiteral("中"), 50.0, 700.0, 12.0, 12.0),
        makeChar(QStringLiteral("文"), 62.0, 700.0, 12.0, 12.0),
    });
    const OfdExportResult result = aipdf::doc::exportPageToOfd(page, QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    const QByteArray content =
        entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));
    QVERIFY(content.contains(QString::fromUtf8("中文").toUtf8()));
}

void OfdExportTests::metadataReachesDocInfo() {
    OfdExportOptions options;
    options.title = QStringLiteral("测试文档");
    options.author = QStringLiteral("测试作者");

    const OfdExportResult result =
        aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0), options);
    QVERIFY(result.ok);
    const QByteArray ofd = entryOf(result.bytes, QStringLiteral("OFD.xml"));
    QVERIFY(ofd.contains(QString::fromUtf8("测试文档").toUtf8()));
    QVERIFY(ofd.contains(QString::fromUtf8("测试作者").toUtf8()));
}

void OfdExportTests::textObjectCarriesRealHeight() {
    const OfdExportResult result = aipdf::doc::exportPageToOfd(samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    const QByteArray content =
        entryOf(result.bytes, QStringLiteral("Doc_0/Pages/Page_0/Content.xml"));

    // Boundary 是「左 上 宽 高」，高度必须换算成毫米（12 点 ≈ 4.233 毫米）。
    // （这里特别容易踩坑：QRectF 的 top() 是较小的 y，而 PDF 的上边是较大的 y，
    //   弄反了高度会算成 0，文本对象就整个塌了。）
    QRegularExpression expression(
        QStringLiteral("Boundary=\"([^\"]*)\""));
    QRegularExpressionMatchIterator iterator =
        expression.globalMatch(QString::fromUtf8(content));
    QVERIFY(iterator.hasNext());
    const QString boundary = iterator.next().captured(1);
    const QStringList parts = boundary.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QCOMPARE(parts.size(), 4);
    QVERIFY2(parts.at(3).toDouble() > 4.0,
             qPrintable(QStringLiteral("文本对象高度不对：%1").arg(boundary)));
    QVERIFY2(parts.at(3).toDouble() < 4.5,
             qPrintable(QStringLiteral("文本对象高度不对：%1").arg(boundary)));
}

QTEST_GUILESS_MAIN(OfdExportTests)

#include "OfdExportTests.moc"
