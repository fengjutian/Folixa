#include "ocr/pipeline/ImagePreprocess.h"

#include <QPainter>
#include <QTransform>
#include <QVector>
#include <QtMath>

#include <algorithm>
#include <cmath>

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

namespace {

// 输入可能是彩色也可能是灰度，统一成 0/255 的二值图。
// 已经是二值的原样返回：重复做一次 Otsu 会让阈值随内容漂移，反而伤到细笔画。
QImage toBinaryImage(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    if (image.format() == QImage::Format_Grayscale8) {
        bool binary = true;
        for (int y = 0; y < image.height() && binary; ++y) {
            const uchar* line = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x) {
                if (line[x] != 0 && line[x] != 255) {
                    binary = false;
                    break;
                }
            }
        }
        if (binary) {
            return image;
        }
        return binarize(image, otsuThreshold(image));
    }
    const QImage gray = toGrayscale(image);
    if (gray.isNull()) {
        return {};
    }
    return binarize(gray, otsuThreshold(gray));
}

// 水平投影方差：行墨量直方图的方差。行结构越整齐，方差越大。
double profileVariance(const QVector<int>& counts) {
    if (counts.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (int count : counts) {
        sum += count;
    }
    const double mean = sum / static_cast<double>(counts.size());
    double variance = 0.0;
    for (int count : counts) {
        const double delta = static_cast<double>(count) - mean;
        variance += delta * delta;
    }
    return variance / static_cast<double>(counts.size());
}

// 图像自身的行墨量方差（校正前后各算一次，用来证明「真的摆正了」）。
double imageProfileVariance(const QImage& binary) {
    QVector<int> counts(binary.height(), 0);
    for (int y = 0; y < binary.height(); ++y) {
        const uchar* line = binary.constScanLine(y);
        int ink = 0;
        for (int x = 0; x < binary.width(); ++x) {
            if (line[x] == 0) {
                ++ink;
            }
        }
        counts[y] = ink;
    }
    return profileVariance(counts);
}

// 把黑像素按 angleDeg 绕图心旋转后统计行墨量，取方差。
//
// 这里只旋转坐标、不重采样：评估阶段做插值会把细笔画糊掉，峰值位置会偏。
// 旋转用 QTransform 的同一套公式，所以搜出来的角度可以直接喂给 QImage::transformed。
//
// offsets 是黑像素相对图心的偏移量：每个候选角度都要把全部墨点过一遍，
// 预先存好偏移能省掉内层循环里的减法，大页面上这是最热的一段。
double rotatedProfileVariance(const QVector<float>& offsetsX, const QVector<float>& offsetsY, double angleDeg,
                               double radius) {
    const double radians = qDegreesToRadians(angleDeg);
    const double sinA = std::sin(radians);
    const double cosA = std::cos(radians);

    // 所有候选角度共用同一套行号桶：桶数固定，比较才公平，
    // 旋转后越出画布的像素也只是换个桶，不会凭空多出或少掉墨量。
    const int bins = static_cast<int>(std::ceil(radius * 2.0)) + 2;
    QVector<int> counts(bins, 0);
    const int inkCount = offsetsX.size();
    for (int i = 0; i < inkCount; ++i) {
        const double rotatedY = offsetsX[i] * sinA + offsetsY[i] * cosA;
        // 旋转不改变到图心的距离，rotatedY 必然落在 ±radius 内，桶号不会越界。
        int row = static_cast<int>(rotatedY + radius);
        row = std::clamp(row, 0, bins - 1);
        ++counts[row];
    }
    return profileVariance(counts);
}

// 旋转并把空白合成到白底。
//
// 用透明画布而不是白画布：白画布旋转后「没画到的地方」和「画到的白背景」分不开，
// 而透明画布里 alpha=0 的就是旋转后的空洞，直接涂白即可，不会留下大片黑边
// 把后续二值化带进沟里。
QImage rotateOnWhite(const QImage& source, double angleDeg, bool smooth) {
    const int width = source.width();
    const int height = source.height();
    if (width <= 0 || height <= 0) {
        return {};
    }
    const double radians = qDegreesToRadians(angleDeg);
    const double sinA = std::abs(std::sin(radians));
    const double cosA = std::abs(std::cos(radians));

    // 旋转后页面的外接矩形：多留 4 像素给插值和舍入。
    const int canvasWidth = static_cast<int>(std::ceil(width * cosA + height * sinA)) + 4;
    const int canvasHeight = static_cast<int>(std::ceil(width * sinA + height * cosA)) + 4;

    QImage canvas(canvasWidth, canvasHeight, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.drawImage((canvasWidth - width) / 2, (canvasHeight - height) / 2,
                          source.convertToFormat(QImage::Format_ARGB32));
    }

    QTransform transform;
    transform.translate(canvasWidth / 2.0, canvasHeight / 2.0);
    transform.rotate(angleDeg);
    transform.translate(-canvasWidth / 2.0, -canvasHeight / 2.0);
    const QImage rotated =
        canvas.transformed(transform, smooth ? Qt::SmoothTransformation : Qt::FastTransformation);

    // 内容包围盒：alpha 非零处即旋转后的页面本体。
    int minX = rotated.width();
    int minY = rotated.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < rotated.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rotated.constScanLine(y));
        for (int x = 0; x < rotated.width(); ++x) {
            if (qAlpha(line[x]) == 0) {
                continue;
            }
            minX = qMin(minX, x);
            maxX = qMax(maxX, x);
            minY = qMin(minY, y);
            maxY = qMax(maxY, y);
        }
    }
    if (maxX < 0) {
        return {};
    }
    const QImage cropped = rotated.copy(QRect(minX, minY, maxX - minX + 1, maxY - minY + 1));

    // 合成到白底：半透明的插值边缘按 alpha 混白，避免留一圈灰边。
    QImage composed(cropped.width(), cropped.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < cropped.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(cropped.constScanLine(y));
        uchar* dst = composed.scanLine(y);
        for (int x = 0; x < cropped.width(); ++x) {
            const int alpha = qAlpha(src[x]);
            if (alpha == 255) {
                dst[x] = static_cast<uchar>(qGray(src[x]));
            } else if (alpha == 0) {
                dst[x] = 255;
            } else {
                const int value = (alpha * qGray(src[x]) + (255 - alpha) * 255 + 127) / 255;
                dst[x] = static_cast<uchar>(std::clamp(value, 0, 255));
            }
        }
    }
    // 旋转+插值后不再是纯二值，再走一次 Otsu 收回硬边。
    return binarize(composed, otsuThreshold(composed));
}

}  // namespace

