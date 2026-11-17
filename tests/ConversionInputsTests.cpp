// 转换输入三件套（剪贴板 / 网页 / 表单文件）的单元测试。
//
// 覆盖三组能力，每组都带负例：
//   1. 剪贴板文本 → PDF：有文本就出 PDF，没文本必须明确报错；
//   2. 网页 HTML → PDF：剥掉脚本/样式/导航再排版，没有正文要报错；
//   3. 表单数据文件 → 模板：FDF / XFDF / XML 三种格式解析，套进模板后
//      **重新用 PDFium 打开读回**，确认值真的写进去了、而且表单仍然可填写。
//
// 为什么用 PDFium 验读回：用 qpdf 只能证明结构没坏，证明不了「别的阅读器
// 看得见」——这是本仓库的硬规矩（见记忆里的教训）。
//
// 为什么自己合成 TrueType 字体：让「嵌入中文字体」这条路径有个不依赖本机
// 字体文件的确定性测试（合成字体只含 .notdef、空格、A、B、中五个字形）。

#include "app/ConversionInputDialog.h"
#include "doc/ConversionInputs.h"
#include "pdf/PdfiumAdapter.h"

#include <QByteArray>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::doc::ConversionResult;
using aipdf::doc::FormDataFormat;
using aipdf::doc::FormDataParseResult;
using aipdf::doc::FormFillReport;
using aipdf::doc::FormFieldValue;
using aipdf::doc::HtmlBody;
using aipdf::doc::UniversalConverterOptions;

