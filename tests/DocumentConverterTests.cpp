#include "app/controllers/DocumentConverter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace aipdf::app;

class DocumentConverterTests final : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingInput();
    void buildsLibreOfficePlan();
    void buildsPdfAPlan();
    void buildsOptimizationPlan();
};

QString makeInput(QTemporaryDir& directory, const QString& name) {
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7) return {};
    return path;
}

void DocumentConverterTests::rejectsMissingInput() {
    const ConversionPlan plan = DocumentConverter::plan(
        ConversionKind::OfficeToPdf, QStringLiteral("missing.docx"), QStringLiteral("out.pdf"),
        QStringLiteral("soffice.exe"));
    QVERIFY(!plan.isValid());
    QVERIFY(plan.error.contains(QStringLiteral("不存在")));
}

void DocumentConverterTests::buildsLibreOfficePlan() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = makeInput(directory, QStringLiteral("report.docx"));
    const ConversionPlan plan = DocumentConverter::plan(
        ConversionKind::OfficeToPdf, input, directory.filePath(QStringLiteral("chosen.pdf")),
        QStringLiteral("C:/Tools/soffice.exe"));
    QVERIFY(plan.isValid());
    QCOMPARE(plan.engineName, QStringLiteral("LibreOffice"));
    QVERIFY(plan.arguments.contains(QStringLiteral("--headless")));
    QVERIFY(plan.arguments.contains(QStringLiteral("--convert-to")));
    QCOMPARE(QFileInfo(plan.expectedOutput).fileName(), QStringLiteral("report.pdf"));
}

void DocumentConverterTests::buildsPdfAPlan() {
    QTemporaryDir directory;
    const QString input = makeInput(directory, QStringLiteral("input.pdf"));
    const QString output = directory.filePath(QStringLiteral("archive.pdf"));
    const ConversionPlan plan = DocumentConverter::plan(
        ConversionKind::PdfA2b, input, output, {}, QStringLiteral("gswin64c.exe"));
    QVERIFY(plan.isValid());
    QVERIFY(plan.arguments.contains(QStringLiteral("-dPDFA=2")));
    QVERIFY(plan.arguments.contains(QStringLiteral("-dPDFACompatibilityPolicy=1")));
    QVERIFY(plan.arguments.contains(QStringLiteral("-sOutputFile=%1").arg(QDir::cleanPath(output))));
}

void DocumentConverterTests::buildsOptimizationPlan() {
    QTemporaryDir directory;
    const QString input = makeInput(directory, QStringLiteral("input.pdf"));
    const ConversionPlan plan = DocumentConverter::plan(
        ConversionKind::OptimizePdf, input, directory.filePath(QStringLiteral("small.pdf")), {},
        QStringLiteral("gswin64c.exe"));
    QVERIFY(plan.isValid());
    QVERIFY(plan.arguments.contains(QStringLiteral("-dPDFSETTINGS=/ebook")));
    QVERIFY(!plan.arguments.contains(QStringLiteral("-dPDFA=2")));
}

QTEST_GUILESS_MAIN(DocumentConverterTests)
#include "DocumentConverterTests.moc"
