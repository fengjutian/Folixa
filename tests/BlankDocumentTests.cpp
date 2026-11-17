#include "doc/BlankDocument.h"

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QTest>

#include <fpdf_transformpage.h>
#include <fpdfview.h>

using aipdf::doc::BlankDocumentOptions;
using aipdf::doc::PageOrientation;
using aipdf::doc::PageSize;
using aipdf::doc::createBlankDocument;
using aipdf::doc::pageSizeInPoints;

namespace {

// PDFium 的库初始化。
//
// 只做一次：PDFium 内部的初始化本身有一次性保护，重复调用也不会出事，
// 但没必要每次都调。
void initPdfium() {
    FPDF_LIBRARY_CONFIG config = {};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

// 用 PDFium 打开一段字节，返回页数；打不开返回 -1。
int pageCountOf(const QByteArray& bytes) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) {
        return -1;
    }
    const int count = FPDF_GetPageCount(document);
    FPDF_CloseDocument(document);
    return count;
}

// 用 PDFium 读第 index 页的 MediaBox。
bool mediaBoxOf(const QByteArray& bytes, int index, float* left, float* bottom, float* right,
                 float* top) {
    FPDF_DOCUMENT document =
        FPDF_LoadMemDocument(bytes.constData(), static_cast<int>(bytes.size()), nullptr);
    if (document == nullptr) {
        return false;
    }
    FPDF_PAGE page = FPDF_LoadPage(document, index);
    if (page == nullptr) {
        FPDF_CloseDocument(document);
        return false;
    }
    const bool ok = FPDFPage_GetMediaBox(page, left, bottom, right, top) != 0;
    FPDF_ClosePage(page);
    FPDF_CloseDocument(document);
    return ok;
}

}  // namespace

class BlankDocumentTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void a4PortraitSizeIs595x842();
    void a4LandscapeSizeIs842x595();
    void otherPaperSizesAreCorrect();
    void defaultOptionsProduceOnePagePdf();
    void threePagesProduceThreePagePdf();
    void zeroPageCountFails();
    void negativePageCountFails();
    void customSizeWithZeroFails();
    void customSizeWithNegativeFails();
    void customSizeAboveLimitFails();
    void legalCustomSizeKeepsExactDimensions();
    void pageCountIsClampedToUpperLimit();
    void generatedBytesLookLikePdf();
    void failureReasonIsChinese();
};

void BlankDocumentTests::initTestCase() {
    initPdfium();
}

void BlankDocumentTests::a4PortraitSizeIs595x842() {
    const QSizeF size = pageSizeInPoints(PageSize::A4, PageOrientation::Portrait);
    QCOMPARE(size.width(), 595.0);
    QCOMPARE(size.height(), 842.0);
}

void BlankDocumentTests::a4LandscapeSizeIs842x595() {
    const QSizeF size = pageSizeInPoints(PageSize::A4, PageOrientation::Landscape);
    QCOMPARE(size.width(), 842.0);
    QCOMPARE(size.height(), 595.0);
}

void BlankDocumentTests::otherPaperSizesAreCorrect() {
    const QSizeF a3 = pageSizeInPoints(PageSize::A3, PageOrientation::Portrait);
    QCOMPARE(a3.width(), 842.0);
    QCOMPARE(a3.height(), 1191.0);

    const QSizeF a5 = pageSizeInPoints(PageSize::A5, PageOrientation::Portrait);
    QCOMPARE(a5.width(), 420.0);
    QCOMPARE(a5.height(), 595.0);

    const QSizeF letter = pageSizeInPoints(PageSize::Letter, PageOrientation::Portrait);
    QCOMPARE(letter.width(), 612.0);
    QCOMPARE(letter.height(), 792.0);

    const QSizeF legal = pageSizeInPoints(PageSize::Legal, PageOrientation::Portrait);
    QCOMPARE(legal.width(), 612.0);
    QCOMPARE(legal.height(), 1008.0);

    // 自定义尺寸随调用而定，尺寸表里没有，返回无效尺寸。
    const QSizeF custom = pageSizeInPoints(PageSize::Custom, PageOrientation::Portrait);
    QVERIFY(!custom.isValid());
    QCOMPARE(custom.width(), -1.0);
    QCOMPARE(custom.height(), -1.0);
}

