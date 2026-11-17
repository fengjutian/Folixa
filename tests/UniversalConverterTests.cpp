// UniversalConverter 的单元测试。
//
// 覆盖三件事：
//   1. 各文件类型的解析规则（Markdown 标题/列表、CSV 对齐、HTML 去标签、JSON 缩进）；
//   2. 负例（空字节、不支持的扩展名、非法 UTF-8、坏字体资源）；
//   3. 生成的 PDF 真的能被 PDFium 重开：页数对、文字抽得回来、超长行不越界。
//
// 为什么用 PDFium 验：用 qpdf 只能证明结构没坏，证明不了「别的阅读器看得见」。
// 合成字体（buildTestFont）是为了让「嵌入中文字体」这条路径有个不依赖本机
// 字体文件的确定性测试。

#include "doc/UniversalConverter.h"
#include "pdf/PdfiumAdapter.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::doc::UniversalConversionResult;
using aipdf::doc::UniversalConverterOptions;
using aipdf::doc::UniversalFileKind;
using aipdf::doc::UniversalTextLine;

namespace {

// ======================================================== 合成 TrueType =====
//
// 造一份最小可用的 TrueType 字体：4 个字形（.notdef、A、B、中）。
// 有了它，测试就不必依赖「这台机器上有没有装中文字体」。

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
    for (int index = 0; index < 5; ++index) {
        appendU16(subtable, 0);  // idRangeOffset
    }

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
    // glyf 与 loca。
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

    // hmtx：五个字形都是 1000/1000 em 宽。
    QByteArray hmtx;
    for (int index = 0; index < 5; ++index) {
        appendU16(hmtx, 1000);
        appendS16(hmtx, 0);
    }

    // head
    QByteArray head;
    appendU32(head, 0x00010000);   // version
    appendU32(head, 0x00010000);   // fontRevision
    appendU32(head, 0);            // checkSumAdjustment
    appendU32(head, 0x5F0F3CF5);   // magicNumber
    appendU16(head, 0x0003);       // flags
    appendU16(head, 1000);         // unitsPerEm
    appendU32(head, 0);
    appendU32(head, 0);            // created
    appendU32(head, 0);
    appendU32(head, 0);            // modified
    appendS16(head, 0);
    appendS16(head, 0);
    appendS16(head, 800);
    appendS16(head, 800);          // 包围盒
    appendU16(head, 0);            // macStyle
    appendU16(head, 8);            // lowestRecPPEM
    appendS16(head, 2);            // fontDirectionHint
    appendS16(head, 1);            // indexToLocFormat：长格式
    appendS16(head, 0);            // glyphDataFormat

    // hhea
    QByteArray hhea;
    appendU32(hhea, 0x00010000);
    appendS16(hhea, 800);   // ascender
    appendS16(hhea, -200);  // descender
    appendS16(hhea, 0);     // lineGap
    appendU16(hhea, 1000);  // advanceWidthMax
    appendS16(hhea, 0);
    appendS16(hhea, 0);
    appendS16(hhea, 800);   // xMaxExtent
    appendS16(hhea, 1);
    appendS16(hhea, 0);
    appendS16(hhea, 0);
    for (int index = 0; index < 4; ++index) {
        appendS16(hhea, 0);  // 保留
    }
    appendS16(hhea, 0);   // metricDataFormat
    appendU16(hhea, 5);   // numberOfHMetrics

    // maxp
    QByteArray maxp;
    appendU32(maxp, 0x00010000);
    appendU16(maxp, 5);  // numGlyphs
    appendU16(maxp, 4);  // maxPoints
    appendU16(maxp, 1);  // maxContours
    for (int index = 0; index < 11; ++index) {
        appendU16(maxp, 0);
    }

    // post（版本 3.0：没有字形名）
    QByteArray post;
    appendU32(post, 0x00030000);
    appendU32(post, 0);  // italicAngle
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
    appendU32(out, 0x00010000);        // sfntVersion
    appendU16(out, static_cast<quint16>(tableCount));
    appendU16(out, static_cast<quint16>(tableCount * 16));  // searchRange
    appendU16(out, 3);                // entrySelector
    appendU16(out, 0);                // rangeShift
    for (int index = 0; index < tableCount; ++index) {
        out.append(tables.at(index).first);
        appendU32(out, 0);  // checkSum（不校验，写 0）
        appendU32(out, offsets.at(index));
        appendU32(out, static_cast<quint32>(tables.at(index).second.size()));
    }
    for (const auto& table : tables) {
        out.append(table.second);
    }
    return out;
}

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
    double maxCharRight = 0.0;
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
        for (const aipdf::pdf::TextChar& character : text->chars) {
            probe.maxCharRight = qMax(probe.maxCharRight, character.box.right());
        }
    }
    return probe;
}

}  // namespace

