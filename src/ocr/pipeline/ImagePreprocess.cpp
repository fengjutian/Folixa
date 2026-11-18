#include "ocr/ImagePreprocess.h"

#include <QVector>

#include <algorithm>

namespace aipdf::ocr {

QImage toGrayscale(const QImage& source) {
    if (source.isNull()) {
        return {};
    }
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    QImage gray(image.width(), image.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        uchar* dst = gray.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = src[x];
            // Rec.601 亮度，整数运算：0.299/0.587/0.114 放大 1000 倍。
            const int luma = (299 * qRed(pixel) + 587 * qGreen(pixel) + 114 * qBlue(pixel)) / 1000;
            dst[x] = static_cast<uchar>(std::clamp(luma, 0, 255));
        }
    }
    return gray;
}

QImage medianDenoise(const QImage& gray) {
    if (gray.isNull() || gray.format() != QImage::Format_Grayscale8) {
        return {};
    }
    const int width = gray.width();
    const int height = gray.height();
    if (width < 3 || height < 3) {
        // 太小的图没有 3×3 邻域，原样返回。
        return gray;
    }

    QImage result(width, height, QImage::Format_Grayscale8);
    result.fill(255);
    for (int y = 1; y < height - 1; ++y) {
        const uchar* above = gray.constScanLine(y - 1);
        const uchar* current = gray.constScanLine(y);
        const uchar* below = gray.constScanLine(y + 1);
        uchar* out = result.scanLine(y);
        for (int x = 1; x < width - 1; ++x) {
            uchar window[9] = {
                above[x - 1], above[x], above[x + 1],
                current[x - 1], current[x], current[x + 1],
                below[x - 1], below[x], below[x + 1],
            };
            std::sort(window, window + 9);
            out[x] = window[4];
        }
    }
    // 边缘一圈不处理，但也不该被 fill(255) 抹掉：拷回原值。
    for (int x = 0; x < width; ++x) {
        result.scanLine(0)[x] = gray.constScanLine(0)[x];
        result.scanLine(height - 1)[x] = gray.constScanLine(height - 1)[x];
    }
    for (int y = 0; y < height; ++y) {
        result.scanLine(y)[0] = gray.constScanLine(y)[0];
        result.scanLine(y)[width - 1] = gray.constScanLine(y)[width - 1];
    }
    return result;
}

int otsuThreshold(const QImage& gray) {
    if (gray.isNull() || gray.format() != QImage::Format_Grayscale8) {
        return 128;
    }
    QVector<double> histogram(256, 0.0);
    const int total = gray.width() * gray.height();
    if (total <= 0) {
        return 128;
    }
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            histogram[line[x]] += 1.0;
        }
    }

    double sum = 0.0;
    for (int i = 0; i < 256; ++i) {
        sum += i * histogram[i];
    }

    double sumBackground = 0.0;
    double weightBackground = 0.0;
    double bestVariance = -1.0;
    int bestThreshold = 128;
    for (int t = 0; t < 256; ++t) {
        weightBackground += histogram[t];
        if (weightBackground == 0.0) {
            continue;
        }
        const double weightForeground = total - weightBackground;
        if (weightForeground == 0.0) {
            break;
        }
        sumBackground += t * histogram[t];
        const double meanBackground = sumBackground / weightBackground;
        const double meanForeground = (sum - sumBackground) / weightForeground;
        const double delta = meanBackground - meanForeground;
        const double variance = weightBackground * weightForeground * delta * delta;
        if (variance > bestVariance) {
            bestVariance = variance;
            bestThreshold = t;
        }
    }
    return bestThreshold;
}

QImage binarize(const QImage& gray, int threshold) {
    if (gray.isNull() || gray.format() != QImage::Format_Grayscale8) {
        return {};
    }
    QImage result(gray.width(), gray.height(), QImage::Format_Grayscale8);
    const uchar cutoff = static_cast<uchar>(std::clamp(threshold, 0, 255));
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* src = gray.constScanLine(y);
        uchar* dst = result.scanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            dst[x] = src[x] > cutoff ? 255 : 0;
        }
    }
    return result;
}

QImage preprocessForOcr(const QImage& source) {
    const QImage gray = toGrayscale(source);
    if (gray.isNull()) {
        return {};
    }
    const QImage denoised = medianDenoise(gray);
    const QImage& working = denoised.isNull() ? gray : denoised;
    return binarize(working, otsuThreshold(working));
}

}  // namespace aipdf::ocr
