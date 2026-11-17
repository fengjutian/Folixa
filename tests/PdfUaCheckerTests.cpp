#include "pdf/PdfUaChecker.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::pdf::PdfUaFinding;
using aipdf::pdf::PdfUaReport;
using aipdf::pdf::PdfUaSeverity;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 把对象列表拼成一份带正确 xref 的 PDF。
//
// 为什么要自己造：PDF/UA 的每一条结论都对应文件里的一个具体结构
//（有没有 /Lang、结构树根是不是 /Document、字体有没有内嵌……），
// 只有自己拼才能精确控制「这一条要报错、那一条要通过」。
//
// infoId 传 0 表示不写 /Info：PDF/UA 的标题检查读的就是 /Info /Title。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects, int rootId, int infoId = 0) {
    QByteArray out("%PDF-1.7\n");
    QHash<int, int> offsets;
    int maxId = 0;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        maxId = qMax(maxId, object.first);
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (offsets.contains(id)) {
            out += QStringLiteral("%1 00000 n \n")
                       .arg(offsets.value(id), 10, 10, QLatin1Char('0'))
                       .toLatin1();
        } else {
            out += QByteArray("0000000000 65535 f \n");
        }
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R").arg(count).arg(rootId).toLatin1();
    if (infoId > 0) out += QStringLiteral(" /Info %1 0 R").arg(infoId).toLatin1();
    out += QStringLiteral(" >>\nstartxref\n%1\n%%EOF\n").arg(xrefOffset).toLatin1();
    return out;
}

// 一个流对象：prefix 是字典里 /Length 之前的内容（不含开头的 << 与收尾的 >>）。
QByteArray streamObject(const QByteArray& prefix, const QByteArray& data) {
    QByteArray out = "<<";
    out += prefix;
    if (!out.endsWith(' ')) out += ' ';
    out += "/Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    return out;
}

// 什么结构都没有的普通 PDF：没有标签树、没有语言、没有标题。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>")));
    return buildPdf(objects, 1);
}

// PDF/UA 各项都做对了的 PDF：标签树完整、语言合法、有标题、
// 字体内嵌、Tab 顺序是 /S、MCID 与内容流对得上、XMP 里声明了 pdfuaid。
QByteArray compliantPdf() {
    const QByteArray xmp =
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "<rdf:Description xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" pdfuaid:part=\"1\"/>\n"
        "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
    const QByteArray content("q /P <</MCID 0>> BDC BT ET Q");

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 4 0 R "
                                           "/MarkInfo << /Marked true >> /Lang (zh-CN) "
                                           "/DisplayDocTitle true /Metadata 9 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Tabs /S "
                                           "/Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /StructTreeRoot /K 7 0 R /ParentTree 12 0 R >>")));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                           "/FontDescriptor 10 0 R >>")));
    objects.append(qMakePair(6, streamObject(QByteArray(), content)));
    objects.append(qMakePair(7, QByteArray("<< /Type /StructElem /S /Document /P 4 0 R /K [8 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Figure /P 4 0 R /Pg 3 0 R /K 0 "
                                           "/Alt (Company logo) >>")));
    objects.append(qMakePair(9, streamObject(QByteArray("/Type /Metadata /Subtype /XML"), xmp)));
    objects.append(qMakePair(10, QByteArray("<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 "
                                            "/FontFile 11 0 R >>")));
    objects.append(qMakePair(11, streamObject(QByteArray(), QByteArray(64, '\0'))));
    objects.append(qMakePair(12, QByteArray("<< /Nums [8 0] >>")));
    objects.append(qMakePair(13, QByteArray("<< /Title (Accessible sample) >>")));
    return buildPdf(objects, 1, 13);
}