class UniversalConverterTests : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- 类型识别 ----
    void supportedTypesAreRecognized();
    void unsupportedTypeIsRejected();
    void unknownKindIsRejectedWithoutSuffix();
    void suggestedFileNameKeepsSourceName();

    // ------------------------------------------------------------ 负例 ----
    void emptyContentIsRejected();
    void invalidUtf8FailsWhenPolicyIsFail();
    void invalidUtf8IsReplacedWhenPolicyIsReplace();
    void invalidFontResourceIsRejected();
    void openTypeCffFontIsRejected();

    // ---------------------------------------------------------- 解析规则 ----
    void markdownHeadingsAndListsAreRecognized();
    void markdownInlineMarkersAreRemoved();
    void csvColumnsAreAligned();
    void htmlTagsAreStripped();
    void htmlScriptContentIsDropped();
    void jsonIsIndented();

    // ---------------------------------------------------------- PDF 输出 ----
    void plainTextProducesReadablePdf();
    void pdfPageCountMatchesLineCount();
    void extractedTextMatchesSource();
    void longLineWrapsInsidePage();
    void markdownHeadingUsesBoldFont();
    void chineseBecomesSubstituteWithoutFont();
    void embeddedFontKeepsChineseReadable();
    void linesPerPageMatchesPaging();
};

void UniversalConverterTests::supportedTypesAreRecognized() {
    QVERIFY(aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.txt")));
    QVERIFY(aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.md")));
    QVERIFY(aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.csv")));
    QVERIFY(aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.json")));
    QVERIFY(aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.html")));
    QVERIFY(!aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.docx")));
    QVERIFY(!aipdf::doc::isSupportedUniversalFile(QStringLiteral("a.pdf")));
    QVERIFY(!aipdf::doc::isSupportedUniversalFile(QStringLiteral("noextension")));

    QCOMPARE(aipdf::doc::universalFileKindFor(QStringLiteral("a.MD")), UniversalFileKind::Markdown);
    QCOMPARE(aipdf::doc::universalFileKindFor(QStringLiteral("a.htm")), UniversalFileKind::Html);
    QCOMPARE(aipdf::doc::universalFileKindFor(QStringLiteral("a.xls")), UniversalFileKind::Unknown);
    QCOMPARE(aipdf::doc::universalFileKindLabel(UniversalFileKind::Csv), QStringLiteral("CSV"));
}

void UniversalConverterTests::unsupportedTypeIsRejected() {
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("hello"), QStringLiteral("report.docx"));
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("不支持的文件类型")));
    // 报错要点名是哪一种类型，调用方才知道该怎么提示用户。
    QVERIFY(result.error.contains(QStringLiteral("docx")));
}

void UniversalConverterTests::unknownKindIsRejectedWithoutSuffix() {
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("hello"), UniversalFileKind::Unknown,
                                 QStringLiteral("report"));
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("不支持的文件类型")));
}

void UniversalConverterTests::suggestedFileNameKeepsSourceName() {
    QCOMPARE(aipdf::doc::suggestedPdfFileName(QStringLiteral("报告.md")),
             QStringLiteral("报告.md.pdf"));
    QCOMPARE(aipdf::doc::suggestedPdfFileName(QStringLiteral("a.txt")), QStringLiteral("a.txt.pdf"));
    QCOMPARE(aipdf::doc::suggestedPdfFileName(QString()), QStringLiteral("文档.pdf"));

    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("hi"), QStringLiteral("报告.md"));
    QVERIFY(result.ok);
    QCOMPARE(result.suggestedFileName, QStringLiteral("报告.md.pdf"));
}

void UniversalConverterTests::emptyContentIsRejected() {
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray(), QStringLiteral("a.txt"));
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("空")));
}

