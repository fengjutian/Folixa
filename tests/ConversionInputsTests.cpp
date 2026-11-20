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

#include "app/dialogs/ConversionInputDialog.h"
#include "doc/convert/ConversionInputs.h"
#include "pdf/kernel/PdfiumAdapter.h"

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

// ============================================================ 造 PNG =====

// 为什么自己造 PNG 而不是用 QImageWriter 写一张：
// 部署包不一定带 Qt 的 PNG 图像插件（scripts/package.ps1 只保证 gif/ico/jpeg/svg），
// 夹具要是靠插件才造得出来，CI 上就会随机红。
//
// 抄自 tests/ImageToPdfTests.cpp 的同一套构造，两处各留一份是刻意的：
// 共享一份测试辅助代码就得再开一个头文件，收益不抵「多一个要维护的文件」。

// PNG 块的 CRC-32（按位版，与解码器里的查表版写法不同：两边独立实现，
// 测试才有资格发现解码器把 CRC 算错了）。
quint32 testCrc32(const QByteArray& data) {
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : data) {
        crc ^= static_cast<quint8>(ch);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendBE32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
}

void appendPngChunk(QByteArray& png, const char* type, const QByteArray& data) {
    appendBE32(png, static_cast<quint32>(data.size()));
    const QByteArray typed = QByteArray(type, 4) + data;
    png.append(typed);
    appendBE32(png, testCrc32(typed));
}

// 造一张 RGB 的 PNG（真 PNG，不是假头）。
//
// pixelsTopDown 是自上而下、每像素 3 字节 RGB 的原始像素。
// qCompress 的输出前面 4 字节是长度头，后面那段才是标准 zlib 流（带 0x78 头与
// adler32），正好是 PNG 要的。
QByteArray makePng(int width, int height, const QByteArray& pixelsTopDown) {
    QByteArray rawRows;
    for (int row = 0; row < height; ++row) {
        rawRows.append(static_cast<char>(0));  // 过滤器 0（None）
        rawRows.append(pixelsTopDown.mid(row * width * 3, width * 3));
    }
    QByteArray png;
    png.append("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr;
    appendBE32(ihdr, static_cast<quint32>(width));
    appendBE32(ihdr, static_cast<quint32>(height));
    ihdr.append(static_cast<char>(8));  // 位深
    ihdr.append(static_cast<char>(2));  // 颜色类型：真彩 RGB
    ihdr.append(static_cast<char>(0));  // 压缩方法
    ihdr.append(static_cast<char>(0));  // 过滤方法
    ihdr.append(static_cast<char>(0));  // 隔行
    appendPngChunk(png, "IHDR", ihdr);
    appendPngChunk(png, "IDAT", qCompress(rawRows).mid(4));
    appendPngChunk(png, "IEND", QByteArray());
    return png;
}

// 一张纯色图。
QByteArray makeSolidPng(int width, int height, char red, char green, char blue) {
    QByteArray pixels;
    for (int index = 0; index < width * height; ++index) {
        pixels.append(red);
        pixels.append(green);
        pixels.append(blue);
    }
    return makePng(width, height, pixels);
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

// 数一页里的图片对象。
//
// 为什么用 PDFium 数：数对象才能证明「图真的在 PDF 里」，
// 只看转换返回 true 证明不了任何事。
int countImageObjects(const aipdf::pdf::PdfPage& page) {
    const QRectF area(0.0, 0.0, page.widthPoints(), page.heightPoints());
    int count = 0;
    for (const aipdf::pdf::PageObjectInfo& info : page.objectsInRect(area)) {
        if (info.type == aipdf::pdf::PageObjectType::Image) {
            ++count;
        }
    }
    return count;
}

// 渲染出来的图里有没有指定颜色的像素。
//
// 这是「别的阅读器看得见」的证据：图在文件里但画不出来，跟没有图一样。
bool hasPixelOfColor(const QImage& image, int red, int green, int blue) {
    if (image.isNull()) {
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() > red && pixel.green() < green && pixel.blue() < blue) {
                return true;
            }
        }
    }
    return false;
}