// 各项都有问题的那一档：根元素不是 /Document、没有 /ParentTree、
// /Marked 不是 true、语言标记不合法、没有标题、图没有 /Alt、
// 字体没内嵌、/Tabs 是 /R、MCID 对不上、XMP 里没有 pdfuaid。
QByteArray brokenPdf() {
    const QByteArray xmp =
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "<rdf:Description/>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
    const QByteArray content("q /P <</MCID 1>> BDC BT ET Q");

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 4 0 R "
                                           "/MarkInfo << /Marked false >> /Lang (zh_CN) "
                                           "/DisplayDocTitle false /Metadata 9 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Tabs /R "
                                           "/Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /StructTreeRoot /K 7 0 R >>")));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(6, streamObject(QByteArray(), content)));
    objects.append(qMakePair(7, QByteArray("<< /Type /StructElem /S /Sect /P 4 0 R /K [8 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Figure /P 4 0 R /Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(9, streamObject(QByteArray("/Type /Metadata /Subtype /XML"), xmp)));
    objects.append(qMakePair(10, QByteArray("<< /Producer (w21a) >>")));
    return buildPdf(objects, 1, 10);
}

// 找一条结论。
const PdfUaFinding* findFinding(const PdfUaReport& report, const QString& code) {
    for (const PdfUaFinding& finding : report.findings) {
        if (finding.code == code) return &finding;
    }
    return nullptr;
}

bool hasCode(const PdfUaReport& report, const QString& code) {
    return findFinding(report, code) != nullptr;
}

// 按结论的实际级别统计一遍，用来核对报告自报的计数。
void countBySeverity(const PdfUaReport& report, int* errors, int* warnings, int* passes) {
    *errors = 0;
    *warnings = 0;
    *passes = 0;
    for (const PdfUaFinding& finding : report.findings) {
        if (finding.severity == PdfUaSeverity::Error) ++(*errors);
        else if (finding.severity == PdfUaSeverity::Warning) ++(*warnings);
        else ++(*passes);
    }
}

}  // namespace

class PdfUaCheckerTests : public QObject {
    Q_OBJECT

private slots:
    void emptyBytesFail();
    void nonPdfBytesFail();
    void failedCheckReturnsOneErrorFinding();
    void plainPdfHasNoStructTree();
    void missingLangHasDedicatedCode();
    void compliantPdfPassesAllChecks();
    void countsMatchFindingsInEveryFixture();
    void summaryContainsNumbers();
    void invalidLangTagIsReported();
    void structRootMustBeDocument();
    void missingAltTextIsReported();
    void nonEmbeddedFontIsReported();
    void readingOrderTabsIsReported();
    void structureOrderTabsPasses();
    void missingMcidInContentIsReported();
    void xmpWithoutPdfuaidIsWarning();
    void everyFindingHasCodeTitleAndDetail();
    void findingOrderIsStable();
    void errorStringIsClearedOnSuccess();
};

void PdfUaCheckerTests::emptyBytesFail() {
    QString error;
    const PdfUaReport report = aipdf::pdf::checkPdfUa(QByteArray(), &error);
    QVERIFY(!report.compliant);
    QCOMPARE(report.errorCount, 1);
    QCOMPARE(report.warningCount, 0);
    QCOMPARE(report.passCount, 0);
    QVERIFY(hasCode(report, QStringLiteral("ua-empty-document")));
    QVERIFY(!error.isEmpty());
    // 失败原因必须是中文，不能只给一个码。
    QVERIFY(error.contains(QStringLiteral("空")));
}

void PdfUaCheckerTests::nonPdfBytesFail() {
    QString error;
    const PdfUaReport report =
        aipdf::pdf::checkPdfUa(QByteArray("this is not a pdf at all"), &error);
    QVERIFY(!report.compliant);
    QVERIFY(hasCode(report, QStringLiteral("ua-not-a-pdf")));
    QVERIFY(error.contains(QStringLiteral("不是 PDF")));
}

void PdfUaCheckerTests::failedCheckReturnsOneErrorFinding() {
    const PdfUaReport report = aipdf::pdf::checkPdfUa(QByteArray("nope"));
    QCOMPARE(report.findings.size(), 1);
    QCOMPARE(report.errorCount, 1);
    QVERIFY(!report.compliant);
    // 失败时也要给出可执行的建议，不能只有一句「不行」。
    QVERIFY(!report.findings.first().suggestion.isEmpty());
}

void PdfUaCheckerTests::plainPdfHasNoStructTree() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(plainPdf());
    QVERIFY(!report.compliant);
    QVERIFY(hasCode(report, QStringLiteral("ua-missing-struct-tree-root")));
    QVERIFY(!hasCode(report, QStringLiteral("ua-struct-tree-root-present")));
    // 结构树都没有，替代文本那一条就不该出现（不能凭空报一个「图没有替代文本」）。
    QVERIFY(!hasCode(report, QStringLiteral("ua-missing-alt-text")));
}