void UniversalConverterTests::invalidUtf8FailsWhenPolicyIsFail() {
    // 0xFF 开头的字节不是合法的 UTF-8。
    QByteArray broken("hello ");
    broken.append(static_cast<char>(0xFF));
    broken.append(static_cast<char>(0xFE));

    UniversalConverterOptions options;
    options.invalidUtf8 = aipdf::doc::InvalidUtf8Policy::Fail;
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(broken, QStringLiteral("a.txt"), options);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("UTF-8")));
}

void UniversalConverterTests::invalidUtf8IsReplacedWhenPolicyIsReplace() {
    QByteArray broken("hello ");
    broken.append(static_cast<char>(0xFF));
    broken.append(static_cast<char>(0xFE));

    UniversalConverterOptions options;
    options.invalidUtf8 = aipdf::doc::InvalidUtf8Policy::Replace;
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(broken, QStringLiteral("a.txt"), options);
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    // 非法字节被换成替代字符（U+FFFD），而不是被悄悄丢掉；
    // U+FFFD 在标准 14 字体里也显示不了，于是落成 '?'。
    const QString extracted = squeezeWhitespace(probe.pageTexts.join(QString()));
    QVERIFY(extracted.contains(QStringLiteral("hello")));
    QVERIFY(extracted.contains(QLatin1Char('?')));
    QCOMPARE(result.replacedCharacterCount, 2);
}

void UniversalConverterTests::invalidFontResourceIsRejected() {
    UniversalConverterOptions options;
    options.regularFont.bytes = QByteArray("this is definitely not a font file");
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("hello"), QStringLiteral("a.txt"), options);
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("字体")));
}

void UniversalConverterTests::openTypeCffFontIsRejected() {
    // 'OTTO'：CFF 轮廓的 OpenType，本模块只支持 TrueType。
    QByteArray openType;
    openType.append("OTTO");
    openType.append(QByteArray(8, '\0'));

    UniversalConverterOptions options;
    options.regularFont.bytes = openType;
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(QByteArray("hello"), QStringLiteral("a.txt"), options);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("OpenType")));
}

void UniversalConverterTests::markdownHeadingsAndListsAreRecognized() {
    const QByteArray source =
        "# 一级标题\n正文段落\n## 二级标题\n- 第一项\n  - 嵌套项\n1. 有序项\n> 引用\n";
    const QVector<UniversalTextLine> lines =
        aipdf::doc::parseUniversalText(source, UniversalFileKind::Markdown);
    QVERIFY(!lines.isEmpty());

    QCOMPARE(lines.at(0).headingLevel, 1);
    QCOMPARE(lines.at(0).text, QStringLiteral("一级标题"));
    QVERIFY(lines.at(0).bold);

    QCOMPARE(lines.at(1).headingLevel, 0);
    QCOMPARE(lines.at(1).text, QStringLiteral("正文段落"));

    QCOMPARE(lines.at(2).headingLevel, 2);
    QCOMPARE(lines.at(2).text, QStringLiteral("二级标题"));

    QVERIFY(lines.at(3).listItem);
    QCOMPARE(lines.at(3).text, QStringLiteral("第一项"));
    QCOMPARE(lines.at(3).indentLevel, 0);

    QVERIFY(lines.at(4).listItem);
    QCOMPARE(lines.at(4).text, QStringLiteral("嵌套项"));
    QVERIFY(lines.at(4).indentLevel > lines.at(3).indentLevel);

    QVERIFY(lines.at(5).listItem);
    QCOMPARE(lines.at(5).text, QStringLiteral("有序项"));

    QVERIFY(lines.at(6).indentLevel > 0);
    QCOMPARE(lines.at(6).text, QStringLiteral("引用"));
}

void UniversalConverterTests::markdownInlineMarkersAreRemoved() {
    const QByteArray source = "这里有 **加粗** 和 `代码`，还有 2 * 3 = 6。\n";
    const QVector<UniversalTextLine> lines =
        aipdf::doc::parseUniversalText(source, UniversalFileKind::Markdown);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.at(0).text.contains(QStringLiteral("加粗")));
    QVERIFY(lines.at(0).text.contains(QStringLiteral("代码")));
    QVERIFY(!lines.at(0).text.contains(QStringLiteral("**")));
    QVERIFY(!lines.at(0).text.contains(QLatin1Char('`')));
    // 单星号不该被吃掉（算式）。
    QVERIFY(lines.at(0).text.contains(QStringLiteral("2 * 3")));
}

