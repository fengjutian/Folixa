#include "pdf/compliance/PreflightChecker.h"

#include <QFile>
#include <QHash>
#include <QString>
#include <QTest>
#include <QVector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::PreflightFinding;
using aipdf::pdf::PreflightReport;
using aipdf::pdf::PreflightSeverity;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 读一份夹具文件。读不到就跳过这一条用例（夹具是构建时生成的）。
QByteArray fixture(const QString& name) {
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes;
}

// 把对象列表拼成一份带正确 xref 的 PDF。
//
// 为什么要自己造：夹具的内容是固定的，改不动；
// 「带 JavaScript 的 PDF」「带未内嵌字体的 PDF」这类样本必须自己拼，
// 才能精确控制要检出的那一条结论。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects, int rootId) {
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
    out += QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R >>\nstartxref\n%3\n%%EOF\n")
               .arg(count)
               .arg(rootId)
               .arg(xrefOffset)
               .toLatin1();
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

// 一份正常的单页 PDF（标准 14 字体，没有内嵌）。
QByteArray simplePdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                                           "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(5, streamObject("", QByteArray("BT /F1 18 Tf 24 60 Td (Hello) Tj ET"))));
    return buildPdf(objects, 1);
}

// 带 JavaScript 动作的 PDF。
QByteArray javascriptPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Names << /JavaScript "
                                           "<< /Names [(doc) 5 0 R] >> >> >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                                           "/Resources << /Font << /F1 4 0 R >> >> /Contents 6 0 R >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(5, QByteArray("<< /S /JavaScript /JS (app.alert\\(1\\);) >>")));
    objects.append(qMakePair(6, streamObject("", QByteArray("BT /F1 18 Tf 24 60 Td (Hello) Tj ET"))));
    return buildPdf(objects, 1);
}

// 报告里有没有某一条码。
bool hasCode(const PreflightReport& report, const QString& code) {
    for (const PreflightFinding& finding : report.findings) {
        if (finding.code == code) return true;
    }
    return false;
}

// 报告里第一条某条码的结论。
PreflightFinding firstWithCode(const PreflightReport& report, const QString& code) {
    for (const PreflightFinding& finding : report.findings) {
        if (finding.code == code) return finding;
    }
    return PreflightFinding();
}

// 一份带「几千层间接对象链」的 PDF。
//
// 形状：catalog 的 /Deep 指向 6 号对象，6 号的 /Next 指向 7 号，7 号指向 8 号……
// 一路排下去，中间没有环。
QByteArray deepChainPdf(int chainLength) {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Deep 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                                           "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(5, streamObject("", QByteArray("BT /F1 18 Tf 24 60 Td (Hello) Tj ET"))));
    for (int i = 0; i < chainLength; ++i) {
        const int id = 6 + i;
        const bool last = (i + 1 >= chainLength);
        const QByteArray body =
            last ? QByteArray("<< /AipdfChainEnd true >>")
                 : QStringLiteral("<< /Next %1 0 R >>").arg(id + 1).toLatin1();
        objects.append(qMakePair(id, body));
    }
    return buildPdf(objects, 1);
}

}  // namespace

class PreflightCheckerTests : public QObject {
    Q_OBJECT

private slots:
    void nonPdfBytesFail();
    void emptyBytesFail();
    void cleanFixtureHasNoErrorFindings();
    void findingsOrderIsStable();
    void summaryContainsNumbers();
    void javascriptIsReportedAsError();
    void nonEmbeddedFontIsReported();
    void okMatchesErrorCount();
    void fixtureDirectoryMacroIsWired();
    void outOfRangePageReferenceDoesNotCrash();
    void pageSizeMismatchIsReported();
    void transparencyGroupIsReported();
    void lowResolutionImageIsReported();
    void externalUriIsReported();
    void deepObjectChainDoesNotCrash();
};

// 1. 非 PDF 字节：失败 + 中文原因。
void PreflightCheckerTests::nonPdfBytesFail() {
    QString error;
    const PreflightReport report =
        aipdf::pdf::runPreflightCheck(QByteArray("this is definitely not a pdf"), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!report.ok);
    // 原因必须是中文（领域层不翻译，文案硬编码中文）。
    QVERIFY(error.contains(QStringLiteral("不是")));
    QCOMPARE(report.errorCount, 1);
    QVERIFY(hasCode(report, QStringLiteral("document-unreadable")));
}

// 2. 空字节：失败 + 中文原因。
void PreflightCheckerTests::emptyBytesFail() {
    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(QByteArray(), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!report.ok);
    QVERIFY(report.findings.size() >= 1);
}

