#include "pdf/TagTreeWriter.h"

#include "pdf/PdfiumAdapter.h"

#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QString>
#include <QTest>
#include <QVector>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::PdfOpenResult;
using aipdf::pdf::TaggedRegion;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 两页样例：每页几行文字，坐标都是已知且互不重叠的。
//
// 为什么自己造 PDF 而不是用夹具：这一组用例要断言「哪个 Span 装哪一行文字」，
// 需要页面内容完全可控。夹具里的 normal.pdf 只有一行字，断言不出顺序。
QByteArray buildSamplePdf() {
    const QByteArray pageOne =
        "BT /F1 12 Tf 40 250 Td (Alpha) Tj ET\n"
        "BT /F1 12 Tf 40 200 Td (Bravo) Tj ET\n"
        "BT /F1 12 Tf 40 150 Td (Charlie) Tj ET";
    const QByteArray pageTwo =
        "BT /F1 12 Tf 40 250 Td (Delta) Tj ET\n"
        "BT /F1 12 Tf 40 200 Td (Echo) Tj ET";

    QList<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(pageOne.size())
                                   .arg(QString::fromLatin1(pageOne))
                                   .toLatin1()));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R >>")));
    objects.append(qMakePair(6, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                                   .arg(pageTwo.size())
                                   .arg(QString::fromLatin1(pageTwo))
                                   .toLatin1()));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));

    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = offsets.size() + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        // 注意 QString::arg 的重载：arg(int, fieldWidth, base, fillChar)。
        // 只传三个参数时第三个是**进制**不是填充字符——写成 48 会把偏移
        // 按 48 进制输出，qpdf 会判定 xref 表损坏。
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    // QString::arg 不会把 %% 还原成 %，所以 EOF 标记这里直接写两个百分号。
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 第一页的三个区域，故意打乱传入次序：order 2 在最前，order 0 在中间。
QVector<aipdf::pdf::TaggedRegion> scrambledPageOneRegions() {
    QVector<aipdf::pdf::TaggedRegion> regions;
    aipdf::pdf::TaggedRegion charlie;
    charlie.rect = QRectF(30.0, 145.0, 120.0, 20.0);
    charlie.order = 2;
    regions.append(charlie);

    aipdf::pdf::TaggedRegion bravo;
    bravo.rect = QRectF(30.0, 195.0, 120.0, 20.0);
    bravo.order = 0;
    regions.append(bravo);

    aipdf::pdf::TaggedRegion alpha;
    alpha.rect = QRectF(30.0, 245.0, 120.0, 20.0);
    alpha.order = 1;
    regions.append(alpha);
    return regions;
}

QVector<aipdf::pdf::TaggedRegion> pageTwoRegions() {
    QVector<aipdf::pdf::TaggedRegion> regions;
    aipdf::pdf::TaggedRegion delta;
    delta.rect = QRectF(30.0, 245.0, 120.0, 20.0);
    delta.order = 0;
    regions.append(delta);

    aipdf::pdf::TaggedRegion echo;
    echo.rect = QRectF(30.0, 195.0, 120.0, 20.0);
    echo.order = 1;
    regions.append(echo);
    return regions;
}

QVector<QVector<aipdf::pdf::TaggedRegion>> sampleRegions() {
    QVector<QVector<aipdf::pdf::TaggedRegion>> regions;
    regions.append(scrambledPageOneRegions());
    regions.append(pageTwoRegions());
    return regions;
}

// 判据：同一页的 MCID 必须从 0 开始、连续、不重复。
//
// 抽出来单独写一个函数，是为了能对它做「负例守卫」——
// 用例必须能证明这个判据不是空转的（见 mcidGuardDetectsBadInput）。
QString mcidViolation(const QVector<int>& mcids) {
    if (mcids.isEmpty()) return QString();
    QVector<int> sorted = mcids;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted.at(i) != i) {
            return QStringLiteral("MCID 不是从 0 开始的连续整数：第 %1 个是 %2")
                .arg(i)
                .arg(sorted.at(i));
        }
    }
    return QString();
}

