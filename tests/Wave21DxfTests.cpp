#include "pdf/convert/DxfExport.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVector>

using aipdf::pdf::CadDxfOptions;
using aipdf::pdf::CadDxfResult;

namespace {

// ============================================================ 夹具 PDF ======

// 造一份带矢量图形的 PDF（每页一份内容流）。
//
// 为什么要自己造：现成夹具都是纯文字的，没有曲线、箭头与颜色，
// 「曲线有没有保住」「标注有没有分层」这些事用它们验不了。
//
// 内容流里画了这些东西（页面 200×100）：
//   - 一条三次贝塞尔曲线（红色、3 点宽）；
//   - 一条直线（蓝色、2 点宽）；
//   - 一条三顶点折线；
//   - 一条尺寸标注：尺寸线 + 两个箭头三角形 + 数值「100」；
//   - 两行文字：「100」（标注数值）与「Hello」（普通文字）；
//   - 一张 2×2 的内嵌位图。
QByteArray buildDxfPdf(const QVector<QByteArray>& contents) {
    const int pageCount = contents.size();
    QVector<QByteArray> bodies;
    QVector<int> pageIds;
    for (int i = 0; i < pageCount; ++i) {
        pageIds.append(3 + 2 * i);
    }
    const int fontId = 3 + 2 * pageCount;
    const int imageId = 4 + 2 * pageCount;

    bodies.append("<< /Type /Catalog /Pages 2 0 R >>");
    QString kids;
    for (int i = 0; i < pageIds.size(); ++i) {
        if (i > 0) kids += QLatin1Char(' ');
        kids += QByteArray::number(pageIds.at(i)) + " 0 R";
    }
    bodies.append("<< /Type /Pages /Kids [" + kids.toUtf8() + "] /Count " +
                  QByteArray::number(pageCount) + " >>");
    for (int i = 0; i < pageCount; ++i) {
        const int contentId = 4 + 2 * i;
        bodies.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources << /Font "
                      "<< /F1 " +
                      QByteArray::number(fontId) + " 0 R >> /XObject << /Im0 " +
                      QByteArray::number(imageId) + " 0 R >> >> /Contents " +
                      QByteArray::number(contentId) + " 0 R >>");
        bodies.append("<< /Length " + QByteArray::number(contents.at(i).size()) +
                      " >>\nstream\n" + contents.at(i) + "\nendstream");
    }
    bodies.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    const QByteArray pixels = QByteArray::fromHex("FF000000FF00000000FFFFFF");
    bodies.append("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB "
                  "/BitsPerComponent 8 /Length " +
                  QByteArray::number(pixels.size()) + " >>\nstream\n" + pixels + "\nendstream");