namespace {

// ======================================================== 合成 TrueType =====
//
// 造一份最小可用的 TrueType 字体：5 个字形（.notdef、空格、A、B、中）。
// 抄自 tests/UniversalConverterTests.cpp 的同一套构造，两处各留一份是刻意的：
// 共享一份测试辅助代码就得再开一个头文件，收益不抵「多一个要维护的文件」。

void appendU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendS16(QByteArray& out, qint16 value) {
    appendU16(out, static_cast<quint16>(value));
}

void appendU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

// 一个方块字形（一个轮廓、四个点）。
QByteArray squareGlyph() {
    QByteArray out;
    appendS16(out, 1);    // numberOfContours
    appendS16(out, 0);    // xMin
    appendS16(out, 0);    // yMin
    appendS16(out, 800);  // xMax
    appendS16(out, 800);  // yMax
    appendU16(out, 3);    // endPtsOfContours[0]
    appendU16(out, 0);    // instructionLength
    for (int index = 0; index < 4; ++index) {
        out.append(static_cast<char>(0x01));  // on-curve，x/y 走 int16 增量
    }
    appendS16(out, 0);
    appendS16(out, 800);
    appendS16(out, 0);
    appendS16(out, -800);
    appendS16(out, 0);
    appendS16(out, 0);
    appendS16(out, 800);
    appendS16(out, 0);
    while (out.size() % 4 != 0) {
        out.append('\0');
    }
    return out;
}

// cmap 子表（格式 4）：五段（空格、A、B、中、结束段）。
QByteArray buildCmap() {
    const quint16 startCodes[5] = {0x0020, 0x0041, 0x0042, 0x4E2D, 0xFFFF};
    const quint16 endCodes[5] = {0x0020, 0x0041, 0x0042, 0x4E2D, 0xFFFF};
    const qint16 idDeltas[5] = {static_cast<qint16>(4 - 0x0020), static_cast<qint16>(1 - 0x0041),
                                static_cast<qint16>(2 - 0x0042), static_cast<qint16>(3 - 0x4E2D), 1};

    QByteArray subtable;
    appendU16(subtable, 4);       // format
    appendU16(subtable, 56);      // length
    appendU16(subtable, 0);       // language
    appendU16(subtable, 10);      // segCountX2
    appendU16(subtable, 10);      // searchRange
    appendU16(subtable, 3);       // entrySelector
    appendU16(subtable, 0);       // rangeShift
    for (int index = 0; index < 5; ++index) {
        appendU16(subtable, endCodes[index]);
    }
    appendU16(subtable, 0);  // reservedPad
    for (int index = 0; index < 5; ++index) {
        appendU16(subtable, startCodes[index]);
    }
    for (int index = 0; index < 5; ++index) {
        appendS16(subtable, idDeltas[index]);
    }
    appendU16(subtable, 0);  // rangeOffset

    QByteArray cmap;
    appendU16(cmap, 0);  // version
    appendU16(cmap, 1);  // numTables
    appendU16(cmap, 3);  // platformID: Windows
    appendU16(cmap, 1);  // encodingID: Unicode BMP
    appendU32(cmap, 12); // 子表偏移
    cmap.append(subtable);
    return cmap;
}

// 造一份最小可用的 TrueType 字体（5 个字形：.notdef、空格、A、B、中）。
QByteArray buildTestFont() {
    const QByteArray glyph = squareGlyph();
    QByteArray glyf;
    glyf.append(QByteArray());  // gid 0：.notdef（空字形）
    glyf.append(glyph);         // gid 1：空格
    glyf.append(glyph);         // gid 2：A
    glyf.append(glyph);         // gid 3：B
    glyf.append(glyph);         // gid 4：中
    QByteArray loca;
    quint32 glyphCursor = 0;
    appendU32(loca, glyphCursor);
    for (int index = 0; index < 5; ++index) {
        glyphCursor += static_cast<quint32>(glyph.size());
        appendU32(loca, glyphCursor);
    }

    QByteArray hmtx;
    for (int index = 0; index < 5; ++index) {
        appendU16(hmtx, 1000);
        appendS16(hmtx, 0);
    }

    QByteArray head;
    appendU32(head, 0x00010000);
    appendU32(head, 0x00010000);
    appendU32(head, 0);
    appendU32(head, 0x5F0F3CF5);
    appendU16(head, 0x0003);
    appendU16(head, 1000);
    appendU32(head, 0);
    appendU32(head, 0);
    appendU32(head, 0);
    appendU32(head, 0);
    appendS16(head, 0);
    appendS16(head, 0);
    appendS16(head, 800);
    appendS16(head, 800);
    appendU16(head, 0);
    appendU16(head, 8);
    appendS16(head, 2);
    appendS16(head, 1);
    appendS16(head, 0);

    QByteArray hhea;
    appendU32(hhea, 0x00010000);
    appendS16(hhea, 800);
    appendS16(hhea, -200);
    appendS16(hhea, 0);
    appendU16(hhea, 1000);
    appendS16(hhea, 0);
    appendS16(hhea, 0);
    appendS16(hhea, 800);
    appendS16(hhea, 1);
    appendS16(hhea, 0);
    appendS16(hhea, 0);
    for (int index = 0; index < 4; ++index) {
        appendS16(hhea, 0);
    }
    appendS16(hhea, 0);
    appendU16(hhea, 5);

    QByteArray maxp;
    appendU32(maxp, 0x00010000);
    appendU16(maxp, 5);
    appendU16(maxp, 4);
    appendU16(maxp, 1);
    for (int index = 0; index < 11; ++index) {
        appendU16(maxp, 0);
    }

    QByteArray post;
    appendU32(post, 0x00030000);
    appendU32(post, 0);
    appendS16(post, -100);
    appendS16(post, 50);
    appendU32(post, 0);
    for (int index = 0; index < 4; ++index) {
        appendU32(post, 0);
    }

    QVector<QPair<QByteArray, QByteArray>> tables;
    tables.append(qMakePair(QByteArrayLiteral("cmap"), buildCmap()));
    tables.append(qMakePair(QByteArrayLiteral("glyf"), glyf));
    tables.append(qMakePair(QByteArrayLiteral("head"), head));
    tables.append(qMakePair(QByteArrayLiteral("hhea"), hhea));
    tables.append(qMakePair(QByteArrayLiteral("hmtx"), hmtx));
    tables.append(qMakePair(QByteArrayLiteral("loca"), loca));
    tables.append(qMakePair(QByteArrayLiteral("maxp"), maxp));
    tables.append(qMakePair(QByteArrayLiteral("post"), post));

    const int tableCount = static_cast<int>(tables.size());
    const int directorySize = 12 + 16 * tableCount;
    QVector<quint32> offsets;
    quint32 cursor = static_cast<quint32>(directorySize);
    for (const auto& table : tables) {
        offsets.append(cursor);
        cursor += static_cast<quint32>(table.second.size());
    }

    QByteArray out;
    appendU32(out, 0x00010000);
    appendU16(out, static_cast<quint16>(tableCount));
    appendU16(out, static_cast<quint16>(tableCount * 16));
    appendU16(out, 3);
    appendU16(out, 0);
    for (int index = 0; index < tableCount; ++index) {
        out.append(tables.at(index).first);
        appendU32(out, 0);
        appendU32(out, offsets.at(index));
        appendU32(out, static_cast<quint32>(tables.at(index).second.size()));
    }
    for (const auto& table : tables) {
        out.append(table.second);
    }
    return out;
}

// ============================================================ 读回验证 =====

// 去掉所有空白，便于比较「抽出来的文字」与「源文字」。
QString squeezeWhitespace(const QString& text) {
    QString result;
    for (const QChar ch : text) {
        if (!ch.isSpace()) {
            result.append(ch);
        }
    }
    return result;
}

// 用 PDFium 重开生成的 PDF，拿页数与每页文字。
struct PdfProbe {
    bool opened = false;
    int pageCount = 0;
    QStringList pageTexts;
    QString allText() const { return pageTexts.join(QString()); }
};

PdfProbe probeWithPdfium(const QByteArray& bytes) {
    PdfProbe probe;
    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(bytes, QStringLiteral("probe.pdf"));
    if (!opened.document.isValid()) {
        return probe;
    }
    probe.opened = true;
    probe.pageCount = opened.document.pageCount();
    for (int page = 0; page < opened.document.pageCount(); ++page) {
        const std::optional<aipdf::pdf::PdfPage> pdfPage = opened.document.page(page);
        if (!pdfPage.has_value()) {
            continue;
        }
        const std::optional<aipdf::pdf::TextPage> text = pdfPage->extractText();
        if (!text.has_value()) {
            continue;
        }
        probe.pageTexts.append(text->plainText());
    }
    return probe;
}

// 读测试夹具（scripts/generate-test-pdfs.py 生成的固定装置）。
QByteArray readFixture(const QString& name) {
    QFile file(QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes;
}

// 在模板里按名字找字段。
QVector<aipdf::pdf::FormField> fieldsNamed(const QVector<aipdf::pdf::FormField>& fields,
                                           const QString& name) {
    QVector<aipdf::pdf::FormField> matched;
    for (const aipdf::pdf::FormField& field : fields) {
        if (field.name == name) {
            matched.append(field);
        }
    }
    return matched;
}

}  // namespace

class ConversionInputsTests : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- 剪贴板 ----
    void clipboardTextProducesReadablePdf();
    void emptyClipboardTextIsRejected();
    void whitespaceOnlyClipboardTextIsRejected();
    void clipboardChineseSurvivesWithEmbeddedFont();
    void clipboardTextReportsPlainTextSource();
    void clipboardHtmlProducesReadablePdf();
    void clipboardHtmlReportsRichTextSource();
    void clipboardHtmlLayoutDiffersFromPlainText();
    void clipboardHtmlWithoutBodyIsRejected();
    void clipboardHtmlWithOnlyStyleIsRejected();