#ifdef AIPDF_WITH_QPDF

// 打开一份 PDF（只读）。
std::unique_ptr<QPDF> openWithQpdf(const QByteArray& bytes) {
    auto pdf = std::make_unique<QPDF>();
    pdf->processMemoryFile("test.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    return pdf;
}

// 标签树根对象的 /Type（不是 /StructTreeRoot 时返回空）。
QString structTreeRootType(const QByteArray& bytes) {
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const QPDFObjectHandle structTreeRoot = pdf->getRoot().getKey("/StructTreeRoot");
    if (!structTreeRoot.isDictionary()) return {};
    const QPDFObjectHandle type = structTreeRoot.getKey("/Type");
    if (!type.isName()) return {};
    return QString::fromStdString(type.getName());
}

// catalog 里有没有 /StructTreeRoot 引用。
bool catalogHasStructTreeRoot(const QByteArray& bytes) {
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const QPDFObjectHandle root = pdf->getRoot();
    if (!root.hasKey("/StructTreeRoot")) return false;
    return root.getKey("/StructTreeRoot").isIndirect();
}

// 文档有没有被标成「带标签」（/MarkInfo <</Marked true>>）。
bool catalogIsMarked(const QByteArray& bytes) {
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const QPDFObjectHandle markInfo = pdf->getRoot().getKey("/MarkInfo");
    if (!markInfo.isDictionary()) return false;
    const QPDFObjectHandle marked = markInfo.getKey("/Marked");
    return marked.isBool() && marked.getBoolValue();
}

// ParentTree 的 /Nums 里的键（按出现顺序）。
QVector<int> parentTreeKeys(const QByteArray& bytes) {
    QVector<int> keys;
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const QPDFObjectHandle parentTree = pdf->getRoot().getKey("/StructTreeRoot").getKey("/ParentTree");
    if (!parentTree.isDictionary()) return keys;
    const QPDFObjectHandle nums = parentTree.getKey("/Nums");
    if (!nums.isArray()) return keys;
    const std::vector<QPDFObjectHandle> entries = nums.getArrayAsVector();
    for (size_t i = 0; i + 1 < entries.size(); i += 2) {
        if (!entries.at(i).isInteger()) continue;
        keys.append(static_cast<int>(entries.at(i).getIntValue()));
    }
    return keys;
}

// ParentTree 每个键对应的值数组长度（按 MCID 下标展开后的长度）。
QVector<int> parentTreeValueSizes(const QByteArray& bytes) {
    QVector<int> sizes;
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const QPDFObjectHandle parentTree = pdf->getRoot().getKey("/StructTreeRoot").getKey("/ParentTree");
    if (!parentTree.isDictionary()) return sizes;
    const QPDFObjectHandle nums = parentTree.getKey("/Nums");
    if (!nums.isArray()) return sizes;
    const std::vector<QPDFObjectHandle> entries = nums.getArrayAsVector();
    for (size_t i = 1; i < entries.size(); i += 2) {
        if (!entries.at(i).isArray()) {
            sizes.append(-1);
            continue;
        }
        sizes.append(entries.at(i).getArrayNItems());
    }
    return sizes;
}

// 取某一页解压后的内容流。
QByteArray pageContent(const QByteArray& bytes, int pageIndex) {
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return {};
    QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
    page.coalesceContentStreams();
    std::vector<QPDFObjectHandle> contents = page.getPageContents();
    QByteArray merged;
    for (QPDFObjectHandle& stream : contents) {
        if (!stream.isStream()) continue;
        const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        merged += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                             static_cast<qsizetype>(data->getSize()));
    }
    return merged;
}