    QByteArray out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QVector<int> offsets;
    for (int i = 0; i < bodies.size(); ++i) {
        offsets.append(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + bodies.at(i) + "\nendobj\n";
    }
    const int xref = out.size();
    out += "xref\n0 " + QByteArray::number(bodies.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (const int offset : offsets) {
        out += QByteArray::number(offset, 10).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(bodies.size() + 1) + " /Root 1 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}

// 第一页的内容流（曲线 + 直线 + 折线 + 尺寸标注 + 文字 + 图片）。
QByteArray firstPageContent() {
    return QByteArray(
        "% 三次贝塞尔曲线（红色、3 点宽）\n"
        "3 w\n"
        "1 0 0 RG\n"
        "100 20 m\n"
        "120 60 160 60 180 20 c\n"
        "S\n"
        "% 直线（蓝色、2 点宽）\n"
        "2 w\n"
        "0 0 1 RG\n"
        "10 10 m\n"
        "50 10 l\n"
        "S\n"
        "% 三顶点折线\n"
        "10 40 m\n"
        "30 40 l\n"
        "50 60 l\n"
        "S\n"
        "% 尺寸标注：尺寸线 + 两个箭头 + 数值\n"
        "1 w\n"
        "0 0 0 RG\n"
        "60 80 m\n"
        "100 80 l\n"
        "S\n"
        "60 80 m\n"
        "64 82 l\n"
        "64 78 l\n"
        "h\n"
        "f\n"
        "100 80 m\n"
        "96 82 l\n"
        "96 78 l\n"
        "h\n"
        "f\n"
        "% 文字：标注数值与普通文字\n"
        "BT /F1 10 Tf 76 82 Td (100) Tj ET\n"
        "BT /F1 10 Tf 10 90 Td (Hello) Tj ET\n"
        "% 内嵌位图\n"
        "q 20 0 0 10 120 40 cm /Im0 Do Q\n");
}

// 第二页的内容流（一条直线，用来验多页竖着摞）。
QByteArray secondPageContent() {
    return QByteArray("2 w\n0 0 1 RG\n10 10 m\n50 10 l\nS\n");
}

// ============================================================ DXF 解析 ======

struct DxfRecord {
    int code = 0;
    QString value;
};

// DXF 是「一行组码、一行值」成对的文本格式。
QVector<DxfRecord> parseDxfRecords(const QByteArray& dxf) {
    QVector<DxfRecord> records;
    const QList<QByteArray> lines = dxf.split('\n');
    for (int i = 0; i + 1 < lines.size(); i += 2) {
        bool ok = false;
        const int code = lines.at(i).trimmed().toInt(&ok);
        if (!ok) continue;
        records.append(DxfRecord{code, QString::fromLatin1(lines.at(i + 1)).trimmed()});
    }
    return records;
}

struct DxfEntityView {
    QString type;
    QString layer;
    QString text;
    QVector<QPointF> points;   // 组码 10/20 与 11/21
    QVector<double> group40;   // 组码 40：文字高度或样条节点
    int flags = 0;             // 组码 70
    int colorIndex = -1;       // 组码 62
    int trueColor = -1;        // 组码 420
    int lineWeight = -1;       // 组码 370
    int degree = -1;           // 组码 71
    int knotCount = -1;        // 组码 72
    int controlPointCount = -1;  // 组码 73
};

QVector<DxfEntityView> dxfEntities(const QByteArray& dxf) {
    const QVector<DxfRecord> records = parseDxfRecords(dxf);
    QVector<DxfEntityView> entities;
    bool inEntities = false;
    bool pendingSection = false;
    DxfEntityView current;
    bool haveEntity = false;
    double lastX = 0.0;
    for (const DxfRecord& record : records) {
        if (record.code == 0) {
            if (record.value == QStringLiteral("SECTION")) {
                pendingSection = true;
                continue;
            }
            if (record.value == QStringLiteral("ENDSEC")) {
                inEntities = false;
                continue;
            }
            if (record.value == QStringLiteral("EOF")) break;
            if (pendingSection) {
                pendingSection = false;
                continue;
            }
            if (inEntities) {
                if (haveEntity) entities.append(current);
                current = DxfEntityView();
                current.type = record.value;
                haveEntity = true;
            }
            continue;
        }
        if (pendingSection && record.code == 2) {
            pendingSection = false;
            inEntities = (record.value == QStringLiteral("ENTITIES"));
            continue;
        }
        if (!inEntities || !haveEntity) continue;
        switch (record.code) {
        case 8: current.layer = record.value; break;
        case 1: current.text = record.value; break;
        case 10:
        case 11: lastX = record.value.toDouble(); break;
        case 20:
            current.points.append(QPointF(lastX, record.value.toDouble()));
            break;
        case 21:
            current.points.append(QPointF(lastX, record.value.toDouble()));
            break;
        case 40: current.group40.append(record.value.toDouble()); break;
        case 70: current.flags = record.value.toInt(); break;
        case 62: current.colorIndex = record.value.toInt(); break;
        case 420: current.trueColor = record.value.toInt(); break;
        case 370: current.lineWeight = record.value.toInt(); break;
        case 71: current.degree = record.value.toInt(); break;
        case 72: current.knotCount = record.value.toInt(); break;
        case 73: current.controlPointCount = record.value.toInt(); break;
        default: break;
        }
    }
    if (haveEntity) entities.append(current);
    return entities;
}

// LAYER 表里的图层名。
QStringList dxfLayerNames(const QByteArray& dxf) {
    const QVector<DxfRecord> records = parseDxfRecords(dxf);
    QStringList names;
    QString tableName;
    bool pendingTableName = false;
    bool inLayerTable = false;
    for (const DxfRecord& record : records) {
        if (record.code == 0) {
            if (record.value == QStringLiteral("TABLE")) {
                pendingTableName = true;
                inLayerTable = false;
                continue;
            }
            if (record.value == QStringLiteral("ENDTAB")) {
                inLayerTable = false;
                pendingTableName = false;
                continue;
            }
            if (record.value == QStringLiteral("LAYER") && tableName == QStringLiteral("LAYER")) {
                inLayerTable = true;
            }
            continue;
        }
        if (pendingTableName && record.code == 2) {
            tableName = record.value;
            pendingTableName = false;
            continue;
        }
        if (inLayerTable && record.code == 2) names.append(record.value);
    }
    return names;
}

// HEADER 段里某个变量的第一个数值（$EXTMAX 之类）。
double dxfHeaderNumber(const QByteArray& dxf, const QByteArray& variable) {
    const int at = dxf.indexOf(variable);
    if (at < 0) return 0.0;
    const QList<QByteArray> lines = dxf.split('\n');
    for (int i = 0; i + 1 < lines.size(); ++i) {
        if (lines.at(i).trimmed() != "10") continue;
        if (i < 1 || !lines.at(i - 1).contains(variable)) continue;
        return lines.at(i + 1).trimmed().toDouble();
    }
    return 0.0;
}

bool nearlyEqual(double a, double b, double tolerance = 0.01) {
    return qAbs(a - b) <= tolerance;
}

}  // namespace

class Wave21DxfTests : public QObject {
    Q_OBJECT

private slots:
    // 曲线
    void cubicCurveBecomesSplineEntity();
    void splineKeepsControlPointsExactly();
    // 图层
    void layerTableListsPathTextAndDim();
    void everyEntityBelongsToALayerInTheTable();
    // 颜色与线宽
    void strokeColorAndLineWidthAreWritten();
    // 尺寸标注
    void dimensionIsDetectedAndPutOnItsOwnLayer();
    void dimensionMeasurementTextIsNotDuplicatedAsPlainText();
    // 文字
    void plainTextGoesToTextLayer();
    // 几何口径
    void scaleMultipliesCoordinates();
    void multiplePagesAreStackedVertically();
    // 杂项
    void imagesAreCountedButNotExported();
    void headerCarriesR2000VersionAndExtents();
    void realFixtureExportsWithoutCrashing();
    void invalidInputIsRejected();
    void exportedFileIsWrittenForCadReaders();
};

void Wave21DxfTests::cubicCurveBecomesSplineEntity() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    CadDxfOptions options;
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.splineCount, 1);
    QVERIFY2(result.dxf.contains("\nSPLINE\n"), result.dxf.constData());

    const QVector<DxfEntityView> entities = dxfEntities(result.dxf);
    int splineCount = 0;
    for (const DxfEntityView& entity : entities) {
        if (entity.type != QStringLiteral("SPLINE")) continue;
        ++splineCount;
        QCOMPARE(entity.degree, 3);
        QCOMPARE(entity.controlPointCount, 4);
        QCOMPARE(entity.knotCount, 8);
        QCOMPARE(static_cast<int>(entity.group40.size()), 8);
    }
    QCOMPARE(splineCount, 1);
}

void Wave21DxfTests::splineKeepsControlPointsExactly() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));

    const QVector<DxfEntityView> entities = dxfEntities(result.dxf);
    const QVector<QPointF> expected = {
        QPointF(100.0, 20.0), QPointF(120.0, 60.0), QPointF(160.0, 60.0), QPointF(180.0, 20.0)};
    // 必须真的找到一条样条：只遍历不计数的话，样条整个消失时这条用例会空过。
    int splineCount = 0;
    for (const DxfEntityView& entity : entities) {
        if (entity.type != QStringLiteral("SPLINE")) continue;
        ++splineCount;
        QCOMPARE(static_cast<int>(entity.points.size()), 4);
        for (int i = 0; i < 4; ++i) {
            QVERIFY2(nearlyEqual(entity.points.at(i).x(), expected.at(i).x()),
                     qPrintable(QStringLiteral("控制点 %1 的 x 不对").arg(i)));
            QVERIFY2(nearlyEqual(entity.points.at(i).y(), expected.at(i).y()),
                     qPrintable(QStringLiteral("控制点 %1 的 y 不对").arg(i)));
        }
    }
    QCOMPARE(splineCount, 1);
}

