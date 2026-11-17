#include "pdf/RichMedia3d.h"

#include "pdf/PortfolioWriter.h"

#include <QColor>
#include <QTest>

#include <fpdfview.h>

using aipdf::pdf::FallbackImage;
using aipdf::pdf::MediaAnnotationInfo;
using aipdf::pdf::MediaAnnotationSpec;
using aipdf::pdf::readMediaAnnotations;
using aipdf::pdf::readPortfolio;
using aipdf::pdf::readRichMedia;
using aipdf::pdf::readThreeDAnnotations;
using aipdf::pdf::RichMediaReadResult;
using aipdf::pdf::ThreeDAnnotationInfo;
using aipdf::pdf::ThreeDAnnotationSpec;
using aipdf::pdf::ThreeDViewSpec;
using aipdf::pdf::writeMediaAnnotation;
using aipdf::pdf::writePortfolio;
using aipdf::pdf::writeThreeDAnnotation;

namespace {

// PDFium 的库初始化。
//
// 只做一次：PDFium 内部的初始化本身有一次性保护，重复调用也不会出事。
void initPdfium() {
    FPDF_LIBRARY_CONFIG config = {};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

// 用 PDFium 打开一段字节，返回页数；打不开返回 -1。
//
// 为什么用 PDFium 验：光比对字节没有意义——文件能不能被真的打开、
// 页数对不对，只有解析器说了算。
int pageCountOf(const QByteArray& bytes) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) return -1;
    const int count = FPDF_GetPageCount(document);
    FPDF_CloseDocument(document);
    return count;
}

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) maxId = qMax(maxId, object.first);
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (!offsets.contains(id)) {
            out += "0000000000 65535 f \n";
            continue;
        }
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

QByteArray streamObject(const QByteArray& content) {
    return QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

// 两页的普通 PDF（没有注解）。
QByteArray twoPagePdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject("BT /F1 12 Tf 40 250 Td (Page one) Tj ET")));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R >>")));
    objects.append(qMakePair(6, streamObject("BT /F1 12 Tf 40 250 Td (Page two) Tj ET")));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    return buildPdf(objects);
}

// 合成的假 PRC 字节。
//
// 说明：本程序**不解析也不生成** U3D 的 PRC 内容，写进去的字节原样搬运。
// 所以这里用一段合成的字节就够——测的是「结构写对了吗、读得回来吗」，
// 不是「模型能不能渲染」（渲染要 U3D 运行时，那是另一件事）。
QByteArray fakePrcBytes() {
    QByteArray data("FAKE-U3D-PRC-BYTES-");
    data.append(QByteArray(64, '\x01'));
    return data;
}

// 合成的假媒体字节。
//
// 同上：本程序不解码音视频，只把字节按规范写进文件。
QByteArray fakeMediaBytes(int size) {
    QByteArray data("FAKE-MEDIA-");
    while (data.size() < size) data.append('M');
    return data;
}

// 一份参数齐全的 3D 视图参数（用来验读回来的是不是同一个）。
ThreeDViewSpec richViewSpec() {
    ThreeDViewSpec view;
    view.externalName = QStringLiteral("主视图");
    view.internalName = QStringLiteral("main");
    view.cameraToWorld = QVector<double>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    view.hasOrbitDistance = true;
    view.orbitDistance = 7.5;
    view.projectionType = QStringLiteral("/Orthographic");
    view.fieldOfView = 30.0;
    view.hasBackground = true;
    view.background = QColor(10, 20, 30);
    view.backgroundShadow = true;
    view.backgroundIlluminated = false;
    view.renderStyle = QStringLiteral("/Shaded");
    view.renderOpacity = 0.5;
    view.lightingScheme = QStringLiteral("/Night");
    return view;
}

}  // namespace