// 从内容流里抽出所有 MCID（按出现顺序）。
QVector<int> extractMcids(const QByteArray& content) {
    QVector<int> mcids;
    static const QRegularExpression pattern(
        QStringLiteral("/Tag\\s*<</MCID\\s+(\\d+)>>\\s*BDC"));
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(QString::fromLatin1(content));
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        mcids.append(match.captured(1).toInt());
    }
    return mcids;
}

// 从内容流里抽出 (文字, MCID) 对，用来确认「哪个 MCID 装的是哪一行」。
QVector<QPair<QString, int>> extractMarkedRuns(const QByteArray& content) {
    QVector<QPair<QString, int>> runs;
    static const QRegularExpression pattern(
        QStringLiteral("/Tag\\s*<</MCID\\s+(\\d+)>>\\s*BDC\\s*\\(([^)]*)\\)\\s*Tj\\s*EMC"));
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(QString::fromLatin1(content));
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        runs.append({match.captured(2), match.captured(1).toInt()});
    }
    return runs;
}

// 每页的 /StructParents。
QVector<int> structParentsOf(const QByteArray& bytes) {
    QVector<int> values;
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    for (const QPDFObjectHandle& page : pdf->getAllPages()) {
        const QPDFObjectHandle key = page.getKey("/StructParents");
        values.append(key.isInteger() ? static_cast<int>(key.getIntValue()) : -1);
    }
    return values;
}

// 标签树里某一页的 Span 的 MCID 列表（按树顺序）。
QVector<QVector<int>> spanMcidsOfPage(const QByteArray& bytes, int pageIndex) {
    QVector<QVector<int>> result;
    const std::unique_ptr<QPDF> pdf = openWithQpdf(bytes);
    const QPDFObjectHandle structTreeRoot = pdf->getRoot().getKey("/StructTreeRoot");
    if (!structTreeRoot.isDictionary()) return result;
    // StructTreeRoot → /K（Document 元素数组）→ /K（各页的 /P 元素）。
    const QPDFObjectHandle rootChildren = structTreeRoot.getKey("/K");
    if (!rootChildren.isArray()) return result;

    const std::vector<QPDFObjectHandle> pages = pdf->getAllPages();
    for (const QPDFObjectHandle& documentElement : rootChildren.getArrayAsVector()) {
        const QPDFObjectHandle pageChildren = documentElement.getKey("/K");
        if (!pageChildren.isArray()) return result;
        for (const QPDFObjectHandle& child : pageChildren.getArrayAsVector()) {
            const QPDFObjectHandle pageObject = child.getKey("/Pg");
            if (!pageObject.isIndirect()) continue;
            int index = -1;
            for (size_t i = 0; i < pages.size(); ++i) {
                if (pages.at(i).getObjectID() == pageObject.getObjectID()) {
                    index = static_cast<int>(i);
                    break;
                }
            }
            if (index != pageIndex) continue;
            const QPDFObjectHandle children = child.getKey("/K");
            if (!children.isArray()) return result;
            for (const QPDFObjectHandle& span : children.getArrayAsVector()) {
                QVector<int> mcids;
                const QPDFObjectHandle keys = span.getKey("/K");
                if (keys.isArray()) {
                    for (const QPDFObjectHandle& key : keys.getArrayAsVector()) {
                        if (key.isInteger()) mcids.append(static_cast<int>(key.getIntValue()));
                    }
                }
                result.append(mcids);
            }
            return result;
        }
    }
    return result;
}

#else  // 没有 qpdf：给同样的函数一个空壳，保证两种构建下测试都能编过；
      // 运行期由 REQUIRE_QPDF() 跳过这些用例。