void Wave21DxfTests::layerTableListsPathTextAndDim() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));

    const QStringList layers = dxfLayerNames(result.dxf);
    QVERIFY2(layers.contains(QStringLiteral("PATH")), qPrintable(layers.join(',')));
    QVERIFY2(layers.contains(QStringLiteral("TEXT")), qPrintable(layers.join(',')));
    QVERIFY2(layers.contains(QStringLiteral("DIM")), qPrintable(layers.join(',')));
    QCOMPARE(static_cast<int>(result.layers.size()), 3);
}

void Wave21DxfTests::everyEntityBelongsToALayerInTheTable() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    CadDxfOptions options;
    options.includeText = true;
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, options);
    QVERIFY2(result.ok, qPrintable(result.error));

    const QStringList layers = dxfLayerNames(result.dxf);
    const QVector<DxfEntityView> entities = dxfEntities(result.dxf);
    QVERIFY(!entities.isEmpty());
    for (const DxfEntityView& entity : entities) {
        QVERIFY2(!entity.layer.isEmpty(), qPrintable(entity.type));
        QVERIFY2(layers.contains(entity.layer),
                 qPrintable(QStringLiteral("实体 %1 用了不在图层表里的图层 %2")
                                .arg(entity.type, entity.layer)));
    }
}