void UniversalConverterTests::csvColumnsAreAligned() {
    const QByteArray source = "name,age\nAlice,30\nBob,7\n";
    const QVector<UniversalTextLine> lines =
        aipdf::doc::parseUniversalText(source, UniversalFileKind::Csv);
    QCOMPARE(lines.size(), 3);
    for (const UniversalTextLine& line : lines) {
        QVERIFY(line.monospace);
    }
    // 第一行是表头（加粗）。
    QVERIFY(lines.at(0).bold);
    QVERIFY(!lines.at(1).bold);

    // 第二列在每一行里的起始位置必须一致（这才是「对齐」）。
    const auto secondColumnStart = [](const QString& text) {
        int index = 0;
        while (index < text.size() && text.at(index) != QLatin1Char(' ')) {
            ++index;
        }
        while (index < text.size() && text.at(index) == QLatin1Char(' ')) {
            ++index;
        }
        return index;
    };
    QCOMPARE(secondColumnStart(lines.at(0).text), secondColumnStart(lines.at(1).text));
    QCOMPARE(secondColumnStart(lines.at(1).text), secondColumnStart(lines.at(2).text));
    // 第二列的内容确实是原表格里的内容。
    QVERIFY(lines.at(0).text.endsWith(QStringLiteral("age")));
    QVERIFY(lines.at(1).text.endsWith(QStringLiteral("30")));
    QVERIFY(lines.at(2).text.endsWith(QStringLiteral("7")));
}

void UniversalConverterTests::htmlTagsAreStripped() {
    const QByteArray source =
        "<html><body><h1>标题</h1><p>第一段</p><ul><li>甲</li><li>乙</li></ul></body></html>";
    const QVector<UniversalTextLine> lines =
        aipdf::doc::parseUniversalText(source, UniversalFileKind::Html);
    QString all;
    for (const UniversalTextLine& line : lines) {
        all += line.text;
    }
    QVERIFY(all.contains(QStringLiteral("标题")));
    QVERIFY(all.contains(QStringLiteral("第一段")));
    QVERIFY(all.contains(QStringLiteral("甲")));
    QVERIFY(!all.contains(QLatin1Char('<')));
    QVERIFY(!all.contains(QStringLiteral("h1")));
}

void UniversalConverterTests::htmlScriptContentIsDropped() {
    const QByteArray source =
        "<p>正文</p><script>var secret = 1;</script><style>p{color:red}</style>";
    const QVector<UniversalTextLine> lines =
        aipdf::doc::parseUniversalText(source, UniversalFileKind::Html);
    QString all;
    for (const UniversalTextLine& line : lines) {
        all += line.text;
    }
    QVERIFY(all.contains(QStringLiteral("正文")));
    QVERIFY(!all.contains(QStringLiteral("secret")));
    QVERIFY(!all.contains(QStringLiteral("color")));
}

void UniversalConverterTests::jsonIsIndented() {
    const QByteArray source = "{\"name\":\"x\",\"list\":[1,2]}";
    const QVector<UniversalTextLine> lines =
        aipdf::doc::parseUniversalText(source, UniversalFileKind::Json);
    QVERIFY(lines.size() > 1);
    QString all;
    for (const UniversalTextLine& line : lines) {
        all += line.text;
        all += QLatin1Char('\n');
    }
    QVERIFY(all.contains(QStringLiteral("\"name\"")));
    // 缩进：子项前面有空格。
    QVERIFY(all.contains(QStringLiteral("  \"name\"")));
}

void UniversalConverterTests::plainTextProducesReadablePdf() {
    const QByteArray source = "Hello world\nSecond line\nThird line";
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"));
    QVERIFY(result.ok);
    QVERIFY(!result.error.isEmpty() == false);
    QVERIFY(result.bytes.startsWith("%PDF-1.7"));
    QCOMPARE(result.pageCount, 1);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(probe.pageCount, 1);
}

void UniversalConverterTests::pdfPageCountMatchesLineCount() {
    const int lineCount = 120;
    QByteArray source;
    for (int index = 0; index < lineCount; ++index) {
        source += QStringLiteral("Line %1 of the sample document\n").arg(index).toUtf8();
    }
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"));
    QVERIFY(result.ok);

    const int perPage = aipdf::doc::linesPerPageFor(UniversalConverterOptions());
    QVERIFY(perPage > 0);
    const int expectedPages = (lineCount + perPage - 1) / perPage;
    QCOMPARE(result.pageCount, expectedPages);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(probe.pageCount, expectedPages);
}