class RichMedia3dTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 负例 ----

    void emptyInputFails() {
        QString error;
        const QByteArray written =
            writeThreeDAnnotation(QByteArray(), ThreeDAnnotationSpec(), &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("没有可写入的 PDF 内容")), qPrintable(error));
    }

    void notAPdfFails() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        const QByteArray written = writeThreeDAnnotation(QByteArray("not a pdf"), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(!error.isEmpty(), "必须写明失败原因");
    }

    void emptyModelBytesFail() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = QByteArray();
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("模型字节为空")), qPrintable(error));
    }

    void pageIndexOutOfRangeFails() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.pageIndex = 5;
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("页码超出范围")), qPrintable(error));
    }

    void negativePageIndexFails() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.pageIndex = -1;
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("页码超出范围")), qPrintable(error));
    }

    void emptyRectFails() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.rect = QRectF(10, 10, 0, 50);
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("宽高必须大于 0")), qPrintable(error));
    }

    void brokenFallbackImageFails() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.fallback.width = 4;
        spec.fallback.height = 4;
        spec.fallback.rgb = QByteArray(10, '\x01');  // 应为 4×4×3 = 48 字节
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("对不上")), qPrintable(error));
    }

    void duplicate3dAnnotationOnTheSameRectFails() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.pageIndex = 0;
        spec.rect = QRectF(20, 30, 100, 80);
        const QByteArray once = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!once.isEmpty(), qPrintable(error));
        // 同一页、同一个矩形：按规则直接失败，不覆盖。
        const QByteArray twice = writeThreeDAnnotation(once, spec, &error);
        QVERIFY(twice.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("矩形相同")), qPrintable(error));
    }

    void emptyMediaBytesFail() {
        QString error;
        MediaAnnotationSpec spec;
        spec.embedded = true;
        spec.mediaBytes = QByteArray();
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("字节为空")), qPrintable(error));
    }

    void externalMediaWithoutPathFails() {
        QString error;
        MediaAnnotationSpec spec;
        spec.embedded = false;
        spec.externalPath = QString();
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("没有给路径")), qPrintable(error));
    }

    void mediaPageIndexOutOfRangeFails() {
        QString error;
        MediaAnnotationSpec spec;
        spec.mediaBytes = fakeMediaBytes(256);
        spec.pageIndex = 9;
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY(written.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("页码超出范围")), qPrintable(error));
    }

    void readingANonPdfFails() {
        const RichMediaReadResult result = readRichMedia(QByteArray("not a pdf"));
        QVERIFY(!result.ok());
        QVERIFY(!result.error.isEmpty());
    }

    // ---- 写 3D 注解 ----

    void written3dAnnotationKeepsThePageCount() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.pageIndex = 1;
        spec.rect = QRectF(20, 30, 100, 80);
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));
        // PDFium 能重新打开，页数不变。
        QCOMPARE(pageCountOf(written), 2);
    }

    void written3dAnnotationCanBeReadBack() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.pageIndex = 1;
        spec.rect = QRectF(20, 30, 100, 80);
        spec.view = richViewSpec();
        // 视图框比注解矩形小一圈：写进去时要换算成「注解目标坐标系」，
        // 读回来时要换算回 PDF user space。
        spec.viewBox = QRectF(30, 40, 60, 40);
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        QString readError;
        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 1, &readError);
        QVERIFY2(!found.isEmpty(), qPrintable(readError));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().pageIndex, 1);
        QVERIFY(qAbs(found.first().rect.left() - 20.0) < 0.01);
        QVERIFY(qAbs(found.first().rect.top() - 30.0) < 0.01);
        QVERIFY(qAbs(found.first().rect.width() - 100.0) < 0.01);
        QVERIFY(qAbs(found.first().rect.height() - 80.0) < 0.01);
        QVERIFY(qAbs(found.first().viewBox.left() - 30.0) < 0.01);
        QVERIFY(qAbs(found.first().viewBox.top() - 40.0) < 0.01);
        QVERIFY(qAbs(found.first().viewBox.width() - 60.0) < 0.01);
        QVERIFY(qAbs(found.first().viewBox.height() - 40.0) < 0.01);
    }

    void the3dViewParametersSurviveTheRoundTrip() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.view = richViewSpec();
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        const ThreeDViewSpec view = found.first().view;
        QCOMPARE(view.externalName, QStringLiteral("主视图"));
        QCOMPARE(view.internalName, QStringLiteral("main"));
        QCOMPARE(view.cameraToWorld.size(), 12);
        QVERIFY(qAbs(view.cameraToWorld.at(0) - 1.0) < 0.0001);
        QVERIFY(qAbs(view.cameraToWorld.at(5) - 1.0) < 0.0001);
        QVERIFY(view.hasOrbitDistance);
        QVERIFY(qAbs(view.orbitDistance - 7.5) < 0.001);
        QCOMPARE(view.projectionType, QStringLiteral("/Orthographic"));
        QVERIFY(qAbs(view.fieldOfView - 30.0) < 0.001);
        QVERIFY(view.hasBackground);
        // QColor::red()/green()/blue() 给的就是 0..255 的分量值。
        QCOMPARE(qRound(static_cast<double>(view.background.red())), 10);
        QCOMPARE(qRound(static_cast<double>(view.background.green())), 20);
        QCOMPARE(qRound(static_cast<double>(view.background.blue())), 30);
        QCOMPARE(view.backgroundShadow, true);
        QCOMPARE(view.backgroundIlluminated, false);
        QCOMPARE(view.renderStyle, QStringLiteral("/Shaded"));
        QVERIFY(qAbs(view.renderOpacity - 0.5) < 0.001);
        QCOMPARE(view.lightingScheme, QStringLiteral("/Night"));
    }

    void the3dModelIsReportedAsEmbedded() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QVERIFY(found.first().hasModel);
        QVERIFY(found.first().modelEmbedded);
        QCOMPARE(found.first().modelSize, static_cast<qint64>(fakePrcBytes().size()));
        QCOMPARE(found.first().modelMimeType, QStringLiteral("/U3D"));
    }

    void theFallbackImageIsWrittenAsTheNormalAppearance() {
        QString error;
        ThreeDAnnotationSpec spec;
        spec.modelBytes = fakePrcBytes();
        spec.fallback.width = 2;
        spec.fallback.height = 2;
        spec.fallback.rgb = QByteArray(12, '\x7F');
        const QByteArray written = writeThreeDAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QVERIFY(found.first().hasFallbackImage);
        // 写了外观流，页数仍然不变。
        QCOMPARE(pageCountOf(written), 2);
    }

    void two3dAnnotationsOnDifferentRectsCoexist() {
        QString error;
        ThreeDAnnotationSpec first;
        first.modelBytes = fakePrcBytes();
        first.rect = QRectF(10, 10, 60, 40);
        const QByteArray once = writeThreeDAnnotation(twoPagePdf(), first, &error);
        QVERIFY2(!once.isEmpty(), qPrintable(error));

        ThreeDAnnotationSpec second;
        second.modelBytes = fakePrcBytes();
        second.rect = QRectF(100, 100, 60, 40);
        const QByteArray twice = writeThreeDAnnotation(once, second, &error);
        QVERIFY2(!twice.isEmpty(), qPrintable(error));

        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(twice, 0, &error);
        QCOMPARE(found.size(), 2);
        QCOMPARE(pageCountOf(twice), 2);
    }

    void aPageWithoutAnnotationsHasNothing() {
        QString error;
        const QVector<ThreeDAnnotationInfo> found = readThreeDAnnotations(twoPagePdf(), 0, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(found.isEmpty());
    }

    // ---- 写多媒体注解 ----

    void writtenMediaAnnotationKeepsThePageCount() {
        QString error;
        MediaAnnotationSpec spec;
        spec.mediaBytes = fakeMediaBytes(1024);
        spec.fileName = QStringLiteral("demo.mp4");
        spec.pageIndex = 0;
        spec.rect = QRectF(50, 60, 120, 90);
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));
        QCOMPARE(pageCountOf(written), 2);
    }

    void theMediaTypeAndSizeSurviveTheRoundTrip() {
        QString error;
        MediaAnnotationSpec spec;
        spec.mediaBytes = fakeMediaBytes(1024);
        spec.fileName = QStringLiteral("demo.mp4");
        spec.rect = QRectF(50, 60, 120, 90);
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<MediaAnnotationInfo> found = readMediaAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().mimeType, QStringLiteral("video/mp4"));
        QCOMPARE(found.first().mediaSize, static_cast<qint64>(1024));
        QCOMPARE(found.first().instanceSubtype, QStringLiteral("/Video"));
        QVERIFY(found.first().embedded);
        QVERIFY(found.first().externalPath.isEmpty());
        QVERIFY(qAbs(found.first().rect.left() - 50.0) < 0.01);
        QVERIFY(qAbs(found.first().rect.width() - 120.0) < 0.01);
    }

    void theMediaTypeIsInferredFromTheFileName() {
        QString error;
        MediaAnnotationSpec spec;
        spec.mediaBytes = fakeMediaBytes(64);
        spec.fileName = QStringLiteral("voice.mp3");
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<MediaAnnotationInfo> found = readMediaAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QCOMPARE(found.first().mimeType, QStringLiteral("audio/mpeg"));
        QCOMPARE(found.first().instanceSubtype, QStringLiteral("/Sound"));
    }

    void anExplicitMediaTypeWins() {
        QString error;
        MediaAnnotationSpec spec;
        spec.mediaBytes = fakeMediaBytes(64);
        spec.fileName = QStringLiteral("voice.mp3");
        spec.mimeType = QStringLiteral("audio/x-custom");
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<MediaAnnotationInfo> found = readMediaAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QCOMPARE(found.first().mimeType, QStringLiteral("audio/x-custom"));
    }

    void externalMediaIsNotEmbedded() {
        QString error;
        MediaAnnotationSpec spec;
        spec.embedded = false;
        spec.fileName = QStringLiteral("clip.mp4");
        spec.externalPath = QStringLiteral("D:/media/clip.mp4");
        const QByteArray written = writeMediaAnnotation(twoPagePdf(), spec, &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));

        const QVector<MediaAnnotationInfo> found = readMediaAnnotations(written, 0, &error);
        QVERIFY2(!found.isEmpty(), qPrintable(error));
        QVERIFY(!found.first().embedded);
        QCOMPARE(found.first().externalPath, QStringLiteral("D:/media/clip.mp4"));
        QCOMPARE(found.first().mediaSize, static_cast<qint64>(0));
    }

    void readingEverythingListsBothKinds() {
        QString error;
        ThreeDAnnotationSpec threeD;
        threeD.modelBytes = fakePrcBytes();
        threeD.rect = QRectF(10, 10, 60, 40);
        QByteArray bytes = writeThreeDAnnotation(twoPagePdf(), threeD, &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));

        MediaAnnotationSpec media;
        media.mediaBytes = fakeMediaBytes(128);
        media.fileName = QStringLiteral("demo.mp4");
        media.rect = QRectF(100, 100, 60, 40);
        bytes = writeMediaAnnotation(bytes, media, &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));

        const RichMediaReadResult result = readRichMedia(bytes);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.threeD.size(), 1);
        QCOMPARE(result.media.size(), 1);
        QCOMPARE(pageCountOf(bytes), 2);
    }

    // ---- 作品集：复用仓库既有的 pdf::writePortfolio / pdf::readPortfolio ----

    void thePortfolioPathIsReusedNotRewritten() {
        // 作品集（PDF 包）不在本模块里另写一套：仓库已有
        // pdf::writePortfolio / pdf::readPortfolio，这里证明那条路是通的。
        QVector<aipdf::pdf::PortfolioEntry> entries;
        aipdf::pdf::PortfolioEntry entry;
        entry.name = QStringLiteral("说明.txt");
        entry.bytes = QByteArray("hello portfolio");
        entry.description = QStringLiteral("包里的说明");
        entries.append(entry);

        aipdf::pdf::PortfolioOptions options;
        options.title = QStringLiteral("作品集");

        QString error;
        const QByteArray bytes = writePortfolio(entries, options, &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));

        const aipdf::pdf::PortfolioReadResult read = readPortfolio(bytes);
        QVERIFY2(read.ok, qPrintable(read.error));
        QCOMPARE(read.entries.size(), 1);
        QCOMPARE(read.entries.first().name, QStringLiteral("说明.txt"));
        QCOMPARE(read.entries.first().bytes, QByteArray("hello portfolio"));
        QCOMPARE(read.title, QStringLiteral("作品集"));
        // 包也能被 PDFium 打开。
        QVERIFY(pageCountOf(bytes) > 0);
    }
};

void RichMedia3dTests::initTestCase() {
    initPdfium();
}

QTEST_GUILESS_MAIN(RichMedia3dTests)
#include "RichMedia3dTests.moc"
