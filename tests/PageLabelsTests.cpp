#include "pdf/PageLabels.h"

#include "pdf/PdfiumAdapter.h"

#include <QFile>
#include <QString>
#include <QTest>
#include <QVector>

#include <memory>
#include <optional>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::PageLabelRange;
using aipdf::pdf::PageLabelScheme;
using aipdf::pdf::PdfOpenResult;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 读夹具 PDF。
QByteArray fixtureBytes(const QString& name) {
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// 文本层（PDFium 读出来的纯文本）。
QString plainTextOf(const QByteArray& bytes) {
    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!result.document.isValid()) return {};
    QString text;
    for (int i = 0; i < result.document.pageCount(); ++i) {
        const std::optional<aipdf::pdf::PdfPage> page = result.document.page(i);
        if (!page.has_value()) continue;
        const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
        if (textPage.has_value()) text += textPage->plainText();
    }
    return text;
}

// 写进去的标签字典（直接问 qpdf，不经过本模块的读回函数）。
//
// 为什么要有这个：读回函数是我们自己写的，用它验证自己写的东西，
// 万一两边一起错就发现不了。这里绕开读回函数，直接看文件里的原始结构。
struct RawLabel {
    int index = -1;
    QString style;
    QString prefix;
    int start = 0;
};

QVector<RawLabel> rawLabelsOf(const QByteArray& bytes) {
    QVector<RawLabel> labels;
#ifdef AIPDF_WITH_QPDF
    const std::unique_ptr<QPDF> pdf = std::make_unique<QPDF>();
    pdf->processMemoryFile("test.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const QPDFObjectHandle pageLabels = pdf->getRoot().getKey("/PageLabels");
    if (!pageLabels.isDictionary()) return labels;
    const QPDFObjectHandle nums = pageLabels.getKey("/Nums");
    if (!nums.isArray()) return labels;
    const std::vector<QPDFObjectHandle> items = nums.getArrayAsVector();
    for (size_t i = 0; i + 1 < items.size(); i += 2) {
        if (!items.at(i).isInteger()) continue;
        RawLabel label;
        label.index = static_cast<int>(items.at(i).getIntValue());
        const QPDFObjectHandle& dict = items.at(i + 1);
        if (dict.isDictionary()) {
            const QPDFObjectHandle style = dict.getKey("/S");
            if (style.isName()) label.style = QString::fromStdString(style.getName());
            const QPDFObjectHandle prefix = dict.getKey("/P");
            if (prefix.isString()) label.prefix = QString::fromUtf8(prefix.getUTF8Value().c_str());
            const QPDFObjectHandle start = dict.getKey("/St");
            if (start.isInteger()) label.start = static_cast<int>(start.getIntValue());
        }
        labels.append(label);
    }
#endif
    return labels;
}

// 前言罗马数字、正文阿拉伯数字的两段规则。
PageLabelScheme twoSectionScheme() {
    PageLabelScheme scheme;
    PageLabelRange front;
    front.startIndex = 0;
    front.count = 3;
    front.style = QStringLiteral("/r");
    front.prefix = QString();
    scheme.ranges.append(front);

    PageLabelRange body;
    body.startIndex = 3;
    body.count = 0;
    body.style = QStringLiteral("/D");
    body.prefix = QStringLiteral("A-");
    scheme.ranges.append(body);
    return scheme;
}

}  // namespace

class PageLabelTests : public QObject {
    Q_OBJECT

private slots:
    void writeThenReadKeepsRanges();
    void documentWithoutPageLabelsReadsEmpty();
    void brokenInputIsRejected();
    void invalidSchemeIsRejected();
    void formatCoversEveryStyle();
    void formatHandlesBoundaries();
    void lettersFollowThePdfRepeatRule();
    void writtenFileStillOpensInPdfium();
    void multipleRangesMapToLabels();
    void prefixAndStartNumberAreApplied();
    void uncoveredPageHasNoLabel();
    void writingAgainReplacesPreviousLabels();
    void emptyStyleInheritsPreviousRange();
};