// 渲染出来的图里有没有接近指定颜色的像素（各通道留一点余量）。
//
// 为什么不用 hasPixelOfColor：那条只认红色（给网页那张图用的），
// 页序那条用例要认三种颜色，得有个认「任意颜色」的。
bool hasColorNear(const QImage& image, int red, int green, int blue, int tolerance = 60) {
    if (image.isNull()) {
        return false;
    }
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (qAbs(pixel.red() - red) <= tolerance && qAbs(pixel.green() - green) <= tolerance &&
                qAbs(pixel.blue() - blue) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

// 假取字节回调：按地址返回预置的字节，取不到就返回空。
aipdf::doc::ImageFetcher makeImageFetcher(const QHash<QString, QByteArray>& table) {
    return [table](const QUrl& url) -> QByteArray {
        return table.value(url.toString());
    };
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

    // ------------------------------------------------------ 剪贴板图片 ----
    void clipboardImageProducesReadablePdf();
    void clipboardImagesKeepPageOrder();
    void clipboardImageWithoutDataIsRejected();
    void clipboardImageWithEmptyBytesIsReported();
    void clipboardImageWithBrokenBytesIsRejected();
    void clipboardImageWithUnsupportedFormatIsRejected();
    void clipboardImagePartialSuccessReportsFailedOnes();
    void clipboardImageLimitIsReported();
    void clipboardHtmlDoesNotEmbedImages();

    // ------------------------------------------- 剪贴板：文字 + 图片 ----
    void clipboardMixedProducesTextThenImagePages();
    void clipboardMixedKeepsImageOrder();
    void clipboardMixedTextOnlyStaysOnePage();
    void clipboardMixedImageOnlyStaysImagePages();
    void clipboardMixedKeepsTextWhenImageIsBroken();

    // ------------------------------------------------------------ 网页 ----
    void htmlBodyDropsScriptStyleAndNavigation();
    void htmlToPdfProducesReadablePdf();
    void htmlWithoutBodyTextIsRejected();
    void htmlWithOnlyScriptIsRejected();
    void relativeLinksAreResolvedAgainstBaseUrl();
    void brokenHtmlStillYieldsText();

    // -------------------------------------------------------- 网页图片 ----
    void webImagesAreCollectedFromHtml();
    void webImageIsEmbeddedIntoPdf();
    void webImageDownloadFailureIsReported();
    void webImageBrokenBytesAreReported();
    void webImageUnsupportedFormatIsReported();
    void webWithoutImageFetcherKeepsOldBehavior();
    void webImageFetcherRejectsNonHttpUrl();

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
    void dialogConvertsImageOnlyClipboard();
    void dialogConvertsInjectedClipboardImages();
    void dialogConvertsMixedClipboardIntoOnePdf();

    // ------------------------------------------- 网页：浏览器引擎优先 ----
    void dialogWebPrefersTheBrowserEngine();
    void dialogWebFallsBackAndSaysSoWhenTheEngineFails();
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

// ============================================================ 剪贴板图片 ====

void ConversionInputsTests::clipboardImageProducesReadablePdf() {
    // 正例：剪贴板里一张 PNG → 一页 PDF，图真的在里面（PDFium 看得见）。
    const ConversionResult result = aipdf::doc::clipboardImageToPdf(
        QVector<QByteArray>{makeSolidPng(120, 120, 255, 0, 0)});
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.pageCount, 1);
    // 结果里要能看出这次走的是图片这一路。
    QCOMPARE(result.sourceKind, aipdf::doc::ConversionSourceKind::Images);
    // 「这次带了图」的结构化字段。
    QCOMPARE(result.clipboardImageOutcome.found, 1);
    QCOMPARE(result.clipboardImageOutcome.embedded, 1);
    QVERIFY(result.clipboardImageOutcome.issues.isEmpty());

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.bytes, QStringLiteral("clipboard.pdf"));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 1);
    const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(countImageObjects(*page), 1);
    // 渲染出来真的有那片红色（图在文件里但画不出来，跟没有图一样）。
    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());
    QVERIFY2(hasColorNear(rendered, 255, 0, 0), "剪贴板图片没有渲染出来");
}

void ConversionInputsTests::clipboardImagesKeepPageOrder() {
    // 正例：三张图 → 三页，页序与传入顺序一致（靠颜色认页）。
    QVector<QByteArray> images;
    images.append(makeSolidPng(120, 120, 255, 0, 0));
    images.append(makeSolidPng(120, 120, 0, 255, 0));
    images.append(makeSolidPng(120, 120, 0, 0, 255));
    const ConversionResult result = aipdf::doc::clipboardImageToPdf(images);
    QVERIFY(result.ok);
    QCOMPARE(result.pageCount, 3);
    QCOMPARE(result.clipboardImageOutcome.embedded, 3);
    QVERIFY(result.clipboardImageOutcome.issues.isEmpty());

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.bytes, QStringLiteral("clipboard.pdf"));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 3);
    const QVector<QColor> expected = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
    for (int index = 0; index < expected.size(); ++index) {
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(index);
        QVERIFY(page.has_value());
        QCOMPARE(countImageObjects(*page), 1);
        const QImage rendered = page->render(1.0);
        QVERIFY(!rendered.isNull());
        const QColor color = expected.at(index);
        QVERIFY2(hasColorNear(rendered, color.red(), color.green(), color.blue()),
                 qPrintable(QStringLiteral("第 %1 页的颜色不对").arg(index + 1)));
    }
}

void ConversionInputsTests::clipboardImageWithoutDataIsRejected() {
    // 负例：剪贴板里一张图都没有 —— 直接失败，不生成空白 PDF。
    const ConversionResult result = aipdf::doc::clipboardImageToPdf(QVector<QByteArray>());
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::NoContent);
    QVERIFY(!result.error.isEmpty());
}