void Wave21DxfTests::strokeColorAndLineWidthAreWritten() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));

    const QVector<DxfEntityView> entities = dxfEntities(result.dxf);
    bool checkedLine = false;
    bool checkedSpline = false;
    for (const DxfEntityView& entity : entities) {
        if (entity.type == QStringLiteral("LINE") && !checkedLine) {
            // 蓝色（0 0 1）、2 点宽 → 索引色 5、真彩 0x0000FF、线宽 70（1/100 毫米）。
            QCOMPARE(entity.colorIndex, 5);
            QCOMPARE(entity.trueColor, 0x0000FF);
            QCOMPARE(entity.lineWeight, 70);
            checkedLine = true;
        }
        if (entity.type == QStringLiteral("SPLINE") && !checkedSpline) {
            // 红色（1 0 0）、3 点宽 → 索引色 1、真彩 0xFF0000、线宽 106。
            QCOMPARE(entity.colorIndex, 1);
            QCOMPARE(entity.trueColor, 0xFF0000);
            QCOMPARE(entity.lineWeight, 106);
            checkedSpline = true;
        }
    }
    QVERIFY(checkedLine);
    QVERIFY(checkedSpline);
}

void Wave21DxfTests::dimensionIsDetectedAndPutOnItsOwnLayer() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.dimensionCount, 1);

    const QVector<DxfEntityView> entities = dxfEntities(result.dxf);
    bool foundDimensionLine = false;
    int arrowCount = 0;
    bool foundMeasurement = false;
    for (const DxfEntityView& entity : entities) {
        if (entity.layer != QStringLiteral("DIM")) continue;
        if (entity.type == QStringLiteral("LINE") && entity.points.size() == 2 &&
            nearlyEqual(entity.points.at(0).x(), 60.0) && nearlyEqual(entity.points.at(0).y(), 80.0) &&
            nearlyEqual(entity.points.at(1).x(), 100.0) &&
            nearlyEqual(entity.points.at(1).y(), 80.0)) {
            foundDimensionLine = true;
        }
        if (entity.type == QStringLiteral("LWPOLYLINE") && entity.flags == 1) {
            ++arrowCount;
        }
        if (entity.type == QStringLiteral("TEXT") && entity.text == QStringLiteral("100")) {
            foundMeasurement = true;
        }
    }
    QVERIFY2(foundDimensionLine, "尺寸线没有落到 DIM 图层");
    QVERIFY2(arrowCount == 2, qPrintable(QStringLiteral("箭头数量 = %1").arg(arrowCount)));
    QVERIFY2(foundMeasurement, "标注数值没有落到 DIM 图层");
}

