#include "pdf/compliance/AltTextGenerator.h"
#include "pdf/compliance/PdfUaChecker.h"
#include "pdf/compliance/RoleInference.h"
#include "pdf/compliance/SemanticTagWriter.h"
#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/TagTreeWriter.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::AltTextIssue;
using aipdf::pdf::AltTextResult;
using aipdf::pdf::checkPdfUa;
using aipdf::pdf::generateAltTexts;
using aipdf::pdf::PdfUaFinding;
using aipdf::pdf::PdfUaReport;
using aipdf::pdf::PdfUaSeverity;
using aipdf::pdf::SemanticBlock;
using aipdf::pdf::StructRole;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 把对象列表拼成一份带正确 xref 的 PDF。
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

// 黄金夹具的可调项。
//
// 为什么做成可调的：检查器的每一条新条款都要有一份「只破坏这一条」的负例。
// 手工拼一份完整的 PDF 再逐字节改，比在这里开开关可靠——
// 每一处改动都看得见，不会顺手改坏别的条款。
struct GoldenOptions {
    // 树上有没有 /Part 元素。
    bool withPart = true;
    // 段落元素的 /S 换成非标准类型（空 = 不换）。
    QString customParagraphType;
    // /StructTreeRoot 里有没有 /RoleMap；映射到的名字。
    bool withRoleMap = false;
    QString roleMapTarget;
    // 把表头格 /TH 从 /TR 里挪到 /Sect 下面（非法嵌套）。
    bool illegalNesting = false;
    // 页上写不写 /StructParents。
    bool pageStructParents = true;
    // 页上写不写 /Tabs /S。
    bool pageTabs = true;
    // /ParentTree 里 MCID 1 指错元素（指到标题上去）。
    bool breakParentTree = false;
    // widget 上写不写 /StructParent。
    bool widgetStructParent = true;
    // 额外加一页：那一页有内容，但结构树里没有它的元素（没被打标）。
    bool extraUntaggedPage = false;
};