    // ------------------------------------------------------------ 网页 ----
    void htmlBodyDropsScriptStyleAndNavigation();
    void htmlToPdfProducesReadablePdf();
    void htmlWithoutBodyTextIsRejected();
    void htmlWithOnlyScriptIsRejected();
    void relativeLinksAreResolvedAgainstBaseUrl();
    void brokenHtmlStillYieldsText();

    // -------------------------------------------------------- 表单解析 ----
    void fdfValuesAreParsed();
    void xfdfValuesAreParsed();
    void xmlValuesAreParsed();
    void emptyFormDataFileIsRejected();
    void fdfWithoutFieldsIsRejected();
    void unknownFormDataFormatIsRejected();

    // -------------------------------------------------------- 套模板 ----
    void formDataIsAppliedToTemplate();
    void readOnlyFieldIsListedAsUnfilled();
    void missingTemplateIsRejected();
    void templateWithoutFormFieldsIsRejected();
    void unknownFieldIsListedAsIssue();
    void brokenTemplateBytesAreRejected();

    // ---------------------------------------------------------- 界面层 ----
    void dialogRejectsEmptyUrl();
    void dialogRejectsNonHttpUrl();
    void dialogClipboardModeUsesClipboard();
    void dialogPrefersClipboardHtmlOverText();
    void dialogRejectsUnreadableTemplateFile();
    void dialogRejectsImageOnlyClipboard();
};

// ============================================================ 剪贴板 =====

void ConversionInputsTests::clipboardTextProducesReadablePdf() {
    const ConversionResult result =
        aipdf::doc::clipboardTextToPdf(QStringLiteral("Hello PDF Editor\nSecond line"),
                                        QStringLiteral("Greeting"));
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.title, QStringLiteral("Greeting"));

    // 读回：PDFium 要能打开，而且文字要抽得回来。
    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(probe.pageCount, 1);
    const QString extracted = squeezeWhitespace(probe.allText());
    QVERIFY(extracted.contains(QStringLiteral("HelloPDFEditor")));
    QVERIFY(extracted.contains(QStringLiteral("Secondline")));
    QVERIFY(extracted.contains(QStringLiteral("Greeting")));
}

void ConversionInputsTests::emptyClipboardTextIsRejected() {
    // 负例：剪贴板里没有文本时必须明确报错，不能静默生成一份空白 PDF。
    const ConversionResult result = aipdf::doc::clipboardTextToPdf(QString());
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("剪贴板")));
}