void Wave21DxfTests::dimensionMeasurementTextIsNotDuplicatedAsPlainText() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    CadDxfOptions options;
    options.includeText = true;
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, options);
    QVERIFY2(result.ok, qPrintable(result.error));

    int plainMeasurement = 0;
    int dimMeasurement = 0;
    for (const DxfEntityView& entity : dxfEntities(result.dxf)) {
        if (entity.type != QStringLiteral("TEXT") || entity.text != QStringLiteral("100")) continue;
        if (entity.layer == QStringLiteral("DIM")) ++dimMeasurement;
        if (entity.layer == QStringLiteral("TEXT")) ++plainMeasurement;
    }
    QCOMPARE(dimMeasurement, 1);
    QCOMPARE(plainMeasurement, 0);
}

void Wave21DxfTests::plainTextGoesToTextLayer() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    CadDxfOptions options;
    options.includeText = true;
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, options);
    QVERIFY2(result.ok, qPrintable(result.error));

    bool found = false;
    for (const DxfEntityView& entity : dxfEntities(result.dxf)) {
        if (entity.type != QStringLiteral("TEXT")) continue;
        if (entity.text != QStringLiteral("Hello")) continue;
        QCOMPARE(entity.layer, QStringLiteral("TEXT"));
        QVERIFY(!entity.group40.isEmpty());
        QVERIFY(entity.group40.first() > 0.0);
        found = true;
    }
    QVERIFY2(found, "普通文字没有落到 TEXT 图层");
}

void Wave21DxfTests::scaleMultipliesCoordinates() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult normal = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(normal.ok, qPrintable(normal.error));

    CadDxfOptions doubled;
    doubled.scale = 2.0;
    const CadDxfResult scaled = aipdf::pdf::exportPdfToCadDxf(pdf, doubled);
    QVERIFY2(scaled.ok, qPrintable(scaled.error));

    QPointF normalStart;
    QPointF scaledStart;
    for (const DxfEntityView& entity : dxfEntities(normal.dxf)) {
        if (entity.type == QStringLiteral("LINE") && entity.points.size() == 2 &&
            nearlyEqual(entity.points.at(0).x(), 10.0)) {
            normalStart = entity.points.at(0);
        }
    }
    for (const DxfEntityView& entity : dxfEntities(scaled.dxf)) {
        if (entity.type == QStringLiteral("LINE") && entity.points.size() == 2 &&
            nearlyEqual(entity.points.at(0).x(), 20.0)) {
            scaledStart = entity.points.at(0);
        }
    }
    QVERIFY2(!normalStart.isNull(), "没有找到基准直线");
    QVERIFY2(!scaledStart.isNull(), "没有找到放大后的直线");
    QVERIFY(nearlyEqual(scaledStart.x(), normalStart.x() * 2.0));
    QVERIFY(nearlyEqual(scaledStart.y(), normalStart.y() * 2.0));
}

void Wave21DxfTests::multiplePagesAreStackedVertically() {
    const QByteArray pdf = buildDxfPdf({firstPageContent(), secondPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));

    QVector<double> lineStarts;
    for (const DxfEntityView& entity : dxfEntities(result.dxf)) {
        if (entity.type != QStringLiteral("LINE") || entity.points.size() != 2) continue;
        if (!nearlyEqual(entity.points.at(0).x(), 10.0)) continue;
        lineStarts.append(entity.points.at(0).y());
    }
    QCOMPARE(static_cast<int>(lineStarts.size()), 2);
    QVERIFY(nearlyEqual(lineStarts.at(0), 10.0));
    // 第二页摞在第一页下面：页高 100 → y 偏移 100。
    QVERIFY2(nearlyEqual(lineStarts.at(1), 110.0),
             qPrintable(QStringLiteral("第二页的 y = %1").arg(lineStarts.at(1))));
}