// **手工构造**的黄金夹具：目标语义树
// Document → Part → Sect → H1 / P / L(LI) / Table(TR(TH, TD)) / Figure / Form。
//
// 这份夹具**不是**生成器产出的：它用来校准检查器。
// 夹具里每一个结构都是按 PDF/UA-1 的要求手写出来的，
// 检查器必须判它合规——否则检查器本身就是错的。
QByteArray goldenPdf(const GoldenOptions& options) {
    const QByteArray xmp =
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
        "<rdf:Description xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" pdfuaid:part=\"1\"/>\n"
        "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";

    // 内容流：7 段标记内容，MCID 0..6。
    const QByteArray content(
        "q /P <</MCID 0>> BDC BT /F1 20 Tf 72 700 Td (Chapter One) Tj ET EMC Q\n"
        "q /P <</MCID 1>> BDC BT /F1 11 Tf 72 670 Td (First paragraph line.) Tj ET EMC Q\n"
        "q /P <</MCID 2>> BDC BT /F1 11 Tf 72 655 Td (Second paragraph line.) Tj ET EMC Q\n"
        "q /P <</MCID 3>> BDC BT /F1 11 Tf 72 640 Td (1) Tj ET EMC Q\n"
        "q /P <</MCID 4>> BDC BT /F1 11 Tf 72 620 Td (Name) Tj ET EMC Q\n"
        "q /P <</MCID 5>> BDC BT /F1 11 Tf 200 620 Td (Alice) Tj ET EMC Q\n"
        "q /P <</MCID 6>> BDC BT /F1 11 Tf 72 580 Td (Company logo) Tj ET EMC Q\n");

    // 结构树。
    QString part = QStringLiteral("<< /Type /StructElem /S /Part /Pg 3 0 R /P 7 0 R /K [9 0 R] >>");
    QString documentChildren = options.withPart ? QStringLiteral("[8 0 R]") : QStringLiteral("[9 0 R]");
    QString partParent = options.withPart ? QStringLiteral("8 0 R") : QStringLiteral("7 0 R");
    QString sectParent = options.withPart ? QStringLiteral("8 0 R") : QStringLiteral("7 0 R");
    QString sectChildren = options.illegalNesting
                               ? QStringLiteral("[10 0 R 11 0 R 12 0 R 14 0 R 16 0 R 18 0 R 19 0 R]")
                               : QStringLiteral("[10 0 R 11 0 R 12 0 R 14 0 R 18 0 R 19 0 R]");
    const QString paragraphType = options.customParagraphType.isEmpty()
                                      ? QStringLiteral("/P")
                                      : QStringLiteral("/") + options.customParagraphType;

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 4 0 R "
                                           "/MarkInfo << /Marked true >> /Lang (zh-CN) "
                                           "/DisplayDocTitle true /Metadata 21 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    QByteArray page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]";
    if (options.pageTabs) page += " /Tabs /S";
    if (options.pageStructParents) page += " /StructParents 0";
    page += " /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R /Annots [20 0 R] >>";
    objects.append(qMakePair(3, page));
    QString structTreeRoot = "<< /Type /StructTreeRoot /K 7 0 R /ParentTree 24 0 R";
    if (options.withRoleMap) {
        structTreeRoot += " /RoleMap << /" + options.customParagraphType.toLatin1() + " /" +
                          options.roleMapTarget.toLatin1() + " >>";
    }
    structTreeRoot += " >>";
    objects.append(qMakePair(4, structTreeRoot.toLatin1()));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                           "/FontDescriptor 22 0 R >>")));
    objects.append(qMakePair(6, streamObject(QByteArray(), content)));
    objects.append(qMakePair(7, QByteArray("<< /Type /StructElem /S /Document /P 4 0 R /K " +
                                           documentChildren.toLatin1() + " >>")));
    objects.append(qMakePair(8, part.toLatin1()));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Sect /Pg 3 0 R /P " +
                                           sectParent.toLatin1() + " /K " + sectChildren.toLatin1() +
                                           " >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /H1 /Pg 3 0 R /P 9 0 R "
                                            "/K [0] /T (Chapter One) >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S " + paragraphType.toLatin1() +
                                            " /Pg 3 0 R /P 9 0 R /K [1 2] >>")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /L /Pg 3 0 R /P 9 0 R "
                                            "/K [13 0 R] >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /LI /Pg 3 0 R /P 12 0 R "
                                            "/K [3] >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /Table /Pg 3 0 R /P 9 0 R "
                                            "/K [15 0 R] /Alt (Name, Alice) >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /TR /Pg 3 0 R /P 14 0 R /K " +
                                            (options.illegalNesting ? QByteArray("[17 0 R]")
                                                                    : QByteArray("[16 0 R 17 0 R]")) +
                                            " >>")));
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /TH /Pg 3 0 R /P " +
                                            (options.illegalNesting ? QByteArray("9 0 R")
                                                                    : QByteArray("15 0 R")) +
                                            " /K [4] /Alt (Name) /Scope /Column >>")));
    objects.append(qMakePair(17, QByteArray("<< /Type /StructElem /S /TD /Pg 3 0 R /P 15 0 R "
                                            "/K [5] >>")));
    objects.append(qMakePair(18, QByteArray("<< /Type /StructElem /S /Figure /Pg 3 0 R /P 9 0 R "
                                            "/K [6] /Alt (Company logo) >>")));
    objects.append(qMakePair(19, QByteArray("<< /Type /StructElem /S /Form /Pg 3 0 R /P 9 0 R "
                                            "/K [20 0 R] /T (Name) /Alt (Full name) >>")));
    const QByteArray widget = QStringLiteral("<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) "
                                             "/TU (Full name) /Rect [72 500 200 520] /P 3 0 R%1 >>")
                                 .arg(options.widgetStructParent ? QStringLiteral(" /StructParent 7")
                                                               : QString())
                                 .toLatin1();
    objects.append(qMakePair(20, widget));
    objects.append(qMakePair(21, streamObject(QByteArray("/Type /Metadata /Subtype /XML"), xmp)));
    objects.append(qMakePair(22, QByteArray("<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 "
                                            "/FontFile 23 0 R >>")));
    objects.append(qMakePair(23, streamObject(QByteArray(), QByteArray(64, '\0'))));

    // /ParentTree：键是页的 /StructParents，值是那一页的数组
    // （下标 = MCID，表单域的 widget 接在 MCID 后面）。
    const int mcid1Target = options.breakParentTree ? 10 : 11;
    QString parentTree = QStringLiteral("<< /Nums [0 [10 0 R %1 0 R 11 0 R 13 0 R 16 0 R 17 0 R "
                                        "18 0 R 19 0 R]] >>")
                            .arg(mcid1Target);
    if (options.extraUntaggedPage) {
        objects.append(qMakePair(26, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                                "/Tabs /S /StructParents 1 "
                                                "/Resources << /Font << /F1 5 0 R >> >> "
                                                "/Contents 27 0 R >>")));
        objects.append(qMakePair(27, streamObject(
            QByteArray(),
            QByteArray("q /P <</MCID 0>> BDC BT /F1 11 Tf 72 700 Td (Untagged page text) Tj ET EMC Q"))));
        objects[1] = qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 26 0 R] /Count 2 >>"));
        parentTree = QStringLiteral("<< /Nums [0 [10 0 R %1 0 R 11 0 R 13 0 R 16 0 R 17 0 R "
                                    "18 0 R 19 0 R] 1 []] >>")
                        .arg(mcid1Target);
    }
    objects.append(qMakePair(24, parentTree.toLatin1()));
    objects.append(qMakePair(25, QByteArray("<< /Title (Accessible golden sample) >>")));
    return buildPdf(objects, 1, 25);
}

