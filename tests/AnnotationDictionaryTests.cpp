// 批注字典额外条目（/L、/LE、/Vertices、/BE）的单元测试。
//
// 这些键是"别的阅读器怎么理解这条批注"的全部依据：没有 /L，直线就是个空矩形；
// 没有 /LE，箭头就是一条线。而它们只能由我们的增量写入器补，所以必须逐字确认。

#include "pdf/annot/AnnotationDictionary.h"

// mimeTypeForFileName 原来住在 annot 层，2026-11-20 那次提交把它挪到了
// kernel/MimeTypes.h（annot 与 document 都要用它，放 kernel 才不反向依赖）。
// 这个测试没跟着改 include，于是「用 using 声明引入」那一步就编译不过。
#include "pdf/kernel/MimeTypes.h"


#include <QTest>

using aipdf::pdf::annotationDictionaryExtras;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::mimeTypeForFileName;

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
    void freeTextWritesDefaultAppearance();
    void stampWritesStandardName();
    void measureWritesScaleAndLength();
    void attachmentBuildsFilespecAndStream();
    void mimeTypeIsGuessedFromFileName();
    void unknownSuffixFallsBackToOctetStream();
    void nonAsciiFileNameUsesUtf16();
    void commonMetadataIsWritten();
    void appearancePropertiesAreWritten();
    void soundBuildsStreamAndReference();
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
    // 顶点不够时不写 /L、/Vertices（但公共元数据 /M 仍会写，这是有意的）。
    Annotation line = make(AnnotationType::Line);
    line.vertices = { QPointF(1.0, 2.0) };
    QVERIFY(!annotationDictionaryExtras(line).contains("/L ["));

    Annotation polygon = make(AnnotationType::Polygon);
    polygon.vertices = { QPointF(1.0, 2.0), QPointF(3.0, 4.0) };
    QVERIFY(!annotationDictionaryExtras(polygon).contains("/Vertices"));
}

void AnnotationDictionaryTests::otherTypesWriteNothing() {
    const QByteArray highlight = annotationDictionaryExtras(make(AnnotationType::Highlight));
    const QByteArray square = annotationDictionaryExtras(make(AnnotationType::Square));
    // 这两类只有公共元数据（/M），不该带任何类型专有键。
    QVERIFY(!highlight.contains("/L ["));
    QVERIFY(!highlight.contains("/Vertices"));
    QVERIFY(!square.contains("/L ["));
    QVERIFY(!square.contains("/BE"));
}

