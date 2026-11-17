// 注释形状（callout 箭头框 + 区域高亮）的单元测试。
//
// 这一层测的是纯函数：给定几何，算出箭头折线与外观流内容。
// 三条不变量：
//   1) 箭头折线必须落在**批注自身坐标系**里（原点是批注矩形左下角）——
//      拿页面坐标直接去画是批注外观最容易踩的坑；
//   2) 生成失败必须返回空 + 中文原因，绝不交出半截流；
//   3) 两种外观流都要能被 qpdf 重新解析（写进文件里结构不能坏）。
//
// 端到端验证（补进 PDF 后 PDFium 真的渲染出来）由写入层的任务负责，
// 这里不重复。

#include "pdf/AnnotationShapes.h"

#include <QTest>

#include <cmath>
#include <limits>
#include <vector>

#if AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::CalloutSpec;
using aipdf::pdf::RegionHighlightSpec;
using aipdf::pdf::buildCalloutAppearanceStream;
using aipdf::pdf::buildCalloutAppearanceStreamExtended;
using aipdf::pdf::buildRegionHighlightAppearanceStream;
using aipdf::pdf::calloutArrowPoints;
using aipdf::pdf::calloutBounds;
using aipdf::pdf::polygonBounds;
using aipdf::pdf::polygonIsSelfIntersecting;
using aipdf::pdf::regionHighlightResources;