void ConversionInputsTests::whitespaceOnlyClipboardTextIsRejected() {
    // 负例：只有空白也算「没有文本」。
    const ConversionResult result =
        aipdf::doc::clipboardTextToPdf(QStringLiteral("   \n\t  \n "));
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("剪贴板")));
}

void ConversionInputsTests::clipboardChineseSurvivesWithEmbeddedFont() {
    UniversalConverterOptions options;
    options.regularFont.bytes = buildTestFont();
    options.regularFont.postScriptName = QStringLiteral("AipdfTestFont");
    options.boldFont = options.regularFont;

    const ConversionResult result =
        aipdf::doc::clipboardTextToPdf(QStringLiteral("A 中 B"), QString(), options);
    QVERIFY(result.ok);
    // 合成字体里有 A、中、B：不该有替换。
    QCOMPARE(result.replacedCharacterCount, 0);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(squeezeWhitespace(probe.allText()), QStringLiteral("A中B"));
}

void ConversionInputsTests::clipboardTextReportsPlainTextSource() {
    // 结果里要能看出这次走的是纯文本这一路。
    const ConversionResult result = aipdf::doc::clipboardTextToPdf(QStringLiteral("plain text"));
    QVERIFY(result.ok);
    QCOMPARE(result.sourceKind, aipdf::doc::ConversionSourceKind::PlainText);
}

void ConversionInputsTests::clipboardHtmlProducesReadablePdf() {
    // 正例：剪贴板里的富文本排出来是一份能读回来的 PDF。
    const QString html = QStringLiteral(
        "<html><body><h1>Rich Title</h1><p>First paragraph</p></body></html>");
    const ConversionResult result = aipdf::doc::clipboardHtmlToPdf(html);
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.pageCount, 1);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    const QString extracted = squeezeWhitespace(probe.allText());
    QVERIFY(extracted.contains(QStringLiteral("RichTitle")));
    QVERIFY(extracted.contains(QStringLiteral("Firstparagraph")));
}

void ConversionInputsTests::clipboardHtmlReportsRichTextSource() {
    // 结果里要能看出这次走的是富文本这一路。
    const ConversionResult result = aipdf::doc::clipboardHtmlToPdf(
        QStringLiteral("<html><body><p>Rich body</p></body></html>"));
    QVERIFY(result.ok);
    QCOMPARE(result.sourceKind, aipdf::doc::ConversionSourceKind::RichText);
}

void ConversionInputsTests::clipboardHtmlLayoutDiffersFromPlainText() {
    // 正例：富文本与纯文本两条路排出来的东西必须不一样（用户要能看出排版差别）。
    const QString html = QStringLiteral(
        "<html><body><p>First paragraph</p>"
        "<a href=\"https://example.com/rich\">Link</a></body></html>");
    const ConversionResult rich = aipdf::doc::clipboardHtmlToPdf(html);
    const ConversionResult plain =
        aipdf::doc::clipboardTextToPdf(QStringLiteral("First paragraph\nLink"));
    QVERIFY(rich.ok);
    QVERIFY(plain.ok);
    QVERIFY(rich.bytes != plain.bytes);

    const QString richText = squeezeWhitespace(probeWithPdfium(rich.bytes).allText());
    const QString plainText = squeezeWhitespace(probeWithPdfium(plain.bytes).allText());
    QVERIFY(richText != plainText);
    // 差别来自富文本路径保住的页面结构（这里是把正文里的链接也排了进去）。
    QVERIFY(richText.contains(QStringLiteral("https://example.com/rich")));
    QVERIFY(!plainText.contains(QStringLiteral("https://example.com/rich")));
}

void ConversionInputsTests::clipboardHtmlWithoutBodyIsRejected() {
    // 负例：HTML 里没有正文 —— 必须明确失败，不能出一份空白 PDF。
    const ConversionResult result = aipdf::doc::clipboardHtmlToPdf(
        QStringLiteral("<html><head><style>p{color:red}</style></head><body></body></html>"));
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::NoHtmlBody);
    QVERIFY(!result.error.isEmpty());
}

void ConversionInputsTests::clipboardHtmlWithOnlyStyleIsRejected() {
    // 负例：整段都是脚本与样式 —— 同样必须明确失败。
    const ConversionResult result = aipdf::doc::clipboardHtmlToPdf(
        QStringLiteral("<html><body><script>var x = 1;</script><style>a{}</style></body></html>"));
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::NoHtmlBody);
}

// ============================================================ 网页 =====

