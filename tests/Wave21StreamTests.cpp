// wave21：全文流式编辑 + 跨文本块合并 + 整篇翻译写回。
//
// 这一组测的是**领域层**的三件事：
//   1. 整篇文档按新页宽重新流排并写回（不只是当前选区）；
//   2. 选区跨多个文本块时合并成一段，样式逐块对应；
//   3. 整篇翻译后把译文写回文档（不只是选区 / 当前页）。
//
// 夹具是自己造的：带标签的两页正文、段中夹图的一份、没有标签的一份。
// 翻译用**假 Provider**，不打真实网络。

#include "pdf/content/ContentStream.h"
#include "pdf/ops/StreamingEdit.h"
#include "pdf/ops/TranslationWriter.h"

#include <QTest>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::ContentTextBlock;
using aipdf::pdf::MergeBlocksOptions;
using aipdf::pdf::MergeBlocksResult;
using aipdf::pdf::StreamingReflowOptions;
using aipdf::pdf::StreamingReflowResult;
using aipdf::pdf::TextParagraph;
using aipdf::pdf::TranslationOptions;
using aipdf::pdf::TranslationProvider;
using aipdf::pdf::TranslationWriteResult;

namespace {

// ---------------------------------------------------------------------------
// 造 PDF 的家伙什
// ---------------------------------------------------------------------------

// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) maxId = qMax(maxId, object.first);
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (!offsets.contains(id)) {
            out += "0000000000 65535 f \n";
            continue;
        }
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

QByteArray streamObject(const QByteArray& content) {
    return QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

const QByteArray kFont =
    QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

// 带标签的两页正文。
//
// 结构刻意做成常见的样子：Document 下面挂 H1、P（里面再套两个 Span）、
// 另一个 P、还有第二页上的两个 P。
//   * 第二个 P 里的两个 Span 是**同一段**（MCID 1 与 2）——
//     跨文本块合并与整篇重排都要拿它当样本；
//   * 两个 Span 的字号与颜色不一样（12 磅红 / 10 磅蓝）——
//     合并之后必须逐块保留，不能统一成一种样式。
QByteArray taggedPdf() {
    const QByteArray pageOne =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 14 Tf 40 285 Td (Chapter one) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 1 0 0 rg 40 230 Td (Body paragraph text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 2 >> BDC\n"
        "BT /F1 10 Tf 0 0 1 rg 40 215 Td (and its second line) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 12 Tf 40 160 Td (Another paragraph) Tj ET\n"
        "EMC";
    const QByteArray pageTwo =
        "/Tag << /MCID 4 >> BDC\n"
        "BT /F1 12 Tf 40 280 Td (Page two body) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 5 >> BDC\n"
        "BT /F1 12 Tf 40 180 Td (Final paragraph) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Lang (en-US) "
                                            "/StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageOne)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R /StructParents 1 >>")));
    objects.append(qMakePair(6, streamObject(pageTwo)));
    objects.append(qMakePair(7, kFont));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                            "/K [10 0 R 11 0 R 12 0 R 13 0 R 16 0 R] >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /H1 /P 9 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    // 一段里套两个 Span：同一段，两个文本块。
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /P /P 9 0 R "
                                             "/Pg 3 0 R /K [14 0 R 15 0 R] >>")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /P /P 9 0 R "
                                             "/Pg 3 0 R /K 3 >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /P /P 9 0 R "
                                             "/Pg 5 0 R /K 4 >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /Span /P 11 0 R /K 1 >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /Span /P 11 0 R /K 2 >>")));
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /P /P 9 0 R "
                                             "/Pg 5 0 R /K 5 >>")));
    return buildPdf(objects);
}

// 一页两段：上面一段放得下，下面那段贴着页底、放不下译文。
//
// 为什么单独造一份：正文里段与段的距离本来就小，
// 「译文比原文长就压到下一段」这件事得单独有个位置来试。
QByteArray bottomParagraphPdf() {
    const QByteArray content =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 280 Td (Top paragraph) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 25 Td (Bottom paragraph) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R "
                                            "/StructTreeRoot 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 4 0 R >> >> "
                                            "/Contents 5 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, kFont));
    objects.append(qMakePair(5, streamObject(content)));
    objects.append(qMakePair(6, QByteArray("<< /Type /StructTreeRoot /K [7 0 R] >>")));
    objects.append(qMakePair(7, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R "
                                            "/K [8 0 R 9 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /P /P 7 0 R "
                                            "/Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /P /P 7 0 R "
                                            "/Pg 3 0 R /K 1 >>")));
    return buildPdf(objects);
}