QImage connectedDenoise(const QImage& binary, const ConnectedDenoiseOptions& options) {
    const QImage image = toBinaryImage(binary);
    if (image.isNull()) {
        return {};
    }
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0) {
        return image;
    }

    const int minArea = std::max(1, options.minArea);
    const int minStrokeLength = std::max(1, options.minStrokeLength);

    // 行首地址缓存一遍：列扫描时要反复按行取像素，存指针比反复调 constScanLine 快得多。
    QVector<const uchar*> rows(height);
    for (int y = 0; y < height; ++y) {
        rows[y] = image.constScanLine(y);
    }

    // 8 邻域连通域标记。用显式栈做洪水填充：递归在大图上会爆栈。
    QVector<int> labels(width * height, 0);
    QVector<int> stack;
    QVector<int> areas;
    QVector<int> maxRunH;
    QVector<int> maxRunV;
    areas.append(0);
    maxRunH.append(0);
    maxRunV.append(0);

    int nextLabel = 0;
    for (int y = 0; y < height; ++y) {
        const uchar* line = rows[y];
        for (int x = 0; x < width; ++x) {
            if (line[x] != 0) {
                continue;
            }
            const int start = y * width + x;
            if (labels[start] != 0) {
                continue;
            }
            ++nextLabel;
            areas.append(0);
            maxRunH.append(0);
            maxRunV.append(0);
            int area = 0;
            stack.clear();
            stack.append(start);
            labels[start] = nextLabel;
            while (!stack.isEmpty()) {
                const int index = stack.takeLast();
                const int cy = index / width;
                const int cx = index % width;
                ++area;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int ny = cy + dy;
                    if (ny < 0 || ny >= height) {
                        continue;
                    }
                    const uchar* neighborRow = rows[ny];
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int nx = cx + dx;
                        if (nx < 0 || nx >= width) {
                            continue;
                        }
                        const int neighbor = ny * width + nx;
                        if (labels[neighbor] != 0 || neighborRow[nx] != 0) {
                            continue;
                        }
                        labels[neighbor] = nextLabel;
                        stack.append(neighbor);
                    }
                }
            }
            areas[nextLabel] = area;
        }
    }

    // 每行的连续黑段长度 → 该连通域的水平笔画长度。
    // 相邻黑像素必然同属一个连通域，所以整段黑只对应一个标签。
    for (int y = 0; y < height; ++y) {
        const uchar* line = rows[y];
        int x = 0;
        while (x < width) {
            if (line[x] != 0) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < width && line[x] == 0) {
                ++x;
            }
            const int label = labels[y * width + start];
            if (label > 0) {
                maxRunH[label] = qMax(maxRunH[label], x - start);
            }
        }
    }
    // 列方向同理，得到竖线长度。
    for (int x = 0; x < width; ++x) {
        int y = 0;
        while (y < height) {
            if (rows[y][x] != 0) {
                ++y;
                continue;
            }
            const int start = y;
            while (y < height && rows[y][x] == 0) {
                ++y;
            }
            const int label = labels[start * width + x];
            if (label > 0) {
                maxRunV[label] = qMax(maxRunV[label], y - start);
            }
        }
    }

    // 判定并输出：面积够大就留；面积不够但里面含细长笔画（下划线、表格线、
    // 或粘在笔画上的飞点）也留。
    QImage result(width, height, QImage::Format_Grayscale8);
    result.fill(255);
    for (int y = 0; y < height; ++y) {
        const uchar* line = rows[y];
        uchar* out = result.scanLine(y);
        for (int x = 0; x < width; ++x) {
            if (line[x] != 0) {
                continue;
            }
            const int label = labels[y * width + x];
            bool keep = areas[label] >= minArea;
            if (!keep && options.protectStrokes &&
                (maxRunH[label] >= minStrokeLength || maxRunV[label] >= minStrokeLength)) {
                keep = true;
            }
            if (keep) {
                out[x] = 0;
            }
        }
    }
    return result;
}