void ConversionInputsTests::clipboardImageWithEmptyBytesIsReported() {
    // 负例：有图片「条目」但字节是空的 —— 逐条报出来，不静默跳过。
    const ConversionResult result =
        aipdf::doc::clipboardImageToPdf(QVector<QByteArray>{QByteArray()});
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::NoContent);
    QCOMPARE(result.clipboardImageOutcome.found, 1);
    QCOMPARE(result.clipboardImageOutcome.embedded, 0);
    QCOMPARE(result.clipboardImageOutcome.issues.size(), 1);
    QCOMPARE(result.clipboardImageOutcome.issues.at(0).kind,
             aipdf::doc::ClipboardImageIssueKind::EmptyData);
}

void ConversionInputsTests::clipboardImageWithBrokenBytesIsRejected() {
    // 负例：字节是 PNG 头但后面全是垃圾 —— 解不开，明确失败，不崩。
    const QByteArray broken =
        QByteArrayLiteral("\x89PNG\r\n\x1a\n") + QByteArrayLiteral("broken bytes");
    const ConversionResult result = aipdf::doc::clipboardImageToPdf(QVector<QByteArray>{broken});
    QVERIFY(!result.ok);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::ImageDecodeFailed);
    QCOMPARE(result.clipboardImageOutcome.embedded, 0);
    QCOMPARE(result.clipboardImageOutcome.issues.size(), 1);
    const aipdf::doc::ClipboardImageIssue& issue = result.clipboardImageOutcome.issues.at(0);
    QCOMPARE(issue.kind, aipdf::doc::ClipboardImageIssueKind::DecodeFailed);
    QCOMPARE(issue.index, 0);
    // 格式认得出来（是 PNG），但数据坏了 —— 这两件事要分开报。
    QCOMPARE(issue.format, QStringLiteral("PNG"));
    QVERIFY(!issue.detail.isEmpty());
}

void ConversionInputsTests::clipboardImageWithUnsupportedFormatIsRejected() {
    // 负例：TIFF / GIF / WebP 明确不支持 —— 要点名格式，不能只写「转换失败」。
    struct Sample {
        QByteArray bytes;
        QString format;
    };
    const QVector<Sample> samples = {
        {QByteArrayLiteral("II\x2a\x00") + QByteArrayLiteral("not-really-a-tiff"),
         QStringLiteral("TIFF")},
        {QByteArrayLiteral("GIF89a") + QByteArrayLiteral("not-really-an-image"),
         QStringLiteral("GIF")},
        {QByteArrayLiteral("RIFF") + QByteArray(4, '\0') +
             QByteArrayLiteral("WEBPVP8 not-really-an-image"),
         QStringLiteral("WebP")},
    };
    for (const Sample& sample : samples) {
        const ConversionResult result =
            aipdf::doc::clipboardImageToPdf(QVector<QByteArray>{sample.bytes});
        QVERIFY2(!result.ok, sample.format.toUtf8().constData());
        QVERIFY(result.bytes.isEmpty());
        QCOMPARE(result.errorKind, aipdf::doc::ConversionErrorKind::UnsupportedImageFormat);
        QCOMPARE(result.clipboardImageOutcome.embedded, 0);
        QCOMPARE(result.clipboardImageOutcome.issues.size(), 1);
        const aipdf::doc::ClipboardImageIssue& issue = result.clipboardImageOutcome.issues.at(0);
        QCOMPARE(issue.kind, aipdf::doc::ClipboardImageIssueKind::UnsupportedFormat);
        QCOMPARE(issue.format, sample.format);
    }
}

void ConversionInputsTests::clipboardImagePartialSuccessReportsFailedOnes() {
    // 一张好的 + 一张不认的：好的要排进去，不认的要逐条报出来（不静默忽略）。
    QVector<QByteArray> images;
    images.append(makeSolidPng(120, 120, 255, 0, 0));
    images.append(QByteArrayLiteral("GIF89a") + QByteArrayLiteral("not-really-an-image"));
    const ConversionResult result = aipdf::doc::clipboardImageToPdf(images);
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.clipboardImageOutcome.found, 2);
    QCOMPARE(result.clipboardImageOutcome.embedded, 1);
    QCOMPARE(result.clipboardImageOutcome.issues.size(), 1);
    const aipdf::doc::ClipboardImageIssue& issue = result.clipboardImageOutcome.issues.at(0);
    QCOMPARE(issue.kind, aipdf::doc::ClipboardImageIssueKind::UnsupportedFormat);
    QCOMPARE(issue.format, QStringLiteral("GIF"));
    QCOMPARE(issue.index, 1);
    // PDF 里只有一张图（坏的那张没有变成空白页）。
    QCOMPARE(result.bytes.count("/Subtype /Image"), 1);
}