// 1. 写进去再读回来：区间、样式、前缀都要对得上。
void PageLabelTests::writeThenReadKeepsRanges() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("multipage.pdf"));
    QVERIFY2(!input.isEmpty(), "夹具 multipage.pdf 不可用");

    QString error;
    const QByteArray output = aipdf::pdf::writePageLabels(input, twoSectionScheme(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<PageLabelRange> ranges = aipdf::pdf::readPageLabels(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ranges.size(), 2);

    QCOMPARE(ranges.at(0).startIndex, 0);
    QCOMPARE(ranges.at(0).style, QStringLiteral("/r"));
    QCOMPARE(ranges.at(0).prefix, QString());
    QCOMPARE(ranges.at(0).count, 3);  // 覆盖到下一段起点为止

    QCOMPARE(ranges.at(1).startIndex, 3);
    QCOMPARE(ranges.at(1).style, QStringLiteral("/D"));
    QCOMPARE(ranges.at(1).prefix, QStringLiteral("A-"));
    QCOMPARE(ranges.at(1).count, 0);  // 最后一段：一直到文档末尾

    // 再直接看文件里的原始结构：/Nums 里确实是「起点 + 标签字典」成对出现。
    // 这一步不经过本模块的读回函数，免得读写两边一起错却发现不了。
    const QVector<RawLabel> raw = rawLabelsOf(output);
    QCOMPARE(raw.size(), 2);
    QCOMPARE(raw.at(0).index, 0);
    QCOMPARE(raw.at(0).style, QStringLiteral("/r"));
    QCOMPARE(raw.at(0).start, 1);
    QCOMPARE(raw.at(1).index, 3);
    QCOMPARE(raw.at(1).style, QStringLiteral("/D"));
    QCOMPARE(raw.at(1).prefix, QStringLiteral("A-"));
    QCOMPARE(raw.at(1).start, 1);  // 换了样式 → 从 1 重新开始
}

// 2. 没有 /PageLabels 的文档读回来是空的，而且不是错误。
void PageLabelTests::documentWithoutPageLabelsReadsEmpty() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("normal.pdf"));
    QVERIFY2(!input.isEmpty(), "夹具 normal.pdf 不可用");

    QString error;
    const QVector<PageLabelRange> ranges = aipdf::pdf::readPageLabels(input, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(ranges.isEmpty());
}

// 3. 空输入、坏输入：都要失败 + 给出原因，而且不能崩。
void PageLabelTests::brokenInputIsRejected() {
    REQUIRE_QPDF();
    QString error;

    const QByteArray empty =
        aipdf::pdf::writePageLabels(QByteArray(), twoSectionScheme(), &error);
    QVERIFY(empty.isEmpty());
    QVERIFY2(!error.isEmpty(), "空输入必须给出失败原因");

    error.clear();
    const QByteArray garbage =
        aipdf::pdf::writePageLabels(QByteArray("this is not a pdf"), twoSectionScheme(), &error);
    QVERIFY(garbage.isEmpty());
    QVERIFY2(!error.isEmpty(), "坏输入必须给出失败原因");

    // 读回也一样。
    error.clear();
    QVERIFY(aipdf::pdf::readPageLabels(QByteArray(), &error).isEmpty());
    QVERIFY2(!error.isEmpty(), "空输入读回必须给出失败原因");

    error.clear();
    QVERIFY(aipdf::pdf::readPageLabels(QByteArray("this is not a pdf"), &error).isEmpty());
    QVERIFY2(!error.isEmpty(), "坏输入读回必须给出失败原因");
}

// 4. 不合法的规则要当场失败，并且**不能**把原文件原样吐回来。
void PageLabelTests::invalidSchemeIsRejected() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("normal.pdf"));
    QVERIFY2(!input.isEmpty(), "夹具 normal.pdf 不可用");
    QString error;

    // 没有任何规则。
    const PageLabelScheme noRules;
    QVERIFY(aipdf::pdf::writePageLabels(input, noRules, &error).isEmpty());
    QVERIFY2(!error.isEmpty(), "空规则必须给出失败原因");

    // 起点是负数。
    error.clear();
    PageLabelScheme negative;
    PageLabelRange bad;
    bad.startIndex = -1;
    bad.style = QStringLiteral("/D");
    negative.ranges.append(bad);
    QVERIFY(aipdf::pdf::writePageLabels(input, negative, &error).isEmpty());
    QVERIFY2(!error.isEmpty(), "负页号必须给出失败原因");

    // 样式不认识。
    error.clear();
    PageLabelScheme unknownStyle;
    PageLabelRange weird;
    weird.startIndex = 0;
    weird.style = QStringLiteral("/X");
    unknownStyle.ranges.append(weird);
    QVERIFY(aipdf::pdf::writePageLabels(input, unknownStyle, &error).isEmpty());
    QVERIFY2(!error.isEmpty(), "未知样式必须给出失败原因");
    QVERIFY2(error.contains(QStringLiteral("样式")), qPrintable(error));
}

