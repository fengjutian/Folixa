#include "pdf/PdfAChecker.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#endif

using aipdf::pdf::PdfACheckKind;
using aipdf::pdf::PdfAPreflightFinding;
using aipdf::pdf::PdfAPreflightReport;
using aipdf::pdf::PdfAPreflightSeverity;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// ------------------------------------------------------------ PDF 拼装 ----

// 把对象列表拼成一份带正确 xref 的 PDF。
//
// 为什么要自己造：PDF/A 预检的每一条结论都对应文件里的一个具体结构
// （有没有 /Metadata、/OutputIntents 里是不是 /GTS_PDFA2、字体有没有内嵌……），
// 只有自己拼才能精确控制「这一条要报错、那一条要通过」。
QByteArray buildPdf(const QString& headerVersion, const QVector<QPair<int, QByteArray>>& objects,
                     int rootId, const QByteArray& trailerExtra) {
    QByteArray out = "%PDF-" + headerVersion.toLatin1() + "\n";
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
    out += trailerExtra;
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

// 一份最小的 ICC 配置文件头。
//
// 为什么自己拼：预检读的是 ICC 头里固定偏移上的设备类别、数据色彩空间与版本，
// 这三项不需要一份真的配置文件；但要造一份「版本 5」或「设备类别是 scnr」的
// 坏配置，还是得能改这几个字节。
QByteArray iccProfile(const QByteArray& deviceClass, const QByteArray& dataSpace, int majorVersion) {
    QByteArray bytes(132, '\0');
    bytes[8] = static_cast<char>(majorVersion);
    for (int i = 0; i < 4 && i < deviceClass.size(); ++i) bytes[12 + i] = deviceClass.at(i);
    for (int i = 0; i < 4 && i < dataSpace.size(); ++i) bytes[16 + i] = dataSpace.at(i);
    bytes.replace(36, 4, QByteArray("acsp"));
    return bytes;
}

// 一份声明了 PDF/A 的 XMP 包。
QByteArray xmpPacket(const QString& part, const QString& conformance) {
    QByteArray out =
        "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
    if (part.isEmpty() && conformance.isEmpty()) {
        out += "<rdf:Description/>\n";
    } else {
        out += "<rdf:Description xmlns:pdfaid=\"http://www.aiim.org/pdfua/ns/id/\"";
        if (!part.isEmpty()) out += " pdfaid:part=\"" + part.toLatin1() + "\"";
        if (!conformance.isEmpty()) out += " pdfaid:conformance=\"" + conformance.toLatin1() + "\"";
        out += "/>\n";
    }
    out += "</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
    return out;
}

// 夹具的可调项。
struct FixtureOptions {
    bool withXmp = true;
    QString pdfaidPart = QStringLiteral("2");
    QString pdfaidConformance = QStringLiteral("B");
    bool withOutputIntent = true;
    QString intentSubtype = QStringLiteral("GTS_PDFA2");
    bool withIccProfile = true;
    bool validIcc = true;
    QByteArray iccDeviceClass = QByteArray("mntr");
    QByteArray iccDataSpace = QByteArray("RGB ");
    int iccVersion = 2;
    bool embedFont = true;
    bool withFileId = true;
    QString headerVersion = QStringLiteral("1.7");
    bool withLzwStream = false;
    bool withExternalReference = false;
    bool withJavaScript = false;
    bool withCieColourSpace = false;
    bool withCmykContent = false;
    bool withForbiddenAnnotation = false;
    bool withAnnotationWithoutAppearance = false;
    bool withTransparency = false;
    bool withPageGroup = true;
    bool withSoftMaskTr = false;
};

// 按选项拼一份 PDF。
//
// 对象编号是固定的（1..15），方便测试里直接引用某一条结论里的对象号。
QByteArray makeFixture(const FixtureOptions& options) {
    QVector<QPair<int, QByteArray>> objects;

    // 1: 文档目录。
    QByteArray catalog = "<< /Type /Catalog /Pages 2 0 R";
    if (options.withXmp) catalog += " /Metadata 8 0 R";
    if (options.withOutputIntent) catalog += " /OutputIntents [9 0 R]";
    if (options.withJavaScript) catalog += " /OpenAction 15 0 R";
    catalog += " >>";
    objects.append(qMakePair(1, catalog));

    // 2: 页树。
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));

    // 3: 页面。
    QByteArray page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >>";
    if (options.withCieColourSpace) {
        page += " /ColorSpace << /CS0 [/CalRGB << /WhitePoint [0.9505 1 1.0502] /BlackPoint [0 0 0] "
                "/Gamma [1 1 1] /Matrix [1 0 0 0 1 0 0 0 1] >>] >>";
    }
    if (options.withTransparency) {
        page += " /ExtGState << /GS1 11 0 R >>";
    }
    if (options.withLzwStream) {
        page += " /XObject << /Im1 13 0 R >>";
    }
    page += " >>";
    if (options.withPageGroup && options.withTransparency) {
        page += " /Group << /S /Transparency /CS /DeviceRGB >>";
    }
    page += " /Contents 7 0 R";
    if (options.withForbiddenAnnotation || options.withAnnotationWithoutAppearance) {
        page += " /Annots [12 0 R]";
    }
    page += " >>";
    objects.append(qMakePair(3, page));

    // 4/5/6: 字体、字体描述符、字体程序。
    QByteArray font = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica";
    if (options.embedFont) font += " /FontDescriptor 5 0 R";
    font += " >>";
    objects.append(qMakePair(4, font));
    QByteArray descriptor = "<< /Type /FontDescriptor /FontName /Helvetica /Flags 32";
    if (options.embedFont) descriptor += " /FontFile 6 0 R";
    descriptor += " >>";
    objects.append(qMakePair(5, descriptor));
    objects.append(qMakePair(6, streamObject(QByteArray(), QByteArray(64, '\0'))));

    // 7: 页面内容流。
    QByteArray content = options.withCmykContent ? QByteArray("0 0 0 1 k 10 10 100 50 re f")
                                                  : QByteArray("0.5 0.5 0.5 rg 10 10 100 50 re f");
    objects.append(qMakePair(7, streamObject(QByteArray(), content)));

    // 8: XMP 元数据流。
    if (options.withXmp) {
        objects.append(qMakePair(8, streamObject(QByteArray("/Type /Metadata /Subtype /XML"),
                                                xmpPacket(options.pdfaidPart,
                                                          options.pdfaidConformance))));
    }

    // 9/10: 输出意图与嵌入的 ICC 配置文件。
    if (options.withOutputIntent) {
        QByteArray intent = "<< /Type /OutputIntent /S /" + options.intentSubtype.toLatin1() +
                            " /OutputConditionIdentifier (sRGB IEC61966-2.1)";
        if (options.withIccProfile) intent += " /DestOutputProfile 10 0 R";
        intent += " >>";
        objects.append(qMakePair(9, intent));
        if (options.withIccProfile) {
            const QByteArray profile = options.validIcc
                                           ? iccProfile(options.iccDeviceClass,
                                                         options.iccDataSpace, options.iccVersion)
                                           : QByteArray(132, '\0');
            objects.append(qMakePair(10, streamObject(QByteArray("/N 3"), profile)));
        }
    }

    // 11: 图形状态（透明）。
    if (options.withTransparency) {
        QByteArray state = "<< /Type /ExtGState /ca 0.5 /CA 0.5";
        if (options.withSoftMaskTr) {
            state += " /SMask << /Type /Mask /S /Luminosity /G 4 0 R /TR /DeviceRGB >>";
        }
        state += " >>";
        objects.append(qMakePair(11, state));
    }

    // 12: 注解。
    if (options.withForbiddenAnnotation) {
        objects.append(qMakePair(12, QByteArray("<< /Type /Annot /Subtype /Movie "
                                                "/Rect [10 10 100 50] /F 4 >>")));
    } else if (options.withAnnotationWithoutAppearance) {
        objects.append(qMakePair(12, QByteArray("<< /Type /Annot /Subtype /Text "
                                                "/Rect [10 10 100 50] /F 4 >>")));
    }

    // 13: 声明用 LZW 压缩的图像流。
    //
    // 为什么数据本身不是合法 LZW：预检查的是流字典里**声明**的过滤名
    // （PDF/A 禁止的是 LZW 这个过滤名本身），不关心数据能不能解开。
    if (options.withLzwStream) {
        objects.append(qMakePair(13, streamObject(QByteArray("/Type /XObject /Subtype /Image "
                                                             "/Width 1 /Height 1 /ColorSpace "
                                                             "/DeviceGray /BitsPerComponent 8 "
                                                             "/Filter /LZWDecode"),
                                                QByteArray("A"))));
    }

    // 14: 指向外部文件的流。
    if (options.withExternalReference) {
        objects.append(qMakePair(14, QByteArray("<< /Length 4 /F (external.bin) >>\nstream\ndata\n"
                                                "endstream")));
    }

    // 15: JavaScript 动作。
    if (options.withJavaScript) {
        objects.append(qMakePair(15, QByteArray("<< /S /JavaScript /JS (app.alert\\(1\\);) >>")));
    }

    QByteArray trailerExtra;
    if (options.withFileId) {
        trailerExtra += " /ID [<0123456789abcdef0123456789abcdef> "
                        "<0123456789abcdef0123456789abcdef>]";
    }
    return buildPdf(options.headerVersion, objects, 1, trailerExtra);
}

