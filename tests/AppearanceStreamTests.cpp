// 批注外观流生成器的单元测试。
//
// 这里只测纯函数：给定批注数据，产出的 content stream 是否满足三条不变量——
//   1) 内容全部落在 BBox 内（越界会被阅读器裁掉，表现为"批注少了半条"）；
//   2) 该透明的透明（缺 ExtGState 就会变成实心块，盖住正文）；
//   3) 该描边的描边（下划线/删除线用填充画会变成色块）。
//
// 端到端验证（补进 PDF 后 PDFium 真的渲染出来）在 1.3.0 的写入层落地时补上，
// 届时用真实文档跑，不再依赖测试里的临时补丁器。

#include "pdf/Annotation.h"
#include "pdf/AppearanceStream.h"

#include <QTest>

#include <cmath>

using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::Appearance;
using aipdf::pdf::appearanceLineWidth;
using aipdf::pdf::appearanceOpacity;
using aipdf::pdf::buildAnnotationAppearance;
using aipdf::pdf::defaultAnnotationColor;

namespace {

Annotation makeHighlight() {
    Annotation annotation;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(60.0, 182.0, 200.0, 18.0);
    annotation.quads = { QRectF(60.0, 182.0, 200.0, 9.0), QRectF(60.0, 191.0, 150.0, 9.0) };
    annotation.color = QColor(255, 214, 0);
    return annotation;
}

// 从 content 里取出所有 "x y w h re" 的矩形。
QVector<QRectF> parseRects(const QByteArray& content) {
    QVector<QRectF> rects;
    const QStringList lines = QString::fromLatin1(content).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QStringList parts = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() == 5 && parts.at(4) == QLatin1String("re")) {
            rects.append(QRectF(parts.at(0).toDouble(), parts.at(1).toDouble(),
                                parts.at(2).toDouble(), parts.at(3).toDouble()));
        }
    }
    return rects;
}

bool insideBbox(const QRectF& rect, const QRectF& bbox, double tolerance = 0.01) {
    return rect.left() >= bbox.left() - tolerance && rect.top() >= bbox.top() - tolerance &&
           rect.right() <= bbox.right() + tolerance && rect.bottom() <= bbox.bottom() + tolerance;
}

}  // namespace

class AppearanceStreamTests : public QObject {
    Q_OBJECT

private slots:
    void highlightFillsEveryQuad();
    void highlightIsTranslucent();
    void underlineStrokesInsteadOfFilling();
    void strikeOutDrawsThroughTheMiddle();
    void squareInsetsTheStroke();
    void inkDrawsEveryStroke();
    void noteHasNoAppearance();
    void contentNeverEscapesBbox();
    void emptyRectIsRejected();
    void invalidColorFallsBackToDefault();
    void opacityAndLineWidthAreSharedPerType();
};

void AppearanceStreamTests::highlightFillsEveryQuad() {
    const Appearance appearance = buildAnnotationAppearance(makeHighlight());

    QVERIFY(!appearance.isEmpty());
    const QVector<QRectF> rects = parseRects(appearance.content);
    QCOMPARE(rects.size(), 2);
    // 两个矩形都要出现（按行着色），第二行比第一行短。
    QCOMPARE(rects.at(0).width(), 200.0);
    QCOMPARE(rects.at(1).width(), 150.0);
    QVERIFY(appearance.content.contains("f\n"));  // 用填充，不是描边
}

void AppearanceStreamTests::highlightIsTranslucent() {
    const Appearance appearance = buildAnnotationAppearance(makeHighlight());

    QVERIFY(appearance.usesTransparency);
    QVERIFY(appearance.resources.contains("/ca"));
    // 没有 /GS0 gs，Resources 写了也白写。
    QVERIFY(appearance.content.contains("/GS0 gs"));
    QCOMPARE(appearanceOpacity(AnnotationType::Highlight), 0.40);
}

void AppearanceStreamTests::underlineStrokesInsteadOfFilling() {
    Annotation annotation = makeHighlight();
    annotation.type = AnnotationType::Underline;
    annotation.quads = { QRectF(60.0, 182.0, 200.0, 18.0) };

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QVERIFY(!appearance.usesTransparency);  // 线条是实心的
    QVERIFY(appearance.content.contains("S\n"));
    QVERIFY(!appearance.content.contains("f\n"));
    QVERIFY(appearance.content.contains("1.2 w"));
}

void AppearanceStreamTests::strikeOutDrawsThroughTheMiddle() {
    Annotation annotation = makeHighlight();
    annotation.type = AnnotationType::StrikeOut;
    annotation.quads = { QRectF(60.0, 182.0, 200.0, 20.0) };

    const Appearance appearance = buildAnnotationAppearance(annotation);
    const QString text = QString::fromLatin1(appearance.content);
    // 删除线的 y 应当在矩形中部（20 高 → y=10），而不是贴底。
    QVERIFY(text.contains("0 10 m"));
}