void ConversionInputsTests::clipboardImageLimitIsReported() {
    // 一次带的图太多：超出的不排，但要逐条记下来（不能静默丢弃）。
    QVector<QByteArray> images;
    for (int index = 0; index < 51; ++index) {
        images.append(makeSolidPng(8, 8, 255, 0, 0));
    }
    const ConversionResult result = aipdf::doc::clipboardImageToPdf(images);
    QVERIFY(result.ok);
    QCOMPARE(result.clipboardImageOutcome.found, 51);
    QCOMPARE(result.clipboardImageOutcome.embedded, 50);
    QCOMPARE(result.pageCount, 50);
    QCOMPARE(result.clipboardImageOutcome.issues.size(), 1);
    QCOMPARE(result.clipboardImageOutcome.issues.at(0).kind,
             aipdf::doc::ClipboardImageIssueKind::LimitReached);
    QCOMPARE(result.clipboardImageOutcome.issues.at(0).index, 50);
}

void ConversionInputsTests::clipboardHtmlDoesNotEmbedImages() {
    // 边界（一）：HTML 里内联的图（data: URI）仍然**不**被嵌入 ——
    // 那一路只排正文。
    const QString html = QStringLiteral(
        "<html><body><p>Text body</p>"
        "<img src=\"data:image/png;base64,iVBORw0KGgo=\"></body></html>");
    const ConversionResult result = aipdf::doc::clipboardHtmlToPdf(html);
    QVERIFY(result.ok);
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.bytes.count("/Subtype /Image"), 0);
    // 网页那套的带图结果不因为这次是剪贴板而变样。
    QVERIFY(!result.imageOutcome.attempted);
    QCOMPARE(result.clipboardImageOutcome.found, 0);

    // 边界（二）：**剪贴板里的**图片是另一份字节（从剪贴板直接拿到的），
    // 它作为**独立页**被嵌进去（第 2 页）；HTML 里那个 <img src> 仍然没被嵌入。
    //
    // 为什么这两件事要分开说：前者是「HTML 源码里引用的图」（地址多半已失效），
    // 后者是「剪贴板里真实存在的图片字节」。合成时只认后者。
    const ConversionResult mixed = aipdf::doc::clipboardMixedToPdf(
        QString(), html, QVector<QByteArray>{makeSolidPng(60, 60, 0, 128, 255)});
    QVERIFY(mixed.ok);
    QCOMPARE(mixed.pageCount, 2);                        // 1 页正文 + 1 页图片
    QCOMPARE(mixed.bytes.count("/Subtype /Image"), 1);  // 只有剪贴板那张图
    QCOMPARE(mixed.clipboardImageOutcome.found, 1);
    QCOMPARE(mixed.clipboardImageOutcome.embedded, 1);
    const PdfProbe probe = probeWithPdfium(mixed.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(probe.pageCount, 2);
    // 第 1 页是 HTML 的正文，第 2 页才是剪贴板里的图。
    QVERIFY(squeezeWhitespace(probe.pageTexts.at(0)).contains(QStringLiteral("Textbody")));
    QVERIFY(squeezeWhitespace(probe.pageTexts.at(1)).isEmpty());
}

// ============================================================ 剪贴板：混合 ====

void ConversionInputsTests::clipboardMixedProducesTextThenImagePages() {
    // 正例（核心）：剪贴板里同时有文字和一张图 → **一份** PDF。
    // 页序（产品决定）：第 1 页正文，第 2 页起是图片（一图一页）。
    const ConversionResult result = aipdf::doc::clipboardMixedToPdf(
        QStringLiteral("Mixed body text"), QString(),
        QVector<QByteArray>{makeSolidPng(120, 120, 255, 0, 0)});
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    // 页数 = 1（正文）+ 1（图片）。
    QCOMPARE(result.pageCount, 2);
    QCOMPARE(result.sourceKind, aipdf::doc::ConversionSourceKind::Mixed);
    QCOMPARE(result.clipboardImageOutcome.found, 1);
    QCOMPARE(result.clipboardImageOutcome.embedded, 1);
    QVERIFY(result.clipboardImageOutcome.issues.isEmpty());

    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QCOMPARE(probe.pageCount, 2);
    // 第 1 页是正文：文字抽得回来。
    QVERIFY(squeezeWhitespace(probe.pageTexts.at(0)).contains(QStringLiteral("Mixedbodytext")));
    // 图在文件里（不是只在返回值里说嵌进去了）。
    QCOMPARE(result.bytes.count("/Subtype /Image"), 1);

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.bytes, QStringLiteral("mixed.pdf"));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 2);
    // 正文那一页**不该**有图（图在第 2 页，不插在正文中间）。
    const std::optional<aipdf::pdf::PdfPage> textPage = opened.document.page(0);
    QVERIFY(textPage.has_value());
    QCOMPARE(countImageObjects(*textPage), 0);
    // 第 2 页是图片页：图真的画得出来（不是空白页）。
    const std::optional<aipdf::pdf::PdfPage> imagePage = opened.document.page(1);
    QVERIFY(imagePage.has_value());
    QCOMPARE(countImageObjects(*imagePage), 1);
    const QImage rendered = imagePage->render(1.0);
    QVERIFY(!rendered.isNull());
    QVERIFY2(hasColorNear(rendered, 255, 0, 0), "剪贴板图片没有渲染出来");
}