// 段与段之间夹着一张图：用来验「块间有图就拒绝合并」。
QByteArray imageBetweenPdf() {
    const QByteArray content =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (First block) Tj ET\n"
        "EMC\n"
        "q 100 0 0 40 40 200 cm /Im1 Do Q\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 170 Td (Second block) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R "
                                            "/StructTreeRoot 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 4 0 R >> "
                                            "/XObject << /Im1 5 0 R >> >> "
                                            "/Contents 7 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, kFont));
    objects.append(qMakePair(5, QByteArray("<< /Type /XObject /Subtype /Image /Width 1 /Height 1 "
                                            "/ColorSpace /DeviceGray /BitsPerComponent 8 /Length 1 >>")));
    objects.append(qMakePair(6, QByteArray("<< /Type /StructTreeRoot /K [8 0 R] >>")));
    objects.append(qMakePair(7, streamObject(content)));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R "
                                            "/K [9 0 R 10 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /P /P 8 0 R "
                                            "/Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /P /P 8 0 R "
                                             "/Pg 3 0 R /K 1 >>")));
    return buildPdf(objects);
}

// 没有标签树的普通 PDF：用来验「没有结构标签就拒绝整篇重排」。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 4 0 R >> >> "
                                            "/Contents 5 0 R >>")));
    objects.append(qMakePair(4, kFont));
    objects.append(qMakePair(5, streamObject("BT /F1 12 Tf 40 250 Td (Plain text) Tj ET")));
    return buildPdf(objects);
}

// 读一页的内容流（用 qpdf，与写回通道无关的另一条路）。
#ifdef AIPDF_WITH_QPDF
QByteArray pageContentOf(const QByteArray& pdfBytes, int pageIndex) {
    QByteArray result;
    try {
        QPDF pdf;
        pdf.processMemoryFile("check.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return result;
        QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
        page.coalesceContentStreams();
        for (QPDFObjectHandle stream : page.getPageContents()) {
            if (!stream.isStream()) continue;
            const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (!data) continue;
            result += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                 static_cast<qsizetype>(data->getSize()));
        }
    } catch (const std::exception&) {
        return QByteArray();
    }
    return result;
}
#else
QByteArray pageContentOf(const QByteArray& pdfBytes, int pageIndex) {
    Q_UNUSED(pdfBytes)
    Q_UNUSED(pageIndex)
    return QByteArray();
}
#endif

// 去掉空白后的文字（比较用）。
QString squeezed(const QString& text) {
    QString result;
    for (const QChar ch : text) {
        if (!ch.isSpace()) result.append(ch);
    }
    return result;
}

// 数一段字节里某个片段出现了几次。
int countOccurrences(const QByteArray& haystack, const char* needle) {
    int count = 0;
    int from = 0;
    while (true) {
        const int at = haystack.indexOf(needle, from);
        if (at < 0) break;
        ++count;
        from = at + 1;
    }
    return count;
}

// 失败时把每一段的原因都打出来（否则只看到一句总结，看不出是哪段坏的）。
QString reflowDetail(const aipdf::pdf::StreamingReflowReport& report) {
    QString detail = report.error;
    for (const aipdf::pdf::ReflowedBlock& block : report.blocks) {
        detail += QStringLiteral(" | 块%1: %2").arg(block.blockId).arg(block.reason);
    }
    return detail;
}

QString translationDetail(const aipdf::pdf::TranslationReport& report) {
    QString detail = report.error;
    for (const aipdf::pdf::TranslationUnit& unit : report.units) {
        detail += QStringLiteral(" | 段%1: %2").arg(unit.blockId).arg(unit.reason);
    }
    return detail;
}

// ---------------------------------------------------------------------------
// 假翻译 Provider（不打真实网络）
// ---------------------------------------------------------------------------

class FakeProvider final : public TranslationProvider {
public:
    bool configured = true;
    bool demo = false;
    QString prefix = QStringLiteral("T-");
    // 这些原文调用时返回失败。
    QStringList failingSources;
    // 这些原文返回与原文一样的内容。
    QStringList echoSources;
    // 返回这么长的译文（用来触发「放不下」）。
    int paddingLength = 0;
    int callCount = 0;

    QString id() const override { return QStringLiteral("fake"); }
    bool isConfigured() const override { return configured; }
    bool isDemo() const override { return demo; }