inline QByteArray pageContent(const QByteArray&, int) { return {}; }
inline QVector<int> extractMcids(const QByteArray&) { return {}; }
inline QVector<QPair<QString, int>> extractMarkedRuns(const QByteArray&) { return {}; }
inline QVector<int> structParentsOf(const QByteArray&) { return {}; }
inline QVector<QVector<int>> spanMcidsOfPage(const QByteArray&, int) { return {}; }
inline QString structTreeRootType(const QByteArray&) { return {}; }
inline bool catalogHasStructTreeRoot(const QByteArray&) { return false; }
inline bool catalogIsMarked(const QByteArray&) { return false; }
inline QVector<int> parentTreeKeys(const QByteArray&) { return {}; }
inline QVector<int> parentTreeValueSizes(const QByteArray&) { return {}; }

#endif  // AIPDF_WITH_QPDF

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

// 渲染第一页，返回位图。
QImage renderFirstPage(const QByteArray& bytes) {
    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(bytes);
    if (!result.document.isValid()) return {};
    const std::optional<aipdf::pdf::PdfPage> page = result.document.page(0);
    if (!page.has_value()) return {};
    return page->render(1.0);
}

// 两张位图逐像素比较：返回不同的像素数。
int differingPixels(const QImage& left, const QImage& right) {
    if (left.isNull() || right.isNull()) return -1;
    if (left.size() != right.size()) return -1;
    if (left.format() != right.format()) return -1;
    int different = 0;
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            if (left.pixel(x, y) != right.pixel(x, y)) ++different;
        }
    }
    return different;
}

bool containsChinese(const QString& text) {
    for (const QChar character : text) {
        if (character.unicode() >= 0x4E00 && character.unicode() <= 0x9FFF) return true;
    }
    return false;
}

}  // namespace

class TagTreeWriterTests : public QObject {
    Q_OBJECT

private slots:
    void emptyInputIsRejected();
    void nonPdfInputIsRejected();
    void noRegionsReturnsInputUnchanged();
    void structTreeRootIsWritten();
    void catalogReferencesStructTreeRoot();
    void everyPageGetsStructParents();
    void parentTreeHasOneKeyPerPage();
    void markedContentIsPresent();
    void mcidsStartAtZeroAndAreContiguous();
    void spanOrderFollowsRegionOrder();
    void reversedRegionListGivesSameTree();
    void mcidGuardDetectsBadInput();
    void writtenFileOpensInPdfium();
    void textLayerIsUnchanged();
    void renderedPageIsUnchanged();
    void taggingTwiceIsIdempotent();
    void readTaggedOrderReportsTreeOrder();
    void fixtureDocumentCanBeTagged();
};

// 1. 空字节：返回空 + 中文错误，不崩。
void TagTreeWriterTests::emptyInputIsRejected() {
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure({}, sampleRegions(), &error);
    QVERIFY(output.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(containsChinese(error), qPrintable(QStringLiteral("错误信息不是中文：%1").arg(error)));
}

// 2. 非 PDF 数据：返回空 + 中文错误。
void TagTreeWriterTests::nonPdfInputIsRejected() {
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(
        QByteArray("this is definitely not a pdf file"), sampleRegions(), &error);
    QVERIFY(output.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(containsChinese(error), qPrintable(QStringLiteral("错误信息不是中文：%1").arg(error)));
}

// 3. 没有区域：原样返回输入字节（口径写在头文件里），不算错误。
void TagTreeWriterTests::noRegionsReturnsInputUnchanged() {
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, {}, &error);
    QCOMPARE(output, input);
    QVERIFY(error.isEmpty());
}

// 4. 写完后 qpdf 能读出 /StructTreeRoot，类型是 /StructTreeRoot。
void TagTreeWriterTests::structTreeRootIsWritten() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    QCOMPARE(structTreeRootType(output), QStringLiteral("/StructTreeRoot"));
}

// 5. catalog 里有 /StructTreeRoot 引用。
void TagTreeWriterTests::catalogReferencesStructTreeRoot() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    QVERIFY(catalogHasStructTreeRoot(output));
    // 文档还应该被标成「带标签」。
    QVERIFY(catalogIsMarked(output));
}