void ConversionInputsTests::clipboardMixedKeepsImageOrder() {
    // 正例：文字 + 两张图 → 3 页（1 页正文 + 2 页图），
    // 图片页序 = 剪贴板里的顺序（靠颜色认页）。
    QVector<QByteArray> images;
    images.append(makeSolidPng(120, 120, 255, 0, 0));
    images.append(makeSolidPng(120, 120, 0, 255, 0));
    const ConversionResult result = aipdf::doc::clipboardMixedToPdf(
        QStringLiteral("Body before images"), QString(), images);
    QVERIFY(result.ok);
    QCOMPARE(result.pageCount, 3);
    QCOMPARE(result.clipboardImageOutcome.found, 2);
    QCOMPARE(result.clipboardImageOutcome.embedded, 2);
    QVERIFY(result.clipboardImageOutcome.issues.isEmpty());

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.bytes, QStringLiteral("mixed.pdf"));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 3);
    const QVector<QColor> expected = {QColor(255, 0, 0), QColor(0, 255, 0)};
    for (int index = 0; index < expected.size(); ++index) {
        const std::optional<aipdf::pdf::PdfPage> page = opened.document.page(index + 1);
        QVERIFY(page.has_value());
        QCOMPARE(countImageObjects(*page), 1);
        const QImage rendered = page->render(1.0);
        QVERIFY(!rendered.isNull());
        const QColor color = expected.at(index);
        QVERIFY2(hasColorNear(rendered, color.red(), color.green(), color.blue()),
                 qPrintable(QStringLiteral("第 %1 页的图片颜色不对").arg(index + 2)));
    }
    // 正文在第 1 页。
    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QVERIFY(squeezeWhitespace(probe.pageTexts.at(0)).contains(QStringLiteral("Bodybeforeimages")));
}

void ConversionInputsTests::clipboardMixedTextOnlyStaysOnePage() {
    // 回归：只有文字 → 仍然是一页，产物与纯文字那一路**逐字节相同**。
    const ConversionResult result = aipdf::doc::clipboardMixedToPdf(
        QStringLiteral("Only text"), QString(), QVector<QByteArray>());
    QVERIFY(result.ok);
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.sourceKind, aipdf::doc::ConversionSourceKind::PlainText);
    const ConversionResult plain = aipdf::doc::clipboardTextToPdf(QStringLiteral("Only text"));
    QVERIFY(plain.ok);
    QCOMPARE(result.bytes, plain.bytes);
}

void ConversionInputsTests::clipboardMixedImageOnlyStaysImagePages() {
    // 回归：只有图片 → 页数 = 图片张数，**不多出**一页空白正文。
    QVector<QByteArray> images;
    images.append(makeSolidPng(120, 120, 255, 0, 0));
    images.append(makeSolidPng(120, 120, 0, 0, 255));
    const ConversionResult result =
        aipdf::doc::clipboardMixedToPdf(QString(), QString(), images);
    QVERIFY(result.ok);
    QCOMPARE(result.pageCount, 2);
    QCOMPARE(result.sourceKind, aipdf::doc::ConversionSourceKind::Images);
    QCOMPARE(result.clipboardImageOutcome.embedded, 2);
    QVERIFY(result.clipboardImageOutcome.issues.isEmpty());
}

void ConversionInputsTests::clipboardMixedKeepsTextWhenImageIsBroken() {
    // 负例：图认不出来 → 整次转换**不该**失败：正文照常出 PDF，
    // 图逐张记成「没带进来」（不静默丢弃）。
    const ConversionResult result = aipdf::doc::clipboardMixedToPdf(
        QStringLiteral("Body survives"), QString(),
        QVector<QByteArray>{QByteArrayLiteral("GIF89a") + QByteArrayLiteral("not-really-an-image")});
    QVERIFY(result.ok);
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.clipboardImageOutcome.embedded, 0);
    QCOMPARE(result.clipboardImageOutcome.issues.size(), 1);
    QCOMPARE(result.clipboardImageOutcome.issues.at(0).kind,
             aipdf::doc::ClipboardImageIssueKind::UnsupportedFormat);
    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QVERIFY(squeezeWhitespace(probe.allText()).contains(QStringLiteral("Bodysurvives")));
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

// ============================================================ 网页图片 ====

void ConversionInputsTests::webImagesAreCollectedFromHtml() {
    // 正例：正文里的 <img> 被收集出来：绝对地址、去重、按出现顺序。
    const QString html =
        QStringLiteral("<html><body><article>"
                       "<p>First paragraph.</p>"
                       "<img src=\"/first.png\">"
                       "<p>Second paragraph.</p>"
                       "<img src=\"https://cdn.example.com/second.png\">"
                       "<p>Third paragraph.</p>"
                       "<img src=\"/first.png\">"          // 重复的只留第一次
                       "<img srcset=\"/small.png 1x, /big.png 2x\">"  // 只取 src，不取 srcset
                       "</article></body></html>");
    const HtmlBody body =
        aipdf::doc::extractHtmlBody(html, QUrl(QStringLiteral("https://example.com/dir/page.html")));
    QCOMPARE(body.images.size(), 2);
    QCOMPARE(body.images.at(0).url, QStringLiteral("https://example.com/first.png"));
    QCOMPARE(body.images.at(1).url, QStringLiteral("https://cdn.example.com/second.png"));
    // 位置要落在正文里（用来把图插到正文对应位置附近）。
    QVERIFY(body.images.at(0).textOffset >= 0);
    QVERIFY(body.images.at(1).textOffset > body.images.at(0).textOffset);
}