    bool translate(const QString& sourceText, const QString& targetLanguage, QString* translation,
                   QString* error) override {
        ++callCount;
        Q_UNUSED(targetLanguage)
        if (failingSources.contains(sourceText)) {
            if (error) *error = QStringLiteral("假 Provider 故意失败");
            return false;
        }
        if (echoSources.contains(sourceText)) {
            if (translation) *translation = sourceText;
            return true;
        }
        QString text = prefix + sourceText;
        if (paddingLength > 0) {
            text += QStringLiteral(" ") + QString(paddingLength, QLatin1Char('x'));
        }
        if (translation) *translation = text;
        return true;
    }
};

}  // namespace

class Wave21StreamTests : public QObject {
    Q_OBJECT

private slots:
    void untaggedDocumentIsRejected();
    void untaggedDocumentCanDegradeToPerParagraph();
    void fullReflowKeepsEveryCharacter();
    void fullReflowCountsParagraphs();
    void mergeAcrossBlocksKeepsBothHalves();
    void mergeAcrossBlocksKeepsPerBlockStyle();
    void mergeRejectsWhenGraphicBetween();
    void mergeRejectsUnknownBlock();
    void mergeRejectsSingleBlock();
    void translateWritesEveryParagraph();
    void translateReportsProviderFailure();
    void translateRejectsUnconfiguredProvider();
    void translateRejectsDemoProvider();
    void translateRejectsSameAsSource();
    void translateRejectsNonLatin1();
    void translateKeepsOriginalWhenDoesNotFit();
    void translateKeepsDocumentWhenNothingFits();
    void writtenContentStreamStaysParseable();
};

// ---------------------------------------------------------------------------
// 全文流式改写
// ---------------------------------------------------------------------------

void Wave21StreamTests::untaggedDocumentIsRejected() {
    StreamingReflowOptions options;
    options.targetPageWidth = 200.0;
    const StreamingReflowResult result = aipdf::pdf::reflowDocumentStream(plainPdf(), options);
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    // 拒绝的理由要写清楚「没有结构标签」与「只能逐段处理」。
    QVERIFY(result.report.error.contains(QStringLiteral("结构标签")));
    QVERIFY(result.report.error.contains(QStringLiteral("逐段")));
}

void Wave21StreamTests::untaggedDocumentCanDegradeToPerParagraph() {
    StreamingReflowOptions options;
    options.targetPageWidth = 200.0;
    options.requireStructTags = false;
    const StreamingReflowResult result = aipdf::pdf::reflowDocumentStream(plainPdf(), options);
    QVERIFY2(result.ok(), qPrintable(reflowDetail(result.report)));
    QVERIFY(!result.bytes.isEmpty());
    QVERIFY(!result.report.hasStructTags);
    // 如实说明「这只是逐段处理」。
    QVERIFY(!result.report.notes.isEmpty());
    QVERIFY(result.report.notes.first().contains(QStringLiteral("逐段")));
}