// 6. 每页页面字典有 /StructParents，值互不相同且连续。
void TagTreeWriterTests::everyPageGetsStructParents() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<int> parents = structParentsOf(output);
    QCOMPARE(parents.size(), 2);
    QCOMPARE(parents.at(0), 0);
    QCOMPARE(parents.at(1), 1);
    QVERIFY(parents.at(0) != parents.at(1));
}

// 7. 文档里有 /ParentTree，/Nums 的键数 = 页数。
void TagTreeWriterTests::parentTreeHasOneKeyPerPage() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    // /Nums 是 [键 值 键 值 …]：两页就是两个键（0、1），值是「MCID → 元素」的数组。
    const QVector<int> keys = parentTreeKeys(output);
    QCOMPARE(keys, QVector<int>({0, 1}));
    const QVector<int> valueSizes = parentTreeValueSizes(output);
    QCOMPARE(valueSizes, QVector<int>({3, 2}));
}

// 8. 内容流里出现了 /Tag 与 BDC / EMC。
void TagTreeWriterTests::markedContentIsPresent() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QByteArray content = pageContent(output, 0);
    QVERIFY(content.contains("/Tag"));
    QVERIFY(content.contains("BDC"));
    QVERIFY(content.contains("EMC"));
    QVERIFY(content.contains("Alpha"));
    QVERIFY(content.contains("Bravo"));
    QVERIFY(content.contains("Charlie"));
    qInfo() << "第一页标记后的内容流：\n" << QString::fromLatin1(content);
}

// 9. marked content 的 MCID 从 0 开始连续（同一页内不重复、不跳号）。
void TagTreeWriterTests::mcidsStartAtZeroAndAreContiguous() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<int> mcids = extractMcids(pageContent(output, 0));
    QCOMPARE(mcids.size(), 3);
    const QString violation = mcidViolation(mcids);
    QVERIFY2(violation.isEmpty(), qPrintable(violation));

    const QVector<int> secondPage = extractMcids(pageContent(output, 1));
    QCOMPARE(secondPage.size(), 2);
    QVERIFY(mcidViolation(secondPage).isEmpty());
}

// 10. 标签树里 Span 的顺序 = 调用方给的 order 顺序（传入顺序是乱的）。
void TagTreeWriterTests::spanOrderFollowsRegionOrder() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    // 内容流里的 MCID：视觉顺序（从上到下）是 Alpha → Bravo → Charlie。
    const QVector<QPair<QString, int>> runs = extractMarkedRuns(pageContent(output, 0));
    QCOMPARE(runs.size(), 3);
    QHash<QString, int> mcidOfText;
    for (const auto& run : runs) mcidOfText.insert(run.first, run.second);

    // 树里的 Span 顺序必须按 region.order 排：Bravo(order 0) → Alpha(order 1) → Charlie(order 2)。
    const QVector<QVector<int>> spans = spanMcidsOfPage(output, 0);
    QCOMPARE(spans.size(), 3);
    QCOMPARE(spans.at(0), QVector<int>{mcidOfText.value(QStringLiteral("Bravo"))});
    QCOMPARE(spans.at(1), QVector<int>{mcidOfText.value(QStringLiteral("Alpha"))});
    QCOMPARE(spans.at(2), QVector<int>{mcidOfText.value(QStringLiteral("Charlie"))});

    // 第二页同样按 order 排。
    const QVector<QVector<int>> secondPage = spanMcidsOfPage(output, 1);
    QCOMPARE(secondPage.size(), 2);
    const QVector<QPair<QString, int>> secondRuns = extractMarkedRuns(pageContent(output, 1));
    QHash<QString, int> secondMcid;
    for (const auto& run : secondRuns) secondMcid.insert(run.first, run.second);
    QCOMPARE(secondPage.at(0), QVector<int>{secondMcid.value(QStringLiteral("Delta"))});
    QCOMPARE(secondPage.at(1), QVector<int>{secondMcid.value(QStringLiteral("Echo"))});
}

