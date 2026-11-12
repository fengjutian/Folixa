#include "pdf/PdfiumAdapter.h"

#include <QDebug>
#include <QTest>

using namespace aipdf;

class TextProbe : public QObject {
    Q_OBJECT

private slots:
    void probeNormal();
    void probeRotated();
};

void TextProbe::probeNormal() {
    const QString path = QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + "/normal.pdf";
    const pdf::PdfOpenResult result = pdf::openDocument(path);
    const auto page = result.document.page(0);
    const auto media = page->mediaBox();
    qDebug() << "[normal] media" << media.left << media.bottom << media.right << media.top
             << "rot" << page->rotationDegrees();
    for (int turns = 0; turns < 4; ++turns) {
        const QImage image = page->render(1.0, turns);
        qDebug() << "[normal] turns" << turns << "image" << image.width() << "x" << image.height();
    }
}

void TextProbe::probeRotated() {
    const QString path = QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + "/rotated.pdf";
    const pdf::PdfOpenResult result = pdf::openDocument(path);
    const auto page = result.document.page(0);
    const auto media = page->mediaBox();
    qDebug() << "[rotated] media" << media.left << media.bottom << media.right << media.top
             << "rot" << page->rotationDegrees()
             << "display" << page->displayWidthPoints() << "x" << page->displayHeightPoints();
    const auto text = page->extractText();
    qDebug() << "[rotated] text" << text->plainText();
    for (int i = 0; i < qMin(3, text->charCount()); ++i) {
        const auto& c = text->chars.at(i);
        qDebug() << "[rotated] char" << i << c.text << "x" << c.box.left() << ".." << c.box.right()
                 << "y" << c.box.top() << ".." << c.box.bottom();
    }
    for (int turns = 0; turns < 4; ++turns) {
        const QImage image = page->render(1.0, turns);
        // 找墨迹包围盒
        int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = image.pixel(x, y);
                if (qRed(p) > 245 && qGreen(p) > 245 && qBlue(p) > 245) continue;
                minX = qMin(minX, x); minY = qMin(minY, y);
                maxX = qMax(maxX, x); maxY = qMax(maxY, y);
            }
        }
        qDebug() << "[rotated] turns" << turns << "image" << image.width() << "x" << image.height()
                 << "ink" << minX << "," << minY << ".." << maxX << "," << maxY;
    }
}

QTEST_MAIN(TextProbe)
#include "TextProbe.moc"
