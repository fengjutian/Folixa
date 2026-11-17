// 批注自定义外观（callout 箭头框 + 区域高亮）的专项单测。
//
// 这一层测的是「能力已跑通」的那一环：
//   1) 批注模型新增的两个字段（customAppearance / customAppearanceResources）
//      能无损往返 —— 内容流是二进制，走 JSON 必须用 base64，
//      里面只要有一个非 ASCII 字节就能把「悄悄转成 UTF-8 改写字节」的问题照出来；
//   2) 外观生成优先用自定义外观，没有自定义外观的批注仍走原来的
//      「按类型生成」那条路（回归保护：普通多边形批注不能因为加了这个字段就变了样）。
//
// 几何与内容流本身由 pdf::AnnotationShapes 负责，那一层有自己的专项单测
// （tests/AnnotationShapesTests.cpp），这里不重复测，只测「接上去以后对不对」。

#include "pdf/Annotation.h"
#include "pdf/AnnotationShapes.h"
#include "pdf/AppearanceStream.h"

#include <QJsonObject>
#include <QTest>

using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::Appearance;
using aipdf::pdf::CalloutSpec;
using aipdf::pdf::RegionHighlightSpec;
using aipdf::pdf::annotationFromJson;
using aipdf::pdf::annotationToJson;
using aipdf::pdf::buildAnnotationAppearance;
using aipdf::pdf::buildCalloutAppearanceStreamExtended;
using aipdf::pdf::buildRegionHighlightAppearanceStream;
using aipdf::pdf::calloutBounds;
using aipdf::pdf::polygonBounds;
using aipdf::pdf::polygonIsSelfIntersecting;
using aipdf::pdf::regionHighlightResources;

namespace {

// 一段含非 ASCII 字节的内容流：用来证明 base64 往返是无损的。
// 0x80 / 0xFF 在 UTF-8 里都不是合法单字节，JSON 序列化时最容易被改写。
QByteArray binaryContentStream() {
    QByteArray bytes = "q\n0.5 0.2 0.1 rg\n";
    bytes.append(static_cast<char>(0x80));
    bytes.append(static_cast<char>(0xFF));
    bytes.append(static_cast<char>(0x00));
    bytes.append("\n0 0 100 100 re\nf\nQ\n");
    return bytes;
}

// 一个普通的四边形顶点集（页面 user space，y 向上）。
QVector<QPointF> squareVertices() {
    return {QPointF(100.0, 100.0), QPointF(300.0, 100.0), QPointF(300.0, 200.0),
            QPointF(100.0, 200.0)};
}

// 一条自交的四边形（第一个顶点与第三个顶点交叉）。
QVector<QPointF> bowtieVertices() {
    return {QPointF(100.0, 100.0), QPointF(300.0, 200.0), QPointF(300.0, 100.0),
            QPointF(100.0, 200.0)};
}

// 矩形逐项比较：QCOMPARE 对 QRectF 没有可读的打印，失败时看不出差在哪。
void compareRects(const QRectF& actual, const QRectF& expected) {
    QCOMPARE(actual.x(), expected.x());
    QCOMPARE(actual.y(), expected.y());
    QCOMPARE(actual.width(), expected.width());
    QCOMPARE(actual.height(), expected.height());
}

}  // namespace

class CustomAppearanceTests : public QObject {
    Q_OBJECT

private slots:
    // 1) 往返后 customAppearance 字节完全相等（含非 ASCII 字节）。
    void customAppearanceRoundTripKeepsBytes();
    // 2) 往返后 customAppearanceResources 相等。
    void customAppearanceResourcesRoundTrip();
    // 3) 空 customAppearance 时往返正常（字段为空）。
    void emptyCustomAppearanceRoundTrips();
    // 4) 带 customAppearance 时外观 content 就是它。
    void appearanceUsesCustomContent();
    // 5) 带 customAppearance 时外观 bbox == annotation.rect。
    void appearanceUsesAnnotationRectAsBoundingBox();
    // 6) 带 customAppearanceResources 时 usesTransparency == true 且资源非空。
    void customResourcesMarkTransparency();
    // 7) 不带 customAppearance 时走原来的按类型生成路径（回归保护）。
    void withoutCustomAppearanceFallsBackToTypeAppearance();
    // 8) callout 批注：内容流用 buildCalloutAppearanceStreamExtended 真实生成。
    void calloutAppearanceMatchesGeneratedStream();
    // 9) 区域高亮批注：内容流真实生成，资源片段含 ExtGState。
    void regionHighlightAppearanceMatchesGeneratedStream();
    // 10) 负例：内容流为空但资源非空。
    void emptyContentWithResourcesFallsBackToTypeAppearance();
    // 11) callout 的 /Rect 必须盖住箭头（箭头出框会被 BBox 裁掉）。
    void calloutBoundsCoverTheArrow();
    // 12) 界面层拒绝自交多边形时，领域层确实判成自交。
    void selfIntersectingPolygonIsRejected();
};

