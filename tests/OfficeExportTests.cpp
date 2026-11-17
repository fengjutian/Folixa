// 导出到 Office 友好格式（DOCX / RTF / 纯文本）的测试。
//
// 测的是「领域层自己拼字节」这条路径：完全离线，不调 LibreOffice，
// 因此结果必须是确定的（同样的输入 → 同样的字节）。

#include <QtTest>

#include <QSet>
#include <QXmlStreamReader>

#include "doc/OfficeExport.h"
#include "doc/ZipWriter.h"

using aipdf::doc::ExportFormat;
using aipdf::doc::ExportResult;
using aipdf::pdf::TextChar;
using aipdf::pdf::TextPage;

namespace {

// XML 能不能被解析器读进去。
//
// 为什么要查：docx 里的每一份 XML 都必须**格式正确**，
// 少一个结束标签，Word 打开时只会说「文件损坏」，不会告诉你错在哪。
bool isWellFormedXml(const QByteArray& xml) {
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    return !reader.hasError();
}

// 造一个字符：x 是左边，bottom 是底边（PDF 用户空间，左下原点）。
TextChar makeChar(const QString& text, double x, double bottom, double width = 10.0,
                   double height = 12.0) {
    TextChar character;
    character.text = text;
    character.box = QRectF(x, bottom, width, height);
    character.index = 0;
    return character;
}

TextPage makePage(const QVector<TextChar>& chars) {
    TextPage page;
    page.pageIndex = 0;
    page.chars = chars;
    page.contentBox = aipdf::pdf::MediaBox{0.0, 0.0, 595.0, 842.0};
    return page;
}

// 一行两句、词间有间隙的页面。
TextPage samplePage() {
    return makePage({
        makeChar(QStringLiteral("Hello"), 50.0, 700.0, 50.0, 12.0),
        makeChar(QStringLiteral("World"), 140.0, 700.0, 50.0, 12.0),
    });
}

QVector<ExportFormat> allFormats() {
    return {ExportFormat::WordOpenXml, ExportFormat::Rtf, ExportFormat::PlainText};
}

}  // namespace

class OfficeExportTests : public QObject {
    Q_OBJECT

private slots:
    void everyFormatExportsWithoutCrashing();
    void wordOpenXmlIsZipPackageContainingDocumentXml();
    void rtfStartsWithRtfHeader();
    void plainTextContainsOriginalText();
    void wordSpacingIsPreserved();
    void chineseTextSurvivesAsUnicodeEscapes();
    void emptyPageExportsWithoutCrashing();
    void capabilityReportsWhatEachFormatKeeps();
    void suggestedSuffixMatchesFormat();
    void failedExportReturnsEmptyBytes();
    void multipleLinesAreOrderedTopToBottom();
};

void OfficeExportTests::everyFormatExportsWithoutCrashing() {
    const TextPage page = samplePage();
    for (ExportFormat format : allFormats()) {
        const ExportResult result = aipdf::doc::exportPageTo(format, page, QSizeF(595.0, 842.0));
        QVERIFY2(result.ok, qPrintable(QStringLiteral("导出失败：%1").arg(result.error)));
        QVERIFY(result.error.isEmpty());
        QVERIFY(!result.suggestedSuffix.isEmpty());
    }
}

void OfficeExportTests::wordOpenXmlIsZipPackageContainingDocumentXml() {
    const ExportResult result =
        aipdf::doc::exportPageTo(ExportFormat::WordOpenXml, samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    QVERIFY(result.bytes.startsWith(QByteArrayLiteral("PK\x03\x04")));
    QVERIFY(result.bytes.contains("word/document.xml"));
    QVERIFY(result.bytes.contains("[Content_Types].xml"));
    QVERIFY(result.bytes.contains("_rels/.rels"));

    // 把包读回来，确认正文真的是 Word 文档而不是空壳。
    const QStringList names = aipdf::doc::zip::listEntryNames(result.bytes);
    QVERIFY(names.contains(QStringLiteral("[Content_Types].xml")));
    QVERIFY(names.contains(QStringLiteral("_rels/.rels")));
    QVERIFY(names.contains(QStringLiteral("word/document.xml")));

    const QByteArray document = aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    QVERIFY(document.contains("<w:document"));
    QVERIFY(document.contains("Hello World"));

    // 三份 XML 都必须能被解析器读进去，否则 Word 会说「文件损坏」。
    QVERIFY(isWellFormedXml(
        aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("[Content_Types].xml"))));
    QVERIFY(isWellFormedXml(aipdf::doc::zip::readEntry(result.bytes, QStringLiteral("_rels/.rels"))));
    QVERIFY(isWellFormedXml(document));
}

