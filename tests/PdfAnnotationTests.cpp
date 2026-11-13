// PDFium 适配层的批注读写测试。
//
// 关注三件事：
//   1) 写进去的批注能读回来（子类型、矩形、四角点、正文）；
//   2) 重新写入是「替换」而不是「累加」，撤销能把批注删干净；
//   3) 文档原有的链接注释既不会被当成批注，也不会被误删。

#include "app/AnnotationController.h"
#include "pdf/Annotation.h"
#include "pdf/PdfiumAdapter.h"

#include <QTest>

using aipdf::app::AnnotationController;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::PdfPage;
using aipdf::pdf::openDocument;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

Annotation makeHighlight(int pageIndex = 0) {
    Annotation annotation;
    annotation.id = 1;
    annotation.pageIndex = pageIndex;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(60.0, 250.0, 200.0, 14.0);
    annotation.quads.append(QRectF(60.0, 250.0, 200.0, 14.0));
    annotation.color = QColor(255, 214, 0);
    return annotation;
}

}  // namespace

class PdfAnnotationTests : public QObject {
    Q_OBJECT

private slots:
    void writeHighlightAndReadBack();
    void writeNoteKeepsContents();
    void rewriteReplacesPreviousAnnotations();
    void emptyListRemovesAnnotations();
    void linkAnnotationsAreNotTreatedAsAnnotations();
    void writingKeepsOriginalLinks();
    void invalidDocumentIsRejected();
    void controllerSyncsAnnotationsIntoDocument();
    void controllerUndoRemovesAnnotationFromDocument();
    void controllerUndoThenRedoRestoresAnnotation();
};

void PdfAnnotationTests::writeHighlightAndReadBack() {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());

    QVector<Annotation> written{makeHighlight(0)};
    QVERIFY(page->setAnnotations(written));

    const QVector<Annotation> read = page->annotations();
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.first().type, AnnotationType::Highlight);
    QCOMPARE(read.first().pageIndex, 0);
    // 矩形是 PDF user space，读回来应当落在同一位置。
    QVERIFY(qAbs(read.first().rect.left() - 60.0) < 0.5);
    QVERIFY(qAbs(read.first().rect.width() - 200.0) < 0.5);
    QCOMPARE(read.first().quads.size(), 1);
}

void PdfAnnotationTests::writeNoteKeepsContents() {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());

    Annotation note;
    note.id = 2;
    note.type = AnnotationType::Note;
    note.rect = QRectF(300.0, 300.0, 16.0, 16.0);
    note.contents = QStringLiteral("批注正文 note");
    note.author = QStringLiteral("Folixa");
    QVERIFY(page->setAnnotations({note}));

    const QVector<Annotation> read = page->annotations();
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.first().type, AnnotationType::Note);
    QCOMPARE(read.first().contents, QStringLiteral("批注正文 note"));
}

void PdfAnnotationTests::rewriteReplacesPreviousAnnotations() {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());

    QVERIFY(page->setAnnotations({makeHighlight(0)}));
    QCOMPARE(page->annotations().size(), 1);

    Annotation second = makeHighlight(0);
    second.id = 2;
    second.type = AnnotationType::Square;
    second.rect = QRectF(100.0, 100.0, 50.0, 50.0);
    second.quads.clear();
    // 再次写入是替换：批注数不该变成 2。
    QVERIFY(page->setAnnotations({second}));
    const QVector<Annotation> read = page->annotations();
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.first().type, AnnotationType::Square);
}

void PdfAnnotationTests::emptyListRemovesAnnotations() {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());

    QVERIFY(page->setAnnotations({makeHighlight(0)}));
    QCOMPARE(page->annotations().size(), 1);

    // 空列表 = 全撤掉：撤销走的就是这条路。
    QVERIFY(page->setAnnotations({}));
    QCOMPARE(page->annotations().size(), 0);
}

void PdfAnnotationTests::linkAnnotationsAreNotTreatedAsAnnotations() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());

    // 文档里本来就有链接注释，但它们不是本版本管理的批注类型。
    QCOMPARE(page->annotations().size(), 0);
    QVERIFY(!page->links().isEmpty());
}

void PdfAnnotationTests::writingKeepsOriginalLinks() {
    const auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const int linksBefore = page->links().size();
    QVERIFY(linksBefore > 0);

    Annotation annotation = makeHighlight(0);
    annotation.rect = QRectF(60.0, 200.0, 160.0, 35.0);
    annotation.quads.clear();
    QVERIFY(page->setAnnotations({annotation}));

    // 写批注不能动到原有的链接注释。
    QCOMPARE(page->links().size(), linksBefore);
    QCOMPARE(page->annotations().size(), 1);
}

void PdfAnnotationTests::invalidDocumentIsRejected() {
    const auto result = openDocument(fixturePath(QStringLiteral("corrupt.pdf")));
    QVERIFY(!result.document.isValid());
    QCOMPARE(result.document.pageCount(), 0);
    QVERIFY(!result.document.page(0).has_value());
}

QTEST_MAIN(PdfAnnotationTests)
#include "PdfAnnotationTests.moc"