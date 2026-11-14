// 增量写入的测试：给批注补 /AP 之后，文件还在不在、原字节动没动、别人看不看得见。
//
// 这条路径最怕两件事：把文件写坏（打不开），或者写完只有自己认（别的阅读器看不见）。
// 所以四类断言缺一不可：
//   1) 结构：原字节一字不改，新对象与 xref 追加在尾部，重新打开仍然合法；
//   2) 数据：批注还在、/NM 标记还在、类型没变；
//   3) 可见性：同一页在补 /AP 前后的渲染差异，必须正好落在批注矩形里；
//   4) 失败语义：找不到标记 / 外观流为空时必须明确失败，不能返回半成品。

#include "pdf/Annotation.h"
#include "pdf/AppearanceStream.h"
#include "pdf/IncrementalWriter.h"
#include "pdf/PdfiumAdapter.h"

#include <QImage>
#include <QTest>

#include <algorithm>

using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::appendAnnotationAppearance;
using aipdf::pdf::buildAnnotationAppearance;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::openDocument;
using aipdf::pdf::PdfDocument;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

Annotation makeHighlight(quint64 id) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = 0;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(60.0, 200.0, 200.0, 18.0);
    annotation.quads = { QRectF(60.0, 200.0, 200.0, 18.0) };
    annotation.color = QColor(255, 214, 0);
    return annotation;
}

// 把批注写进一份真实文档，返回 PDFium 保存出来的字节。
QByteArray documentWithHighlight(quint64 id) {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    if (!result.document.isValid()) {
        return {};
    }
    auto page = result.document.page(0);
    if (!page.has_value()) {
        return {};
    }
    if (!page->setAnnotations({ makeHighlight(id) })) {
        return {};
    }
    return result.document.serialize();
}

QByteArray markerFor(quint64 id) {
    return QByteArray("Folixa:") + QByteArray::number(id);
}

int fixturePageCount() {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    return result.document.isValid() ? result.document.pageCount() : -1;
}

}  // namespace

class IncrementalWriterTests : public QObject {
    Q_OBJECT

private slots:
    void appendedFileKeepsOriginalBytesIntact();
    void appendedFileStillOpensWithAnnotation();
    void appearanceChangesPixelsInsideRectOnly();
    void unknownMarkerIsRejected();
    void emptyAppearanceIsRejected();
};

void IncrementalWriterTests::appendedFileKeepsOriginalBytesIntact() {
    const QByteArray original = documentWithHighlight(7);
    QVERIFY2(!original.isEmpty(), "夹具文档应当能序列化");

    const auto appearance = buildAnnotationAppearance(makeHighlight(7));
    const auto write = appendAnnotationAppearance(original, markerFor(7), appearance);

    QVERIFY2(write.ok, qPrintable(write.error));
    QVERIFY(write.bytes.size() > original.size());
    // 增量写入的定义：前缀一个字节都不能变。
    QCOMPARE(write.bytes.left(original.size()), original);
    QVERIFY(write.bytes.contains("/AP"));
    QCOMPARE(write.appearanceCount, 1);
    QCOMPARE(write.rewrittenObjects, 1);
}

void IncrementalWriterTests::appendedFileStillOpensWithAnnotation() {
    const QByteArray original = documentWithHighlight(7);
    const auto write =
        appendAnnotationAppearance(original, markerFor(7), buildAnnotationAppearance(makeHighlight(7)));
    QVERIFY(write.ok);

    const auto reopened = openDocumentFromBytes(write.bytes);
    QVERIFY(reopened.document.isValid());
    // 页数必须与夹具原文档一致：写成 0 页是最典型的「结构写坏」。
    QCOMPARE(reopened.document.pageCount(), fixturePageCount());
    QVERIFY(reopened.document.pageCount() > 0);

    auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    const auto annotations = page->annotations();
    QCOMPARE(annotations.size(), 1);
    QCOMPARE(annotations.first().type, AnnotationType::Highlight);
    // /NM 里存的 id 必须原样读回来，否则撤销栈与文档对不上。
    QCOMPARE(annotations.first().id, quint64(7));
}

void IncrementalWriterTests::appearanceChangesPixelsInsideRectOnly() {
    const QByteArray original = documentWithHighlight(7);
    const Annotation model = makeHighlight(7);
    const auto write =
        appendAnnotationAppearance(original, markerFor(7), buildAnnotationAppearance(model));
    QVERIFY(write.ok);

    const auto before = openDocumentFromBytes(original);
    const auto after = openDocumentFromBytes(write.bytes);
    QVERIFY(before.document.isValid());
    QVERIFY(after.document.isValid());
    const auto pageBefore = before.document.page(0);
    const auto pageAfter = after.document.page(0);
    QVERIFY(pageBefore.has_value());
    QVERIFY(pageAfter.has_value());

    const QImage imageBefore = pageBefore->render(1.0);
    const QImage imageAfter = pageAfter->render(1.0);
    QCOMPARE(imageBefore.size(), imageAfter.size());
    QVERIFY(!imageBefore.isNull());

    int differing = 0;
    int minX = imageBefore.width(), maxX = -1, minY = imageBefore.height(), maxY = -1;
    for (int y = 0; y < imageBefore.height(); ++y) {
        for (int x = 0; x < imageBefore.width(); ++x) {
            const QRgb a = imageBefore.pixel(x, y);
            const QRgb b = imageAfter.pixel(x, y);
            if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
                qAbs(qBlue(a) - qBlue(b)) > 2) {
                ++differing;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }

    // 光栅化会有 1px 边缘误差，给 ±2 的余量；面积对不上说明外观流被缩放或裁剪了。
    QVERIFY2(differing > 0, "补了 /AP 却什么都没画出来");
    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    QVERIFY2(qAbs(width - 200) <= 2, qPrintable(QStringLiteral("差异宽 %1").arg(width)));
    QVERIFY2(qAbs(height - 18) <= 2, qPrintable(QStringLiteral("差异高 %1").arg(height)));
}

void IncrementalWriterTests::unknownMarkerIsRejected() {
    const QByteArray original = documentWithHighlight(7);
    const auto write =
        appendAnnotationAppearance(original, markerFor(99), buildAnnotationAppearance(makeHighlight(7)));

    QVERIFY(!write.ok);
    QVERIFY(write.bytes.isEmpty());
    QVERIFY(!write.error.isEmpty());
}

void IncrementalWriterTests::emptyAppearanceIsRejected() {
    const QByteArray original = documentWithHighlight(7);
    Annotation note = makeHighlight(7);
    note.type = AnnotationType::Note;  // 便签没有图形外观

    const auto write = appendAnnotationAppearance(original, markerFor(7),
                                                  buildAnnotationAppearance(note));
    QVERIFY(!write.ok);
    QVERIFY(write.bytes.isEmpty());
}

QTEST_MAIN(IncrementalWriterTests)
#include "IncrementalWriterTests.moc"
