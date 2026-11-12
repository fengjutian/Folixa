#include "pdf/PdfiumAdapter.h"

#include "app/PageTransform.h"

#include <QImage>
#include <QRectF>
#include <QTest>

using aipdf::app::PageTransform;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::PdfOpenResult;
using aipdf::pdf::PdfPage;
using aipdf::pdf::TextPage;

namespace {

QString fixturePath(const char* name) {
    return QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') +
           QString::fromLatin1(name);
}

// 渲染结果里非白像素的包围盒（图像坐标，单位是像素）。
QRectF inkBoundingBox(const QImage& image) {
    double left = image.width();
    double top = image.height();
    double right = -1.0;
    double bottom = -1.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qRed(pixel) > 245 && qGreen(pixel) > 245 && qBlue(pixel) > 245) {
                continue;
            }
            left = qMin(left, static_cast<double>(x));
            top = qMin(top, static_cast<double>(y));
            right = qMax(right, static_cast<double>(x));
            bottom = qMax(bottom, static_cast<double>(y));
        }
    }
    if (right < 0.0) {
        return {};
    }
    return QRectF(left, top, right - left + 1.0, bottom - top + 1.0);
}

}  // namespace

class TextExtractionTests : public QObject {
    Q_OBJECT

private slots:
    void extractsPlainText();
    void charBoxesUsePdfUserSpace();
    void charBoxesAlignWithRenderedImage();
    void rotatedPageTextStaysAligned();
};

void TextExtractionTests::extractsPlainText() {
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath("normal.pdf"));
    QVERIFY(result.document.isValid());

    const std::optional<PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());

    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QCOMPARE(text->pageIndex, 0);
    QCOMPARE(text->plainText(), QStringLiteral("Hello PDFium"));
    QCOMPARE(text->charCount(), 12);
    QVERIFY(!text->unicodeMapError);
}

void TextExtractionTests::charBoxesUsePdfUserSpace() {
    // normal.pdf：MediaBox 200x100，文本基线在 y=60（PDF user space 左下原点）。
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath("normal.pdf"));
    const std::optional<PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());
    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY(!text->isEmpty());

    const aipdf::pdf::TextChar& first = text->chars.first();
    // 字符框必须在 PDF user space：'H' 的框中心在基线之上（y 明显大于 55），
    // 如果坐标系是左上原点，中心会落在 30 附近。
    const double centerY = first.box.center().y();
    QVERIFY2(centerY > 55.0,
             qPrintable(QStringLiteral("字符框 y 中心 %1 不是 user space 坐标").arg(centerY)));
    QVERIFY(centerY < 95.0);
    // 左边距 24 点。
    QVERIFY(first.box.left() > 20.0);
    QVERIFY(first.box.left() < 30.0);
}

void TextExtractionTests::charBoxesAlignWithRenderedImage() {
    // 关键检查：把字符框变换到显示空间后，必须盖住渲染图像里的墨迹。
    // 坐标系或旋转方向搞错时，墨迹会落在字符框之外，这个用例会立刻变红。
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath("normal.pdf"));
    const std::optional<PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());
    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());

    const aipdf::pdf::MediaBox media = page->mediaBox();
    QCOMPARE(media.width(), 200.0);
    QCOMPARE(media.height(), 100.0);

    for (int turns = 0; turns < 4; ++turns) {
        const QImage image = page->render(1.0, turns);
        QVERIFY(!image.isNull());

        PageTransform transform;
        transform.setPage(media.left, media.bottom, media.width(), media.height());
        transform.setRotationDegrees(turns * 90);

        QRectF unionBox;
        bool first = true;
        for (const aipdf::pdf::TextChar& character : text->chars) {
            const QRectF box = transform.toDisplay(character.box);
            unionBox = first ? box : unionBox.united(box);
            first = false;
        }
        QVERIFY(!unionBox.isEmpty());

        const QRectF ink = inkBoundingBox(image);
        QVERIFY2(!ink.isEmpty(), "渲染结果里没有找到文字墨迹");
        // 允许 2 点的抗锯齿误差。
        const QRectF allowed = unionBox.adjusted(-2.0, -2.0, 2.0, 2.0);
        QVERIFY2(allowed.contains(ink),
                 qPrintable(QStringLiteral("旋转 %1 圈时墨迹 %2,%3 %4x%5 超出字符框 %6,%7 %8x%9")
                                .arg(turns)
                                .arg(ink.left())
                                .arg(ink.top())
                                .arg(ink.width())
                                .arg(ink.height())
                                .arg(unionBox.left())
                                .arg(unionBox.top())
                                .arg(unionBox.width())
                                .arg(unionBox.height())));
    }
}

void TextExtractionTests::rotatedPageTextStaysAligned() {
    // rotated.pdf：页面自带 /Rotate 90，渲染时要用同样的角度。
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath("rotated.pdf"));
    const std::optional<PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->rotationDegrees(), 90);

    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QCOMPARE(text->plainText(), QStringLiteral("Rotated"));

    const aipdf::pdf::MediaBox media = page->mediaBox();
    PageTransform transform;
    transform.setPage(media.left, media.bottom, media.width(), media.height());
    // 页面自带旋转 + 用户旋转 0。
    transform.setRotationDegrees(page->rotationDegrees());

    QRectF unionBox;
    bool first = true;
    for (const aipdf::pdf::TextChar& character : text->chars) {
        const QRectF box = transform.toDisplay(character.box);
        unionBox = first ? box : unionBox.united(box);
        first = false;
    }

    const QImage image = page->render(1.0, 0);
    QVERIFY(!image.isNull());
    const QRectF ink = inkBoundingBox(image);
    QVERIFY(!ink.isEmpty());
    const QRectF allowed = unionBox.adjusted(-2.0, -2.0, 2.0, 2.0);
    QVERIFY2(allowed.contains(ink),
             qPrintable(QStringLiteral("旋转页的墨迹超出字符框：墨迹 %1,%2 %3x%4 字符框 %5,%6 %7x%8")
                            .arg(ink.left())
                            .arg(ink.top())
                            .arg(ink.width())
                            .arg(ink.height())
                            .arg(unionBox.left())
                            .arg(unionBox.top())
                            .arg(unionBox.width())
                            .arg(unionBox.height())));
}

QTEST_MAIN(TextExtractionTests)
#include "TextExtractionTests.moc"