void ConversionInputsTests::webImageIsEmbeddedIntoPdf() {
    // 正例：注入假的取字节回调 → PDF 里真的有图。
    QHash<QString, QByteArray> table;
    table.insert(QStringLiteral("https://example.com/pic.png"), makeSolidPng(40, 40, 255, 0, 0));
    const QString html =
        QStringLiteral("<html><body><article><p>Before the picture.</p>"
                       "<img src=\"/pic.png\"><p>After the picture.</p></article></body></html>");
    const ConversionResult result =
        aipdf::doc::htmlToPdf(html, QUrl(QStringLiteral("https://example.com/dir/page.html")),
                              UniversalConverterOptions(), makeImageFetcher(table));
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.imageOutcome.found, 1);
    QCOMPARE(result.imageOutcome.embedded, 1);
    QVERIFY(result.imageOutcome.issues.isEmpty());
    // 正文与图片各占一页：文字在前后，图插在中间。
    QCOMPARE(result.pageCount, 3);
    // PDF 里确实有一个图像对象（不能只看「转换成功」）。
    QCOMPARE(result.bytes.count("/Subtype /Image"), 1);

    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.bytes, QStringLiteral("web.pdf"));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 3);

    const std::optional<aipdf::pdf::PdfPage> imagePage = opened.document.page(1);
    QVERIFY(imagePage.has_value());
    QCOMPARE(countImageObjects(*imagePage), 1);
    // 渲染出来真的有那片红色（图在文件里但画不出来，跟没有图一样）。
    const QImage rendered = imagePage->render(1.0);
    QVERIFY(!rendered.isNull());
    QVERIFY2(hasPixelOfColor(rendered, 200, 60, 60), "图片没有渲染出来");

    // 其余正文照常在 PDF 里。
    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    const QString extracted = squeezeWhitespace(probe.allText());
    QVERIFY(extracted.contains(QStringLiteral("Beforethepicture")));
    QVERIFY(extracted.contains(QStringLiteral("Afterthepicture")));
}

void ConversionInputsTests::webImageDownloadFailureIsReported() {
    // 负例的关键：一张图下载失败**不能**让整篇转换失败。
    const QString html =
        QStringLiteral("<html><body><article><p>Before the picture.</p>"
                       "<img src=\"/pic.png\"><p>After the picture.</p></article></body></html>");
    const ConversionResult result =
        aipdf::doc::htmlToPdf(html, QUrl(QStringLiteral("https://example.com/dir/page.html")),
                              UniversalConverterOptions(), makeImageFetcher({}));
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.imageOutcome.found, 1);
    QCOMPARE(result.imageOutcome.embedded, 0);
    QCOMPARE(result.imageOutcome.issues.size(), 1);
    QCOMPARE(result.imageOutcome.issues.at(0).url, QStringLiteral("https://example.com/pic.png"));
    QCOMPARE(result.imageOutcome.issues.at(0).kind,
             aipdf::doc::HtmlImageIssueKind::DownloadFailed);
    // 其余内容照常出 PDF（没有图，也不该多出空白页）。
    QCOMPARE(result.pageCount, 1);
    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    const QString extracted = squeezeWhitespace(probe.allText());
    QVERIFY(extracted.contains(QStringLiteral("Beforethepicture")));
    QVERIFY(extracted.contains(QStringLiteral("Afterthepicture")));
    QCOMPARE(result.bytes.count("/Subtype /Image"), 0);
}

void ConversionInputsTests::webImageBrokenBytesAreReported() {
    // 负例：图拿到了但是坏的 —— 同样不能中断整篇转换，原因要写清楚。
    QHash<QString, QByteArray> table;
    table.insert(QStringLiteral("https://example.com/pic.png"),
                 QByteArrayLiteral("\x89PNG\r\n\x1a\n") + QByteArrayLiteral("broken bytes"));
    const QString html =
        QStringLiteral("<html><body><article><p>Before the picture.</p>"
                       "<img src=\"/pic.png\"></article></body></html>");
    const ConversionResult result =
        aipdf::doc::htmlToPdf(html, QUrl(QStringLiteral("https://example.com/dir/page.html")),
                              UniversalConverterOptions(), makeImageFetcher(table));
    QVERIFY(result.ok);
    QCOMPARE(result.imageOutcome.embedded, 0);
    QCOMPARE(result.imageOutcome.issues.size(), 1);
    QCOMPARE(result.imageOutcome.issues.at(0).url, QStringLiteral("https://example.com/pic.png"));
    QCOMPARE(result.imageOutcome.issues.at(0).kind,
             aipdf::doc::HtmlImageIssueKind::DecodeFailed);
    // 技术细节（ASCII）也要有，方便排查。
    QVERIFY(!result.imageOutcome.issues.at(0).detail.isEmpty());
    // 正文照常出 PDF。
    const PdfProbe probe = probeWithPdfium(result.bytes);
    QVERIFY(probe.opened);
    QVERIFY(squeezeWhitespace(probe.allText()).contains(QStringLiteral("Beforethepicture")));
}