// 报告里的错误码集合。
QSet<QString> errorCodes(const PdfUaReport& report) {
    QSet<QString> codes;
    for (const PdfUaFinding& finding : report.findings) {
        if (finding.severity == PdfUaSeverity::Error) codes.insert(finding.code);
    }
    return codes;
}

// 报告里有没有某一条结论。
const PdfUaFinding* findFinding(const PdfUaReport& report, const QString& code) {
    for (const PdfUaFinding& finding : report.findings) {
        if (finding.code == code) return &finding;
    }
    return nullptr;
}

// 一份只有两行正文的 PDF（给写语义树的用例用）。
QByteArray twoLinePdf() {
    const QByteArray content(
        "BT /F1 20 Tf 72 700 Td (Report Title) Tj ET\n"
        "BT /F1 12 Tf 72 670 Td (Body paragraph line one.) Tj ET\n"
        "BT /F1 12 Tf 72 655 Td (Body paragraph line two.) Tj ET\n");
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), content)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                           "/FontDescriptor 6 0 R >>")));
    objects.append(qMakePair(6, QByteArray("<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 "
                                           "/FontFile 7 0 R >>")));
    objects.append(qMakePair(7, streamObject(QByteArray(), QByteArray(64, '\0'))));
    return buildPdf(objects, 1);
}

}  // namespace

class SemanticTagWriterTests : public QObject {
    Q_OBJECT

private slots:
    void goldenFixtureIsCompliant();
    void goldenFixturePassesEveryClause();
    void missingPartElementIsReported();
    void missingRoleMapIsReported();
    void unmappedRoleIsReported();
    void illegalNestingIsReported();
    void pageMissingStructParentsIsReported();
    void parentTreeMismatchIsReported();
    void untaggedPageContentIsReported();
    void missingTabsOrderIsError();
    void widgetWithoutStructParentIsReported();
    void everyNegativeFixtureBreaksExactlyOneClause();
    void roleInferenceFindsHeadingParagraphAndList();
    void roleInferenceMarksUncertainBlocks();
    void altTextGeneratorNeverInventsText();
    void semanticWriterProducesDocumentPartSectTree();
    void legacyWriteTaggedStructureStillWorks();
};

void SemanticTagWriterTests::goldenFixtureIsCompliant() {
    REQUIRE_QPDF();
    const PdfUaReport report = checkPdfUa(goldenPdf(GoldenOptions()));
    QVERIFY2(report.compliant, qPrintable(report.summary()));
    QCOMPARE(report.errorCount, 0);
    QCOMPARE(report.warningCount, 0);
}