void ConversionInputsTests::htmlBodyDropsScriptStyleAndNavigation() {
    const QString html =
        QStringLiteral(
            "<html><head><title>Article</title><style>body{color:red}</style></head>"
            "<body><nav>Menu: <a href='/home'>Home</a></nav>"
            "<header>Site header</header>"
            "<article><h1>Real Title</h1><p>Body text.</p>"
            "<script>alert('boom')</script></article>"
            "<footer>Copyright</footer></body></html>");
    const HtmlBody body = aipdf::doc::extractHtmlBody(html, QUrl(QStringLiteral("https://example.com/a/b")));
    QCOMPARE(body.title, QStringLiteral("Article"));
    QVERIFY(body.text.contains(QStringLiteral("Real Title")));
    QVERIFY(body.text.contains(QStringLiteral("Body text.")));
    // 脚本、样式、导航、页眉页脚都不能进正文。
    QVERIFY(!body.text.contains(QStringLiteral("alert")));
    QVERIFY(!body.text.contains(QStringLiteral("color:red")));
    QVERIFY(!body.text.contains(QStringLiteral("Menu")));
    QVERIFY(!body.text.contains(QStringLiteral("Home")));
    QVERIFY(!body.text.contains(QStringLiteral("Site header")));
    QVERIFY(!body.text.contains(QStringLiteral("Copyright")));

    // 没有 <article> 时也要剥掉导航：正文区退回 <body>。
    const QString simple =
        QStringLiteral("<html><body><nav>Menu</nav><p>Only paragraph</p></body></html>");
    const HtmlBody plain = aipdf::doc::extractHtmlBody(simple, QUrl());
    QVERIFY(!plain.text.contains(QStringLiteral("Menu")));
    QVERIFY(plain.text.contains(QStringLiteral("Only paragraph")));
}

void ConversionInputsTests::htmlToPdfProducesReadablePdf() {
    const QString html =
        QStringLiteral("<html><head><title>Doc Title</title></head>"
                       "<body><article><p>Hello web</p></article></body></html>");
    const ConversionResult result =
        aipdf::doc::htmlToPdf(html, QUrl(QStringLiteral("https://example.com/")));
    QVERIFY(result.ok);
    QCOMPARE(result.title, QStringLiteral("Doc Title"));
    QVERIFY(!result.bytes.isEmpty());

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QVERIFY(probe.pageCount >= 1);
    const QString extracted = squeezeWhitespace(probe.allText());
    QVERIFY(extracted.contains(QStringLiteral("Helloweb")));
    QVERIFY(extracted.contains(QStringLiteral("DocTitle")));
}

void ConversionInputsTests::htmlWithoutBodyTextIsRejected() {
    // 负例：页面只有标题没有正文时，不能生成一份只有标题的空壳 PDF。
    const ConversionResult result = aipdf::doc::htmlToPdf(
        QStringLiteral("<html><head><title>Only title</title></head><body></body></html>"),
        QUrl());
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    // 失败原因用结构化的种类表示（给用户看的中文句子由 app 层拼）。
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::NoHtmlBody);
    QVERIFY(!result.error.isEmpty());
}

void ConversionInputsTests::htmlWithOnlyScriptIsRejected() {
    // 负例：整页都是脚本与样式（正文为空）时必须报错。
    const ConversionResult result = aipdf::doc::htmlToPdf(
        QStringLiteral("<html><body><script>var x = 1;</script><style>a{}</style></body></html>"),
        QUrl());
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::NoHtmlBody);
    QVERIFY(!result.error.isEmpty());
}

void ConversionInputsTests::relativeLinksAreResolvedAgainstBaseUrl() {
    const QString html =
        QStringLiteral("<html><body><p><a href=\"/about\">About</a>"
                       "<a href=\"https://other.example/x\">Other</a></p></body></html>");
    const HtmlBody body =
        aipdf::doc::extractHtmlBody(html, QUrl(QStringLiteral("https://example.com/docs/page.html")));
    QVERIFY(body.links.contains(QStringLiteral("https://example.com/about")));
    QVERIFY(body.links.contains(QStringLiteral("https://other.example/x")));
}

void ConversionInputsTests::brokenHtmlStillYieldsText() {
    // 负例（残缺标记）：不能崩，也不能把已经读到的文字丢掉。
    const QString html = QStringLiteral("<html><body><p>Unclosed paragraph <b>bold</body>");
    const HtmlBody body = aipdf::doc::extractHtmlBody(html, QUrl());
    QVERIFY(body.text.contains(QStringLiteral("Unclosed")));
    QVERIFY(body.text.contains(QStringLiteral("bold")));
}

// ============================================================ 表单解析 =====