void ConversionInputsTests::webImageUnsupportedFormatIsReported() {
    // 负例：完全认不出来的格式（这里用 GIF 头）—— 原因要与「数据坏了」分开。
    QHash<QString, QByteArray> table;
    table.insert(QStringLiteral("https://example.com/pic.gif"),
                 QByteArrayLiteral("GIF89a-not-really-an-image"));
    const QString html =
        QStringLiteral("<html><body><article><p>Before the picture.</p>"
                       "<img src=\"/pic.gif\"></article></body></html>");
    const ConversionResult result =
        aipdf::doc::htmlToPdf(html, QUrl(QStringLiteral("https://example.com/dir/page.html")),
                              UniversalConverterOptions(), makeImageFetcher(table));
    QVERIFY(result.ok);
    QCOMPARE(result.imageOutcome.embedded, 0);
    QCOMPARE(result.imageOutcome.issues.size(), 1);
    QCOMPARE(result.imageOutcome.issues.at(0).url, QStringLiteral("https://example.com/pic.gif"));
    QCOMPARE(result.imageOutcome.issues.at(0).kind,
             aipdf::doc::HtmlImageIssueKind::UnsupportedFormat);
}

void ConversionInputsTests::webWithoutImageFetcherKeepsOldBehavior() {
    // 没有注入取字节回调 = 这次不下载图片：行为与从前一致，不报错也不带图。
    const QString html =
        QStringLiteral("<html><body><article><p>Before the picture.</p>"
                       "<img src=\"/pic.png\"><p>After the picture.</p></article></body></html>");
    const ConversionResult result =
        aipdf::doc::htmlToPdf(html, QUrl(QStringLiteral("https://example.com/dir/page.html")));
    QVERIFY(result.ok);
    QVERIFY(!result.bytes.isEmpty());
    // 明确说明「这次没有下载图片」。
    QVERIFY(!result.imageOutcome.attempted);
    QCOMPARE(result.imageOutcome.embedded, 0);
    QCOMPARE(result.imageOutcome.found, 1);
    QVERIFY(result.imageOutcome.issues.isEmpty());
    // 没有图片，也没有多出空白页。
    QCOMPARE(result.bytes.count("/Subtype /Image"), 0);
    QCOMPARE(result.pageCount, 1);
    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(result.bytes, QStringLiteral("web.pdf"));
    QVERIFY(opened.document.isValid());
    const std::optional<aipdf::pdf::PdfPage> firstPage = opened.document.page(0);
    QVERIFY(firstPage.has_value());
    QCOMPARE(countImageObjects(*firstPage), 0);
}

void ConversionInputsTests::webImageFetcherRejectsNonHttpUrl() {
    // 负例：只支持 http / https —— 别的协议直接取不到（回调返回空字节），
    // 不能让 doc 层拿着一个乱七八糟的地址去下载。
    const aipdf::doc::ImageFetcher fetcher = aipdf::app::webImageFetcher(1000);
    QVERIFY(fetcher(QUrl(QStringLiteral("ftp://example.com/pic.png"))).isEmpty());
    QVERIFY(fetcher(QUrl()).isEmpty());
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

void ConversionInputsTests::dialogConvertsInjectedClipboardImages() {
    // 注入路径：单测不该去动用户的剪贴板（系统剪贴板被别的进程占着就读不到，
    // 测试会莫名其妙地跳过）。所以这里直接塞字节。
    const QByteArray png = makeSolidPng(8, 8, 255, 0, 0);
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Clipboard);
    dialog.setClipboardContent(aipdf::app::ConversionInputDialog::ClipboardContent::Images);
    dialog.setClipboardImages({png});
    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(dialog.convert(&outcome));
    QVERIFY(outcome.ok);
    QVERIFY(!outcome.bytes.isEmpty());
    QCOMPARE(outcome.pageCount, 1);
    QCOMPARE(outcome.sourceKind, aipdf::doc::ConversionSourceKind::Images);
    // 走图片这一路要说明白：每张图一页。
    QVERIFY(outcome.detailLines.join(QStringLiteral("|")).contains(QStringLiteral("图片")));
}