void AnnotationDictionaryTests::freeTextWritesDefaultAppearance() {
    Annotation annotation = make(AnnotationType::FreeText);
    annotation.fontSize = 14.0;

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/DA ("));
    QVERIFY(extras.contains("/Helvetica 14.00 Tf"));
}

void AnnotationDictionaryTests::stampWritesStandardName() {
    Annotation annotation = make(AnnotationType::Stamp);
    annotation.contents = QStringLiteral("approved");
    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/Name /Approved"));
}

void AnnotationDictionaryTests::measureWritesScaleAndLength() {
    Annotation annotation = make(AnnotationType::Measure);
    annotation.vertices = { QPointF(10.0, 10.0), QPointF(110.0, 10.0) };
    annotation.measureScale = 2.0;

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/L [10.000 10.000 110.000 10.000]"));
    QVERIFY(extras.contains("/Measure << /Type /Measure /Subtype /RL /R 2.000 /X (pt) >>"));
}

void AnnotationDictionaryTests::attachmentBuildsFilespecAndStream() {
    Annotation annotation = make(AnnotationType::FileAttachment);
    annotation.attachmentName = QStringLiteral("note.txt");
    annotation.attachmentData = QByteArray("hello attachment");

    const auto parts = annotationDictionaryParts(annotation);
    QVERIFY(QString::fromLatin1(parts.entries).contains("/FS @EXTRA0 0 R"));
    QCOMPARE(parts.objects.size(), 2);
    QVERIFY(parts.objects.at(0).contains("/Type /Filespec"));
    QVERIFY(parts.objects.at(0).contains("/EF << /F @EXTRA1 0 R >>"));
    QVERIFY(parts.objects.at(1).contains("/Type /EmbeddedFile"));
    QVERIFY(parts.objects.at(1).contains("hello attachment"));
    // 文件流的 /Length 必须与实际字节数一致，否则阅读器会读歪。
    QVERIFY(parts.objects.at(1).contains("/Length 16"));
}

void AnnotationDictionaryTests::mimeTypeIsGuessedFromFileName() {
    // 音视频附件必须带上 MIME：阅读器靠它决定「点开用什么程序放」。
    QCOMPARE(mimeTypeForFileName(QStringLiteral("clip.avi")), QByteArray("video/x-msvideo"));
    QCOMPARE(mimeTypeForFileName(QStringLiteral("clip.mp4")), QByteArray("video/mp4"));
    QCOMPARE(mimeTypeForFileName(QStringLiteral("voice.wav")), QByteArray("audio/wav"));
    QCOMPARE(mimeTypeForFileName(QStringLiteral("song.mp3")), QByteArray("audio/mpeg"));
    // 大小写不固定是 Windows 的常态，按原样比对会漏掉一半文件。
    QCOMPARE(mimeTypeForFileName(QStringLiteral("CLIP.AVI")), QByteArray("video/x-msvideo"));
}

void AnnotationDictionaryTests::unknownSuffixFallsBackToOctetStream() {
    // 认不出来就说不知道：application/octet-stream 是 PDF 规范里的默认值，
    // 比编一个具体类型诚实。
    QCOMPARE(mimeTypeForFileName(QStringLiteral("payload.bin")),
             QByteArray("application/octet-stream"));
    QCOMPARE(mimeTypeForFileName(QStringLiteral("no-extension")),
             QByteArray("application/octet-stream"));
    // 视频附件的 MIME 要真的写进嵌入文件流里。
    Annotation annotation = make(AnnotationType::FileAttachment);
    annotation.attachmentName = QStringLiteral("clip.avi");
    annotation.attachmentData = QByteArray("fake-avi-bytes");
    const auto parts = annotationDictionaryParts(annotation);
    QVERIFY(parts.objects.at(1).contains("/Subtype (video/x-msvideo)"));
}

void AnnotationDictionaryTests::nonAsciiFileNameUsesUtf16() {
    Annotation annotation = make(AnnotationType::FileAttachment);
    annotation.attachmentName = QStringLiteral("合同.pdf");
    annotation.attachmentData = QByteArray("x");

    const auto parts = annotationDictionaryParts(annotation);
    const QByteArray filespec = parts.objects.at(0);
    // /UF 一律 UTF-16；/F 也必须是 UTF-16，否则中文名在别的阅读器里是乱码。
    QVERIFY(filespec.contains("/UF <FEFF"));
    QVERIFY(filespec.contains("/F <FEFF"));
    // "合" = U+5408
    QVERIFY(filespec.contains("5408"));
}

void AnnotationDictionaryTests::commonMetadataIsWritten() {
    Annotation annotation = make(AnnotationType::Highlight);
    annotation.author = QStringLiteral("张三");
    annotation.reviewState = QStringLiteral("Accepted");
    annotation.replyToId = 7;
    annotation.createdAt = QDateTime(QDate(2026, 11, 15), QTime(10, 30, 0), Qt::UTC);

    const QByteArray extras = annotationDictionaryExtras(annotation);
    // 作者是中文：必须走 UTF-16，否则福昕里显示乱码。
    QVERIFY(extras.contains("/T <FEFF"));
    QVERIFY(extras.contains("/M (D:20261115103000Z)"));
    QVERIFY(extras.contains("/State (Accepted)"));
    QVERIFY(extras.contains("/StateModel /Review"));
    QVERIFY(extras.contains("/IRT @ANNOTFolixa:7 0 R"));
    QVERIFY(extras.contains("/RT /R"));
}

void AnnotationDictionaryTests::appearancePropertiesAreWritten() {
    Annotation annotation = make(AnnotationType::Square);
    annotation.lineWidth = 3.5;
    annotation.opacity = 0.4;

    const QString extras = QString::fromLatin1(annotationDictionaryExtras(annotation));
    QVERIFY(extras.contains("/BS << /W 3.500 >>"));
    QVERIFY(extras.contains("/CA 0.400"));

    // 默认值不写：写 /CA 1.000 只是噪音，还会让"没改过"和"改过"看起来一样。
    Annotation plain = make(AnnotationType::Square);
    const QString plainExtras = QString::fromLatin1(annotationDictionaryExtras(plain));
    QVERIFY(!plainExtras.contains("/BS"));
    QVERIFY(!plainExtras.contains("/CA"));
}

void AnnotationDictionaryTests::soundBuildsStreamAndReference() {
    Annotation annotation = make(AnnotationType::Sound);
    annotation.soundData = QByteArray("\x01\x02\x03\x04", 4);
    annotation.soundRate = 22050;
    annotation.soundChannels = 2;
    annotation.soundBits = 16;

    const auto parts = annotationDictionaryParts(annotation);
    QVERIFY(QString::fromLatin1(parts.entries).contains("/Sound @EXTRA0 0 R"));
    QCOMPARE(parts.objects.size(), 1);
    const QByteArray stream = parts.objects.first();
    QVERIFY(stream.contains("/Type /Sound"));
    QVERIFY(stream.contains("/R 22050"));
    QVERIFY(stream.contains("/C 2"));
    QVERIFY(stream.contains("/B 16"));
    QVERIFY(stream.contains("/Length 4"));
    // PCM 必须原样写进去：多一个字节阅读器就会放错。
    QVERIFY(stream.contains(QByteArray("\x01\x02\x03\x04", 4)));
}

QTEST_MAIN(AnnotationDictionaryTests)
#include "AnnotationDictionaryTests.moc"
