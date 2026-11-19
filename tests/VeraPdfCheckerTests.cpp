// VeraPdfChecker 的专项单测。
//
// 为什么这些用例不依赖本机装没装 veraPDF：
//   * 探测：候选路径是**塞进去**的（测试自己造一个临时 .cmd 当「veraPDF」），
//     纯路径逻辑因此可以单测；
//   * 命令行拼装是纯函数；
//   * 报告解析是纯函数，喂一份夹具 XML。
// 真正跑一次 veraPDF 需要 Java 与 veraPDF 本身，那是**环境能力**，
// 不该成为单元测试的前提；本模块提供的是接入点，不是自带的认证器。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "pdf/compliance/VeraPdfChecker.h"

using namespace aipdf::pdf;

namespace {

// 一份 veraPDF 风格的 XML 报告夹具。
//
// 为什么按「find 所有 rule 元素」解析（而不是死抠层级）：
// veraPDF 的报告在不同版本里套了几层 validationReport，
// 固定路径取容易随版本走样；rule 元素与它的属性是稳定的。
QByteArray fixtureXml(bool withFailure) {
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<validationReport>\n"
        "  <profileInformation>\n"
        "    <name>PDF/A-2b</name>\n"
        "  </profileInformation>\n"
        "  <validationReport>\n"
        "    <rule specification=\"ISO 19005-1\" clause=\"6.1.2\" testNumber=\"1\" "
        "status=\"passed\" description=\"The file header version is valid\"/>\n"
        "    <rule specification=\"ISO 19005-1\" clause=\"6.2.5\" testNumber=\"2\" "
        "status=\"passed\" description=\"Fonts are embedded\"/>\n");
    if (withFailure) {
        xml += QStringLiteral(
            "    <rule specification=\"ISO 19005-1\" clause=\"6.8.1\" testNumber=\"3\" "
            "status=\"failed\" description=\"The XMP metadata is missing\"/>\n");
    }
    xml += QStringLiteral("  </validationReport>\n</validationReport>\n");
    return xml.toUtf8();
}

// 造一个「可执行文件」占位：Windows 上 veraPDF 常常是 .bat / .cmd 包装，
// 所以探测逻辑要看后缀，不能只看文件在不在。
QString makeFakeExecutable(const QTemporaryDir& dir, const QString& name) {
    const QString path = QDir(dir.path()).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
    file.write("@echo off\r\nexit /b 0\r\n");
    file.close();
    return path;
}

}  // namespace

class VeraPdfCheckerTests : public QObject {
    Q_OBJECT

private slots:
    void explicitPathWinsOverEverything();
    void missingToolGivesChineseReason();
    void planRefusesWithoutProgram();
    void planRefusesWithoutInput();
    void planBuildsTheExpectedArguments();
    void parseCountsEveryRule();
    void parseReportsCompliantWhenAllPassed();
    void parseReportsFailedWhenARuleFails();
    void parseRejectsEmptyReport();
    void parseRejectsReportWithoutRules();
    void parseRejectsMalformedXml();
    void unavailableNoticeSaysPreflightIsNotCertification();
    void failedNoticeListsFailedRules();
};

void VeraPdfCheckerTests::explicitPathWinsOverEverything() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = makeFakeExecutable(dir, QStringLiteral("verapdf.cmd"));
    QVERIFY(!fake.isEmpty());

    const VeraPdfTool tool = findVeraPdf({fake});
    QVERIFY2(tool.found, qPrintable(tool.error));
    // 路径要归一化（分隔符统一、变成绝对路径）。
    QCOMPARE(QDir::cleanPath(tool.executable), QDir::cleanPath(QFileInfo(fake).absoluteFilePath()));
    QVERIFY(tool.error.isEmpty());
}

void VeraPdfCheckerTests::missingToolGivesChineseReason() {
    // 传一个明显不存在的路径：探测会继续往下找，最后什么都没找到。
    const VeraPdfTool tool = findVeraPdf({QStringLiteral("D:/绝对不存在的路径/verapdf.exe")});
    if (tool.found) {
        // 本机恰好装了 veraPDF：那这条用例只验证「探测成功时不该有错误文案」。
        QVERIFY(tool.error.isEmpty());
        return;
    }
    QVERIFY(!tool.error.isEmpty());
    // 原因必须是中文、且说清楚「本仓库不自带它」。
    QVERIFY2(tool.error.contains(QStringLiteral("veraPDF")), qPrintable(tool.error));
    QVERIFY2(tool.error.contains(QStringLiteral("本仓库不自带")), qPrintable(tool.error));
}

void VeraPdfCheckerTests::planRefusesWithoutProgram() {
    const VeraPdfPlan plan = planVeraPdfValidation(QString(), QStringLiteral("in.pdf"),
                                                    QStringLiteral("out.xml"));
    QVERIFY(!plan.valid);
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(plan.arguments.isEmpty());
}

void VeraPdfCheckerTests::planRefusesWithoutInput() {
    const VeraPdfPlan plan = planVeraPdfValidation(QStringLiteral("verapdf"), QString(),
                                                    QStringLiteral("out.xml"));
    QVERIFY(!plan.valid);
    QVERIFY(!plan.error.isEmpty());
}