void ConversionInputsTests::fdfValuesAreParsed() {
    const QByteArray fdf =
        "%FDF-1.2\n1 0 obj\n<< /FDF << /Fields [ << /T (name) /V (Alice) >> "
        "<< /T (city) /V (Beijing) >> ] >> >>\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n";
    const FormDataParseResult result = aipdf::doc::parseFormDataFile(fdf, QStringLiteral("data.fdf"));
    QVERIFY(result.ok);
    QCOMPARE(result.format, FormDataFormat::Fdf);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0).name, QStringLiteral("name"));
    QCOMPARE(result.values.at(0).value, QStringLiteral("Alice"));
    QCOMPARE(result.values.at(1).name, QStringLiteral("city"));
    QCOMPARE(result.values.at(1).value, QStringLiteral("Beijing"));
}

void ConversionInputsTests::xfdfValuesAreParsed() {
    const QByteArray xfdf =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><fields>"
        "<field name=\"name\"><value>Alice</value></field>"
        "<field name=\"note\"><value>hello</value></field>"
        "</fields></xfdf>";
    const FormDataParseResult result =
        aipdf::doc::parseFormDataFile(xfdf, QStringLiteral("data.xfdf"));
    QVERIFY(result.ok);
    QCOMPARE(result.format, FormDataFormat::Xfdf);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0).name, QStringLiteral("name"));
    QCOMPARE(result.values.at(0).value, QStringLiteral("Alice"));
    QCOMPARE(result.values.at(1).value, QStringLiteral("hello"));
}

void ConversionInputsTests::xmlValuesAreParsed() {
    const QByteArray xml =
        "<form-data><field name=\"name\">Alice</field>"
        "<field name=\"note\"><value>hi</value></field></form-data>";
    const FormDataParseResult result =
        aipdf::doc::parseFormDataFile(xml, QStringLiteral("data.xml"));
    QVERIFY(result.ok);
    QCOMPARE(result.format, FormDataFormat::Xml);
    QCOMPARE(result.values.size(), 2);
    QCOMPARE(result.values.at(0).name, QStringLiteral("name"));
    QCOMPARE(result.values.at(0).value, QStringLiteral("Alice"));
    QCOMPARE(result.values.at(1).name, QStringLiteral("note"));
    QCOMPARE(result.values.at(1).value, QStringLiteral("hi"));
}

void ConversionInputsTests::emptyFormDataFileIsRejected() {
    // 负例：空文件必须报错，不能当成「没有要填的字段」就当成功。
    const FormDataParseResult result =
        aipdf::doc::parseFormDataFile(QByteArray(), QStringLiteral("data.fdf"));
    QVERIFY(!result.ok);
    QVERIFY(result.values.isEmpty());
    QVERIFY(!result.error.isEmpty());
}

void ConversionInputsTests::fdfWithoutFieldsIsRejected() {
    // 负例：扩展名对但里面没有字段值 —— 也要明确失败。
    const FormDataParseResult result = aipdf::doc::parseFormDataFile(
        QByteArray("%FDF-1.2\n1 0 obj\n<< /FDF << /Fields [] >> >>\nendobj\n%%EOF\n"),
        QStringLiteral("data.fdf"));
    QVERIFY(!result.ok);
    QVERIFY(result.values.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("FDF")));
}

void ConversionInputsTests::unknownFormDataFormatIsRejected() {
    // 负例：既不是 FDF 也不是 XML 的内容，格式认不出来。
    const FormDataParseResult result = aipdf::doc::parseFormDataFile(
        QByteArray("just some plain text, not form data"), QStringLiteral("data.txt"));
    QVERIFY(!result.ok);
    QVERIFY(result.values.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("格式")));
}

// ============================================================ 套模板 =====

void ConversionInputsTests::formDataIsAppliedToTemplate() {
    const QByteArray templateBytes = readFixture(QStringLiteral("form.pdf"));
    QVERIFY2(!templateBytes.isEmpty(), "找不到 form.pdf 夹具");

    QVector<FormFieldValue> values;
    values.append(FormFieldValue{QStringLiteral("fullName"), QStringLiteral("Zhang San")});
    values.append(FormFieldValue{QStringLiteral("country"), QStringLiteral("Other")});

    const FormFillReport report = aipdf::doc::applyFormDataToTemplate(templateBytes, values);
    QVERIFY(report.ok);
    QVERIFY(report.bytes.startsWith("%PDF"));
    QCOMPARE(report.applied.size(), 2);
    QVERIFY(report.issues.isEmpty());
    QCOMPARE(report.templateFieldCount, 4);

    // 读回：PDFium 重开之后，值要真的写进去了。
    aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(report.bytes, QStringLiteral("filled.pdf"));
    QVERIFY(opened.document.isValid());
    const QVector<aipdf::pdf::FormField> fields = opened.document.formFields();
    const QVector<aipdf::pdf::FormField> names = fieldsNamed(fields, QStringLiteral("fullName"));
    QCOMPARE(names.size(), 1);
    QCOMPARE(names.at(0).value, QStringLiteral("Zhang San"));
    const QVector<aipdf::pdf::FormField> countries = fieldsNamed(fields, QStringLiteral("country"));
    QCOMPARE(countries.size(), 1);
    QCOMPARE(countries.at(0).value, QStringLiteral("Other"));

    // 仍然是**可填写**的表单：字段还在，而且还能再写一次。
    QVERIFY(opened.document.setFormFieldValue(0, QStringLiteral("fullName"),
                                               QStringLiteral("Li Si")));
}