// 11/13. 负例守卫：把区域列表整个倒过来写，树里的顺序仍然按 order 升序。
void TagTreeWriterTests::reversedRegionListGivesSameTree() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();

    QVector<QVector<aipdf::pdf::TaggedRegion>> reversed = sampleRegions();
    for (QVector<aipdf::pdf::TaggedRegion>& pageRegions : reversed) {
        std::reverse(pageRegions.begin(), pageRegions.end());
    }

    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, reversed, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<QVector<int>> spans = spanMcidsOfPage(output, 0);
    QCOMPARE(spans.size(), 3);
    const QVector<QPair<QString, int>> runs = extractMarkedRuns(pageContent(output, 0));
    QHash<QString, int> mcidOfText;
    for (const auto& run : runs) mcidOfText.insert(run.first, run.second);
    // 与正序输入完全一致：order 0 的 Bravo 在最前。
    QCOMPARE(spans.at(0), QVector<int>{mcidOfText.value(QStringLiteral("Bravo"))});
    QCOMPARE(spans.at(1), QVector<int>{mcidOfText.value(QStringLiteral("Alpha"))});
    QCOMPARE(spans.at(2), QVector<int>{mcidOfText.value(QStringLiteral("Charlie"))});
}

// 14. 负例守卫：MCID 判据本身必须能抓到坏数据，否则上面的用例就是空转。
void TagTreeWriterTests::mcidGuardDetectsBadInput() {
    // 起点不是 0。
    QVERIFY(!mcidViolation(QVector<int>{5, 6, 7}).isEmpty());
    // 跳号。
    QVERIFY(!mcidViolation(QVector<int>{0, 2}).isEmpty());
    // 重复。
    QVERIFY(!mcidViolation(QVector<int>{0, 1, 1}).isEmpty());
    // 好的数据不能被误判。
    QVERIFY(mcidViolation(QVector<int>{0, 1, 2}).isEmpty());

    // 再用真实的写出来的东西验一次接线：从内容流里抽出来的 MCID 必须过判据。
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));
    QVERIFY(mcidViolation(extractMcids(pageContent(output, 0))).isEmpty());
}

// 11. 写完后 PDFium 能打开这份 PDF，页数不变。
void TagTreeWriterTests::writtenFileOpensInPdfium() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY2(result.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(result.errorMessage)));
    QCOMPARE(result.document.pageCount(), 2);
}

// 12. 写完后 PDFium 读出的文本层内容与写之前完全一致。
void TagTreeWriterTests::textLayerIsUnchanged() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QString before = plainTextOf(input);
    const QString after = plainTextOf(output);
    QVERIFY(!before.isEmpty());
    QCOMPARE(after, before);
    QVERIFY(after.contains(QStringLiteral("Alpha")));
    QVERIFY(after.contains(QStringLiteral("Charlie")));
    QVERIFY(after.contains(QStringLiteral("Echo")));
    qInfo() << "写前文本层：" << before << "| 写后文本层：" << after;
}

// 15. 页面外观不变：写前写后各渲染一页，逐像素比较必须完全相同。
void TagTreeWriterTests::renderedPageIsUnchanged() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QImage before = renderFirstPage(input);
    const QImage after = renderFirstPage(output);
    QVERIFY(!before.isNull());
    QVERIFY(!after.isNull());
    QCOMPARE(after.size(), before.size());
    const int different = differingPixels(before, after);
    QCOMPARE(different, 0);
    qInfo() << "渲染尺寸" << before.size() << "×" << before.height() << "，不同像素数：" << different;
}