// 一份各项都做对了的 PDF：PDF 1.7、带 /ID、不加密、XMP 声明 PDF/A-2b、
// 有 /GTS_PDFA2 输出意图与嵌入的 ICC、字体已内嵌、只用 DeviceRGB、没有透明与注解。
QByteArray cleanPdf() {
    return makeFixture(FixtureOptions());
}

// 找一条结论。
const PdfAPreflightFinding* findFinding(const PdfAPreflightReport& report, const QString& code) {
    for (const PdfAPreflightFinding& finding : report.findings) {
        if (finding.code == code) return &finding;
    }
    return nullptr;
}

bool hasCode(const PdfAPreflightReport& report, const QString& code) {
    return findFinding(report, code) != nullptr;
}

// 按结论的实际级别统计一遍，用来核对报告自报的计数。
void countBySeverity(const PdfAPreflightReport& report, int* errors, int* warnings, int* passes) {
    *errors = 0;
    *warnings = 0;
    *passes = 0;
    for (const PdfAPreflightFinding& finding : report.findings) {
        if (finding.severity == PdfAPreflightSeverity::Error) ++(*errors);
        else if (finding.severity == PdfAPreflightSeverity::Warning) ++(*warnings);
        else ++(*passes);
    }
}

#ifdef AIPDF_WITH_QPDF
// 把一份 PDF 改写成加密的（AES-256，用户密码留空、所有者密码有值）。
//
// 为什么用 qpdf 写而不是手拼 /Encrypt 字典：/O、/U 的取值是 RC4 加密过的结果，
// 手拼要自己实现一遍加密算法；借 qpdf 写出来的是真文件，测的是真行为。
QByteArray encryptedCopyOf(const QByteArray& plain) {
    QPDF pdf;
    pdf.processMemoryFile("plain.pdf", plain.constData(), static_cast<size_t>(plain.size()));
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.setR6EncryptionParameters("", "owner-password", true, false, false, true, true, false,
                                      qpdf_r3p_full, true);
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<int>(buffer->getSize()));
}
#endif

}  // namespace