void BlankDocumentTests::defaultOptionsProduceOnePagePdf() {
    QString error;
    const QByteArray bytes = createBlankDocument(BlankDocumentOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());
    // 能被 PDFium 重新打开，而且页数就是 1。
    QCOMPARE(pageCountOf(bytes), 1);
}

void BlankDocumentTests::threePagesProduceThreePagePdf() {
    BlankDocumentOptions options;
    options.pageCount = 3;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QCOMPARE(pageCountOf(bytes), 3);
}

void BlankDocumentTests::zeroPageCountFails() {
    BlankDocumentOptions options;
    options.pageCount = 0;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
}

void BlankDocumentTests::negativePageCountFails() {
    BlankDocumentOptions options;
    options.pageCount = -5;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
}

void BlankDocumentTests::customSizeWithZeroFails() {
    BlankDocumentOptions options;
    options.size = PageSize::Custom;
    options.widthPt = 0.0;
    options.heightPt = 0.0;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
}

void BlankDocumentTests::customSizeWithNegativeFails() {
    BlankDocumentOptions options;
    options.size = PageSize::Custom;
    options.widthPt = -100.0;
    options.heightPt = 200.0;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
}

void BlankDocumentTests::customSizeAboveLimitFails() {
    BlankDocumentOptions options;
    options.size = PageSize::Custom;
    options.widthPt = 14401.0;
    options.heightPt = 200.0;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());

    // 恰好等于上限（200 英寸）应当放行。
    options.widthPt = 14400.0;
    QVERIFY(!createBlankDocument(options, &error).isEmpty());
}

void BlankDocumentTests::legalCustomSizeKeepsExactDimensions() {
    BlankDocumentOptions options;
    options.size = PageSize::Custom;
    options.widthPt = 400.5;
    options.heightPt = 300.25;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QCOMPARE(pageCountOf(bytes), 1);

    float left = 0.0F;
    float bottom = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    QVERIFY(mediaBoxOf(bytes, 0, &left, &bottom, &right, &top));
    QVERIFY(qAbs(left) < 0.01F);
    QVERIFY(qAbs(bottom) < 0.01F);
    QVERIFY(qAbs(right - 400.5F) < 0.01F);
    QVERIFY(qAbs(top - 300.25F) < 0.01F);
}

void BlankDocumentTests::pageCountIsClampedToUpperLimit() {
    BlankDocumentOptions options;
    options.pageCount = 1001;
    QString error;
    const QByteArray bytes = createBlankDocument(options, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    // 上限是 1000 页：1001 被夹到 1000，不是失败。
    QCOMPARE(pageCountOf(bytes), 1000);
}

void BlankDocumentTests::generatedBytesLookLikePdf() {
    QString error;
    const QByteArray bytes = createBlankDocument(BlankDocumentOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVERIFY(bytes.startsWith("%PDF-"));
    QVERIFY(bytes.contains("%%EOF"));
}

void BlankDocumentTests::failureReasonIsChinese() {
    BlankDocumentOptions options;
    options.pageCount = 0;
    QString error;
    createBlankDocument(options, &error);
    QVERIFY(!error.isEmpty());
    // 原因必须是中文，不能是英文占位串。
    bool hasChinese = false;
    for (const QChar character : error) {
        if (character.unicode() >= 0x4E00 && character.unicode() <= 0x9FFF) {
            hasChinese = true;
            break;
        }
    }
    QVERIFY2(hasChinese, qPrintable(error));
}

QTEST_GUILESS_MAIN(BlankDocumentTests)
#include "BlankDocumentTests.moc"