void OfficeExportTests::rtfStartsWithRtfHeader() {
    const ExportResult result =
        aipdf::doc::exportPageTo(ExportFormat::Rtf, samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    QVERIFY(result.bytes.startsWith(QByteArrayLiteral("{\\rtf1")));
    QVERIFY(result.bytes.endsWith(QByteArrayLiteral("}")));
    QVERIFY(result.bytes.contains("Hello World"));
}

void OfficeExportTests::plainTextContainsOriginalText() {
    const ExportResult result =
        aipdf::doc::exportPageTo(ExportFormat::PlainText, samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    QVERIFY(result.bytes.contains("Hello World"));
    QCOMPARE(result.bytes, QByteArrayLiteral("Hello World"));
}

void OfficeExportTests::wordSpacingIsPreserved() {
    // 两个词之间留了 40 点的空隙，导出后应当还是一个空格。
    const TextPage page = samplePage();
    const ExportResult result =
        aipdf::doc::exportPageTo(ExportFormat::PlainText, page, QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    QCOMPARE(QString::fromUtf8(result.bytes), QStringLiteral("Hello World"));
    QVERIFY(!result.bytes.contains("HelloWorld"));

    // 词内不留空格：两个字符紧挨着（间隙 0）时不应插入空格。
    const TextPage tight = makePage({
        makeChar(QStringLiteral("A"), 50.0, 700.0, 10.0, 12.0),
        makeChar(QStringLiteral("B"), 60.0, 700.0, 10.0, 12.0),
    });
    const ExportResult tightResult =
        aipdf::doc::exportPageTo(ExportFormat::PlainText, tight, QSizeF(595.0, 842.0));
    QVERIFY(tightResult.ok);
    QCOMPARE(QString::fromUtf8(tightResult.bytes), QStringLiteral("AB"));
}

void OfficeExportTests::chineseTextSurvivesAsUnicodeEscapes() {
    const TextPage page = makePage({
        makeChar(QStringLiteral("中"), 50.0, 700.0, 12.0, 12.0),
        makeChar(QStringLiteral("文"), 62.0, 700.0, 12.0, 12.0),
    });

    // RTF：非 ASCII 走 \u 转义。
    const ExportResult rtf = aipdf::doc::exportPageTo(ExportFormat::Rtf, page, QSizeF(595.0, 842.0));
    QVERIFY(rtf.ok);
    const QString rtfText = QString::fromUtf8(rtf.bytes);
    QVERIFY(rtfText.contains(QStringLiteral("\\u20013?")));  // U+4E2D
    QVERIFY(rtfText.contains(QStringLiteral("\\u25991?")));  // U+6587

    // 纯文本：按 UTF-8 原样输出。
    const ExportResult text =
        aipdf::doc::exportPageTo(ExportFormat::PlainText, page, QSizeF(595.0, 842.0));
    QVERIFY(text.ok);
    QCOMPARE(QString::fromUtf8(text.bytes), QStringLiteral("中文"));

    // DOCX：正文里是 UTF-8 的 XML 文本。
    const ExportResult docx =
        aipdf::doc::exportPageTo(ExportFormat::WordOpenXml, page, QSizeF(595.0, 842.0));
    QVERIFY(docx.ok);
    const QByteArray document =
        aipdf::doc::zip::readEntry(docx.bytes, QStringLiteral("word/document.xml"));
    QVERIFY(document.contains(QString::fromUtf8("中文").toUtf8()));
}

void OfficeExportTests::emptyPageExportsWithoutCrashing() {
    const TextPage empty = makePage({});
    for (ExportFormat format : allFormats()) {
        const ExportResult result = aipdf::doc::exportPageTo(format, empty, QSizeF(595.0, 842.0));
        QVERIFY2(result.ok, qPrintable(QStringLiteral("空页导出失败：%1").arg(result.error)));
    }
}

void OfficeExportTests::capabilityReportsWhatEachFormatKeeps() {
    QSet<QString> limitations;
    for (ExportFormat format : allFormats()) {
        const aipdf::doc::ExportCapability capability = aipdf::doc::exportCapability(format);
        QVERIFY(capability.keepsText);
        QVERIFY(!capability.limitation.isEmpty());
        QVERIFY(!capability.keepsImages);
        QVERIFY(!capability.keepsTables);
        limitations.insert(capability.limitation);
    }
    // 三种格式的局限说明必须各不相同，否则就是没认真写。
    QCOMPARE(limitations.size(), 3);
}

void OfficeExportTests::suggestedSuffixMatchesFormat() {
    const TextPage page = samplePage();
    QCOMPARE(aipdf::doc::exportPageTo(ExportFormat::WordOpenXml, page, QSizeF(595.0, 842.0))
                 .suggestedSuffix,
             QStringLiteral("docx"));
    QCOMPARE(aipdf::doc::exportPageTo(ExportFormat::Rtf, page, QSizeF(595.0, 842.0)).suggestedSuffix,
             QStringLiteral("rtf"));
    QCOMPARE(aipdf::doc::exportPageTo(ExportFormat::PlainText, page, QSizeF(595.0, 842.0))
                 .suggestedSuffix,
             QStringLiteral("txt"));
}

void OfficeExportTests::failedExportReturnsEmptyBytes() {
    // 越界的格式值：必须失败，并且不给任何字节。
    const ExportResult result =
        aipdf::doc::exportPageTo(static_cast<ExportFormat>(99), samplePage(), QSizeF(595.0, 842.0));
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(!result.error.isEmpty());
    // 失败原因必须是中文（不含 ASCII 句子）。
    QVERIFY(result.error.contains(QStringLiteral("导出失败")));
}

void OfficeExportTests::multipleLinesAreOrderedTopToBottom() {
    // 两行：上面一行 y=700，下面一行 y=500（PDF 的 y 向上）。
    // 导出结果必须是「上面那行在前」。
    const TextPage page = makePage({
        makeChar(QStringLiteral("Second"), 50.0, 500.0, 60.0, 12.0),
        makeChar(QStringLiteral("First"), 50.0, 700.0, 60.0, 12.0),
    });
    const ExportResult result =
        aipdf::doc::exportPageTo(ExportFormat::PlainText, page, QSizeF(595.0, 842.0));
    QVERIFY(result.ok);
    QCOMPARE(QString::fromUtf8(result.bytes), QStringLiteral("First\nSecond"));
}

QTEST_GUILESS_MAIN(OfficeExportTests)

#include "OfficeExportTests.moc"
