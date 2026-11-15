// 批注字典额外条目（/L、/LE、/Vertices、/BE）的单元测试。
//
// 这些键是"别的阅读器怎么理解这条批注"的全部依据：没有 /L，直线就是个空矩形；
// 没有 /LE，箭头就是一条线。而它们只能由我们的增量写入器补，所以必须逐字确认。

#include "pdf/AnnotationDictionary.h"

#include <QTest>

using aipdf::pdf::annotationDictionaryExtras;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;

namespace {

Annotation make(AnnotationType type) {
    Annotation annotation;
    annotation.type = type;
    annotation.rect = QRectF(100.0, 100.0, 160.0, 60.0);
    return annotation;
}

}  // namespace

class AnnotationDictionaryTests : public QObject {
    Q_OBJECT

private slots:
    void lineWritesEndpoints();
    void arrowWritesLineEnding();
    void polygonWritesVertices();
    void cloudWritesBorderEffect();
    void degenerateInputWritesNothing();
    void otherTypesWriteNothing();
};

void AnnotationDictionaryTests::lineWritesEndpoints() {
    Annotation annotation = make(AnnotationType::Line);
    annotation.vertices = { QPointF(100.0, 120.0), QPointF(260.0, 180.0) };

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/L [100.000 120.000 260.000 180.000]"));
    QVERIFY(!extras.contains("/LE"));  // 普通直线没有箭头
}

void AnnotationDictionaryTests::arrowWritesLineEnding() {
    Annotation annotation = make(AnnotationType::Arrow);
    annotation.vertices = { QPointF(0.0, 0.0), QPointF(10.0, 0.0) };

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/LE [/None /OpenArrow]"));
}

void AnnotationDictionaryTests::polygonWritesVertices() {
    Annotation annotation = make(AnnotationType::Polygon);
    annotation.vertices = { QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(5.0, 8.0) };

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/Vertices [0.000 0.000 10.000 0.000 5.000 8.000]"));
    QVERIFY(!extras.contains("/BE"));
}

void AnnotationDictionaryTests::cloudWritesBorderEffect() {
    Annotation annotation = make(AnnotationType::Cloud);
    annotation.vertices = { QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(5.0, 8.0) };

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/BE << /S /C /I 1 >>"));
}

void AnnotationDictionaryTests::degenerateInputWritesNothing() {
    // 顶点不够：写半条直线或两个点的"多边形"都是坏数据，宁可什么都不写。
    Annotation line = make(AnnotationType::Line);
    line.vertices = { QPointF(1.0, 2.0) };
    QVERIFY(annotationDictionaryExtras(line).isEmpty());

    Annotation polygon = make(AnnotationType::Polygon);
    polygon.vertices = { QPointF(1.0, 2.0), QPointF(3.0, 4.0) };
    QVERIFY(annotationDictionaryExtras(polygon).isEmpty());
}

void AnnotationDictionaryTests::otherTypesWriteNothing() {
    QVERIFY(annotationDictionaryExtras(make(AnnotationType::Highlight)).isEmpty());
    QVERIFY(annotationDictionaryExtras(make(AnnotationType::Square)).isEmpty());
}

QTEST_MAIN(AnnotationDictionaryTests)
#include "AnnotationDictionaryTests.moc"