namespace {

// 注释框在页面上的位置：x 100..220，y 200..280（PDF user space，y 向上）。
QRectF testBox() {
    return QRectF(100.0, 200.0, 120.0, 80.0);
}

// 直箭头：折点与箭头尖重合。
CalloutSpec straightArrow() {
    CalloutSpec spec;
    spec.box = testBox();
    spec.tip = QPointF(160.0, 210.0);
    spec.knee = spec.tip;
    return spec;
}

// 折线箭头：带一个折点。
CalloutSpec bentArrow() {
    CalloutSpec spec;
    spec.box = testBox();
    spec.knee = QPointF(130.0, 250.0);
    spec.tip = QPointF(180.0, 215.0);
    return spec;
}

#if AIPDF_WITH_QPDF

// 把一段外观流塞进一个最小 PDF（一个带 /AP 的批注）。
//
// 为什么手工拼字节而不用 qpdf 生成：qpdf 从零造文件要自己搭页树，
// 很容易踩到"根对象没写成间接对象"这类坑；这里按 PDF 语法直接拼，
// xref 偏移自己算，qpdf 读的时候不会产生任何警告。
//
// 为什么真的写一遍文件再读回来：内容流写坏了 PDFium 未必报错，
// 只有让 qpdf 重新解析整个文件，才算证明结构没问题。
QByteArray buildPdfWithAppearance(const QByteArray& content, double width, double height) {
    const QString size = QString::number(width, 'f', 3) + ' ' + QString::number(height, 'f', 3);

    QVector<QByteArray> bodies;
    bodies << "<< /Type /Catalog /Pages 2 0 R >>";
    bodies << "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    bodies << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> "
             "/Annots [4 0 R] >>";
    bodies << QByteArray("<< /Type /Annot /Subtype /Square /Rect [0 0 ") + size.toLatin1() +
               "] /AP << /N 5 0 R >> >>";
    bodies << QByteArray("<< /Type /XObject /Subtype /Form /BBox [0 0 ") + size.toLatin1() +
               "] /Resources << >> /Length " + QByteArray::number(content.size()) +
               " >>\nstream\n" + content + "\nendstream";

    QByteArray pdf = "%PDF-1.7\n";
    QVector<qint64> offsets;
    offsets.reserve(bodies.size());
    for (int i = 0; i < bodies.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n";
        pdf += bodies.at(i);
        pdf += "\nendobj\n";
    }

    const qint64 xrefOffset = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(bodies.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (qint64 offset : offsets) {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(bodies.size() + 1) + " /Root 1 0 R >>\n";
    pdf += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return pdf;
}

// 读回刚才写出的 PDF：确认能解析、没有警告、并且 /AP /N 的内容流还在。
bool qpdfReadsAppearance(const QByteArray& content, double width, double height) {
    try {
        const QByteArray bytes = buildPdfWithAppearance(content, width, height);
        if (bytes.isEmpty()) {
            return false;
        }
        QPDF document;
        document.processMemoryFile("appearance.pdf", bytes.constData(),
                                    static_cast<size_t>(bytes.size()));
        if (document.getPDFVersion().empty()) {
            return false;
        }
        if (!document.getWarnings().empty()) {
            return false;
        }
        QPDFObjectHandle pages = document.getRoot().getKey("/Pages");
        const std::vector<QPDFObjectHandle> kids = pages.getKey("/Kids").getArrayAsVector();
        if (kids.empty()) {
            return false;
        }
        const std::vector<QPDFObjectHandle> annotations =
            kids.front().getKey("/Annots").getArrayAsVector();
        if (annotations.empty()) {
            return false;
        }
        QPDFObjectHandle appearance = annotations.front().getKey("/AP").getKey("/N");
        if (!appearance.isStream()) {
            return false;
        }
        const std::shared_ptr<Buffer> data = appearance.getStreamData();
        if (!data) {
            return false;
        }
        const std::string text(reinterpret_cast<const char*>(data->getBuffer()), data->getSize());
        // 内容流必须被原样读回来：多一个算子少一个算子都会让渲染结果变形。
        QString decoded = QString::fromLatin1(text.c_str(), static_cast<int>(text.size()));
        decoded.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        const QString expected = QString::fromLatin1(content);
        return decoded == expected;
    } catch (const std::exception& error) {
        qWarning("qpdf 解析外观流时抛异常：%s", error.what());
        return false;
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

class AnnotationShapesTests : public QObject {
    Q_OBJECT

private slots:
    void straightArrowHasTwoPoints();
    void bentArrowHasThreePointsInOrder();
    void arrowPointsLiveInAnnotationSpace();
    void arrowOutsideBoxIsRejected();
    void degenerateInputIsRejected();
    void calloutStreamHasPathOperators();
    void triangleBoundsAreCorrect();
    void polygonNeedsThreeVertices();
    void selfIntersectingPolygonIsRejected();
    void convexPolygonIsAccepted();
    void regionHighlightFillsWithOpacity();
    void vertexOrderDoesNotMatter();
    void calloutBoundsCoverBoxAndArrow();
    void extendedCalloutAllowsOutwardArrow();
    void qpdfReadsCalloutAppearance();
    void qpdfReadsRegionHighlightAppearance();
};

void AnnotationShapesTests::straightArrowHasTwoPoints() {
    const QVector<QPointF> points = calloutArrowPoints(straightArrow());

    // 直箭头只有两个点：引出点 + 箭头尖。
    QCOMPARE(points.size(), 2);
    // 引出点在批注框的下边界上（框中心 (160,240) 朝箭头尖 (160,210) 的射线
    // 打到下边框），换算到批注自身坐标就是 (60, 0)。
    QCOMPARE(points.at(0), QPointF(60.0, 0.0));
    QCOMPARE(points.at(1), QPointF(60.0, 10.0));
}

void AnnotationShapesTests::bentArrowHasThreePointsInOrder() {
    const CalloutSpec spec = bentArrow();
    const QVector<QPointF> points = calloutArrowPoints(spec);

    // 折线箭头三个点：引出点 → 折点 → 箭头尖，顺序不能乱。
    QCOMPARE(points.size(), 3);
    QCOMPARE(points.at(0), QPointF(0.0, 60.0));
    QCOMPARE(points.at(1), QPointF(30.0, 50.0));
    QCOMPARE(points.at(2), QPointF(80.0, 15.0));
}

void AnnotationShapesTests::arrowPointsLiveInAnnotationSpace() {
    const CalloutSpec spec = bentArrow();
    const QVector<QPointF> points = calloutArrowPoints(spec);

    QCOMPARE(points.size(), 3);
    // 批注自身坐标 = 页面坐标 - 批注矩形原点（左下角 (100,200)）。
    const QPointF origin(spec.box.x(), spec.box.y());
    QCOMPARE(points.at(1), QPointF(spec.knee.x() - origin.x(), spec.knee.y() - origin.y()));
    QCOMPARE(points.at(2), QPointF(spec.tip.x() - origin.x(), spec.tip.y() - origin.y()));
    // 不能把页面坐标原样交出去 —— 那会让整条箭头错位一个批注矩形。
    QVERIFY(points.at(1) != spec.knee);
    QVERIFY(points.at(2) != spec.tip);
}

void AnnotationShapesTests::arrowOutsideBoxIsRejected() {
    CalloutSpec spec = straightArrow();
    // 箭头尖完全落在批注框外：出框的箭头会被外观流的 BBox 裁成半截。
    spec.tip = QPointF(400.0, 500.0);
    spec.knee = spec.tip;

    QString error;
    const QByteArray stream = buildCalloutAppearanceStream(spec, &error);
    QVERIFY2(stream.isEmpty(), "出框的箭头不该生成外观流");
    QVERIFY2(!error.isEmpty(), "失败必须给出中文原因");
    QVERIFY(error.contains(QStringLiteral("批注框")));
}

void AnnotationShapesTests::degenerateInputIsRejected() {
    QString error;

    // 宽为 0 的批注框。
    CalloutSpec zeroWidth = straightArrow();
    zeroWidth.box = QRectF(100.0, 200.0, 0.0, 80.0);
    QVERIFY(buildCalloutAppearanceStream(zeroWidth, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 高为 0 的批注框。
    CalloutSpec zeroHeight = straightArrow();
    zeroHeight.box = QRectF(100.0, 200.0, 120.0, 0.0);
    QVERIFY(buildCalloutAppearanceStream(zeroHeight, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // NaN 与无穷的坐标。
    CalloutSpec nanTip = straightArrow();
    nanTip.tip = QPointF(std::numeric_limits<double>::quiet_NaN(), 210.0);
    nanTip.knee = nanTip.tip;
    QVERIFY(buildCalloutAppearanceStream(nanTip, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    CalloutSpec infiniteKnee = bentArrow();
    infiniteKnee.knee = QPointF(130.0, std::numeric_limits<double>::infinity());
    QVERIFY(buildCalloutAppearanceStream(infiniteKnee, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 退化输入不能崩，折线点数也必须为空。
    QVERIFY(calloutArrowPoints(nanTip).isEmpty());
}

void AnnotationShapesTests::calloutStreamHasPathOperators() {
    QString error;
    const QByteArray stream = buildCalloutAppearanceStream(bentArrow(), &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));

    // 路径算子：起笔 m、连线 l、描边 S。
    QVERIFY(stream.contains(" m\n"));
    QVERIFY(stream.contains(" l\n"));
    QVERIFY(stream.contains("S\n"));
    // 图形状态与变换要有配对。
    QVERIFY(stream.contains("q\n"));
    QVERIFY(stream.contains("Q\n"));
    // 裁剪：先设裁剪区再画。
    QVERIFY(stream.contains("W n"));
}

void AnnotationShapesTests::triangleBoundsAreCorrect() {
    const QVector<QPointF> triangle = {
        QPointF(10.0, 20.0), QPointF(40.0, 20.0), QPointF(25.0, 50.0)
    };
    const QRectF bounds = polygonBounds(triangle);

    QCOMPARE(bounds.x(), 10.0);
    QCOMPARE(bounds.y(), 20.0);
    QCOMPARE(bounds.width(), 30.0);
    QCOMPARE(bounds.height(), 30.0);
}

void AnnotationShapesTests::polygonNeedsThreeVertices() {
    RegionHighlightSpec spec;
    spec.polygon = { QPointF(10.0, 20.0), QPointF(40.0, 20.0) };

    QString error;
    QVERIFY(buildRegionHighlightAppearanceStream(spec, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void AnnotationShapesTests::selfIntersectingPolygonIsRejected() {
    // 蝴蝶结：两条对角线交叉，填充会翻车。
    const QVector<QPointF> bowtie = {
        QPointF(0.0, 0.0), QPointF(10.0, 10.0), QPointF(0.0, 10.0), QPointF(10.0, 0.0)
    };
    QVERIFY(polygonIsSelfIntersecting(bowtie));

    RegionHighlightSpec spec;
    spec.polygon = bowtie;
    QString error;
    QVERIFY(buildRegionHighlightAppearanceStream(spec, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void AnnotationShapesTests::convexPolygonIsAccepted() {
    const QVector<QPointF> square = {
        QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 10.0), QPointF(0.0, 10.0)
    };
    QVERIFY(!polygonIsSelfIntersecting(square));

    RegionHighlightSpec spec;
    spec.polygon = square;
    QString error;
    const QByteArray stream = buildRegionHighlightAppearanceStream(spec, &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));
}

void AnnotationShapesTests::regionHighlightFillsWithOpacity() {
    RegionHighlightSpec spec;
    spec.polygon = { QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 10.0) };
    spec.opacity = 0.35;

    QString error;
    const QByteArray stream = buildRegionHighlightAppearanceStream(spec, &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));

    // 用填充（f）而不是描边，并且要引用半透明的 ExtGState。
    QVERIFY(stream.contains("f\n"));
    QVERIFY(stream.contains("/GS0 gs"));
    QVERIFY(!stream.contains("S\n"));

    // 不透明度真正的落点是 /Resources 里的 ExtGState。
    const QByteArray resources = regionHighlightResources(spec.opacity);
    QVERIFY(resources.contains("/ExtGState"));
    QVERIFY(resources.contains("/GS0"));
    QVERIFY(resources.contains("/ca 0.35"));
}

void AnnotationShapesTests::vertexOrderDoesNotMatter() {
    RegionHighlightSpec clockwise;
    clockwise.polygon = { QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 10.0),
                          QPointF(0.0, 10.0) };
    RegionHighlightSpec counterClockwise;
    counterClockwise.polygon = { QPointF(0.0, 10.0), QPointF(10.0, 10.0), QPointF(10.0, 0.0),
                                 QPointF(0.0, 0.0) };

    QString error;
    const QByteArray first = buildRegionHighlightAppearanceStream(clockwise, &error);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    const QByteArray second = buildRegionHighlightAppearanceStream(counterClockwise, &error);
    QVERIFY2(!second.isEmpty(), qPrintable(error));

    // 两种绕向都要生成得出高亮（填充按非零环绕规则，与绕向无关）。
    QVERIFY(first.contains("f\n"));
    QVERIFY(second.contains("f\n"));
}

void AnnotationShapesTests::calloutBoundsCoverBoxAndArrow() {
    CalloutSpec spec = straightArrow();
    spec.tip = QPointF(400.0, 500.0);
    spec.knee = spec.tip;

    const QRectF bounds = calloutBounds(spec);
    // 批注矩形必须同时罩住注释框与箭头尖，否则出框的箭头会被裁掉。
    QVERIFY(bounds.contains(spec.box));
    QVERIFY(bounds.contains(spec.tip));
    // 精确值：注释框 (100,200)-(220,280) 与箭头尖 (400,500) 的并集。
    QCOMPARE(bounds.x(), 100.0);
    QCOMPARE(bounds.y(), 200.0);
    QCOMPARE(bounds.width(), 300.0);
    QCOMPARE(bounds.height(), 300.0);
}

void AnnotationShapesTests::extendedCalloutAllowsOutwardArrow() {
    CalloutSpec spec = straightArrow();
    spec.tip = QPointF(400.0, 500.0);
    spec.knee = spec.tip;

    // 严格版：箭头出框 → 失败。
    QString error;
    QVERIFY(buildCalloutAppearanceStream(spec, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 扩展版：坐标原点变成"注释框 ∪ 箭头"的并集，箭头可以指向框外正文。
    const QByteArray stream = buildCalloutAppearanceStreamExtended(spec, &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));
    QVERIFY(stream.contains(" m\n"));
    QVERIFY(stream.contains("S\n"));
}

void AnnotationShapesTests::qpdfReadsCalloutAppearance() {
#if AIPDF_WITH_QPDF
    QString error;
    const QByteArray stream = buildCalloutAppearanceStream(bentArrow(), &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));

    QVERIFY2(qpdfReadsAppearance(stream, 120.0, 80.0), "qpdf 没能读回 callout 外观流");
#else
    QSKIP("没有 qpdf，跳过结构校验");
#endif
}

void AnnotationShapesTests::qpdfReadsRegionHighlightAppearance() {
#if AIPDF_WITH_QPDF
    RegionHighlightSpec spec;
    spec.polygon = { QPointF(0.0, 0.0), QPointF(20.0, 0.0), QPointF(20.0, 20.0),
                     QPointF(0.0, 20.0) };
    QString error;
    const QByteArray stream = buildRegionHighlightAppearanceStream(spec, &error);
    QVERIFY2(!stream.isEmpty(), qPrintable(error));

    QVERIFY2(qpdfReadsAppearance(stream, 20.0, 20.0), "qpdf 没能读回区域高亮外观流");
#else
    QSKIP("没有 qpdf，跳过结构校验");
#endif
}

QTEST_GUILESS_MAIN(AnnotationShapesTests)
#include "AnnotationShapesTests.moc"