// 5. 纯编号格式函数：五种样式都要对。
void PageLabelTests::formatCoversEveryStyle() {
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/D"), 12), QStringLiteral("12"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/R"), 4), QStringLiteral("IV"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/r"), 4), QStringLiteral("iv"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 1), QStringLiteral("A"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/a"), 2), QStringLiteral("b"));

    // 没给样式、或者给了不认识的样式：按阿拉伯处理。
    QCOMPARE(aipdf::pdf::formatPageNumber(QString(), 7), QStringLiteral("7"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/X"), 7), QStringLiteral("7"));

    // 一串号码。
    const QVector<QString> romans = aipdf::pdf::formatPageNumbers(QStringLiteral("/r"), 1, 4);
    QCOMPARE(romans, QVector<QString>({QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("iii"),
                                       QStringLiteral("iv")}));
    const QVector<QString> decimals = aipdf::pdf::formatPageNumbers(QStringLiteral("/D"), 5, 3);
    QCOMPARE(decimals, QVector<QString>({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7")}));
}

// 6. 边界：0、负数、超出范围、数量非正。
void PageLabelTests::formatHandlesBoundaries() {
    // 号码必须 ≥ 1（PDF 的页码就是这么规定的）。
    QVERIFY(aipdf::pdf::formatPageNumber(QStringLiteral("/D"), 0).isEmpty());
    QVERIFY(aipdf::pdf::formatPageNumber(QStringLiteral("/r"), -3).isEmpty());

    // 罗马数字没有 4000 以上的标准写法：退回阿拉伯，而不是给一个空标签。
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/R"), 3999), QStringLiteral("MMMCMXCIX"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/R"), 4000), QStringLiteral("4000"));

    // 数量非正 → 空列表。
    QVERIFY(aipdf::pdf::formatPageNumbers(QStringLiteral("/D"), 1, 0).isEmpty());
    QVERIFY(aipdf::pdf::formatPageNumbers(QStringLiteral("/D"), 1, -2).isEmpty());

    // 起始号不合法时从 1 开始。
    const QVector<QString> clamped = aipdf::pdf::formatPageNumbers(QStringLiteral("/D"), 0, 2);
    QCOMPARE(clamped, QVector<QString>({QStringLiteral("1"), QStringLiteral("2")}));
}

// 7. 字母编号要按 **PDF 的规矩**（同一个字母重复），不是 Excel 的 AA、AB、AC。
void PageLabelTests::lettersFollowThePdfRepeatRule() {
    // PDF 32000-1 Table 159：A–Z、AA–ZZ、AAA–ZZZ。
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 1), QStringLiteral("A"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 26), QStringLiteral("Z"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 27), QStringLiteral("AA"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 28), QStringLiteral("BB"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 52), QStringLiteral("ZZ"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/A"), 53), QStringLiteral("AAA"));
    QCOMPARE(aipdf::pdf::formatPageNumber(QStringLiteral("/a"), 27), QStringLiteral("aa"));
}

// 8. 写完标签之后，文件仍然能打开：页数不变、正文一字不差。
void PageLabelTests::writtenFileStillOpensInPdfium() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("multipage.pdf"));
    QVERIFY2(!input.isEmpty(), "夹具 multipage.pdf 不可用");

    QString error;
    const QByteArray output = aipdf::pdf::writePageLabels(input, twoSectionScheme(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));
    QCOMPARE(result.document.pageCount(), 12);
    QVERIFY(!plainTextOf(input).isEmpty());
    QCOMPARE(plainTextOf(output), plainTextOf(input));
}

// 9. 多段规则（前半罗马、后半阿拉伯）：读回来要对，编号也要换算对。
void PageLabelTests::multipleRangesMapToLabels() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("multipage.pdf"));
    QString error;
    const QByteArray output = aipdf::pdf::writePageLabels(input, twoSectionScheme(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<PageLabelRange> ranges = aipdf::pdf::readPageLabels(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ranges.size(), 2);

    // 前言：罗马数字。
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 0), QStringLiteral("i"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 1), QStringLiteral("ii"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 2), QStringLiteral("iii"));
    // 正文：换成阿拉伯数字后从 1 重新开始，并带上前缀。
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 3), QStringLiteral("A-1"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 4), QStringLiteral("A-2"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 11), QStringLiteral("A-9"));
}

