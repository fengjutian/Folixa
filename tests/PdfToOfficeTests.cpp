// PDF → Excel / PowerPoint / Word（整篇）的单元测试。
//
// 守的是三件事：
//  1. **产物是真的包**：xlsx / pptx / docx 必须是 zip 包，且部件齐全
//     （Excel 少一个 sheet 部件就说「文件已损坏」）。
//  2. **内容对得上**：表格的单元格要落在正确的行列；整篇导出的页数
//     要与原文一致；表格外的文字不能被吞掉。
//  3. **坏输入老实报错**：打不开的 PDF 不许产出半成品。
//
// 另有一条端到端脚本（build/verify-office-packages.py）：产物由
// **openpyxl / python-pptx / LibreOffice** 这些独立工具打开验证——
// 用本仓库自己的 zip 读回器验证自己写的包，等于自己给自己判卷。
#include "doc/PdfToOffice.h"
#include "doc/ZipWriter.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

using namespace aipdf::doc;

namespace {

QString fixturePath(const char* name) {
    return QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') +
           QString::fromLatin1(name);
}

QByteArray fixtureBytes(const char* name) {
    QFile file(fixturePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// 把产物落到磁盘（给独立工具验证用）。
//
// 为什么要有这一步：单测里用本仓库自己的 zip 读回器检查自己写的包，
// 等于自己给自己判卷。真正的验收是「**别的软件**能打开它」：
// openpyxl / python-pptx / LibreOffice 打开得了吗。
//
// 设了环境变量 AIPDF_OFFICE_DUMP_DIR 才落盘；平时不落，免得污染目录。
void dumpIfRequested(const QString& name, const QByteArray& bytes) {
    const QString directory = qEnvironmentVariable("AIPDF_OFFICE_DUMP_DIR");
    if (directory.isEmpty() || bytes.isEmpty()) {
        return;
    }
    QDir().mkpath(directory);
    QFile file(directory + QLatin1Char('/') + name);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(bytes);
    file.close();
}

}  // namespace

class PdfToOfficeTests : public QObject {
    Q_OBJECT

private slots:
    void excelExportWritesATableSheet();
    void excelExportKeepsTheTextOutsideTheTable();
    void excelExportKeepsOneSheetPerPage();
    void pptxExportWritesOneSlidePerPage();
    void wordExportKeepsTheWholeDocument();
    void brokenPdfIsRejectedHonestly();
};

void PdfToOfficeTests::excelExportWritesATableSheet() {
    const QByteArray pdf = fixtureBytes("table.pdf");
    QVERIFY2(!pdf.isEmpty(), "缺少夹具 table.pdf");
    const ExcelExportResult result = exportPdfToExcel(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.bytes.isEmpty());
    dumpIfRequested(QStringLiteral("table.xlsx"), result.bytes);
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.tableCount, 1);

    // 包结构：工作簿、工作表、样式表都要在。
    const QStringList entries = zip::listEntryNames(result.bytes);
    QVERIFY(entries.contains(QStringLiteral("[Content_Types].xml")));
    QVERIFY(entries.contains(QStringLiteral("xl/workbook.xml")));
    QVERIFY(entries.contains(QStringLiteral("xl/_rels/workbook.xml.rels")));
    QVERIFY(entries.contains(QStringLiteral("xl/styles.xml")));
    QVERIFY(entries.contains(QStringLiteral("xl/worksheets/sheet1.xml")));
    // 表外的文字另起一张表。
    QVERIFY(entries.contains(QStringLiteral("xl/worksheets/sheet2.xml")));

    // 表格的内容：3 列 × 3 行，单元格落在正确的行列上。
    const QByteArray sheet = zip::readEntry(result.bytes, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(!sheet.isEmpty());
    const QByteArray sheetText = sheet;
    QVERIFY2(sheetText.contains("Item"), sheetText.constData());
    QVERIFY2(sheetText.contains("Apple"), sheetText.constData());
    QVERIFY2(sheetText.contains("5.25"), sheetText.constData());
    // 第一行是表头：A1 / B1 / C1 三个单元格。
    QVERIFY2(sheetText.contains("r=\"A1\""), sheetText.constData());
    QVERIFY2(sheetText.contains("r=\"C1\""), sheetText.constData());
    // 第三行第三列：Pear / 7 / 5.25。
    QVERIFY2(sheetText.contains("r=\"C3\""), sheetText.constData());

    // 工作簿里要声明两张表，名字也要写清楚。
    const QByteArray workbook = zip::readEntry(result.bytes, QStringLiteral("xl/workbook.xml"));
    QVERIFY(workbook.contains("第 1 页 表"));
    QVERIFY(workbook.contains("第 1 页 文字"));
}

void PdfToOfficeTests::excelExportKeepsTheTextOutsideTheTable() {
    const QByteArray pdf = fixtureBytes("table.pdf");
    QVERIFY2(!pdf.isEmpty(), "缺少夹具 table.pdf");
    const ExcelExportResult result = exportPdfToExcel(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 表格之外的标题（「Quarterly report」）必须在文字表里，不能被吞掉。
    const QByteArray textSheet =
        zip::readEntry(result.bytes, QStringLiteral("xl/worksheets/sheet2.xml"));
    QVERIFY(!textSheet.isEmpty());
    QVERIFY2(textSheet.contains("Quarterly report"), textSheet.constData());
    // 表格里的内容不该在文字表里重复一遍。
    QVERIFY2(!textSheet.contains("Apple"), textSheet.constData());
}

void PdfToOfficeTests::excelExportKeepsOneSheetPerPage() {
    const QByteArray pdf = fixtureBytes("multipage.pdf");
    QVERIFY2(!pdf.isEmpty(), "缺少夹具 multipage.pdf");
    const ExcelExportResult result = exportPdfToExcel(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 12 页 → 至少 12 张表（每页至少一张；认出表格的页会多一张文字表）。
    QVERIFY(result.pageCount >= 12);
    QVERIFY(result.sheetCount >= result.pageCount);

    // 最后一个工作表的部件也要在（部件数与表数对得上）。
    const QStringList entries = zip::listEntryNames(result.bytes);
    QCOMPARE(entries.filter(QStringLiteral("xl/worksheets/sheet")).size(), result.sheetCount);
    // 工作簿声明的工作表数与部件数一致。
    const QByteArray workbook = zip::readEntry(result.bytes, QStringLiteral("xl/workbook.xml"));
    QCOMPARE(workbook.count("<sheet "), result.sheetCount);
}

void PdfToOfficeTests::pptxExportWritesOneSlidePerPage() {
    const QByteArray pdf = fixtureBytes("multipage.pdf");
    QVERIFY2(!pdf.isEmpty(), "缺少夹具 multipage.pdf");
    const PptxExportResult result = exportPdfToPptx(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.slideCount, 12);
    dumpIfRequested(QStringLiteral("multipage.pptx"), result.bytes);

    const QStringList entries = zip::listEntryNames(result.bytes);
    QVERIFY(entries.contains(QStringLiteral("ppt/presentation.xml")));
    QVERIFY(entries.contains(QStringLiteral("ppt/slides/slide1.xml")));
    QVERIFY(entries.contains(QStringLiteral("ppt/slides/slide12.xml")));

    // 幻灯片里的文字要与 PDF 里对得上。
    const QByteArray slide = zip::readEntry(result.bytes, QStringLiteral("ppt/slides/slide1.xml"));
    QVERIFY(!slide.isEmpty());
    QVERIFY2(slide.contains("Page 1"), slide.constData());
    // 标题是「第 N 页」。
    QVERIFY2(slide.contains("第 1 页"), slide.constData());
}

void PdfToOfficeTests::wordExportKeepsTheWholeDocument() {
    const QByteArray pdf = fixtureBytes("text.pdf");
    QVERIFY2(!pdf.isEmpty(), "缺少夹具 text.pdf");
    const WordExportResult result = exportPdfToWord(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.pageCount, 2);
    dumpIfRequested(QStringLiteral("text.docx"), result.bytes);

    const QByteArray document =
        zip::readEntry(result.bytes, QStringLiteral("word/document.xml"));
    QVERIFY(!document.isEmpty());
    // 两页的文字都要在（原来只导一页）。
    QVERIFY2(document.contains("Folixa"), document.constData());
    QVERIFY2(document.contains("Search hits are listed"), document.constData());
    // 页与页之间要有分页符。
    QVERIFY2(document.contains("w:br w:type=\"page\""), document.constData());
}

void PdfToOfficeTests::brokenPdfIsRejectedHonestly() {
    const QByteArray garbage = QByteArray("this is not a pdf at all");
    const ExcelExportResult excel = exportPdfToExcel(garbage);
    QVERIFY(!excel.ok);
    QVERIFY(!excel.error.isEmpty());
    QVERIFY(excel.bytes.isEmpty());

    const PptxExportResult pptx = exportPdfToPptx(garbage);
    QVERIFY(!pptx.ok);
    QVERIFY(!pptx.error.isEmpty());
    QVERIFY(pptx.bytes.isEmpty());

    const WordExportResult word = exportPdfToWord(garbage);
    QVERIFY(!word.ok);
    QVERIFY(!word.error.isEmpty());
    QVERIFY(word.bytes.isEmpty());
}

QTEST_MAIN(PdfToOfficeTests)
#include "PdfToOfficeTests.moc"
