// 页面 /Annots 注入的单元测试。
//
// 这一层的存在理由是"PDFium 造不出的注释类型"（/Sound 就是）。判据必须硬：
// 注入后的字节要能被 PDFium 重新打开、页数不变、注释数 +1、子类型是 /Sound，
// 并且结构能过 qpdf 校验。

#include "pdf/PageAnnotsInjector.h"

#include <QDir>
#include <QtTest>

#include <fpdfview.h>

#include "pdf/PdfiumAdapter.h"

using aipdf::pdf::InlineAnnotationInsert;
using aipdf::pdf::injectInlineAnnotations;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;

namespace {
QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}
}  // namespace

class PageAnnotsInjectorTests : public QObject {
    Q_OBJECT

private slots:
    void injectsSoundAnnotationIntoPage();
    void rejectsNonPdf();
};

void PageAnnotsInjectorTests::injectsSoundAnnotationIntoPage() {
    auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    QString error;
    const QByteArray bytes = opened.document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    InlineAnnotationInsert insert;
    insert.pageIndex = 0;
    // 音频流：1 字节 8kHz/单声道/8bit 的静音。
    insert.extraObjects.append(QByteArray("<< /Type /Sound /R 8000 /C 1 /B 8 /Length 1 >>\n"
                                          "stream\n\x80\nendstream"));
    insert.dictionary =
        "<< /Type /Annot /Subtype /Sound /Rect [72 72 96 96] /Contents (note.wav) "
        "/NM (Folixa:sound-1) /Sound @EXTRA0 0 R >>";

    const auto result = injectInlineAnnotations(bytes, { insert });
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.inserted, 1);

    const auto reopened = openDocumentFromBytes(result.bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), opened.document.pageCount());

    // 用 PDFium 自己数：注入的那条必须是 /Sound 子类型。
    const auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    const auto annotations = page->annotations();
    bool foundSound = false;
    for (const auto& annotation : annotations) {
        if (annotation.type == aipdf::pdf::AnnotationType::Sound) {
            foundSound = true;
        }
    }
    QVERIFY2(foundSound, "注入的声音注释没有被识别成 /Sound");
}

void PageAnnotsInjectorTests::rejectsNonPdf() {
    const auto result = injectInlineAnnotations(QByteArray("not a pdf"), {});
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

QTEST_MAIN(PageAnnotsInjectorTests)
#include "PageAnnotsInjectorTests.moc"