// 内部用：假定输入已经是二值图，避免 deskew 里重复做一次二值化扫描。
static double estimateSkewAngleBinary(const QImage& image, const DeskewOptions& options) {
    const int width = image.width();
    const int height = image.height();

    const double centerX = (width - 1) / 2.0;
    const double centerY = (height - 1) / 2.0;

    // 墨点相对图心的偏移：每个候选角度都要把这批点重算一遍，先存好省内层开销。
    QVector<float> offsetsX;
    QVector<float> offsetsY;
    for (int y = 0; y < height; ++y) {
        const uchar* line = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            if (line[x] == 0) {
                offsetsX.append(static_cast<float>(x - centerX));
                offsetsY.append(static_cast<float>(y - centerY));
            }
        }
    }
    if (offsetsX.isEmpty()) {
        return 0.0;
    }

    // 退化判定：白页没有墨；每行墨量完全一致（纯色块、纯竖线）时投影方差恒为 0，
    // 「方差最大者」没有意义。这两种情况一律按 0 度处理。
    const double radius = std::sqrt(static_cast<double>(width) * width + static_cast<double>(height) * height) / 2.0;
    const double flatVariance = rotatedProfileVariance(offsetsX, offsetsY, 0.0, radius);
    const double meanInk = static_cast<double>(offsetsX.size()) / static_cast<double>(std::max(1, height));
    if (flatVariance <= 1e-6 * meanInk * meanInk) {
        return 0.0;
    }

    const double maxAngle = std::clamp(std::abs(options.maxAngleDeg), 0.0, 45.0);
    const double coarseStep = std::clamp(std::abs(options.coarseStepDeg), 0.01, 5.0);
    const double fineStep = std::clamp(std::abs(options.fineStepDeg), 0.001, 1.0);

    // 粗搜用抽稀后的墨点：一张 300 DPI 页面有上百万个墨点，粗搜要算 20 多次，
    // 全量算下来是整个纠偏里最贵的一段。抽到 25 万点以内后，峰值位置不受影响
    //（每行仍按比例取样），精修阶段再用全量墨点把角度定死。
    const int maxCoarsePoints = 250000;
    const int inkCount = static_cast<int>(offsetsX.size());
    const int stride = std::max(1, (inkCount + maxCoarsePoints - 1) / maxCoarsePoints);
    QVector<float> coarseX;
    QVector<float> coarseY;
    if (stride > 1) {
        coarseX.reserve(offsetsX.size() / stride + 1);
        coarseY.reserve(offsetsY.size() / stride + 1);
        for (int i = 0; i < offsetsX.size(); i += stride) {
            coarseX.append(offsetsX[i]);
            coarseY.append(offsetsY[i]);
        }
    }
    const QVector<float>& coarseOffsetsX = stride > 1 ? coarseX : offsetsX;
    const QVector<float>& coarseOffsetsY = stride > 1 ? coarseY : offsetsY;

    // 粗搜：先在 0.5° 的网格上定位峰值区间。
    double bestAngle = 0.0;
    double bestVariance = -1.0;
    const int coarseSteps = static_cast<int>(std::floor(maxAngle / coarseStep));
    for (int i = -coarseSteps; i <= coarseSteps; ++i) {
        const double angle = i * coarseStep;
        const double variance = rotatedProfileVariance(coarseOffsetsX, coarseOffsetsY, angle, radius);
        // 并列时取更接近 0 的角度：本来就是正的页面不能被「原样返回」以外的角度抢走。
        if (variance > bestVariance + 1e-9 ||
            (std::abs(variance - bestVariance) <= 1e-9 && std::abs(angle) < std::abs(bestAngle))) {
            bestVariance = variance;
            bestAngle = angle;
        }
    }

    // 精修：在粗搜峰值附近按 0.1° 再扫一遍，把角度精度压到 0.05° 以内。
    const double coarseBest = bestAngle;
    const int fineSteps = static_cast<int>(std::llround(2.0 * coarseStep / fineStep));
    for (int i = -fineSteps; i <= fineSteps; ++i) {
        const double angle = coarseBest + i * fineStep;
        if (angle < -maxAngle || angle > maxAngle) {
            continue;
        }
        const double variance = rotatedProfileVariance(offsetsX, offsetsY, angle, radius);
        if (variance > bestVariance + 1e-9 ||
            (std::abs(variance - bestVariance) <= 1e-9 && std::abs(angle) < std::abs(bestAngle))) {
            bestVariance = variance;
            bestAngle = angle;
        }
    }
    return bestAngle;
}