void PdfUaCheckerTests::missingLangHasDedicatedCode() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(plainPdf());
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-missing-lang"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("/Lang")));
    QVERIFY(!finding->suggestion.isEmpty());
}

void PdfUaCheckerTests::compliantPdfPassesAllChecks() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(compliantPdf());
    QVERIFY2(report.compliant, qPrintable(report.summary()));
    QCOMPARE(report.errorCount, 0);
    QCOMPARE(report.warningCount, 0);
    QCOMPARE(report.passCount, report.findings.size());
    // 这份夹具是按 12 条结论设计的，逐条点名，免得「少查了一项」也被当成合规。
    QVERIFY(hasCode(report, QStringLiteral("ua-struct-tree-root-present")));
    QVERIFY(hasCode(report, QStringLiteral("ua-struct-root-document")));
    QVERIFY(hasCode(report, QStringLiteral("ua-parent-tree-present")));
    QVERIFY(hasCode(report, QStringLiteral("ua-mark-info-marked")));
    QVERIFY(hasCode(report, QStringLiteral("ua-lang-valid")));
    QVERIFY(hasCode(report, QStringLiteral("ua-title-present")));
    QVERIFY(hasCode(report, QStringLiteral("ua-display-doc-title-ok")));
    QVERIFY(hasCode(report, QStringLiteral("ua-alt-text-ok")));
    QVERIFY(hasCode(report, QStringLiteral("ua-fonts-embedded")));
    QVERIFY(hasCode(report, QStringLiteral("ua-tabs-structure-order")));
    QVERIFY(hasCode(report, QStringLiteral("ua-mcid-content-ok")));
    QVERIFY(hasCode(report, QStringLiteral("ua-xmp-metadata-ok")));
}

void PdfUaCheckerTests::countsMatchFindingsInEveryFixture() {
    REQUIRE_QPDF();
    const QVector<QByteArray> fixtures = {plainPdf(), compliantPdf(), brokenPdf()};
    for (const QByteArray& bytes : fixtures) {
        const PdfUaReport report = aipdf::pdf::checkPdfUa(bytes);
        int errors = 0;
        int warnings = 0;
        int passes = 0;
        countBySeverity(report, &errors, &warnings, &passes);
        QCOMPARE(report.errorCount, errors);
        QCOMPARE(report.warningCount, warnings);
        QCOMPARE(report.passCount, passes);
        QCOMPARE(report.errorCount + report.warningCount + report.passCount,
                 report.findings.size());
        QCOMPARE(report.compliant, report.errorCount == 0);
    }
}

void PdfUaCheckerTests::summaryContainsNumbers() {
    REQUIRE_QPDF();
    const PdfUaReport good = aipdf::pdf::checkPdfUa(compliantPdf());
    const QString goodSummary = good.summary();
    QVERIFY(goodSummary.contains(QStringLiteral("共 %1 项").arg(good.findings.size())));
    QVERIFY(goodSummary.contains(QStringLiteral("通过 %1 项").arg(good.passCount)));
    QVERIFY(goodSummary.contains(QStringLiteral("错误 %1 项").arg(good.errorCount)));
    QVERIFY(goodSummary.contains(QStringLiteral("结论：合规")));

    const PdfUaReport bad = aipdf::pdf::checkPdfUa(plainPdf());
    QVERIFY(bad.summary().contains(QStringLiteral("结论：不合规")));
}

void PdfUaCheckerTests::invalidLangTagIsReported() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-invalid-lang"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("zh_CN")));
    // 语言不合法与「根本没有语言」是两条不同的结论，不能混成一条。
    QVERIFY(!hasCode(report, QStringLiteral("ua-missing-lang")));
}