void Wave21StreamTests::fullReflowKeepsEveryCharacter() {
    StreamingReflowOptions options;
    options.targetPageWidth = 200.0;
    options.marginLeft = 20.0;
    options.marginRight = 20.0;
    const QByteArray source = taggedPdf();
    const StreamingReflowResult result = aipdf::pdf::reflowDocumentStream(source, options);
    QVERIFY2(result.ok(), qPrintable(reflowDetail(result.report)));
    QVERIFY(!result.bytes.isEmpty());

    // 写回之后重新打开（PDFium）逐页抽文字：每个字都得还在。
    int pageCount = 0;
    QString error;
    const QStringList before = aipdf::pdf::extractPageTexts(source, &pageCount, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QStringList after = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(after.size(), before.size());
    for (int index = 0; index < before.size(); ++index) {
        QCOMPARE(squeezed(after.at(index)), squeezed(before.at(index)));
    }
    // 写回后的字数不能比原文少。
    QVERIFY(result.report.outputCharCount >= result.report.sourceCharCount - before.size());
    // 而且**确实改了**：窄栏重排之后，一段文字被折成了更多行，
    // 内容流里的显示操作必然变多。没变多就说明压根没写回去。
    const QByteArray originalContent = pageContentOf(source, 0);
    const QByteArray writtenContent = pageContentOf(result.bytes, 0);
    QVERIFY(!originalContent.isEmpty());
    QVERIFY(!writtenContent.isEmpty());
    QVERIFY(writtenContent != originalContent);
    QVERIFY(countOccurrences(writtenContent, "Tj") > countOccurrences(originalContent, "Tj"));
}

void Wave21StreamTests::fullReflowCountsParagraphs() {
    StreamingReflowOptions options;
    options.targetPageWidth = 200.0;
    options.marginLeft = 20.0;
    options.marginRight = 20.0;
    const StreamingReflowResult result = aipdf::pdf::reflowDocumentStream(taggedPdf(), options);
    QVERIFY2(result.ok(), qPrintable(reflowDetail(result.report)));
    // 两页正文共 6 段、7 个文本块（其中一段由两个文本块拼成）。
    QCOMPARE(result.report.blockCount, 6);
    QCOMPARE(result.report.blocks.size(), 5);
    QCOMPARE(result.report.reflowedBlockCount, 5);
    for (const aipdf::pdf::ReflowedBlock& block : result.report.blocks) {
        QVERIFY2(block.written, qPrintable(block.reason));
    }
    // 窄栏重排之后，原来一行的段落被折成了多行。
    QVERIFY(result.report.blocks.at(1).reflowedLineCount >= 2);
    int reflowedLines = 0;
    for (const aipdf::pdf::ReflowedBlock& block : result.report.blocks) {
        reflowedLines += block.reflowedLineCount;
    }
    QVERIFY(reflowedLines >= result.report.blocks.size());
}

// ---------------------------------------------------------------------------
// 跨文本块合并
// ---------------------------------------------------------------------------

void Wave21StreamTests::mergeAcrossBlocksKeepsBothHalves() {
    const QByteArray source = taggedPdf();
    QVector<ContentTextBlock> blocks;
    QString error;
    QVERIFY2(aipdf::pdf::readContentTextBlocks(source, &blocks, &error), qPrintable(error));
    QCOMPARE(blocks.size(), 6);
    // 同一段的两个块：第 2、3 号（正文的前后两半）。
    QCOMPARE(blocks.at(1).text, QStringLiteral("Body paragraph text"));
    QCOMPARE(blocks.at(2).text, QStringLiteral("and its second line"));

    MergeBlocksOptions options;
    options.targetWidth = 120.0;
    const MergeBlocksResult result =
        aipdf::pdf::mergeTextBlocksAcrossSelection(source, QVector<int>{1, 2}, options);
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    QVERIFY(!result.bytes.isEmpty());
    QCOMPARE(result.report.blockCount, 2);
    // 合并之后自动换行：栏宽 120 点放不下整段。
    QVERIFY(result.report.mergedLineCount >= 2);

    // 两半都还在页面上，一个字都没少。
    int pageCount = 0;
    const QStringList texts = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString pageText = squeezed(texts.value(0));
    QVERIFY(pageText.contains(QStringLiteral("Bodyparagraphtext")));
    QVERIFY(pageText.contains(QStringLiteral("anditssecondline")));
    // 原来那两块的字样不能再各占一段（否则是复制而不是合并）。
    QCOMPARE(pageText.count(QStringLiteral("Bodyparagraphtext")), 1);
    // 而且**确实合并了**：合并之后是一段连续的文字，
    // 内容流里的显示操作比原来多（拆成了几行）。
    const QByteArray originalContent = pageContentOf(source, 0);
    const QByteArray content = pageContentOf(result.bytes, 0);
    QVERIFY(!content.isEmpty());
    QVERIFY(countOccurrences(content, "Tj") > countOccurrences(originalContent, "Tj"));
}

void Wave21StreamTests::mergeAcrossBlocksKeepsPerBlockStyle() {
    MergeBlocksOptions options;
    options.targetWidth = 400.0;
    const MergeBlocksResult result = aipdf::pdf::mergeTextBlocksAcrossSelection(
        taggedPdf(), QVector<int>{1, 2}, options);
    QVERIFY2(result.ok(), qPrintable(result.report.error));
    const QByteArray content = pageContentOf(result.bytes, 0);
    QVERIFY(!content.isEmpty());
    // 两个块本来是 12 磅红 / 10 磅蓝：合并之后必须都还在，
    // 不能整段统一成一种样式。
    QVERIFY(content.contains("12 Tf"));
    QVERIFY(content.contains("10 Tf"));
    QVERIFY(content.contains("1 0 0 rg"));
    QVERIFY(content.contains("0 0 1 rg"));
    // 合并成一段之后，两半落在**同一行**上（栏宽 400 放得下整段）。
    int pageCount = 0;
    QString error;
    const QStringList texts = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(texts.value(0).contains(QStringLiteral("Body paragraph text and its second line")));
}

void Wave21StreamTests::mergeRejectsWhenGraphicBetween() {
    MergeBlocksOptions options;
    const MergeBlocksResult result =
        aipdf::pdf::mergeTextBlocksAcrossSelection(imageBetweenPdf(), QVector<int>{0, 1}, options);
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.report.imageBetween || result.report.tableBetween);
    QVERIFY(result.report.error.contains(QStringLiteral("图片")) ||
            result.report.error.contains(QStringLiteral("表格")));
}

