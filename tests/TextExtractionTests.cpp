#include "pdf/PdfiumAdapter.h"

#include "app/PageTransform.h"

#include <QFile>
#include <QImage>
#include <QRectF>
#include <QTest>
#include <QVector>

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

// 生成一个最小的中文 PDF：Type0 / Identity-H 编码 + ToUnicode CMap。
//
// 不内嵌字体：文本提取只依赖 ToUnicode，够验证「中文能被提出来」这条链路；
// 渲染是否好看由带内嵌字体的验收夹具负责。
QByteArray buildCjkPdf(const QString& text) {
    QByteArray hexCodes;
    QString bfChars;
    int cid = 1;
    const QVector<uint> codePoints = text.toUcs4();
    for (uint codePoint : codePoints) {
        hexCodes += QStringLiteral("%1").arg(cid, 4, 16, QLatin1Char('0')).toUpper().toLatin1();
        const QString utf16 = QString::fromUcs4(&codePoint, 1);
        QString target;
        for (const QChar ch : utf16) {
            target += QStringLiteral("%1").arg(ch.unicode(), 4, 16, QLatin1Char('0')).toUpper();
        }
        bfChars += QStringLiteral("<%1> <%2>\n")
                       .arg(QStringLiteral("%1").arg(cid, 4, 16, QLatin1Char('0')).toUpper())
                       .arg(target);
        ++cid;
    }

    const QByteArray content =
        "BT /F1 24 Tf 40 100 Td <" + hexCodes + "> Tj ET";

    const QString cmap =
        QStringLiteral("/CIDInit /ProcSet findresource begin\n"
                       "12 dict begin\n"
                       "begincmap\n"
                       "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
                       "/CMapName /Adobe-Identity-UCS def\n"
                       "/CMapType 2 def\n"
                       "1 begincodespacerange\n"
                       "<0000> <FFFF>\n"
                       "endcodespacerange\n"
                       "%1 beginbfchar\n"
                       "%2"
                       "endbfchar\n"
                       "endcmap\n"
                       "CMapName currentdict /CMap defineresource pop\n"
                       "end\n"
                       "end\n")
            .arg(codePoints.size())
            .arg(bfChars);

    QList<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(
        3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 200] "
                      "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(
        4, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
               .arg(content.size())
               .arg(QString::fromLatin1(content)).toLatin1()));
    objects.append(qMakePair(
        5, QByteArray("<< /Type /Font /Subtype /Type0 /BaseFont /STSong "
                      "/Encoding /Identity-H /DescendantFonts [6 0 R] "
                      "/ToUnicode 7 0 R >>")));
    objects.append(qMakePair(
        6, QByteArray("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /STSong "
                      "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) "
                      "/Supplement 0 >> /FontDescriptor 8 0 R /DW 1000 >>")));
    objects.append(qMakePair(
        7, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
               .arg(cmap.toUtf8().size())
               .arg(cmap).toUtf8()));
    objects.append(qMakePair(
        8, QByteArray("<< /Type /FontDescriptor /FontName /STSong /Flags 4 "
                      "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 880 "
                      "/Descent -120 /CapHeight 880 /StemV 80 >>")));

    QByteArray out = QByteArray("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = offsets.size() + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
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
    void extractsCjkText();
    void extractsCjkFixtureText();
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

void TextExtractionTests::extractsCjkText() {
    // 中文文本层：ToUnicode CMap 把 CID 映射回 Unicode，
    // 提取必须原样拿回中文字符（而不是问号或乱码）。
    const QString expected = QString::fromUtf8("中文选择测试");
    const QString path = fixturePath("cjk-inline.pdf");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(buildCjkPdf(expected)) > 0);
    file.close();

    const PdfOpenResult result = aipdf::pdf::openDocument(path);
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));
    const std::optional<PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());
    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QCOMPARE(text->plainText(), expected);
    QCOMPARE(text->charCount(), expected.size());
    QVERIFY(!text->unicodeMapError);
}

void TextExtractionTests::extractsCjkFixtureText() {
    // cjk.pdf：内嵌中文字体的真实文档（Type0/Identity-H + ToUnicode）。
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath("cjk.pdf"));
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));
    const std::optional<PdfPage> page = result.document.page(0);
    QVERIFY(page.has_value());
    const std::optional<TextPage> text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY(!text->isEmpty());

    const QString plain = text->plainText();
    QVERIFY2(plain.contains(QString::fromUtf8("中文选择测试")),
             qPrintable(QStringLiteral("中文夹具提取结果：%1").arg(plain)));

    // 字符框必须能用：同一行内 x 单调递增，宽度为正。
    // PDFium 在换行处会插入 \r 控制字符（0 宽 0 高），不参与几何检查。
    double previousRight = -1.0;
    double previousTop = -1.0;
    for (const aipdf::pdf::TextChar& character : text->chars) {
        if (character.text.size() == 1 && character.text.at(0).unicode() < 0x20) {
            continue;
        }
        QVERIFY(character.box.width() > 0.0);
        QVERIFY(character.box.height() > 0.0);
        // 换行（基线变了）就重新起算 x。
        const bool sameLine = previousTop >= 0.0 &&
                              qAbs(character.box.top() - previousTop) < character.box.height();
        if (sameLine) {
            QVERIFY2(character.box.left() >= previousRight - 0.5,
                     qPrintable(QStringLiteral("中文字符框不连续：%1 之后是 %2")
                                    .arg(previousRight)
                                    .arg(character.box.left())));
        }
        previousRight = character.box.right();
        previousTop = character.box.top();
    }
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
             qPrintable(QStringLiteral("旋转页的墨迹超出字符框：图像 %1x%2 墨迹 %3,%4 %5x%6 字符框 %7,%8 %9x%10")
                            .arg(image.width())
                            .arg(image.height())
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