class PdfACheckerTests : public QObject {
    Q_OBJECT

private slots:
    void emptyBytesFail();
    void nonPdfBytesFail();
    void failedCheckReturnsOneErrorFinding();
    void reportIsPreflightNotCertification();
    void cleanPdfHasNoProblems();
    void cleanPdfReportsEveryCheckAsPass();
    void missingXmpIsReported();
    void wrongPdfaidPartIsReported();
    void wrongConformanceIsReported();
    void declaredPartAndConformanceAreExposed();
    void missingOutputIntentIsReported();
    void wrongOutputIntentSubtypeIsReported();
    void missingEmbeddedIccIsReported();
    void invalidIccProfileIsReported();
    void notEmbeddedFontIsReported();
    void encryptedDocumentIsReported();
    void lzwCompressionIsReported();
    void externalFileReferenceIsReported();
    void javascriptIsReported();
    void cieColourSpaceIsReported();
    void deviceColourMismatchIsReported();
    void forbiddenAnnotationIsReported();
    void annotationWithoutAppearanceIsReported();
    void transparencyIsAllowedInPdfa2();
    void transparencyWithoutPageGroupIsWarning();
    void softMaskTransferFunctionIsReported();
    void missingFileIdIsReported();
    void unsupportedPdfVersionIsReported();
    void countsMatchFindingsInEveryFixture();
    void summaryContainsNumbers();
    void issueCountMatchesErrorsPlusWarnings();
    void findingOrderIsStable();
    void everyFindingHasCodeClauseTitleAndDetail();
    void errorStringIsClearedOnSuccess();
};