// 10. 前缀与起始页码都要落到文件里。
void PageLabelTests::prefixAndStartNumberAreApplied() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("normal.pdf"));
    QVERIFY2(!input.isEmpty(), "夹具 normal.pdf 不可用");
    QString error;

    PageLabelScheme scheme;
    PageLabelRange range;
    range.startIndex = 0;
    range.count = 0;
    range.style = QStringLiteral("/D");
    range.prefix = QStringLiteral("A-");
    scheme.ranges.append(range);
    scheme.firstPageNumber = 5;

    const QByteArray output = aipdf::pdf::writePageLabels(input, scheme, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<PageLabelRange> ranges = aipdf::pdf::readPageLabels(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).prefix, QStringLiteral("A-"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 0, 5), QStringLiteral("A-5"));

    // 文件里写的 /St 就是起始页码 5。
    const QVector<RawLabel> raw = rawLabelsOf(output);
    QCOMPARE(raw.size(), 1);
    QCOMPARE(raw.at(0).start, 5);

    // 中文前缀要能原样读回来（写的时候用的是 Unicode 字符串）。
    error.clear();
    PageLabelScheme chinese;
    PageLabelRange chineseRange;
    chineseRange.startIndex = 0;
    chineseRange.count = 0;
    chineseRange.style = QStringLiteral("/D");
    chineseRange.prefix = QStringLiteral("第 ");
    chinese.ranges.append(chineseRange);

    const QByteArray chineseOutput = aipdf::pdf::writePageLabels(input, chinese, &error);
    QVERIFY2(!chineseOutput.isEmpty(), qPrintable(error));
    const QVector<PageLabelRange> chineseRanges = aipdf::pdf::readPageLabels(chineseOutput, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chineseRanges.size(), 1);
    QCOMPARE(chineseRanges.at(0).prefix, QStringLiteral("第 "));
    QCOMPARE(aipdf::pdf::pageLabelText(chineseRanges, 0), QStringLiteral("第 1"));
}

// 11. 没有规则覆盖到的页没有标签。
void PageLabelTests::uncoveredPageHasNoLabel() {
    QVector<PageLabelRange> ranges;
    PageLabelRange range;
    range.startIndex = 3;
    range.count = 0;
    range.style = QStringLiteral("/D");
    ranges.append(range);

    QVERIFY(aipdf::pdf::pageLabelText(ranges, 0).isEmpty());
    QVERIFY(aipdf::pdf::pageLabelText(ranges, 2).isEmpty());
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 3), QStringLiteral("1"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 5), QStringLiteral("3"));
    QVERIFY(aipdf::pdf::pageLabelText(ranges, -1).isEmpty());
    QVERIFY(aipdf::pdf::pageLabelText(QVector<PageLabelRange>(), 0).isEmpty());
}

// 12. 再写一次是**替换**，不是叠加。
void PageLabelTests::writingAgainReplacesPreviousLabels() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("multipage.pdf"));
    QString error;
    const QByteArray once = aipdf::pdf::writePageLabels(input, twoSectionScheme(), &error);
    QVERIFY2(!once.isEmpty(), qPrintable(error));

    PageLabelScheme single;
    PageLabelRange range;
    range.startIndex = 0;
    range.count = 0;
    range.style = QStringLiteral("/D");
    single.ranges.append(range);

    const QByteArray twice = aipdf::pdf::writePageLabels(once, single, &error);
    QVERIFY2(!twice.isEmpty(), qPrintable(error));

    const QVector<PageLabelRange> ranges = aipdf::pdf::readPageLabels(twice, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ranges.size(), 1);
    QCOMPARE(ranges.at(0).style, QStringLiteral("/D"));
    QCOMPARE(ranges.at(0).prefix, QString());
}

// 13. 样式留空 = 沿用上一段；沿用出来的样式要显式写进文件，
//     这样任何阅读器看到的编号都和界面预览一致。
void PageLabelTests::emptyStyleInheritsPreviousRange() {
    REQUIRE_QPDF();
    const QByteArray input = fixtureBytes(QStringLiteral("multipage.pdf"));
    QString error;

    PageLabelScheme scheme;
    PageLabelRange front;
    front.startIndex = 0;
    front.count = 3;
    front.style = QStringLiteral("/r");
    scheme.ranges.append(front);
    PageLabelRange body;
    body.startIndex = 3;
    body.count = 0;
    body.style = QString();
    scheme.ranges.append(body);

    const QByteArray output = aipdf::pdf::writePageLabels(input, scheme, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<PageLabelRange> ranges = aipdf::pdf::readPageLabels(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges.at(1).style, QStringLiteral("/r"));

    // 同一套样式就接着数：前言 3 页（i、ii、iii），正文从 iv 开始。
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 0), QStringLiteral("i"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 2), QStringLiteral("iii"));
    QCOMPARE(aipdf::pdf::pageLabelText(ranges, 3), QStringLiteral("iv"));
}

QTEST_GUILESS_MAIN(PageLabelTests)
#include "PageLabelsTests.moc"