double estimateSkewAngle(const QImage& binary, const DeskewOptions& options) {
    const QImage image = toBinaryImage(binary);
    if (image.isNull()) {
        return 0.0;
    }
    return estimateSkewAngleBinary(image, options);
}

DeskewResult deskew(const QImage& binary, const DeskewOptions& options) {
    DeskewResult result;
    const QImage image = toBinaryImage(binary);
    if (image.isNull()) {
        return result;
    }
    result.valid = true;
    result.image = image;
    result.varianceBefore = imageProfileVariance(image);

    const double angle = estimateSkewAngleBinary(image, options);
    result.angleDeg = angle;
    if (std::abs(angle) < 0.05) {
        // 角度小到肉眼看不出来，就别去折腾像素了：保持原图最稳。
        result.varianceAfter = result.varianceBefore;
        return result;
    }

    const QImage rotated = rotateOnWhite(image, angle, options.smoothInterpolation);
    if (!rotated.isNull()) {
        result.image = rotated;
        result.varianceAfter = imageProfileVariance(rotated);
    }
    return result;
}

ImagePrepResult preprocessScannedPage(const QImage& source, const ImagePrepOptions& options) {
    ImagePrepResult result;
    const QImage binarized = preprocessForOcr(source);
    if (binarized.isNull()) {
        return result;
    }
    result.valid = true;

    QImage image = binarized;
    if (options.denoise) {
        const QImage denoised = connectedDenoise(image, options.denoiseOptions);
        if (!denoised.isNull()) {
            image = denoised;
        }
    }
    if (options.deskew) {
        const DeskewResult skewed = deskew(image, options.deskewOptions);
        if (skewed.valid && !skewed.image.isNull()) {
            image = skewed.image;
            result.deskewAngleDeg = skewed.angleDeg;
        }
    }
    result.image = image;
    return result;
}

}  // namespace aipdf::ocr