void ConversionInputsTests::dialogConvertsMixedClipboardIntoOnePdf() {
    // 注入路径（不碰用户剪贴板）：文字 + 图片 → **一份** PDF。
    //
    // 这条钉死的是「图片没有进 PDF」那句话不再出现：
    // 图真的进了 PDF（图在第 2 页），所以不该再警告用户说图被丢了。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Clipboard);
    dialog.setClipboardText(QStringLiteral("Body text"));
    dialog.setClipboardHtml(QStringLiteral("<html><body><p>Body text</p></body></html>"));
    dialog.setClipboardImages({makeSolidPng(40, 40, 255, 0, 0)});
    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(dialog.convert(&outcome));
    // 一份 PDF：1 页正文 + 1 页图片。
    QCOMPARE(outcome.pageCount, 2);
    QCOMPARE(outcome.sourceKind, aipdf::doc::ConversionSourceKind::Mixed);
    // 「图片没有进 PDF」这句话不该再出现。
    QVERIFY2(!outcome.warning.contains(QStringLiteral("图片没有进 PDF")),
             "混合内容不该再警告「图片没有进 PDF」");
    // 如实说明页序：正文在前，图片在后。
    const QString details = outcome.detailLines.join(QStringLiteral("|"));
    QVERIFY2(details.contains(QStringLiteral("已合成")), "没有说明这一份是怎么拼出来的");
    QVERIFY(details.contains(QStringLiteral("正文")));
    QVERIFY(details.contains(QStringLiteral("图片")));
}

void ConversionInputsTests::dialogConvertsImageOnlyClipboard() {
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

    // 剪贴板里只有图片 —— 现在能转了：一图一页。
    // （以前这里断言「不支持」，那是能力没落地时的行为。）
    QVERIFY2(converted, qPrintable(error));
    QVERIFY(error.isEmpty());
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

void ConversionInputsTests::dialogWebPrefersTheBrowserEngine() {
    // 正例：浏览器引擎成功 → 产物就是引擎给的字节，结果里说明用的是哪个引擎，
    // 而且**没有**降级提示（没走抓正文那条路）。
    //
    // 为什么用假的引擎而不是真起一个浏览器：起一次浏览器要几秒，而且依赖本机
    // 装没装 Edge —— 单测不能因为「这台机器没装浏览器」就红。真的引擎由
    // BrowserPdfPrinterTests 那一组验（那边有真渲染的用例）。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Web);
    dialog.setUrlText(QStringLiteral("https://example.com/page"));
    bool called = false;
    dialog.setWebBrowserPrinter([&called](const aipdf::doc::BrowserPrintOptions& options) {
        called = true;
        // 注入点要真的把参数传下去：url 传错的话，浏览器会打开别的页面。
        Q_UNUSED(options);
        aipdf::doc::BrowserPrintResult result;
        result.ok = true;
        result.bytes = QByteArray("%PDF-1.7 fake browser output");
        result.engineName = QStringLiteral("Edge");
        result.pageCount = 3;
        return result;
    });
    aipdf::app::ConversionInputOutcome outcome;
    QVERIFY(dialog.convert(&outcome));
    QVERIFY(called);
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.bytes, QByteArray("%PDF-1.7 fake browser output"));
    QCOMPARE(outcome.pageCount, 3);
    // 结果里要说明这次用了哪个引擎（用户得知道版面是谁排的）。
    bool mentionsEngine = false;
    for (const QString& line : outcome.detailLines) {
        if (line.contains(QStringLiteral("Edge"))) {
            mentionsEngine = true;
        }
    }
    QVERIFY(mentionsEngine);
    // 走了浏览器就不该再说降级的话。
    QVERIFY(!outcome.warning.contains(QStringLiteral("JavaScript 没有执行")));
}

void ConversionInputsTests::dialogWebFallsBackAndSaysSoWhenTheEngineFails() {
    // 负例：浏览器引擎没装 → 退回抓正文排版，并且**明说**这次走的是降级路径。
    //
    // 用 127.0.0.1:1（本地未监听端口）而不是真的网址：连接会被立刻拒绝，
    // 单测因此不依赖外网，也不会等到超时。
    aipdf::app::ConversionInputDialog dialog;
    dialog.setMode(aipdf::app::ConversionInputDialog::Mode::Web);
    dialog.setUrlText(QStringLiteral("http://127.0.0.1:1/page"));
    dialog.setWebBrowserPrinter([](const aipdf::doc::BrowserPrintOptions&) {
        aipdf::doc::BrowserPrintResult result;
        result.ok = false;
        result.errorKind = aipdf::doc::BrowserPrintError::NoEngine;
        result.error = QStringLiteral("本机没有可用的浏览器引擎");
        return result;
    });
    aipdf::app::ConversionInputOutcome outcome;
    // 抓不到网页 → 转换失败。但**失败信息里必须带着降级说明**：
    // 用户要知道这次不是浏览器渲染的版面。
    QVERIFY(!dialog.convert(&outcome));
    QVERIFY(!outcome.ok);
    QVERIFY2(outcome.error.contains(QStringLiteral("JavaScript 没有执行")),
             qPrintable(outcome.error));
    QVERIFY(outcome.error.contains(QStringLiteral("没有可用的浏览器引擎")));
}

QTEST_MAIN(ConversionInputsTests)
#include "ConversionInputsTests.moc"