void SemanticTagWriterTests::goldenFixturePassesEveryClause() {
    REQUIRE_QPDF();
    const PdfUaReport report = checkPdfUa(goldenPdf(GoldenOptions()));
    // 这份夹具是按检查器的每一条条款设计的，逐条点名：
    // 少查一项也会在这里露出来。
    const QStringList expected = {
        QStringLiteral("ua-struct-tree-root-present"),
        QStringLiteral("ua-struct-root-document"),
        QStringLiteral("ua-parent-tree-present"),
        QStringLiteral("ua-mark-info-marked"),
        QStringLiteral("ua-lang-valid"),
        QStringLiteral("ua-title-present"),
        QStringLiteral("ua-display-doc-title-ok"),
        QStringLiteral("ua-alt-text-ok"),
        QStringLiteral("ua-fonts-embedded"),
        QStringLiteral("ua-tabs-structure-order"),
        QStringLiteral("ua-mcid-content-ok"),
        QStringLiteral("ua-xmp-metadata-ok"),
        QStringLiteral("ua-part-element-present"),
        QStringLiteral("ua-role-map-ok"),
        QStringLiteral("ua-structure-nesting-valid"),
        QStringLiteral("ua-page-struct-parents-present"),
        QStringLiteral("ua-parent-tree-aligned"),
        QStringLiteral("ua-page-content-tagged"),
    };
    for (const QString& code : expected) {
        QVERIFY2(findFinding(report, code) != nullptr, qPrintable(QStringLiteral("少了结论：%1").arg(code)));
    }
}

void SemanticTagWriterTests::missingPartElementIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.withPart = false;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-missing-part-element"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("Part")));
    QVERIFY(findFinding(report, QStringLiteral("ua-part-element-present")) == nullptr);
}

void SemanticTagWriterTests::missingRoleMapIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.customParagraphType = QStringLiteral("MyParagraph");
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-missing-role-map"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("MyParagraph")));
    // 「没有 /RoleMap」与「有 /RoleMap 但没映射到标准类型」是两条不同的结论。
    QVERIFY(findFinding(report, QStringLiteral("ua-non-standard-structure-type")) == nullptr);
}

void SemanticTagWriterTests::unmappedRoleIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.customParagraphType = QStringLiteral("MyParagraph");
    options.withRoleMap = true;
    // RoleMap 把它映射到另一个自定义类型：仍然不是标准类型。
    options.roleMapTarget = QStringLiteral("MyHeading");
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding =
        findFinding(report, QStringLiteral("ua-non-standard-structure-type"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("MyParagraph")));
    QVERIFY(findFinding(report, QStringLiteral("ua-missing-role-map")) == nullptr);
}

void SemanticTagWriterTests::illegalNestingIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.illegalNesting = true;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding =
        findFinding(report, QStringLiteral("ua-illegal-structure-nesting"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("TH")));
    QVERIFY(findFinding(report, QStringLiteral("ua-structure-nesting-valid")) == nullptr);
}

void SemanticTagWriterTests::pageMissingStructParentsIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.pageStructParents = false;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding =
        findFinding(report, QStringLiteral("ua-page-missing-struct-parents"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("/StructParents")));
}

void SemanticTagWriterTests::parentTreeMismatchIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.breakParentTree = true;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-parent-tree-mismatch"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("MCID")));
}

void SemanticTagWriterTests::untaggedPageContentIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.extraUntaggedPage = true;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-untagged-page-content"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("第 2 页")));
}

void SemanticTagWriterTests::missingTabsOrderIsError() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.pageTabs = false;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding = findFinding(report, QStringLiteral("ua-missing-tabs-order"));
    QVERIFY(finding != nullptr);
    // PDF/UA 要求 Tab 顺序跟标签结构一致：这一条是 Error，不是 Warning。
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
}

void SemanticTagWriterTests::widgetWithoutStructParentIsReported() {
    REQUIRE_QPDF();
    GoldenOptions options;
    options.widgetStructParent = false;
    const PdfUaReport report = checkPdfUa(goldenPdf(options));
    const PdfUaFinding* finding =
        findFinding(report, QStringLiteral("ua-widget-missing-struct-parent"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfUaSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("/StructParent")));
    QVERIFY(findFinding(report, QStringLiteral("ua-widget-struct-parent-present")) == nullptr);
}