void ConversionInputsTests::readOnlyFieldIsListedAsUnfilled() {
    // 负例：模板里的只读域填不进去 —— 必须逐个列出来，不能静默跳过。
    const QByteArray templateBytes = readFixture(QStringLiteral("form.pdf"));
    QVERIFY2(!templateBytes.isEmpty(), "找不到 form.pdf 夹具");

    QVector<FormFieldValue> values;
    values.append(FormFieldValue{QStringLiteral("issuedBy"), QStringLiteral("Someone")});

    const FormFillReport report = aipdf::doc::applyFormDataToTemplate(templateBytes, values);
    QVERIFY(report.ok);
    QVERIFY(report.applied.isEmpty());
    QCOMPARE(report.issues.size(), 1);
    QCOMPARE(report.issues.at(0).name, QStringLiteral("issuedBy"));
    QVERIFY(report.issues.at(0).reason.contains(QStringLiteral("只读")));
}

void ConversionInputsTests::missingTemplateIsRejected() {
    // 负例：没有模板就没法填，必须明说。
    QVector<FormFieldValue> values;
    values.append(FormFieldValue{QStringLiteral("fullName"), QStringLiteral("Zhang San")});
    const FormFillReport report = aipdf::doc::applyFormDataToTemplate(QByteArray(), values);
    QVERIFY(!report.ok);
    QVERIFY(report.bytes.isEmpty());
    QVERIFY(report.error.contains(QStringLiteral("模板")));
}

void ConversionInputsTests::templateWithoutFormFieldsIsRejected() {
    // 负例：模板里根本没有表单域 —— 也要明说，不能报「成功」。
    const QByteArray templateBytes = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY2(!templateBytes.isEmpty(), "找不到 normal.pdf 夹具");
    QVector<FormFieldValue> values;
    values.append(FormFieldValue{QStringLiteral("fullName"), QStringLiteral("Zhang San")});
    const FormFillReport report = aipdf::doc::applyFormDataToTemplate(templateBytes, values);
    QVERIFY(!report.ok);
    QVERIFY(report.bytes.isEmpty());
    QVERIFY(report.error.contains(QStringLiteral("表单域")));
}

void ConversionInputsTests::unknownFieldIsListedAsIssue() {
    // 负例：表单数据里有模板没有的字段 —— 逐个列出来，不静默跳过。
    const QByteArray templateBytes = readFixture(QStringLiteral("form.pdf"));
    QVERIFY2(!templateBytes.isEmpty(), "找不到 form.pdf 夹具");

    QVector<FormFieldValue> values;
    values.append(FormFieldValue{QStringLiteral("nickname"), QStringLiteral("Nobody")});

    const FormFillReport report = aipdf::doc::applyFormDataToTemplate(templateBytes, values);
    QVERIFY(report.ok);
    QVERIFY(report.applied.isEmpty());
    QCOMPARE(report.issues.size(), 1);
    QCOMPARE(report.issues.at(0).name, QStringLiteral("nickname"));
    QVERIFY(report.issues.at(0).reason.contains(QStringLiteral("没有同名")));
}

void ConversionInputsTests::brokenTemplateBytesAreRejected() {
    // 负例：模板字节坏了 —— 明确失败，不是「填了个寂寞」。
    QVector<FormFieldValue> values;
    values.append(FormFieldValue{QStringLiteral("fullName"), QStringLiteral("Zhang San")});
    const FormFillReport report =
        aipdf::doc::applyFormDataToTemplate(QByteArray("this is not a pdf"), values);
    QVERIFY(!report.ok);
    QVERIFY(report.bytes.isEmpty());
    QVERIFY(!report.error.isEmpty());
}

// ============================================================ 界面层 =====