void UniversalConverterTests::extractedTextMatchesSource() {
    const QByteArray source = "Hello world\nSecond line\nThird line";
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"));
    QVERIFY(result.ok);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(squeezeWhitespace(probe.pageTexts.join(QString())),
             squeezeWhitespace(QString::fromUtf8(source)));
}

void UniversalConverterTests::longLineWrapsInsidePage() {
    // 一行很长的句子：必须折行，而且折出来的每一段都不能超出页面右边。
    QString sentence;
    for (int index = 0; index < 60; ++index) {
        sentence += QStringLiteral("word%1 ").arg(index);
    }
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(sentence.toUtf8(), QStringLiteral("a.txt"));
    QVERIFY(result.ok);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    // 一页放不下这么长的一行：折行后仍然是 1 页，但文字分布在多行上。
    const QString extracted = probe.pageTexts.join(QString());
    QVERIFY(extracted.count(QLatin1Char('\r')) >= 1);

    // 页面右边是 595 点，边距 56 点：文字不能越过 539 点。
    QVERIFY2(probe.maxCharRight <= 539.5,
             qPrintable(QStringLiteral("文字越界，右边界=%1").arg(probe.maxCharRight)));
}

void UniversalConverterTests::markdownHeadingUsesBoldFont() {
    const QByteArray source = "# Heading\n\nbody\n";
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.md"));
    QVERIFY(result.ok);
    // 标题走粗体字面：PDF 里必须真的引了 Helvetica-Bold。
    QVERIFY(result.bytes.contains("/Helvetica-Bold"));
    // 标题字号比正文大。
    QVERIFY(result.bytes.contains("/F2 "));
}

void UniversalConverterTests::chineseBecomesSubstituteWithoutFont() {
    const QByteArray source = QStringLiteral("中文内容测试").toUtf8();
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"));
    QVERIFY(result.ok);
    // 没给字体时中文显示不了：必须**如实计数并告警**，不能默默丢字。
    QCOMPARE(result.replacedCharacterCount, 6);
    QVERIFY(!result.warning.isEmpty());
    QVERIFY(result.warning.contains(QStringLiteral("标准 14")));
    QVERIFY(!result.usedEmbeddedFont);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    // 每个中文字符都变成了替代字形 '?'，字符数一个不少。
    QCOMPARE(probe.pageTexts.join(QString()).count(QLatin1Char('?')), 6);
}

void UniversalConverterTests::embeddedFontKeepsChineseReadable() {
    UniversalConverterOptions options;
    options.regularFont.bytes = buildTestFont();
    options.regularFont.postScriptName = QStringLiteral("AipdfTestFont");

    const QByteArray source = QStringLiteral("A 中 B").toUtf8();
    const UniversalConversionResult result =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"), options);
    QVERIFY(result.ok);
    QVERIFY(result.usedEmbeddedFont);
    // 合成字体里有 A、中、B 三个字形：不该有替换。
    QCOMPARE(result.replacedCharacterCount, 0);

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(probe.pageCount, 1);
    // 嵌入字体路径：中文既显示得出来，也抽得回原文。
    const QString extracted = squeezeWhitespace(probe.pageTexts.join(QString()));
    QCOMPARE(extracted, QStringLiteral("A中B"));
}

void UniversalConverterTests::linesPerPageMatchesPaging() {
    const int perPage = aipdf::doc::linesPerPageFor(UniversalConverterOptions());
    QCOMPARE(perPage, 49);  // (842 - 2*56) / (11 * 1.35)

    QByteArray source;
    for (int index = 0; index < perPage; ++index) {
        source += "line\n";
    }
    const UniversalConversionResult exact =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"));
    QVERIFY(exact.ok);
    QCOMPARE(exact.pageCount, 1);

    source += "one more\n";
    const UniversalConversionResult overflow =
        aipdf::doc::convertToPdf(source, QStringLiteral("a.txt"));
    QVERIFY(overflow.ok);
    QCOMPARE(overflow.pageCount, 2);
}

QTEST_GUILESS_MAIN(UniversalConverterTests)
#include "UniversalConverterTests.moc"
