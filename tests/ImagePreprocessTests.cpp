// v0.13.0：OCR 前图像预处理的单元测试。
//
// 预处理是「纯函数」，所以这里不需要任何图片素材：用 QImage 现画几个像素就能断言。
#include "ocr/ImagePreprocess.h"

#include <QImage>
#include <QtTest>

using namespace aipdf::ocr;

namespace {

QImage solidImage(int width, int height, QRgb color) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

// 左半边黑、右半边白：Otsu 应该落在这两档之间。
QImage bimodalImage() {
    QImage image(64, 32, QImage::Format_ARGB32);
    image.fill(Qt::white);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width() / 2; ++x) {
            image.setPixel(x, y, qRgb(20, 20, 20));
        }
    }
    return image;
}

}  // namespace

class ImagePreprocessTests : public QObject {
    Q_OBJECT

private slots:
    void grayscaleUsesLuminance();
    void grayscaleKeepsSize();
    void otsuSeparatesBimodalImage();
    void otsuHandlesUniformImage();
    void binarizeProducesOnlyBlackAndWhite();
    void medianRemovesIsolatedPixel();
    void medianKeepsEdges();
    void preprocessPipelineIsBinaryAndSameSize();
    void nullImageReturnsNull();
};

void ImagePreprocessTests::grayscaleUsesLuminance() {
    QImage image = solidImage(2, 2, qRgb(255, 0, 0));
    const QImage gray = toGrayscale(image);
    // 纯红按 Rec.601 约 76（0.299*255）。
    QCOMPARE(static_cast<int>(gray.constScanLine(0)[0]), 76);

    const QImage white = toGrayscale(solidImage(2, 2, qRgb(255, 255, 255)));
    QCOMPARE(static_cast<int>(white.constScanLine(0)[0]), 255);

    const QImage black = toGrayscale(solidImage(2, 2, qRgb(0, 0, 0)));
    QCOMPARE(static_cast<int>(black.constScanLine(0)[0]), 0);
}

void ImagePreprocessTests::grayscaleKeepsSize() {
    const QImage gray = toGrayscale(solidImage(17, 5, qRgb(10, 20, 30)));
    QCOMPARE(gray.size(), QSize(17, 5));
    QCOMPARE(gray.format(), QImage::Format_Grayscale8);
}

void ImagePreprocessTests::otsuSeparatesBimodalImage() {
    const QImage gray = toGrayscale(bimodalImage());
    const int threshold = otsuThreshold(gray);
    // 两档是 20 和 255：阈值必须落在它们之间，否则二值化会把两类混在一起。
    QVERIFY2(threshold >= 20 && threshold < 255, qPrintable(QString::number(threshold)));
}

void ImagePreprocessTests::otsuHandlesUniformImage() {
    const QImage gray = toGrayscale(solidImage(8, 8, qRgb(128, 128, 128)));
    const int threshold = otsuThreshold(gray);
    QVERIFY(threshold >= 0 && threshold <= 255);
}

void ImagePreprocessTests::binarizeProducesOnlyBlackAndWhite() {
    const QImage binary = binarize(toGrayscale(bimodalImage()), 128);
    for (int y = 0; y < binary.height(); ++y) {
        const uchar* line = binary.constScanLine(y);
        for (int x = 0; x < binary.width(); ++x) {
            QVERIFY(line[x] == 0 || line[x] == 255);
        }
    }
}

void ImagePreprocessTests::medianRemovesIsolatedPixel() {
    QImage gray(5, 5, QImage::Format_Grayscale8);
    gray.fill(255);
    gray.setPixel(2, 2, 0);
    const QImage denoised = medianDenoise(gray);
    QCOMPARE(static_cast<int>(denoised.constScanLine(2)[2]), 255);
}

void ImagePreprocessTests::medianKeepsEdges() {
    // 一条竖线（3 像素宽）经过中值滤波后仍然存在：均值滤波会把它抹淡。
    QImage gray(9, 9, QImage::Format_Grayscale8);
    gray.fill(255);
    for (int y = 0; y < 9; ++y) {
        for (int x = 4; x < 7; ++x) {
            gray.setPixel(x, y, 0);
        }
    }
    const QImage denoised = medianDenoise(gray);
    QCOMPARE(static_cast<int>(denoised.constScanLine(4)[5]), 0);
}

void ImagePreprocessTests::preprocessPipelineIsBinaryAndSameSize() {
    const QImage input = bimodalImage();
    const QImage output = preprocessForOcr(input);
    QCOMPARE(output.size(), input.size());
    QCOMPARE(output.format(), QImage::Format_Grayscale8);
    const uchar* line = output.constScanLine(0);
    QVERIFY(line[0] == 0 || line[0] == 255);
}

void ImagePreprocessTests::nullImageReturnsNull() {
    QVERIFY(toGrayscale(QImage()).isNull());
    QVERIFY(medianDenoise(QImage()).isNull());
    QVERIFY(binarize(QImage(), 128).isNull());
    QVERIFY(preprocessForOcr(QImage()).isNull());
}

QTEST_MAIN(ImagePreprocessTests)
#include "ImagePreprocessTests.moc"