void Wave21StreamTests::mergeRejectsUnknownBlock() {
    MergeBlocksOptions options;
    const MergeBlocksResult result =
        aipdf::pdf::mergeTextBlocksAcrossSelection(taggedPdf(), QVector<int>{0, 99}, options);
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.report.error.contains(QStringLiteral("99")));
}

void Wave21StreamTests::mergeRejectsSingleBlock() {
    MergeBlocksOptions options;
    const MergeBlocksResult result =
        aipdf::pdf::mergeTextBlocksAcrossSelection(taggedPdf(), QVector<int>{1}, options);
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(!result.report.error.isEmpty());
}

// ---------------------------------------------------------------------------
// 整篇翻译写回
// ---------------------------------------------------------------------------

void Wave21StreamTests::translateWritesEveryParagraph() {
    FakeProvider provider;
    TranslationOptions options;
    const QByteArray source = taggedPdf();
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(source, &provider, options);
    QVERIFY2(result.ok(), qPrintable(translationDetail(result.report)));
    QVERIFY(!result.bytes.isEmpty());
    // 整篇 6 段都翻了。
    QCOMPARE(result.report.unitCount, 5);
    QCOMPARE(result.report.writtenCount, 5);
    QCOMPARE(provider.callCount, 5);

    // 译文确实落进 PDF 了（用 PDFium 独立回读）。
    int pageCount = 0;
    QString error;
    const QStringList texts = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(pageCount, 2);
    const QString all = squeezed(texts.join(QString()));
    QVERIFY(all.contains(QStringLiteral("T-Chapterone")));
    QVERIFY(all.contains(QStringLiteral("T-Bodyparagraphtextanditssecondline")));
    QVERIFY(all.contains(QStringLiteral("T-Anotherparagraph")));
    QVERIFY(all.contains(QStringLiteral("T-Pagetwobody")));
    QVERIFY(all.contains(QStringLiteral("T-Finalparagraph")));
    
    // 原文已经被译文换掉了：原文只作为译文的一部分出现一次，没有多出来一份。
    QCOMPARE(all.count(QStringLiteral("Chapterone")), 1);
}

void Wave21StreamTests::translateReportsProviderFailure() {
    FakeProvider provider;
    provider.failingSources.append(QStringLiteral("Another paragraph"));
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(taggedPdf(), &provider, options);
    QVERIFY2(result.ok(), qPrintable(translationDetail(result.report)));
    // 翻成了 4 段，失败 1 段。
    QCOMPARE(result.report.writtenCount, 4);
    QCOMPARE(result.report.failedCount, 1);
    QString reason;
    for (const aipdf::pdf::TranslationUnit& unit : result.report.units) {
        if (unit.sourceText == QStringLiteral("Another paragraph")) {
            reason = unit.reason;
            QVERIFY(!unit.written);
        }
    }
    QVERIFY(!reason.isEmpty());
    QVERIFY(reason.contains(QStringLiteral("失败")));
    // 失败的那一段保留原文，没有被写成译文。
    int pageCount = 0;
    QString error;
    const QStringList texts = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    const QString all = squeezed(texts.join(QString()));
    QVERIFY(all.contains(QStringLiteral("Anotherparagraph")));
    QVERIFY(!all.contains(QStringLiteral("T-Anotherparagraph")));
}

void Wave21StreamTests::translateRejectsUnconfiguredProvider() {
    FakeProvider provider;
    provider.configured = false;
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(taggedPdf(), &provider, options);
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(!result.report.error.isEmpty());
    QCOMPARE(provider.callCount, 0);
}

void Wave21StreamTests::translateRejectsDemoProvider() {
    FakeProvider provider;
    provider.demo = true;
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(taggedPdf(), &provider, options);
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.report.error.contains(QStringLiteral("演示")));
    QCOMPARE(provider.callCount, 0);
}