void PdfACheckerTests::emptyBytesFail() {
    QString error;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(QByteArray(), &error);
    QCOMPARE(report.errorCount, 1);
    QCOMPARE(report.warningCount, 0);
    QCOMPARE(report.passCount, 0);
    QVERIFY(hasCode(report, QStringLiteral("a-empty-document")));
    QVERIFY(!error.isEmpty());
}

void PdfACheckerTests::nonPdfBytesFail() {
    QString error;
    const PdfAPreflightReport report =
        aipdf::pdf::checkPdfAPreflight(QByteArray("this is not a pdf at all"), &error);
    QCOMPARE(report.errorCount, 1);
    QVERIFY(hasCode(report, QStringLiteral("a-not-a-pdf")));
    QVERIFY(!error.isEmpty());
}

void PdfACheckerTests::failedCheckReturnsOneErrorFinding() {
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(QByteArray("nope"));
    QCOMPARE(report.findings.size(), 1);
    QCOMPARE(report.errorCount, 1);
    // 失败时也要给出可执行的建议，不能只有一句「不行」。
    QVERIFY(!report.findings.first().suggestion.isEmpty());
}

void PdfACheckerTests::reportIsPreflightNotCertification() {
    REQUIRE_QPDF();
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(cleanPdf());
    // 这是「预检」这件事本身要能被机器读到，不能只写在注释里。
    QCOMPARE(report.kind, PdfACheckKind::Preflight);
    QVERIFY(!report.certification);
    // 摘要与边界说明都必须说清楚这是预检、认证要 veraPDF。
    QVERIFY(report.summary().contains(QStringLiteral("preflight")));
    QVERIFY(report.summary().contains(QStringLiteral("not a conformance certification")));
    const QString disclaimer = aipdf::pdf::pdfAPreflightDisclaimer();
    QVERIFY(disclaimer.contains(QStringLiteral("veraPDF")));
    QVERIFY(disclaimer.contains(QStringLiteral("not a conformance certification")));
    // 领域层不得出现「已通过 PDF/A 校验」这类结论。
    for (const PdfAPreflightFinding& finding : report.findings) {
        const QString text = finding.title + QLatin1Char(' ') + finding.detail + QLatin1Char(' ') +
                             finding.suggestion;
        // 先把判断结果算出来再进断言：QVERIFY2 的第一个参数里不能出现顶层逗号。
        const bool mentionsCompliant = text.contains(QStringLiteral("compliant"));
        const bool mentionsPassed = text.contains(QStringLiteral("passed PDF/A"));
        QVERIFY2(!mentionsCompliant, qPrintable(finding.code));
        QVERIFY2(!mentionsPassed, qPrintable(finding.code));
    }
}

void PdfACheckerTests::cleanPdfHasNoProblems() {
    REQUIRE_QPDF();
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(cleanPdf());
    QVERIFY2(report.errorCount == 0, qPrintable(report.summary()));
    QCOMPARE(report.warningCount, 0);
    QCOMPARE(report.passCount, report.findings.size());
}