void SemanticTagWriterTests::everyNegativeFixtureBreaksExactlyOneClause() {
    REQUIRE_QPDF();
    const QSet<QString> golden = errorCodes(checkPdfUa(goldenPdf(GoldenOptions())));
    QVERIFY(golden.isEmpty());

    struct Variant {
        QString name;
        GoldenOptions options;
        QString expectedCode;
    };
    QVector<Variant> variants;
    {
        GoldenOptions options;
        options.withPart = false;
        variants.append({QStringLiteral("缺 Part 元素"), options,
                         QStringLiteral("ua-missing-part-element")});
    }
    {
        GoldenOptions options;
        options.customParagraphType = QStringLiteral("MyParagraph");
        variants.append({QStringLiteral("自定义类型没有 RoleMap"), options,
                         QStringLiteral("ua-missing-role-map")});
    }
    {
        GoldenOptions options;
        options.customParagraphType = QStringLiteral("MyParagraph");
        options.withRoleMap = true;
        options.roleMapTarget = QStringLiteral("MyHeading");
        variants.append({QStringLiteral("RoleMap 没映射到标准类型"), options,
                         QStringLiteral("ua-non-standard-structure-type")});
    }
    {
        GoldenOptions options;
        options.illegalNesting = true;
        variants.append({QStringLiteral("表头格不在表格行里"), options,
                         QStringLiteral("ua-illegal-structure-nesting")});
    }
    {
        GoldenOptions options;
        options.pageStructParents = false;
        variants.append({QStringLiteral("页缺 StructParents"), options,
                         QStringLiteral("ua-page-missing-struct-parents")});
    }
    {
        GoldenOptions options;
        options.breakParentTree = true;
        variants.append({QStringLiteral("ParentTree 指错元素"), options,
                         QStringLiteral("ua-parent-tree-mismatch")});
    }
    {
        GoldenOptions options;
        options.extraUntaggedPage = true;
        variants.append({QStringLiteral("有一页没被打标"), options,
                         QStringLiteral("ua-untagged-page-content")});
    }
    {
        GoldenOptions options;
        options.pageTabs = false;
        variants.append({QStringLiteral("页没写 Tabs"), options,
                         QStringLiteral("ua-missing-tabs-order")});
    }
    {
        GoldenOptions options;
        options.widgetStructParent = false;
        variants.append({QStringLiteral("表单域没挂进标签结构"), options,
                         QStringLiteral("ua-widget-missing-struct-parent")});
    }

    for (const Variant& variant : variants) {
        const PdfUaReport report = checkPdfUa(goldenPdf(variant.options));
        const QSet<QString> codes = errorCodes(report);
        // 只多出这一条：多一条就说明两条结论重叠了，少一条就说明没查出来。
        QSet<QString> extra = codes;
        for (const QString& code : golden) extra.remove(code);
        QCOMPARE(extra.size(), 1);
        QVERIFY2(extra.contains(variant.expectedCode),
                 qPrintable(QStringLiteral("%1：多报了 %2").arg(variant.name, extra.values().join(","))));
        QVERIFY2(!report.compliant, qPrintable(variant.name));
    }
}

void SemanticTagWriterTests::roleInferenceFindsHeadingParagraphAndList() {
    QVector<SemanticBlock> blocks;
    {
        SemanticBlock heading;
        heading.pageIndex = 0;
        heading.rect = QRectF(72, 700, 120, 20);
        heading.text = QStringLiteral("Report Title");
        heading.fontSize = 20.0;
        blocks.append(heading);
    }
    {
        SemanticBlock line1;
        line1.pageIndex = 0;
        line1.rect = QRectF(72, 670, 200, 12);
        line1.text = QStringLiteral("Body paragraph line one.");
        line1.fontSize = 12.0;
        blocks.append(line1);
    }
    {
        SemanticBlock line2;
        line2.pageIndex = 0;
        line2.rect = QRectF(72, 655, 200, 12);
        line2.text = QStringLiteral("Body paragraph line two.");
        line2.fontSize = 12.0;
        blocks.append(line2);
    }
    {
        SemanticBlock item;
        item.pageIndex = 0;
        item.rect = QRectF(72, 640, 100, 12);
        item.text = QStringLiteral("- first item");
        item.fontSize = 12.0;
        blocks.append(item);
    }

    const aipdf::pdf::RoleInferenceResult result =
        aipdf::pdf::inferSemanticRoles(blocks);
    // 标题是 H1；两行正文合成一个段落；列表项是 LI。
    QCOMPARE(result.blocks.at(0).role, StructRole::H1);
    QCOMPARE(result.blocks.at(1).role, StructRole::P);
    QCOMPARE(result.blocks.at(3).role, StructRole::LI);
    // 树：Document → Part → Sect → H1 / P / L。
    QCOMPARE(result.tree.nodes.at(result.tree.rootIndex).role, StructRole::Document);
    bool sawPart = false;
    bool sawSect = false;
    bool sawList = false;
    for (const aipdf::pdf::SemanticNode& node : result.tree.nodes) {
        if (node.role == StructRole::Part) sawPart = true;
        if (node.role == StructRole::Sect) sawSect = true;
        if (node.role == StructRole::L) sawList = true;
    }
    QVERIFY(sawPart);
    QVERIFY(sawSect);
    QVERIFY(sawList);
}