void CustomAppearanceTests::customAppearanceRoundTripKeepsBytes() {
    Annotation source;
    source.id = 42;
    source.pageIndex = 3;
    source.type = AnnotationType::Square;
    source.rect = QRectF(10.0, 20.0, 100.0, 50.0);
    source.color = QColor(226, 62, 62);
    source.customAppearance = binaryContentStream();

    const QJsonObject json = annotationToJson(source);
    QVERIFY(json.contains(QStringLiteral("customAppearance")));

    Annotation restored;
    QVERIFY(annotationFromJson(json, &restored));
    // 一个字节都不能差：内容流被改写一个字节，PDF 里画出来就是另一个样子。
    QCOMPARE(restored.customAppearance, source.customAppearance);
    QCOMPARE(restored.customAppearance.size(), source.customAppearance.size());
    // 顺带确认往返没把别的字段弄丢。
    QCOMPARE(restored.id, source.id);
    QCOMPARE(restored.pageIndex, source.pageIndex);
    QCOMPARE(restored.type, source.type);
}

void CustomAppearanceTests::customAppearanceResourcesRoundTrip() {
    Annotation source;
    source.id = 7;
    source.pageIndex = 0;
    source.type = AnnotationType::Polygon;
    source.rect = QRectF(0.0, 0.0, 10.0, 10.0);
    source.customAppearance = QByteArray("q\n/GS0 gs\nf\nQ\n");
    source.customAppearanceResources = regionHighlightResources(0.35);

    const QJsonObject json = annotationToJson(source);
    QVERIFY(json.contains(QStringLiteral("customAppearanceResources")));

    Annotation restored;
    QVERIFY(annotationFromJson(json, &restored));
    QCOMPARE(restored.customAppearanceResources, source.customAppearanceResources);
    QCOMPARE(restored.customAppearance, source.customAppearance);
}

void CustomAppearanceTests::emptyCustomAppearanceRoundTrips() {
    Annotation source;
    source.id = 1;
    source.pageIndex = 2;
    source.type = AnnotationType::Polygon;
    source.rect = QRectF(1.0, 2.0, 3.0, 4.0);
    source.quads = {QRectF(1.0, 2.0, 3.0, 4.0)};

    const QJsonObject json = annotationToJson(source);
    // 空内容流不该往 JSON 里塞一个空字段。
    QVERIFY(!json.contains(QStringLiteral("customAppearance")));

    Annotation restored;
    QVERIFY(annotationFromJson(json, &restored));
    QVERIFY(restored.customAppearance.isEmpty());
    QVERIFY(restored.customAppearanceResources.isEmpty());
    QCOMPARE(restored.quads.size(), source.quads.size());
    compareRects(restored.rect, source.rect);
    QCOMPARE(restored.type, source.type);
}

void CustomAppearanceTests::appearanceUsesCustomContent() {
    Annotation annotation;
    annotation.type = AnnotationType::Square;
    annotation.rect = QRectF(10.0, 20.0, 120.0, 80.0);
    annotation.customAppearance = binaryContentStream();

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QCOMPARE(appearance.content, annotation.customAppearance);
    QVERIFY(!appearance.isEmpty());
}

void CustomAppearanceTests::appearanceUsesAnnotationRectAsBoundingBox() {
    Annotation annotation;
    annotation.type = AnnotationType::Square;
    annotation.rect = QRectF(10.0, 20.0, 120.0, 80.0);
    annotation.customAppearance = QByteArray("q\nf\nQ\n");

    const Appearance appearance = buildAnnotationAppearance(annotation);
    // 自定义内容流是按批注矩形左下角为原点写的，所以 bbox 必须就是批注矩形。
    compareRects(appearance.bbox, annotation.rect);
}

void CustomAppearanceTests::customResourcesMarkTransparency() {
    Annotation annotation;
    annotation.type = AnnotationType::Polygon;
    annotation.rect = QRectF(0.0, 0.0, 200.0, 100.0);
    annotation.vertices = squareVertices();
    annotation.customAppearance = QByteArray("q\n/GS0 gs\nf\nQ\n");
    annotation.customAppearanceResources = regionHighlightResources(0.35);

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QCOMPARE(appearance.resources, annotation.customAppearanceResources);
    QVERIFY(!appearance.resources.isEmpty());
    // 用了 ExtGState 就必须声明要透明，否则半透明会退化成不透明色块。
    QVERIFY(appearance.usesTransparency);
}