void PdfACheckerTests::cleanPdfReportsEveryCheckAsPass() {
    REQUIRE_QPDF();
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(cleanPdf());
    // 逐条点名：少查了一项也会被这里抓住。
    QVERIFY(hasCode(report, QStringLiteral("a-pdf-version-ok")));
    QVERIFY(hasCode(report, QStringLiteral("a-file-id-ok")));
    QVERIFY(hasCode(report, QStringLiteral("a-document-not-encrypted")));
    QVERIFY(hasCode(report, QStringLiteral("a-xmp-pdfaid-ok")));
    QVERIFY(hasCode(report, QStringLiteral("a-output-intent-ok")));
    QVERIFY(hasCode(report, QStringLiteral("a-fonts-embedded")));
    QVERIFY(hasCode(report, QStringLiteral("a-no-lzw-compression")));
    QVERIFY(hasCode(report, QStringLiteral("a-no-external-file-reference")));
    QVERIFY(hasCode(report, QStringLiteral("a-no-javascript")));
    QVERIFY(hasCode(report, QStringLiteral("a-colorspaces-ok")));
    QVERIFY(hasCode(report, QStringLiteral("a-no-transparency")));
    QVERIFY(hasCode(report, QStringLiteral("a-annotations-ok")));
}

void PdfACheckerTests::missingXmpIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withXmp = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-missing-xmp-metadata"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.6.2.1"));
    QVERIFY(!finding->suggestion.isEmpty());
}

void PdfACheckerTests::wrongPdfaidPartIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.pdfaidPart = QStringLiteral("1");
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-xmp-wrong-part"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    // 说明里要指出文件自己声明的是哪一版。
    QVERIFY(finding->detail.contains(QStringLiteral("1")));
    QVERIFY(!hasCode(report, QStringLiteral("a-xmp-pdfaid-ok")));
}

void PdfACheckerTests::wrongConformanceIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.pdfaidConformance = QStringLiteral("A");
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-xmp-wrong-conformance"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("A")));
}

void PdfACheckerTests::declaredPartAndConformanceAreExposed() {
    REQUIRE_QPDF();
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(cleanPdf());
    QCOMPARE(report.declaredPart, QStringLiteral("2"));
    QCOMPARE(report.declaredConformance, QStringLiteral("B"));
}

void PdfACheckerTests::missingOutputIntentIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withOutputIntent = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-missing-output-intent"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.2.3"));
    QVERIFY(finding->detail.contains(QStringLiteral("GTS_PDFA2")));
    QVERIFY(!hasCode(report, QStringLiteral("a-output-intent-ok")));
}

void PdfACheckerTests::wrongOutputIntentSubtypeIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.intentSubtype = QStringLiteral("GTS_PDFA1");
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-output-intent-wrong-subtype"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    // 说明里要写出文件里实际有的那个子类型。
    QVERIFY(finding->detail.contains(QStringLiteral("GTS_PDFA1")));
}

void PdfACheckerTests::missingEmbeddedIccIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withIccProfile = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-output-intent-icc-missing"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("DestOutputProfile")));
}

void PdfACheckerTests::invalidIccProfileIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.validIcc = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-output-intent-icc-invalid"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.2.4.2"));
    // 没有 'acsp' 签名这件事必须说出来。
    QVERIFY(finding->detail.contains(QStringLiteral("acsp")));
}

void PdfACheckerTests::notEmbeddedFontIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.embedFont = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-font-not-embedded"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    // 说明里要指出是哪个字体，否则用户不知道去改哪一个。
    QVERIFY(finding->detail.contains(QStringLiteral("Helvetica")));
    QVERIFY(!hasCode(report, QStringLiteral("a-fonts-embedded")));
}

void PdfACheckerTests::encryptedDocumentIsReported() {
    REQUIRE_QPDF();
#ifdef AIPDF_WITH_QPDF
    const QByteArray encrypted = encryptedCopyOf(cleanPdf());
    QVERIFY(!encrypted.isEmpty());
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(encrypted);
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-document-encrypted"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.1.3"));
    QVERIFY(!hasCode(report, QStringLiteral("a-document-not-encrypted")));
#endif
}