void Wave21StreamTests::translateRejectsSameAsSource() {
    FakeProvider provider;
    provider.echoSources.append(QStringLiteral("Final paragraph"));
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(taggedPdf(), &provider, options);
    QVERIFY2(result.ok(), qPrintable(translationDetail(result.report)));
    QCOMPARE(result.report.failedCount, 1);
    QString reason;
    for (const aipdf::pdf::TranslationUnit& unit : result.report.units) {
        if (unit.sourceText == QStringLiteral("Final paragraph")) reason = unit.reason;
    }
    QVERIFY(reason.contains(QStringLiteral("原文")));
    // 这一段保留原文，没有把原文当译文写回去。
    int pageCount = 0;
    QString error;
    const QStringList texts = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    const QString all = squeezed(texts.join(QString()));
    QCOMPARE(all.count(QStringLiteral("Finalparagraph")), 1);
}

void Wave21StreamTests::translateRejectsNonLatin1() {
    FakeProvider provider;
    provider.prefix = QStringLiteral("译文：");
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(taggedPdf(), &provider, options);
    // 非 Latin-1 的译文这一版写不进去（要嵌入中文字体才行），
    // 所以整篇都不写回，报告里写明原因。
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QVERIFY(result.report.error.contains(QStringLiteral("字体")));
}

void Wave21StreamTests::translateKeepsOriginalWhenDoesNotFit() {
    FakeProvider provider;
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(bottomParagraphPdf(), &provider, options);
    QVERIFY2(result.ok(), qPrintable(translationDetail(result.report)));
    // 上面那段放得下，下面那段贴着页底放不下。
    QCOMPARE(result.report.unitCount, 2);
    QCOMPARE(result.report.writtenCount, 1);
    QCOMPARE(result.report.failedCount, 1);
    bool sawOverflow = false;
    for (const aipdf::pdf::TranslationUnit& unit : result.report.units) {
        if (unit.sourceText == QStringLiteral("Bottom paragraph")) {
            sawOverflow = true;
            QVERIFY(!unit.written);
            QVERIFY(unit.reason.contains(QStringLiteral("放不下")));
        }
    }
    QVERIFY(sawOverflow);
    // 放不下的段落保留原文，一个字都没被截断。
    int pageCount = 0;
    QString error;
    const QStringList texts = aipdf::pdf::extractPageTexts(result.bytes, &pageCount, &error);
    const QString all = squeezed(texts.join(QString()));
    QVERIFY(all.contains(QStringLiteral("Bottomparagraph")));
    QVERIFY(!all.contains(QStringLiteral("T-Bottomparagraph")));
    // 其余的段落确实翻掉了。
    QVERIFY(all.contains(QStringLiteral("T-Topparagraph")));
}

void Wave21StreamTests::translateKeepsDocumentWhenNothingFits() {
    FakeProvider provider;
    provider.paddingLength = 4000;
    TranslationOptions options;
    const TranslationWriteResult result =
        aipdf::pdf::translateDocument(taggedPdf(), &provider, options);
    // 一段都写不进去时整篇不写，文档保持原样。
    QVERIFY(!result.ok());
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.report.writtenCount, 0);
    QCOMPARE(result.report.failedCount, 5);
    QVERIFY(result.report.error.contains(QStringLiteral("放不下")));
}

// ---------------------------------------------------------------------------
// 写回后的内容流必须还是合法的
// ---------------------------------------------------------------------------

void Wave21StreamTests::writtenContentStreamStaysParseable() {
    StreamingReflowOptions options;
    options.targetPageWidth = 200.0;
    const StreamingReflowResult result = aipdf::pdf::reflowDocumentStream(taggedPdf(), options);
    QVERIFY2(result.ok(), qPrintable(reflowDetail(result.report)));
    for (int page = 0; page < 2; ++page) {
        const QByteArray content = pageContentOf(result.bytes, page);
        QVERIFY(!content.isEmpty());
        // 用仓库自己的词法再解析一遍：相邻 token 粘在一起（"24Tf"）会被这里挡住。
        const aipdf::pdf::ContentParseResult parsed =
            aipdf::pdf::ContentStream::tokenize(content);
        QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
        // 数字与算子之间必须留分隔符：数一数 "Tf" 前面是不是空格。
        QVERIFY(!content.contains("Tf") || content.contains(" Tf"));
    }
}

QTEST_MAIN(Wave21StreamTests)
#include "Wave21StreamTests.moc"