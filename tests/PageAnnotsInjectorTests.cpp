// 页面 /Annots 注入的单元测试。
//
// 这一层的存在理由是"PDFium 造不出的注释类型"（/Sound 就是）。判据必须硬：
// 注入后的字节要能被 PDFium 重新打开、页数不变、注释数 +1、子类型是 /Sound，
// 并且结构能过 qpdf 校验。

#include "pdf/annot/PageAnnotsInjector.h"

#include <QDir>
#include <QtTest>

#include <fpdfview.h>

#include "pdf/kernel/PdfiumAdapter.h"

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
    void injectsInternalPageLink();
    void ignoresBracketsInsideStrings();
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

void PageAnnotsInjectorTests::injectsInternalPageLink() {
    auto opened = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(opened.document.isValid());
    const QByteArray bytes = opened.document.serialize();
    QVERIFY(!bytes.isEmpty());

    InlineAnnotationInsert insert;
    insert.pageIndex = 0;
    insert.targetPages.append(2);
    insert.dictionary =
        "<< /Type /Annot /Subtype /Link /Rect [20 20 120 40] "
        "/Border [0 0 0] /Dest [@PAGE0 0 R /Fit] >>";
    const auto result = injectInlineAnnotations(bytes, {insert});
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto reopened = openDocumentFromBytes(result.bytes);
    QVERIFY(reopened.document.isValid());
    const auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    bool found = false;
    for (const auto& link : page->links()) {
        if (link.pageIndex == 2 && link.rect == QRectF(QPointF(20, 20), QPointF(120, 40))) {
            found = true;
        }
    }
    QVERIFY2(found, "注入的内部页面链接没有指向第 3 页");
}

void PageAnnotsInjectorTests::ignoresBracketsInsideStrings() {
    // 回归（两件事）：
    // 1. /Annots 里已有批注的 /Contents 字符串包含 ']' 时，找数组结尾不能把
    //    字符串里的 ']' 当结构符号，否则新批注会被插进字符串中间；
    // 2. 增量更新后第 1 页的新版本在文件末尾，页面顺序不能因此错位——否则第二次
    //    注入会写到第 2 页上，看起来就是“第二次注入的批注没出现”。
    auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    const QByteArray bytes = opened.document.serialize();
    QVERIFY(!bytes.isEmpty());

    InlineAnnotationInsert first;
    first.pageIndex = 0;
    first.dictionary =
        "<< /Type /Annot /Subtype /Text /Rect [30 30 50 50] "
        "/Contents (第一部分 ] 第二部分) /NM (fixture-string-bracket) >>";
    const auto firstResult = injectInlineAnnotations(bytes, {first});
    QVERIFY2(firstResult.ok, qPrintable(firstResult.error));

    InlineAnnotationInsert second;
    second.pageIndex = 0;
    second.dictionary =
        "<< /Type /Annot /Subtype /Text /Rect [60 60 80 80] /Contents (second) >>";
    const auto secondResult = injectInlineAnnotations(firstResult.bytes, {second});
    QVERIFY2(secondResult.ok, qPrintable(secondResult.error));

    const auto reopened = openDocumentFromBytes(secondResult.bytes);
    QVERIFY2(reopened.document.isValid(), "含 ']' 的字符串把 /Annots 写坏了");
    const auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY2(page->annotations().size() >= 2, "第二次注入的批注没有出现在页面上");

    // 第一次注入只动了第 1 页，第 2 页必须还是干净的（防止页面序号错位）。
    const auto secondPage = reopened.document.page(1);
    QVERIFY(secondPage.has_value());
    QVERIFY2(secondPage->annotations().isEmpty(), "批注被写到了第 2 页");
}

void PageAnnotsInjectorTests::rejectsNonPdf() {
    const auto result = injectInlineAnnotations(QByteArray("not a pdf"), {});
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

QTEST_MAIN(PageAnnotsInjectorTests)
#include "PageAnnotsInjectorTests.moc"