// 3. 正常的夹具 PDF：报告里没有 Error 级问题。
void PreflightCheckerTests::cleanFixtureHasNoErrorFindings() {
    REQUIRE_QPDF();
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("夹具不可用");

    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(report.errorCount, 0);
    QVERIFY(report.ok);
    for (const PreflightFinding& finding : report.findings) {
        QVERIFY2(finding.severity != PreflightSeverity::Error,
                 qPrintable(QStringLiteral("不该出现的 Error：%1").arg(finding.code)));
    }
}

// 4. 同样的输入两次跑，结论顺序一样。
void PreflightCheckerTests::findingsOrderIsStable() {
    REQUIRE_QPDF();
    const QByteArray bytes = fixture(QStringLiteral("multipage.pdf"));
    if (bytes.isEmpty()) QSKIP("夹具不可用");

    QString error;
    const PreflightReport first = aipdf::pdf::runPreflightCheck(bytes, &error);
    const PreflightReport second = aipdf::pdf::runPreflightCheck(bytes, &error);
    QCOMPARE(first.findings.size(), second.findings.size());
    for (int i = 0; i < first.findings.size(); ++i) {
        QCOMPARE(first.findings.at(i).code, second.findings.at(i).code);
        QCOMPARE(first.findings.at(i).pageIndex, second.findings.at(i).pageIndex);
        QCOMPARE(first.findings.at(i).detail, second.findings.at(i).detail);
    }
}

// 5. summary() 里带数字。
void PreflightCheckerTests::summaryContainsNumbers() {
    REQUIRE_QPDF();
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    if (bytes.isEmpty()) QSKIP("夹具不可用");

    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes);
    const QString summary = report.summary();
    QVERIFY(!summary.isEmpty());
    bool hasDigit = false;
    for (const QChar ch : summary) {
        if (ch.isDigit()) {
            hasDigit = true;
            break;
        }
    }
    QVERIFY2(hasDigit, qPrintable(summary));
    QVERIFY(summary.contains(QString::number(report.findings.size())));
}

// 6. 带 JavaScript 的 PDF：报出 JS 级别的 Error。
void PreflightCheckerTests::javascriptIsReportedAsError() {
    REQUIRE_QPDF();
    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(javascriptPdf(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(hasCode(report, QStringLiteral("javascript")));
    const PreflightFinding finding = firstWithCode(report, QStringLiteral("javascript"));
    QCOMPARE(finding.severity, PreflightSeverity::Error);
    QVERIFY(report.errorCount >= 1);
    QVERIFY(!report.ok);
}

// 7. 带未内嵌字体的 PDF：报出字体那条。
void PreflightCheckerTests::nonEmbeddedFontIsReported() {
    REQUIRE_QPDF();
    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(simplePdf(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(hasCode(report, QStringLiteral("font-not-embedded")));
    const PreflightFinding finding = firstWithCode(report, QStringLiteral("font-not-embedded"));
    QCOMPARE(finding.severity, PreflightSeverity::Warning);
    QCOMPARE(finding.pageIndex, 0);
    QVERIFY(finding.detail.contains(QStringLiteral("Helvetica")));
    QCOMPARE(report.errorCount, 0);
    QVERIFY(report.ok);
}

// 8. ok 与 errorCount 始终一致。
void PreflightCheckerTests::okMatchesErrorCount() {
    REQUIRE_QPDF();
    const QVector<QByteArray> samples = {simplePdf(), javascriptPdf()};
    for (const QByteArray& bytes : samples) {
        QString error;
        const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
        QCOMPARE(report.ok, report.errorCount == 0);
        if (report.errorCount > 0) {
            QVERIFY(!report.ok);
        }
        // 计数必须与结论里实际的级别数一致。
        int errors = 0;
        int warnings = 0;
        for (const PreflightFinding& finding : report.findings) {
            if (finding.severity == PreflightSeverity::Error) ++errors;
            if (finding.severity == PreflightSeverity::Warning) ++warnings;
        }
        QCOMPARE(report.errorCount, errors);
        QCOMPARE(report.warningCount, warnings);
    }
}

// 9. 夹具路径来自 AIPDF_PDF_FIXTURES_DIR 宏。
void PreflightCheckerTests::fixtureDirectoryMacroIsWired() {
    const QString directory = QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR);
    QVERIFY2(!directory.isEmpty(), "夹具目录宏没有定义");
    QVERIFY(directory.contains(QStringLiteral("fixtures")));
    const QByteArray bytes = fixture(QStringLiteral("normal.pdf"));
    QVERIFY2(!bytes.isEmpty(), "按宏拼出来的路径读不到夹具");
    QVERIFY(bytes.startsWith("%PDF-"));
}

// 10. 指向越界页码的链接：检查器不能崩。
void PreflightCheckerTests::outOfRangePageReferenceDoesNotCrash() {
    REQUIRE_QPDF();
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    // 目标页对象 99 根本不存在：坏文件里这种地方很常见，检查器必须扛得住。
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                                           "/Resources << /Font << /F1 4 0 R >> >> "
                                           "/Annots [<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
                                           "/Dest [99 0 R /Fit] >>] >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    const QByteArray bytes = buildPdf(objects, 1);

    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!report.findings.isEmpty());
    QVERIFY(hasCode(report, QStringLiteral("font-not-embedded")));
    // 报出来的页号必须落在文档的页范围内。
    for (const PreflightFinding& finding : report.findings) {
        QVERIFY(finding.pageIndex >= -1);
        QVERIFY(finding.pageIndex <= 0);
    }
}

// 11. 页面尺寸不一致：报一条 Info。
void PreflightCheckerTests::pageSizeMismatchIsReported() {
    REQUIRE_QPDF();
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] >>")));
    objects.append(qMakePair(4, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] >>")));
    const QByteArray bytes = buildPdf(objects, 1);

    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    QVERIFY(hasCode(report, QStringLiteral("page-size-mismatch")));
    const PreflightFinding finding = firstWithCode(report, QStringLiteral("page-size-mismatch"));
    QCOMPARE(finding.severity, PreflightSeverity::Info);
    QCOMPARE(finding.pageIndex, -1);  // 整篇级别的问题
}

// 12. 透明度组：报一条 Info。
void PreflightCheckerTests::transparencyGroupIsReported() {
    REQUIRE_QPDF();
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                                           "/Group << /S /Transparency /CS /DeviceRGB /I true >> >>")));
    const QByteArray bytes = buildPdf(objects, 1);

    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    QVERIFY(hasCode(report, QStringLiteral("transparency-group")));
    const PreflightFinding finding = firstWithCode(report, QStringLiteral("transparency-group"));
    QCOMPARE(finding.severity, PreflightSeverity::Info);
    QCOMPARE(finding.pageIndex, 0);
}

