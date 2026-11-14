// 保存路径的测试：serializeForSave 保证「保存下来的文件，批注在别的阅读器里也看得见」。
//
// 这一层最容易出的错是"漏补"——保存成功、文件合法、批注却在外单位置集体消失。
// 所以每条断言都盯着「补了几条」和「补了之后渲染有没有变化」，而不是"没报错"。

#include "pdf/Annotation.h"
#include "pdf/DocumentWriter.h"
#include "pdf/PdfiumAdapter.h"

#include <QImage>
#include <QTest>

#include <algorithm>

using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::serializeForSave;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

Annotation makeAnnotation(quint64 id, AnnotationType type, const QRectF& rect) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = 0;
    annotation.type = type;
    annotation.rect = rect;
    annotation.quads = { rect };
    annotation.color = type == AnnotationType::Square ? QColor(220, 40, 40) : QColor(255, 214, 0);
    return annotation;
}

// 打开夹具、写入批注、返回准备保存的字节。
aipdf::pdf::SaveBytes saveBytesWith(const QVector<QVector<Annotation>>& perPage) {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    if (!opened.document.isValid()) {
        return {};
    }
    for (int pageIndex = 0; pageIndex < perPage.size(); ++pageIndex) {
        auto page = opened.document.page(pageIndex);
        if (!page.has_value()) {
            return {};
        }
        if (!page->setAnnotations(perPage.at(pageIndex))) {
            return {};
        }
    }
    return serializeForSave(opened.document);
}

}  // namespace

class DocumentWriterTests : public QObject {
    Q_OBJECT

private slots:
    void everyAnnotationGetsAnAppearance();
    void appearanceChangesPixelsOnSave();
    void noteOnlyDocumentIsLeftUntouched();
    void annotationsOnEveryPageAreCovered();
};

void DocumentWriterTests::everyAnnotationGetsAnAppearance() {
    const aipdf::pdf::SaveBytes prepared = saveBytesWith({ {
        makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)),
        makeAnnotation(2, AnnotationType::Highlight, QRectF(60.0, 230.0, 160.0, 18.0)),
        makeAnnotation(3, AnnotationType::Square, QRectF(300.0, 120.0, 80.0, 40.0)),
    } });

    QVERIFY2(prepared.ok, qPrintable(prepared.error));
    QVERIFY2(prepared.warning.isEmpty(), qPrintable(prepared.warning));
    QCOMPARE(prepared.appearanceCount, 3);

    const auto reopened = openDocumentFromBytes(prepared.bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), 2);
    auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->annotations().size(), 3);
    // 三条批注的 /AP 引用都得在文件里，一条都不能少。
    QCOMPARE(prepared.bytes.count("/AP << /N"), 3);
}

void DocumentWriterTests::appearanceChangesPixelsOnSave() {
    const aipdf::pdf::SaveBytes plain = saveBytesWith({ {
        makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)),
    } });
    QVERIFY(plain.ok);

    // 同样的文档、同样的批注，但走"不补外观"的路径做对照。
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations({ makeAnnotation(1, AnnotationType::Highlight,
                                                  QRectF(60.0, 200.0, 200.0, 18.0)) }));
    QString error;
    const QByteArray withoutAppearance = opened.document.serialize(&error);
    QVERIFY2(!withoutAppearance.isEmpty(), qPrintable(error));

    const auto before = openDocumentFromBytes(withoutAppearance);
    const auto after = openDocumentFromBytes(plain.bytes);
    QVERIFY(before.document.isValid());
    QVERIFY(after.document.isValid());

    const auto pageBefore = before.document.page(0);
    const auto pageAfter = after.document.page(0);
    QVERIFY(pageBefore.has_value());
    QVERIFY(pageAfter.has_value());
    const QImage imageBefore = pageBefore->render(1.0);
    const QImage imageAfter = pageAfter->render(1.0);
    QCOMPARE(imageBefore.size(), imageAfter.size());

    int differing = 0;
    for (int y = 0; y < imageBefore.height(); ++y) {
        for (int x = 0; x < imageBefore.width(); ++x) {
            const QRgb a = imageBefore.pixel(x, y);
            const QRgb b = imageAfter.pixel(x, y);
            if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
                qAbs(qBlue(a) - qBlue(b)) > 2) {
                ++differing;
            }
        }
    }
    // 200×18 的高亮，光栅化误差留 10% 余量。
    QVERIFY2(differing >= 3000 && differing <= 4200,
             qPrintable(QStringLiteral("差异像素 %1").arg(differing)));
}

void DocumentWriterTests::noteOnlyDocumentIsLeftUntouched() {
    // 便签没有图形外观：不该被补 /AP，字节也不该被追加任何东西。
    const aipdf::pdf::SaveBytes prepared = saveBytesWith({ {
        makeAnnotation(9, AnnotationType::Note, QRectF(40.0, 40.0, 24.0, 24.0)),
    } });
    QVERIFY(prepared.ok);
    QVERIFY(prepared.warning.isEmpty());
    QCOMPARE(prepared.appearanceCount, 0);
    QVERIFY(!prepared.bytes.contains("/AP << /N"));
}

void DocumentWriterTests::annotationsOnEveryPageAreCovered() {
    const aipdf::pdf::SaveBytes prepared = saveBytesWith({
        { makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 120.0, 14.0)) },
        { makeAnnotation(2, AnnotationType::Highlight, QRectF(80.0, 160.0, 120.0, 14.0)) },
    });
    QVERIFY(prepared.ok);
    QCOMPARE(prepared.appearanceCount, 2);

    const auto reopened = openDocumentFromBytes(prepared.bytes);
    QVERIFY(reopened.document.isValid());
    for (int pageIndex = 0; pageIndex < 2; ++pageIndex) {
        auto page = reopened.document.page(pageIndex);
        QVERIFY(page.has_value());
        QCOMPARE(page->annotations().size(), 1);
    }
}

QTEST_MAIN(DocumentWriterTests)
#include "DocumentWriterTests.moc"