void PdfACheckerTests::lzwCompressionIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withLzwStream = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-lzw-compression"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.1.7"));
    // 要指出是哪个对象用了 LZW。
    QVERIFY(finding->detail.contains(QStringLiteral("13")));
    QVERIFY(!hasCode(report, QStringLiteral("a-no-lzw-compression")));
}

void PdfACheckerTests::externalFileReferenceIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withExternalReference = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-external-file-reference"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("14")));
}

void PdfACheckerTests::javascriptIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withJavaScript = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-javascript-action"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("15")));
}

void PdfACheckerTests::cieColourSpaceIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withCieColourSpace = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-colorspace-not-allowed"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.2.4"));
    // 必须指出是哪个色彩空间、以及为什么不行。
    QVERIFY(finding->detail.contains(QStringLiteral("CalRGB")));
    QVERIFY(finding->detail.contains(QStringLiteral("ICC")));
    QVERIFY(!hasCode(report, QStringLiteral("a-colorspaces-ok")));
}

void PdfACheckerTests::deviceColourMismatchIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withCmykContent = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-colorspace-not-allowed"));
    QVERIFY(finding != nullptr);
    // 内容流里的 `k` 操作符就是 DeviceCMYK，与 RGB 的输出意图配在一起就是违规。
    QVERIFY(finding->detail.contains(QStringLiteral("DeviceCMYK")));
    QVERIFY(finding->detail.contains(QStringLiteral("output intent")));
}

void PdfACheckerTests::forbiddenAnnotationIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withForbiddenAnnotation = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-forbidden-annotation"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.3"));
    QVERIFY(finding->detail.contains(QStringLiteral("Movie")));
}

void PdfACheckerTests::annotationWithoutAppearanceIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withAnnotationWithoutAppearance = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-annotation-missing-appearance"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QVERIFY(finding->detail.contains(QStringLiteral("/AP")));
    // Print 标志已经设对了，就不该再报标志问题。
    QVERIFY(!hasCode(report, QStringLiteral("a-annotation-flags")));
}

void PdfACheckerTests::transparencyIsAllowedInPdfa2() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withTransparency = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    // 透明在 PDF/A-1 里被禁止，在 PDF/A-2 里是允许的——不能把它当成违规。
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-transparency-allowed"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Pass);
    QVERIFY(!hasCode(report, QStringLiteral("a-transparency-without-output-intent")));
    QVERIFY(!hasCode(report, QStringLiteral("a-transparency-without-page-group")));
}

void PdfACheckerTests::transparencyWithoutPageGroupIsWarning() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withTransparency = true;
    options.withPageGroup = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-transparency-without-page-group"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Warning);
    QVERIFY(finding->detail.contains(QStringLiteral("/Group")));
}

void PdfACheckerTests::softMaskTransferFunctionIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withTransparency = true;
    options.withSoftMaskTr = true;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-softmask-tr-not-identity"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.2.10.2"));
    QVERIFY(finding->detail.contains(QStringLiteral("/TR")));
}

void PdfACheckerTests::missingFileIdIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withFileId = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding = findFinding(report, QStringLiteral("a-missing-file-id"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.1.3"));
}

void PdfACheckerTests::unsupportedPdfVersionIsReported() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.headerVersion = QStringLiteral("1.4");
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    const PdfAPreflightFinding* finding =
        findFinding(report, QStringLiteral("a-pdf-version-unsupported"));
    QVERIFY(finding != nullptr);
    QCOMPARE(finding->severity, PdfAPreflightSeverity::Error);
    QCOMPARE(finding->clause, QStringLiteral("6.1.2"));
    QVERIFY(finding->detail.contains(QStringLiteral("1.4")));
}