// 13. 分辨率过低的图片：报一条 Warning。
void PreflightCheckerTests::lowResolutionImageIsReported() {
    REQUIRE_QPDF();
    // 10x10 像素的图被拉到 200x200 点：有效分辨率只有 3.6 dpi。
    QByteArray pixels(100, static_cast<char>(0x80));
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
                                           "/Resources << /XObject << /Im1 4 0 R >> >> /Contents 5 0 R >>")));
    objects.append(qMakePair(4, streamObject(QByteArray("/Type /XObject /Subtype /Image /Width 10 "
                                                        "/Height 10 /ColorSpace /DeviceGray "
                                                        "/BitsPerComponent 8"),
                                            pixels)));
    objects.append(qMakePair(5, streamObject("", QByteArray("q 200 0 0 200 0 0 cm /Im1 Do Q"))));
    const QByteArray bytes = buildPdf(objects, 1);

    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(hasCode(report, QStringLiteral("image-resolution-low")));
    const PreflightFinding finding = firstWithCode(report, QStringLiteral("image-resolution-low"));
    QCOMPARE(finding.severity, PreflightSeverity::Warning);
    QCOMPARE(finding.pageIndex, 0);
    QVERIFY(finding.detail.contains(QStringLiteral("dpi")));
}

// 14. 外部链接：报一条 Warning。
void PreflightCheckerTests::deepObjectChainDoesNotCrash() {
    REQUIRE_QPDF();
    // 一条几千层深的间接对象链：印前检查必须扫完，不能把栈爆掉。
    //
    // 曾经的缺陷：对象图遍历是递归的，链有多深递归就有多深，
    // 坏文件里一条长链会让进程直接崩（退出码 0xC00000FD = 栈溢出）。
    // 印前检查已经接进界面，所以这是一条用户能碰到的崩溃路径。
    //
    // 这条用例在旧代码上会**整个测试进程崩掉**（不是普通断言失败），
    // 那正是它要守的东西。
    const QByteArray bytes = deepChainPdf(6000);
    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    // 关键不是报告内容，是「进程还活着，并且给出了结论」。
    QVERIFY(!report.summary().isEmpty());
}

void PreflightCheckerTests::externalUriIsReported() {
    REQUIRE_QPDF();
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                                           "/Annots [<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
                                           "/A << /S /URI /URI (https://example.com/a.pdf) >> >>] >>")));
    const QByteArray bytes = buildPdf(objects, 1);

    QString error;
    const PreflightReport report = aipdf::pdf::runPreflightCheck(bytes, &error);
    QVERIFY(hasCode(report, QStringLiteral("external-reference")));
    const PreflightFinding finding = firstWithCode(report, QStringLiteral("external-reference"));
    QCOMPARE(finding.severity, PreflightSeverity::Warning);
    QVERIFY(finding.detail.contains(QStringLiteral("example.com")));
    QCOMPARE(report.errorCount, 0);
}

QTEST_GUILESS_MAIN(PreflightCheckerTests)
#include "PreflightCheckerTests.moc"