void ConversionInputsTests::dialogRejectsEmptyUrl() {
    // 负例：没填地址 —— 不能去抓一个空地址。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Web);
    dialog.setUrlText(QString());
    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(!dialog.convert(&outcome));
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.bytes.isEmpty());
    QVERIFY(outcome.error.contains(QStringLiteral("网址")));
}

void ConversionInputsTests::dialogRejectsNonHttpUrl() {
    // 负例：不是 http / https 的地址要单独说清楚，不能笼统报「抓取失败」。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Web);
    dialog.setUrlText(QStringLiteral("ftp://example.com/file"));
    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(!dialog.convert(&outcome));
    QVERIFY(outcome.error.contains(QStringLiteral("http")));
}

void ConversionInputsTests::dialogClipboardModeUsesClipboard() {
    // 正例：剪贴板模式读的是系统剪贴板，读回验证也要过。
    //
    // 剪贴板是**全机器共享**的：别的进程正开着剪贴板时，OpenClipboard 会失败，
    // 这条用例就会莫名其妙地红。系统剪贴板用不了就跳过 ——
    // 「剪贴板不可用」不是本功能的缺陷，让 CI 因此变红才是。
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        QSKIP("本机没有剪贴板");
    }
    clipboard->setText(QStringLiteral("Clipboard round trip"));
    if (!clipboard->text().contains(QStringLiteral("Clipboard round trip"))) {
        QSKIP("系统剪贴板当前不可用（可能被别的进程占用）");
    }
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Clipboard);
    QVERIFY(dialog.clipboardText().contains(QStringLiteral("Clipboard round trip")));

    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(dialog.convert(&outcome));
    QVERIFY(outcome.ok);
    QVERIFY(!outcome.bytes.isEmpty());
    QCOMPARE(outcome.pageCount, 1);

    const PdfProbe probe = probeWithPdfium(outcome.bytes);
    QVERIFY(probe.opened);
    QVERIFY(squeezeWhitespace(probe.allText()).contains(QStringLiteral("Clipboardroundtrip")));
}

void ConversionInputsTests::dialogPrefersClipboardHtmlOverText() {
    // 正例：剪贴板里有富文本时按富文本排版，结果里也要说明用的是哪一路。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Clipboard);
    dialog.setClipboardHtml(QStringLiteral(
        "<html><body><p>Rich body</p>"
        "<a href=\"https://example.com/rich\">Link</a></body></html>"));

    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(dialog.convert(&outcome));
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.sourceKind, aipdf::doc::ConversionSourceKind::RichText);

    bool mentionedRichText = false;
    for (const QString& line : outcome.detailLines) {
        if (line.contains(QStringLiteral("富文本"))) {
            mentionedRichText = true;
        }
    }
    QVERIFY(mentionedRichText);

    const PdfProbe probe = probeWithPdfium(outcome.bytes);
    QVERIFY(probe.opened);
    QVERIFY(squeezeWhitespace(probe.allText()).contains(QStringLiteral("https://example.com/rich")));
}

void ConversionInputsTests::dialogRejectsImageOnlyClipboard() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        QSKIP("本机没有剪贴板");
    }
    const QString savedText = clipboard->text();
    const QImage savedImage = clipboard->image();

    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    clipboard->setImage(image);
    if (!clipboard->mimeData()->hasImage()) {
        // 剪贴板被别的进程占着：先放回去再跳过。
        if (!savedImage.isNull()) {
            clipboard->setImage(savedImage);
        } else {
            clipboard->setText(savedText);
        }
        QSKIP("系统剪贴板当前不可用（可能被别的进程占用）");
    }

    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Clipboard);
    aipdf::app::ConversionInputOutcome outcome;
    dialog.convert(&outcome);
    const bool converted = outcome.ok;
    const QString error = outcome.error;

    // 还原剪贴板：测试不该把用户剪贴板里的东西弄丢。
    if (!savedImage.isNull()) {
        clipboard->setImage(savedImage);
    } else {
        clipboard->setText(savedText);
    }

    // 负例：剪贴板里只有图片 —— 明确说「不支持」，不能笼统报「没内容」。
    QVERIFY(!converted);
    QVERIFY(error.contains(QStringLiteral("图片")));
}

void ConversionInputsTests::dialogRejectsUnreadableTemplateFile() {
    // 负例：模板路径指向不存在的文件 —— 明确失败。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::FormFile);
    dialog.setTemplatePath(QStringLiteral("Z:/definitely-missing/template.pdf"));
    dialog.setFormDataPath(QStringLiteral("Z:/definitely-missing/data.fdf"));
    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(!dialog.convert(&outcome));
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.error.contains(QStringLiteral("读不了")));
}

QTEST_MAIN(ConversionInputsTests)
#include "ConversionInputsTests.moc"