void PdfACheckerTests::countsMatchFindingsInEveryFixture() {
    REQUIRE_QPDF();
    QVector<QByteArray> fixtures;
    fixtures.append(cleanPdf());
    FixtureOptions broken;
    broken.withXmp = false;
    broken.embedFont = false;
    broken.withOutputIntent = false;
    broken.withLzwStream = true;
    broken.withJavaScript = true;
    broken.withTransparency = true;
    broken.withForbiddenAnnotation = true;
    fixtures.append(makeFixture(broken));
    for (const QByteArray& bytes : fixtures) {
        const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(bytes);
        int errors = 0;
        int warnings = 0;
        int passes = 0;
        countBySeverity(report, &errors, &warnings, &passes);
        QCOMPARE(report.errorCount, errors);
        QCOMPARE(report.warningCount, warnings);
        QCOMPARE(report.passCount, passes);
        QCOMPARE(report.errorCount + report.warningCount + report.passCount, report.findings.size());
    }
}

void PdfACheckerTests::summaryContainsNumbers() {
    REQUIRE_QPDF();
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(cleanPdf());
    const QString summary = report.summary();
    QVERIFY(summary.contains(QStringLiteral("%1 checks").arg(report.findings.size())));
    QVERIFY(summary.contains(QStringLiteral("%1 errors").arg(report.errorCount)));
    QVERIFY(summary.contains(QStringLiteral("%1 warnings").arg(report.warningCount)));
    QVERIFY(summary.contains(QStringLiteral("%1 passed").arg(report.passCount)));
}

void PdfACheckerTests::issueCountMatchesErrorsPlusWarnings() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withXmp = false;
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(makeFixture(options));
    QCOMPARE(report.issueCount(), report.errorCount + report.warningCount);
    QVERIFY(report.issueCount() > 0);
}

void PdfACheckerTests::findingOrderIsStable() {
    REQUIRE_QPDF();
    FixtureOptions options;
    options.withXmp = false;
    options.embedFont = false;
    const QByteArray bytes = makeFixture(options);
    const PdfAPreflightReport first = aipdf::pdf::checkPdfAPreflight(bytes);
    const PdfAPreflightReport second = aipdf::pdf::checkPdfAPreflight(bytes);
    QCOMPARE(first.findings.size(), second.findings.size());
    for (int i = 0; i < first.findings.size(); ++i) {
        QCOMPARE(first.findings.at(i).code, second.findings.at(i).code);
    }
}

void PdfACheckerTests::everyFindingHasCodeClauseTitleAndDetail() {
    REQUIRE_QPDF();
    QVector<QByteArray> fixtures;
    fixtures.append(cleanPdf());
    FixtureOptions broken;
    broken.withXmp = false;
    broken.withOutputIntent = false;
    broken.withIccProfile = false;
    broken.embedFont = false;
    broken.withLzwStream = true;
    broken.withExternalReference = true;
    broken.withJavaScript = true;
    broken.withCieColourSpace = true;
    broken.withForbiddenAnnotation = true;
    broken.withTransparency = true;
    fixtures.append(makeFixture(broken));

    for (const QByteArray& bytes : fixtures) {
        const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(bytes);
        QSet<QString> codes;
        for (const PdfAPreflightFinding& finding : report.findings) {
            QVERIFY2(!finding.code.isEmpty(), "结论必须有机器可读的码");
            QVERIFY2(!finding.clause.isEmpty(), "结论必须给出 ISO 19005-2 条款号");
            QVERIFY2(!finding.title.isEmpty(), "结论必须有标题");
            QVERIFY2(!finding.detail.isEmpty(), "结论必须有说明");
            QVERIFY2(!codes.contains(finding.code), "结论的码不能重复");
            codes.insert(finding.code);
            // 领域层只给 ASCII 技术细节：中文句子由 app 层拼。
            for (const QChar& ch : finding.title + finding.detail + finding.suggestion) {
                QVERIFY2(ch.unicode() < 128, "领域层结论文本必须是 ASCII");
            }
        }
    }
}

void PdfACheckerTests::errorStringIsClearedOnSuccess() {
    REQUIRE_QPDF();
    QString error = QStringLiteral("上一次的残留");
    const PdfAPreflightReport report = aipdf::pdf::checkPdfAPreflight(cleanPdf(), &error);
    QVERIFY2(error.isEmpty(), "检查成功时不该留下错误说明");
}

QTEST_GUILESS_MAIN(PdfACheckerTests)
#include "PdfACheckerTests.moc"