void CustomAppearanceTests::withoutCustomAppearanceFallsBackToTypeAppearance() {
    Annotation annotation;
    annotation.type = AnnotationType::Polygon;
    annotation.rect = polygonBounds(squareVertices());
    annotation.vertices = squareVertices();
    annotation.color = QColor(255, 0, 0);
    // 没有自定义外观：必须走「按类型生成」那条路，产出多边形的描边外观。
    QVERIFY(annotation.customAppearance.isEmpty());

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QVERIFY(!appearance.content.isEmpty());
    // 描边特征：RG（描边色）+ s（闭合路径）。
    QVERIFY(appearance.content.contains(" RG"));
    QVERIFY(appearance.content.contains("\ns\n"));
    // 多边形默认不透明。
    QVERIFY(!appearance.usesTransparency);
    QVERIFY(appearance.resources.isEmpty());
}

void CustomAppearanceTests::calloutAppearanceMatchesGeneratedStream() {
    CalloutSpec spec;
    spec.box = QRectF(100.0, 200.0, 120.0, 80.0);
    spec.tip = QPointF(60.0, 120.0);
    spec.knee = spec.tip;
    spec.color = QColor(226, 62, 62);

    QString error;
    const QByteArray stream = buildCalloutAppearanceStreamExtended(spec, &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));

    Annotation annotation;
    annotation.type = AnnotationType::Square;
    annotation.rect = calloutBounds(spec);
    annotation.customAppearance = stream;

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QCOMPARE(appearance.content, stream);
    compareRects(appearance.bbox, annotation.rect);
}

void CustomAppearanceTests::regionHighlightAppearanceMatchesGeneratedStream() {
    RegionHighlightSpec spec;
    spec.polygon = squareVertices();
    spec.opacity = 0.35;
    spec.color = QColor(255, 214, 0);

    QString error;
    const QByteArray stream = buildRegionHighlightAppearanceStream(spec, &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));

    Annotation annotation;
    annotation.type = AnnotationType::Polygon;
    annotation.rect = polygonBounds(spec.polygon);
    annotation.vertices = spec.polygon;
    annotation.opacity = spec.opacity;
    annotation.customAppearance = stream;
    annotation.customAppearanceResources = regionHighlightResources(spec.opacity);

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QCOMPARE(appearance.content, stream);
    // 资源片段里必须有 ExtGState，否则 /GS0 gs 不生效，半透明会变成不透明色块。
    QVERIFY(appearance.resources.contains("ExtGState"));
    QVERIFY(appearance.usesTransparency);
}

void CustomAppearanceTests::emptyContentWithResourcesFallsBackToTypeAppearance() {
    // 这里明确选的行为：**回退到按类型生成**。
    //
    // 理由：内容流是外观的全部，资源片段只是「内容流要用的东西」。
    // 没有内容流时那份资源没有依附对象，硬拼上去会写出一个引用了
    // /GS0 却没有 /GS0 gs 的内容流，阅读器要么报错要么忽略 ——
    // 比老老实实画一个普通多边形更糟。
    Annotation annotation;
    annotation.type = AnnotationType::Polygon;
    annotation.rect = polygonBounds(squareVertices());
    annotation.vertices = squareVertices();
    annotation.customAppearance = QByteArray();
    annotation.customAppearanceResources = regionHighlightResources(0.35);

    const Appearance appearance = buildAnnotationAppearance(annotation);
    // 走的是按类型生成那条路。
    QVERIFY(!appearance.content.isEmpty());
    QVERIFY(appearance.content.contains(" RG"));
    // 自带的资源片段被丢弃，不会混进按类型生成的外观里。
    QVERIFY(!appearance.resources.contains("ExtGState"));
}

void CustomAppearanceTests::calloutBoundsCoverTheArrow() {
    CalloutSpec spec;
    spec.box = QRectF(100.0, 200.0, 120.0, 80.0);
    spec.tip = QPointF(40.0, 90.0);
    spec.knee = spec.tip;

    const QRectF bounds = calloutBounds(spec);
    // 箭头尖在框外，/Rect 必须把它包进去，否则外观流的 BBox 会把箭头裁掉半截。
    QVERIFY(bounds.contains(spec.tip));
    QVERIFY(bounds.width() > spec.box.width());
    QVERIFY(bounds.height() > spec.box.height());
    // 注释框本身也还在里面。
    QVERIFY(bounds.contains(spec.box.topLeft()));
    QVERIFY(bounds.contains(spec.box.bottomRight()));
}

void CustomAppearanceTests::selfIntersectingPolygonIsRejected() {
    QVERIFY(polygonIsSelfIntersecting(bowtieVertices()));
    // 界面层就是靠这个判定拒绝用户画歪的区域。
    RegionHighlightSpec spec;
    spec.polygon = bowtieVertices();
    QString error;
    const QByteArray stream = buildRegionHighlightAppearanceStream(spec, &error);
    QVERIFY(stream.isEmpty());
    QVERIFY(!error.isEmpty());
    // 普通多边形不该被误判。
    QVERIFY(!polygonIsSelfIntersecting(squareVertices()));
}

QTEST_GUILESS_MAIN(CustomAppearanceTests)
#include "CustomAppearanceTests.moc"