void VeraPdfCheckerTests::planBuildsTheExpectedArguments() {
    const VeraPdfPlan plan = planVeraPdfValidation(QStringLiteral("C:/tools/verapdf.exe"),
                                                    QStringLiteral("C:/tmp/in.pdf"),
                                                    QStringLiteral("C:/tmp/out.xml"),
                                                    QStringLiteral("PDF/A-2b"));
    QVERIFY2(plan.valid, qPrintable(plan.error));
    QCOMPARE(plan.program, QStringLiteral("C:/tools/verapdf.exe"));
    // 报告要落到文件里：XML 里可能带内嵌的 PDF 元数据，走管道不方便。
    QVERIFY(plan.arguments.contains(QStringLiteral("--format")));
    QVERIFY(plan.arguments.contains(QStringLiteral("xml")));
    QVERIFY(plan.arguments.contains(QStringLiteral("--reportfile")));
    QVERIFY(plan.arguments.contains(QStringLiteral("C:/tmp/out.xml")));
    QVERIFY(plan.arguments.contains(QStringLiteral("--flavour")));
    QVERIFY(plan.arguments.contains(QStringLiteral("PDF/A-2b")));
    // 待检文件必须是最后一个参数。
    QCOMPARE(plan.arguments.last(), QStringLiteral("C:/tmp/in.pdf"));
}

void VeraPdfCheckerTests::parseCountsEveryRule() {
    VeraPdfReport report = parseVeraPdfXmlReport(fixtureXml(false));
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.findings.size(), 2);
    QCOMPARE(report.passedCount, 2);
    QCOMPARE(report.failedCount, 0);
    QCOMPARE(report.warningCount, 0);
    // 条款号与状态要读出来：界面要逐条列给用户看。
    QCOMPARE(report.findings.at(0).clause, QStringLiteral("6.1.2"));
    QCOMPARE(report.findings.at(0).status, QStringLiteral("passed"));
    QVERIFY(report.findings.at(0).passed);
    QCOMPARE(report.findings.at(0).specification, QStringLiteral("ISO 19005-1"));
    // 规范名也要读出来。
    QCOMPARE(report.profile, QStringLiteral("PDF/A-2b"));
}

void VeraPdfCheckerTests::parseReportsCompliantWhenAllPassed() {
    const VeraPdfReport report = parseVeraPdfXmlReport(fixtureXml(false));
    QVERIFY(report.ok);
    QVERIFY(report.compliant);
    QVERIFY(!report.summary.isEmpty());
}

void VeraPdfCheckerTests::parseReportsFailedWhenARuleFails() {
    const VeraPdfReport report = parseVeraPdfXmlReport(fixtureXml(true));
    QVERIFY2(report.ok, qPrintable(report.error));
    QVERIFY(!report.compliant);
    QCOMPARE(report.failedCount, 1);
    QCOMPARE(report.passedCount, 2);
    // 失败那条的条款与原文要留着。
    QCOMPARE(report.findings.at(2).clause, QStringLiteral("6.8.1"));
    QVERIFY(report.findings.at(2).message.contains(QStringLiteral("XMP")));
}

void VeraPdfCheckerTests::parseRejectsEmptyReport() {
    QString error;
    const VeraPdfReport report = parseVeraPdfXmlReport(QByteArray(), &error);
    QVERIFY(!report.ok);
    QVERIFY(!report.compliant);
    QVERIFY(!error.isEmpty());
}

void VeraPdfCheckerTests::parseRejectsReportWithoutRules() {
    QString error;
    const VeraPdfReport report = parseVeraPdfXmlReport(
        QByteArray("<?xml version=\"1.0\"?><validationReport></validationReport>"), &error);
    QVERIFY(!report.ok);
    QVERIFY(!report.compliant);
    QVERIFY2(error.contains(QStringLiteral("逐条结论")), qPrintable(error));
}

void VeraPdfCheckerTests::parseRejectsMalformedXml() {
    QString error;
    const VeraPdfReport report = parseVeraPdfXmlReport(
        QByteArray("<?xml version=\"1.0\"?><validationReport><rule status=\"passed\""), &error);
    QVERIFY(!report.ok);
    QVERIFY(!report.compliant);
    QVERIFY(!error.isEmpty());
}

void VeraPdfCheckerTests::unavailableNoticeSaysPreflightIsNotCertification() {
    const QString notice = veraPdfUnavailableNotice();
    QVERIFY(!notice.isEmpty());
    // 必须说清「预检 ≠ 认证」。
    QVERIFY2(notice.contains(QStringLiteral("预检")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("认证")), qPrintable(notice));
    // 不许出现「已通过」这种断言。
    QVERIFY2(!notice.contains(QStringLiteral("已通过")), qPrintable(notice));
    // 也不许把「预检通过」说成「合规」。
    QVERIFY2(!notice.contains(QStringLiteral("预检通过等于合规")), qPrintable(notice));
}

void VeraPdfCheckerTests::failedNoticeListsFailedRules() {
    const VeraPdfReport report = parseVeraPdfXmlReport(fixtureXml(true));
    QVERIFY(report.ok);
    const QString notice = veraPdfFailedNotice(report);
    QVERIFY2(notice.contains(QStringLiteral("6.8.1")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("XMP")), qPrintable(notice));
    // 通过的条款不该一条条列出来（噪音）。
    QVERIFY2(!notice.contains(QStringLiteral("6.1.2")), qPrintable(notice));
}

QTEST_MAIN(VeraPdfCheckerTests)
#include "VeraPdfCheckerTests.moc"