// 18. 反复「应用区域顺序」不能把标记套两层（MCID 会重复）。
void TagTreeWriterTests::taggingTwiceIsIdempotent() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray once = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!once.isEmpty(), qPrintable(error));
    const QByteArray twice = aipdf::pdf::writeTaggedStructure(once, sampleRegions(), &error);
    QVERIFY2(!twice.isEmpty(), qPrintable(error));

    // 两次打出来的标记必须一模一样：文字与 MCID 的对应关系不能变。
    const QVector<QPair<QString, int>> first = extractMarkedRuns(pageContent(once, 0));
    const QVector<QPair<QString, int>> second = extractMarkedRuns(pageContent(twice, 0));
    QCOMPARE(second.size(), first.size());
    for (int i = 0; i < first.size(); ++i) {
        QCOMPARE(second.at(i).first, first.at(i).first);
        QCOMPARE(second.at(i).second, first.at(i).second);
    }
    // MCID 依然是从 0 开始的连续整数（没有出现重复）。
    QVERIFY(mcidViolation(extractMcids(pageContent(twice, 0))).isEmpty());
    // 也不能留下上一轮的残渣：内容流里 "/Tag" 的个数应当正好等于显示操作符的个数。
    const QByteArray content = pageContent(twice, 0);
    int tagCount = 0;
    for (int at = content.indexOf("/Tag"); at >= 0; at = content.indexOf("/Tag", at + 1)) ++tagCount;
    QCOMPARE(tagCount, first.size());
    // 文字与外观也不能被第二遍改动。
    QCOMPARE(plainTextOf(twice), plainTextOf(input));
    QCOMPARE(differingPixels(renderFirstPage(input), renderFirstPage(twice)), 0);
}

// 16. readTaggedOrder 读回来的顺序与标签树一致（且能看出与 MCID 顺序的差别）。
void TagTreeWriterTests::readTaggedOrderReportsTreeOrder() {
    REQUIRE_QPDF();
    const QByteArray input = buildSamplePdf();
    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, sampleRegions(), &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const QVector<QVector<int>> order = aipdf::pdf::readTaggedOrder(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(order.size(), 2);
    QCOMPARE(order.at(0).size(), 3);
    QCOMPARE(order.at(1).size(), 2);

    // 身份下标按最小 MCID 排：第一页的 Span 是 Bravo(1) → Alpha(0) → Charlie(2)，
    // 于是身份是 1 → 0 → 2，正好说明「树顺序」与「MCID 顺序」不是一回事。
    QCOMPARE(order.at(0), QVector<int>({1, 0, 2}));
    QCOMPARE(order.at(1), QVector<int>({0, 1}));
}

// 17. 仓库自己生成的夹具文档也能打标签（证明不是只对手工构造的 PDF 有效）。
void TagTreeWriterTests::fixtureDocumentCanBeTagged() {
    REQUIRE_QPDF();
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QStringLiteral("/normal.pdf"));
    if (!file.open(QIODevice::ReadOnly)) {
        QSKIP("夹具 normal.pdf 不可用");
    }
    const QByteArray input = file.readAll();
    file.close();

    QVector<QVector<aipdf::pdf::TaggedRegion>> regions;
    QVector<aipdf::pdf::TaggedRegion> pageRegions;
    aipdf::pdf::TaggedRegion region;
    region.rect = QRectF(10.0, 45.0, 180.0, 30.0);
    region.order = 0;
    pageRegions.append(region);
    regions.append(pageRegions);

    QString error;
    const QByteArray output = aipdf::pdf::writeTaggedStructure(input, regions, &error);
    QVERIFY2(!output.isEmpty(), qPrintable(error));

    const PdfOpenResult result = aipdf::pdf::openDocumentFromBytes(output);
    QVERIFY2(result.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(result.errorMessage)));
    QCOMPARE(result.document.pageCount(), 1);
    QCOMPARE(plainTextOf(output), plainTextOf(input));

    QCOMPARE(structTreeRootType(output), QStringLiteral("/StructTreeRoot"));
}

QTEST_MAIN(TagTreeWriterTests)
#include "TagTreeWriterTests.moc"
