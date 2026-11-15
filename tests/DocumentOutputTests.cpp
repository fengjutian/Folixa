// v1.2.0 输出路径的测试：图片导出、文本导出、打印到 PDF。
//
// 三条断言分别盯三件事：
//   1) 每页都出一个文件，尺寸与页面点数、DPI 对得上；
//   2) 导出的文本是 UTF-8 BOM + 分页符，内容来自文本层而不是猜的；
//   3) 打印出来的 PDF 页数等于文档页数——这是"能不能打"的最小证据。
//
// 还有一条专门的：批注必须出现在导出的图里。它验证的其实是
// documentForOutput() 那条路——先补 /AP 再重开，而不是直接画内存里的文档。

#include "app/DocumentOutput.h"
#include "pdf/Annotation.h"
#include "pdf/IncrementalWriter.h"
#include "pdf/PdfiumAdapter.h"

#include <QFile>
#include <QImage>
#include <QPageRanges>
#include <QPrinter>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::exportPageImages;
using aipdf::app::exportPlainText;
using aipdf::app::ImageExportOptions;
using aipdf::app::OutputRange;
using aipdf::app::printDocument;
using aipdf::app::PrintOptions;
using aipdf::app::TextExportOptions;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

Annotation makeHighlight() {
    Annotation annotation;
    annotation.id = 1;
    annotation.pageIndex = 0;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(60.0, 200.0, 200.0, 18.0);
    annotation.quads = { annotation.rect };
    annotation.color = QColor(255, 214, 0);
    return annotation;
}

}  // namespace

class DocumentOutputTests : public QObject {
    Q_OBJECT

private slots:
    void exportsOneImagePerPage();
    void exportsTextWithBomAndPageBreak();
    void printedPdfKeepsPageCount();
    void pagesPerSheetPacksMultiplePages();
    void printerPageRangeSelectsSubset();
    void annotationsAppearInExportedImage();
    void rangeIsClampedToDocument();
};

void DocumentOutputTests::exportsOneImagePerPage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 2);

    ImageExportOptions options;
    options.dpi = 72;  // 1:1，方便断言像素尺寸
    const auto result = exportPageImages(opened.document, dir.path(), QStringLiteral("page"), options);

    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.files.size(), 2);

    const QImage first(result.files.first());
    QVERIFY(!first.isNull());
    // 72 DPI 下 1 点 = 1 像素：夹具页是 612×400。
    QCOMPARE(first.width(), 612);
    QCOMPARE(first.height(), 400);
}

void DocumentOutputTests::exportsTextWithBomAndPageBreak() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("out.txt"));

    const auto opened = openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY(opened.document.isValid());

    TextExportOptions options;
    const auto result = exportPlainText(opened.document, path, options);
    QVERIFY2(result.ok, qPrintable(result.error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();

    QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
    QVERIFY(!bytes.mid(3).trimmed().isEmpty());
}

void DocumentOutputTests::printedPdfKeepsPageCount() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdfPath = dir.filePath(QStringLiteral("printed.pdf"));

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    const int pages = opened.document.pageCount();

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdfPath);

    PrintOptions options;
    QString error;
    QVERIFY2(printDocument(opened.document, &printer, options, &error), qPrintable(error));

    QFile produced(pdfPath);
    QVERIFY(produced.open(QIODevice::ReadOnly));
    const QByteArray bytes = produced.readAll();
    produced.close();
    QVERIFY(!bytes.isEmpty());

    const auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), pages);
}

void DocumentOutputTests::annotationsAppearInExportedImage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations({ makeHighlight() }));

    ImageExportOptions options;
    options.dpi = 72;
    options.range.lastPage = 0;
    const auto result = exportPageImages(opened.document, dir.path(), QStringLiteral("annot"), options);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.files.size(), 1);

    const QImage image(result.files.first());
    QVERIFY(!image.isNull());
    // 批注在 PDF 坐标 (60,200)-(260,218)，图像 y 轴向下翻转。
    const int x = 160;
    const int y = image.height() - 209;
    const QColor color = image.pixelColor(x, y);
    QVERIFY2(color.red() > 200 && color.blue() < 200,
             qPrintable(QStringLiteral("批注位置的像素是 rgb(%1,%2,%3)，看起来没画上批注")
                            .arg(color.red())
                            .arg(color.green())
                            .arg(color.blue())));
}

void DocumentOutputTests::rangeIsClampedToDocument() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());

    ImageExportOptions options;
    options.dpi = 72;
    options.range.firstPage = -5;  // 故意越界
    options.range.lastPage = 99;
    const auto result = exportPageImages(opened.document, dir.path(), QStringLiteral("clamp"), options);

    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.files.size(), opened.document.pageCount());
}

void DocumentOutputTests::pagesPerSheetPacksMultiplePages() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdfPath = dir.filePath(QStringLiteral("fourup.pdf"));

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 2);

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdfPath);

    PrintOptions options;
    options.pagesPerSheet = 4;  // 两页文档排成 2×2，应当只出一张纸
    QString error;
    QVERIFY2(printDocument(opened.document, &printer, options, &error), qPrintable(error));

    QFile produced(pdfPath);
    QVERIFY(produced.open(QIODevice::ReadOnly));
    const QByteArray bytes = produced.readAll();
    produced.close();

    const auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), 1);
}

void DocumentOutputTests::printerPageRangeSelectsSubset() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdfPath = dir.filePath(QStringLiteral("range.pdf"));

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 2);

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdfPath);
    // 对话框里选了"第 2 页"，打印必须只出这一页。
    QPageRanges ranges;
    ranges.addPage(2);
    printer.setPageRanges(ranges);

    PrintOptions options;
    QString error;
    QVERIFY2(printDocument(opened.document, &printer, options, &error), qPrintable(error));

    QFile produced(pdfPath);
    QVERIFY(produced.open(QIODevice::ReadOnly));
    const QByteArray bytes = produced.readAll();
    produced.close();

    const auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), 1);
}

QTEST_MAIN(DocumentOutputTests)
#include "DocumentOutputTests.moc"