void PdfUaCheckerTests::structRootMustBeDocument() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    QVERIFY(hasCode(report, QStringLiteral("ua-struct-root-not-document")));
    QVERIFY(!hasCode(report, QStringLiteral("ua-struct-root-document")));
    QVERIFY(hasCode(report, QStringLiteral("ua-missing-parent-tree")));
}

void PdfUaCheckerTests::missingAltTextIsReported() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-missing-alt-text"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    // 说明里要指出是哪个结构元素缺，否则用户不知道去改哪一个。
    QVERIFY(finding->detail.contains(QStringLiteral("Figure")));
}

void PdfUaCheckerTests::nonEmbeddedFontIsReported() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-font-not-embedded"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("Helvetica")));
    QVERIFY(!hasCode(report, QStringLiteral("ua-fonts-embedded")));
}

void PdfUaCheckerTests::readingOrderTabsIsReported() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-tabs-reading-order"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("/R")));
}

void PdfUaCheckerTests::structureOrderTabsPasses() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(compliantPdf());
    QVERIFY(hasCode(report, QStringLiteral("ua-tabs-structure-order")));
    QVERIFY(!hasCode(report, QStringLiteral("ua-tabs-reading-order")));
    QVERIFY(!hasCode(report, QStringLiteral("ua-missing-tabs-order")));
}

void PdfUaCheckerTests::missingMcidInContentIsReported() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    const PdfUaFinding* finding =
        findFinding(report, QStringLiteral("ua-mcid-missing-in-content"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("MCID")));
}

void PdfUaCheckerTests::xmpWithoutPdfuaidIsWarning() {
    REQUIRE_QPDF();
    const PdfUaReport report = aipdf::pdf::checkPdfUa(brokenPdf());
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-xmp-missing-pdfuaid"));
    QVERIFY(finding != nullptr);
    // 只是「没声明按哪一版 PDF/UA 制作」，不该拦下合规判定。
    QCOMPARE(finding->severity, PdfUaSeverity::Warning);
    QCOMPARE(report.warningCount, 1);
}

void PdfUaCheckerTests::everyFindingHasCodeTitleAndDetail() {
    REQUIRE_QPDF();
    const QVector<QByteArray> fixtures = {plainPdf(), compliantPdf(), brokenPdf()};
    for (const QByteArray& bytes : fixtures) {
        const PdfUaReport report = aipdf::pdf::checkPdfUa(bytes);
        QSet<QString> codes;
        for (const PdfUaFinding& finding : report.findings) {
            QVERIFY2(!finding.code.isEmpty(), "结论必须有机器可读的码");
            QVERIFY2(!finding.title.isEmpty(), "结论必须有标题");
            QVERIFY2(!finding.detail.isEmpty(), "结论必须有说明");
            QVERIFY2(!codes.contains(finding.code), "结论的码不能重复");
            codes.insert(finding.code);
        }
    }
}

void PdfUaCheckerTests::findingOrderIsStable() {
    REQUIRE_QPDF();
    const QByteArray bytes = brokenPdf();
    const PdfUaReport first = aipdf::pdf::checkPdfUa(bytes);
    const PdfUaReport second = aipdf::pdf::checkPdfUa(bytes);
    QCOMPARE(first.findings.size(), second.findings.size());
    for (int i = 0; i < first.findings.size(); ++i) {
        QCOMPARE(first.findings.at(i).code, second.findings.at(i).code);
    }
}

void PdfUaCheckerTests::errorStringIsClearedOnSuccess() {
    REQUIRE_QPDF();
    QString error = QStringLiteral("上一次的残留");
    const PdfUaReport report = aipdf::pdf::checkPdfUa(compliantPdf(), &error);
    QVERIFY(report.compliant);
    QVERIFY2(error.isEmpty(), "检查成功时不该留下错误说明");
}

QTEST_GUILESS_MAIN(PdfUaCheckerTests)
#include "PdfUaCheckerTests.moc"