void SemanticTagWriterTests::roleInferenceMarksUncertainBlocks() {
    QVector<SemanticBlock> blocks;
    SemanticBlock line;
    line.pageIndex = 0;
    line.rect = QRectF(72, 700, 200, 12);
    line.text = QStringLiteral("Name    Alice    Engineer");
    line.fontSize = 12.0;
    // 一行分成了三列，但没有第二行能对照：判不了是不是表格。
    line.columnStarts = QVector<double>{72.0, 150.0, 230.0};
    blocks.append(line);

    const aipdf::pdf::RoleInferenceResult result = aipdf::pdf::inferSemanticRoles(blocks);
    QCOMPARE(result.blocks.at(0).reliable, false);
    QVERIFY(!result.blocks.at(0).unreliableReason.isEmpty());
    QCOMPARE(result.issues.size(), 1);
    // 不可靠的块按段落处理，但**必须**标出来。
    QCOMPARE(result.blocks.at(0).role, StructRole::P);
}

void SemanticTagWriterTests::altTextGeneratorNeverInventsText() {
    QVector<SemanticBlock> blocks;
    {
        SemanticBlock figure;
        figure.pageIndex = 0;
        figure.rect = QRectF(72, 600, 200, 150);
        figure.image = true;
        blocks.append(figure);
    }
    {
        SemanticBlock caption;
        caption.pageIndex = 0;
        caption.rect = QRectF(72, 560, 200, 12);
        caption.text = QStringLiteral("图 1 公司标志");
        blocks.append(caption);
    }
    {
        SemanticBlock field;
        field.pageIndex = 0;
        field.formField = true;
        field.fieldName = QStringLiteral("firstName");
        field.fieldTooltip = QStringLiteral("名字");
        blocks.append(field);
    }
    {
        SemanticBlock nameless;
        nameless.pageIndex = 0;
        nameless.formField = true;
        blocks.append(nameless);
    }

    const AltTextResult result = generateAltTexts(blocks);
    QCOMPARE(result.alternatives.size(), blocks.size());
    // 图：认得出旁边的说明文字就用它。
    QCOMPARE(result.alternatives.at(0), QStringLiteral("图 1 公司标志"));
    // 表单域：提示文字优先。
    QCOMPARE(result.alternatives.at(2), QStringLiteral("名字"));
    // 既没有提示文字也没有字段名：不编，记一条问题。
    QVERIFY(result.alternatives.at(3).isEmpty());
    QCOMPARE(result.issues.size(), 1);
}