void AppearanceStreamTests::squareInsetsTheStroke() {
    Annotation annotation;
    annotation.type = AnnotationType::Square;
    annotation.rect = QRectF(100.0, 100.0, 80.0, 40.0);
    annotation.color = QColor(220, 40, 40);

    const Appearance appearance = buildAnnotationAppearance(annotation);
    const QVector<QRectF> rects = parseRects(appearance.content);
    QCOMPARE(rects.size(), 1);
    // 半线宽内缩，否则边框会有一半被 BBox 裁掉。
    const double inset = appearanceLineWidth(AnnotationType::Square) * 0.5;
    QCOMPARE(rects.first().left(), inset);
    QCOMPARE(rects.first().width(), 80.0 - inset * 2);
    QVERIFY(appearance.content.contains("S\n"));
}

void AppearanceStreamTests::inkDrawsEveryStroke() {
    Annotation annotation;
    annotation.type = AnnotationType::Ink;
    annotation.rect = QRectF(10.0, 10.0, 100.0, 50.0);
    annotation.strokes = {
        { QPointF(20.0, 20.0), QPointF(40.0, 45.0), QPointF(60.0, 25.0) },
        { QPointF(70.0, 30.0), QPointF(100.0, 55.0) },
    };
    annotation.color = QColor(20, 20, 20);

    const Appearance appearance = buildAnnotationAppearance(annotation);
    const QString text = QString::fromLatin1(appearance.content);
    QCOMPARE(text.count(QLatin1String(" m\n")), 2);   // 两条笔画各一个起点
    QCOMPARE(text.count(QLatin1String(" l\n")), 3);   // 3 + 1 个点，减去两个起点
    QCOMPARE(text.count(QLatin1String("S\n")), 2);    // 每条笔画单独描边
}

void AppearanceStreamTests::noteHasNoAppearance() {
    Annotation annotation;
    annotation.type = AnnotationType::Note;
    annotation.rect = QRectF(0.0, 0.0, 20.0, 20.0);

    const Appearance appearance = buildAnnotationAppearance(annotation);
    QVERIFY(appearance.isEmpty());
    QCOMPARE(appearanceOpacity(AnnotationType::Note), 0.0);
}

void AppearanceStreamTests::contentNeverEscapesBbox() {
    Annotation annotation = makeHighlight();
    // 故意让第二行伸出矩形右边，模拟四角点与包围盒对不上的脏数据。
    annotation.quads = { QRectF(60.0, 182.0, 200.0, 9.0), QRectF(200.0, 191.0, 100.0, 9.0) };

    const Appearance appearance = buildAnnotationAppearance(annotation);
    const QRectF local(0.0, 0.0, appearance.bbox.width(), appearance.bbox.height());
    for (const QRectF& rect : parseRects(appearance.content)) {
        QVERIFY2(insideBbox(rect, local),
                 qPrintable(QStringLiteral("矩形 %1,%2 %3x%4 越界")
                                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height())));
    }
}

void AppearanceStreamTests::emptyRectIsRejected() {
    Annotation annotation;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF();
    QVERIFY(buildAnnotationAppearance(annotation).isEmpty());

    annotation.rect = QRectF(10.0, 10.0, 0.0, 30.0);
    QVERIFY(buildAnnotationAppearance(annotation).isEmpty());
}

void AppearanceStreamTests::invalidColorFallsBackToDefault() {
    Annotation annotation = makeHighlight();
    annotation.color = QColor();  // 无效颜色
    const Appearance appearance = buildAnnotationAppearance(annotation);
    const QColor fallback = defaultAnnotationColor(AnnotationType::Highlight);
    QVERIFY(!appearance.isEmpty());

    // content 里的颜色是 "r g b rg"，小数位被裁剪过，所以按数值比，不按字符串比。
    const QString text = QString::fromLatin1(appearance.content);
    const QStringList tokens = text.split(QLatin1Char('\n')).first().split(QLatin1Char(' '));
    Q_UNUSED(tokens);
    const int rgAt = text.indexOf(QLatin1String(" rg"));
    QVERIFY(rgAt > 0);
    const QString colorLine = text.left(rgAt).split(QLatin1Char('\n')).last();
    const QStringList parts = colorLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QCOMPARE(parts.size(), 3);
    QVERIFY(std::abs(parts.at(0).toDouble() - fallback.redF()) < 0.002);
    QVERIFY(std::abs(parts.at(1).toDouble() - fallback.greenF()) < 0.002);
    QVERIFY(std::abs(parts.at(2).toDouble() - fallback.blueF()) < 0.002);
}

void AppearanceStreamTests::opacityAndLineWidthAreSharedPerType() {
    // 屏幕绘制与外观流必须读同一组常量，否则"屏幕一个样、别人打开又一个样"。
    QCOMPARE(appearanceOpacity(AnnotationType::Underline), 1.0);
    QCOMPARE(appearanceOpacity(AnnotationType::Square), 1.0);
    QCOMPARE(appearanceLineWidth(AnnotationType::Underline), 1.2);
    QCOMPARE(appearanceLineWidth(AnnotationType::Ink), 1.5);
    QVERIFY(appearanceLineWidth(AnnotationType::Highlight) > 0.0);
}

QTEST_MAIN(AppearanceStreamTests)
#include "AppearanceStreamTests.moc"