void Wave21DxfTests::imagesAreCountedButNotExported() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.imageCount, 1);
    QVERIFY2(!result.dxf.contains("\nIMAGE\n"), "DXF 里不该出现 IMAGE 实体");
    QVERIFY2(!result.dxf.contains("\nWIPEOUT\n"), "DXF 里不该出现 WIPEOUT 实体");
}

void Wave21DxfTests::headerCarriesR2000VersionAndExtents() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.dxf.contains("\nAC1015\n"));
    QVERIFY(result.dxf.contains("$EXTMIN"));
    QVERIFY(result.dxf.contains("$EXTMAX"));
    QVERIFY(nearlyEqual(dxfHeaderNumber(result.dxf, "$EXTMAX"), 200.0, 0.5));
    QVERIFY(nearlyEqual(dxfHeaderNumber(result.dxf, "$EXTMIN"), 0.0, 0.5));
    QVERIFY(result.dxf.startsWith("  0\nSECTION"));
    QVERIFY(result.dxf.trimmed().endsWith("EOF"));
}

void Wave21DxfTests::realFixtureExportsWithoutCrashing() {
    // 真夹具（构建时生成的 PDF）走一遍深度导出：不能崩、不能报错，
    // 产出的仍是一份结构完整的 DXF。
    //
    // 为什么要有这一条：自造的夹具只覆盖我们自己写的那几种图形；
    // 真 PDF 里有旋转页、多页、空页这些东西，只有真夹具能碰到。
    const QString directory = QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR);
    for (const QString& name : {QStringLiteral("normal.pdf"), QStringLiteral("multipage.pdf"),
                                QStringLiteral("rotated.pdf"), QStringLiteral("table.pdf"),
                                QStringLiteral("text.pdf")}) {
        QFile file(directory + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;  // 夹具不在就跳过这一份
        }
        const QByteArray pdf = file.readAll();
        file.close();

        CadDxfOptions options;
        options.includeText = true;
        const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, options);
        QVERIFY2(result.ok, qPrintable(QStringLiteral("%1: %2").arg(name, result.error)));
        QVERIFY2(result.dxf.startsWith("  0\nSECTION"), name.toLatin1().constData());
        QVERIFY2(result.dxf.trimmed().endsWith("EOF"), name.toLatin1().constData());
        QVERIFY2(result.dxf.contains("\nENTITIES\n"), name.toLatin1().constData());
        // 图层表必须在：实体引用的图层要能在表里找到。
        const QStringList layers = dxfLayerNames(result.dxf);
        QVERIFY2(layers.contains(QStringLiteral("PATH")), name.toLatin1().constData());
        for (const DxfEntityView& entity : dxfEntities(result.dxf)) {
            QVERIFY2(layers.contains(entity.layer),
                     qPrintable(QStringLiteral("%1: 实体 %2 用了表外的图层 %3")
                                    .arg(name, entity.type, entity.layer)));
        }
    }
}

void Wave21DxfTests::invalidInputIsRejected() {
    const CadDxfResult empty = aipdf::pdf::exportPdfToCadDxf(QByteArray(), CadDxfOptions{});
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());

    const CadDxfResult broken = aipdf::pdf::exportPdfToCadDxf(QByteArray("this is not a pdf"));
    QVERIFY(!broken.ok);
    QVERIFY2(broken.error.contains(QStringLiteral("PDF")), qPrintable(broken.error));
    QVERIFY(broken.dxf.isEmpty());
}

void Wave21DxfTests::exportedFileIsWrittenForCadReaders() {
    const QByteArray pdf = buildDxfPdf({firstPageContent()});
    const CadDxfResult result = aipdf::pdf::exportPdfToCadDxf(pdf, CadDxfOptions{});
    QVERIFY2(result.ok, qPrintable(result.error));

    // 落一份到夹具目录：给 CAD 读档器（ezdxf）做交叉校验用。
    const QString directory = QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR);
    QVERIFY(QDir().mkpath(directory));
    const QString path = directory + QLatin1String("/wave21-dxf-sample.dxf");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(result.dxf), static_cast<qint64>(result.dxf.size()));
    file.close();
    QVERIFY(QFileInfo(path).size() > 0);
}

QTEST_MAIN(Wave21DxfTests)
#include "Wave21DxfTests.moc"