void SemanticTagWriterTests::semanticWriterProducesDocumentPartSectTree() {
    REQUIRE_QPDF();
    aipdf::pdf::ScannedDocument scan;
    QString error;
    QVERIFY2(aipdf::pdf::scanDocumentContent(twoLinePdf(), &scan, &error), qPrintable(error));
    QCOMPARE(scan.pageCount(), 1);

    QVector<SemanticBlock> blocks;
    for (const aipdf::pdf::ScannedTextLine& line : scan.pages.at(0).lines) {
        SemanticBlock block;
        block.pageIndex = 0;
        block.rect = line.bounds;
        block.text = line.text.trimmed();
        block.fontSize = line.fontSize;
        block.runIndices = line.runIndices;
        blocks.append(block);
    }
    QVERIFY(!blocks.isEmpty());

    const aipdf::pdf::RoleInferenceResult inference = aipdf::pdf::inferSemanticRoles(blocks);
    aipdf::pdf::SemanticWriteOptions options;
    options.documentLanguage = QStringLiteral("zh-CN");
    options.documentTitle = QStringLiteral("Two line sample");
    options.displayDocTitle = true;
    const aipdf::pdf::SemanticWriteResult written =
        aipdf::pdf::writeSemanticStructure(twoLinePdf(), inference.blocks, inference.tree, options);
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY(!written.bytes.isEmpty());

#ifdef AIPDF_WITH_QPDF
    // 独立复算：直接用 qpdf 读产物，不复用写手的任何数据结构。
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("semantic-check.pdf", written.bytes.constData(),
                          static_cast<size_t>(written.bytes.size()));
    const QPDFObjectHandle catalog = pdf.getRoot();
    const QPDFObjectHandle root = catalog.getKey("/StructTreeRoot");
    QVERIFY(root.isDictionary());
    // /K 是数组（规范允许直接给一个字典，这里按数组处理）。
    const std::vector<QPDFObjectHandle> documentElements = root.getKey("/K").getArrayAsVector();
    QCOMPARE(documentElements.size(), 1u);
    const QPDFObjectHandle document = documentElements.front();
    QCOMPARE(document.getKey("/S").getName(), std::string("/Document"));
    const std::vector<QPDFObjectHandle> parts = document.getKey("/K").getArrayAsVector();
    QCOMPARE(parts.size(), 1u);
    QCOMPARE(parts.at(0).getKey("/S").getName(), std::string("/Part"));
    const std::vector<QPDFObjectHandle> sections = parts.at(0).getKey("/K").getArrayAsVector();
    QCOMPARE(sections.size(), 1u);
    QCOMPARE(sections.at(0).getKey("/S").getName(), std::string("/Sect"));
    // 标题与段落都在 Sect 下面。
    QSet<QString> types;
    for (const QPDFObjectHandle& section : sections) {
        for (const QPDFObjectHandle& child : section.getKey("/K").getArrayAsVector()) {
            types.insert(QString::fromStdString(child.getKey("/S").getName()));
        }
    }
    QVERIFY(types.contains(QStringLiteral("/H1")));
    QVERIFY(types.contains(QStringLiteral("/P")));
    // 页上写了 /StructParents 与 /Tabs /S。
    const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    QCOMPARE(pages.size(), 1u);
    QVERIFY(pages.at(0).getKey("/StructParents").isInteger());
    QCOMPARE(pages.at(0).getKey("/Tabs").getName(), std::string("/S"));
#endif
}

void SemanticTagWriterTests::legacyWriteTaggedStructureStillWorks() {
    REQUIRE_QPDF();
    QVector<QVector<aipdf::pdf::TaggedRegion>> regions;
    regions.append(QVector<aipdf::pdf::TaggedRegion>{aipdf::pdf::TaggedRegion{
        QRectF(70, 695, 130, 20), 0}});
    QString error;
    const QByteArray written = aipdf::pdf::writeTaggedStructure(twoLinePdf(), regions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

#ifdef AIPDF_WITH_QPDF
    // 老接口的树形状不变：Document → P → Span。
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("legacy-check.pdf", written.constData(),
                          static_cast<size_t>(written.size()));
    const QPDFObjectHandle root = pdf.getRoot().getKey("/StructTreeRoot");
    const std::vector<QPDFObjectHandle> documentElements = root.getKey("/K").getArrayAsVector();
    QCOMPARE(documentElements.size(), 1u);
    const QPDFObjectHandle document = documentElements.front();
    QCOMPARE(document.getKey("/S").getName(), std::string("/Document"));
    const std::vector<QPDFObjectHandle> pages = document.getKey("/K").getArrayAsVector();
    QCOMPARE(pages.size(), 1u);
    QCOMPARE(pages.at(0).getKey("/S").getName(), std::string("/P"));
    const std::vector<QPDFObjectHandle> spans = pages.at(0).getKey("/K").getArrayAsVector();
    QCOMPARE(spans.size(), 1u);
    QCOMPARE(spans.at(0).getKey("/S").getName(), std::string("/Span"));
#endif
}

QTEST_GUILESS_MAIN(SemanticTagWriterTests)
#include "SemanticTagWriterTests.moc"
