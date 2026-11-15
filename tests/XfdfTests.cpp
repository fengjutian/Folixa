// XFDF 往返的单元测试。
//
// 这一条要证明的是"批注不是我们的私货"：导出成 XFDF 再导回来，类型、位置、
// 颜色、正文、顶点、笔画都不能变。只要有一项丢了，用户换一个审阅工具就会丢数据。

#include "pdf/Xfdf.h"

#include <QTest>

using aipdf::pdf::annotationsFromXfdf;
using aipdf::pdf::annotationsToXfdf;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;

namespace {

Annotation make(AnnotationType type, int page) {
    Annotation annotation;
    annotation.id = 42;
    annotation.pageIndex = page;
    annotation.type = type;
    annotation.rect = QRectF(10.0, 20.0, 100.0, 30.0);
    annotation.color = QColor(0xff, 0xd6, 0x00);
    return annotation;
}

}  // namespace

class XfdfTests : public QObject {
    Q_OBJECT

private slots:
    void roundTripsSimpleMarkup();
    void roundTripsVerticesAndStrokes();
    void roundTripsContents();
    void dropsOutOfRangePages();
    void ignoresUnknownElements();
    void malformedXmlYieldsNothing();
    void roundTripsReviewMetadata();
};

void XfdfTests::roundTripsSimpleMarkup() {
    QVector<Annotation> source{ make(AnnotationType::Highlight, 1),
                                make(AnnotationType::Underline, 0) };
    const QString xml = annotationsToXfdf(source, 3);
    const auto parsed = annotationsFromXfdf(xml, 3);

    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).type, AnnotationType::Highlight);
    QCOMPARE(parsed.at(0).pageIndex, 1);
    QCOMPARE(parsed.at(1).type, AnnotationType::Underline);
    // 坐标往返误差应在 0.01 点以内（导出时保留两位小数）。
    QVERIFY(qAbs(parsed.at(0).rect.left() - 10.0) < 0.01);
    QVERIFY(qAbs(parsed.at(0).rect.width() - 100.0) < 0.01);
    QCOMPARE(parsed.at(0).color, QColor(0xff, 0xd6, 0x00));
}

void XfdfTests::roundTripsVerticesAndStrokes() {
    Annotation polygon = make(AnnotationType::Polygon, 0);
    polygon.vertices = { QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(5.0, 8.0) };

    Annotation ink = make(AnnotationType::Ink, 0);
    ink.strokes = { { QPointF(1.0, 1.0), QPointF(2.0, 2.0) },
                    { QPointF(3.0, 3.0), QPointF(4.0, 4.0) } };

    const auto parsed = annotationsFromXfdf(annotationsToXfdf({ polygon, ink }, 1), 1);
    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).vertices.size(), 3);
    QCOMPARE(parsed.at(1).strokes.size(), 2);
    QCOMPARE(parsed.at(1).strokes.at(1).size(), 2);
}

void XfdfTests::roundTripsContents() {
    Annotation text = make(AnnotationType::FreeText, 0);
    text.contents = QStringLiteral("合同金额：100 元 (含税)");
    const auto parsed = annotationsFromXfdf(annotationsToXfdf({ text }, 1), 1);
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first().contents, text.contents);
}

void XfdfTests::dropsOutOfRangePages() {
    QVector<Annotation> source{ make(AnnotationType::Highlight, 5) };
    const auto parsed = annotationsFromXfdf(annotationsToXfdf(source, 2), 2);
    QVERIFY(parsed.isEmpty());
}

void XfdfTests::ignoresUnknownElements() {
    // 别的软件会带我们不认识的元素与属性：跳过它们，不整份失败。
    const QString xml = QStringLiteral(
        "<?xml version=\"1.0\"?><xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots>"
        "<caret page=\"0\" rect=\"0,0,10,10\"/>"
        "<square page=\"0\" rect=\"1,2,3,4\" color=\"#ff0000\" flags=\"print\">"
        "<contents>box</contents></square>"
        "</annots></xfdf>");
    const auto parsed = annotationsFromXfdf(xml, 1);
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first().type, AnnotationType::Square);
    QCOMPARE(parsed.first().contents, QStringLiteral("box"));
}

void XfdfTests::malformedXmlYieldsNothing() {
    QVERIFY(annotationsFromXfdf(QStringLiteral("<xfdf><annots>"), 1).isEmpty());
}

void XfdfTests::roundTripsReviewMetadata() {
    Annotation annotation = make(AnnotationType::Note, 0);
    annotation.author = QStringLiteral("张三");
    annotation.reviewState = QStringLiteral("Accepted");
    annotation.replyToId = 9;
    annotation.createdAt = QDateTime(QDate(2026, 11, 15), QTime(9, 0, 0), Qt::UTC);
    annotation.contents = QStringLiteral("同意");

    const auto parsed = annotationsFromXfdf(annotationsToXfdf({ annotation }, 1), 1);
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first().author, QStringLiteral("张三"));
    QCOMPARE(parsed.first().reviewState, QStringLiteral("Accepted"));
    QCOMPARE(parsed.first().replyToId, 9);
    QCOMPARE(parsed.first().createdAt.toUTC(), annotation.createdAt);
}

QTEST_MAIN(XfdfTests)
#include "XfdfTests.moc"
