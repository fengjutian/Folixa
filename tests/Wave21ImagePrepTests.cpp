// 2026-11-19 wave21：扫描件图像优化（连通域去噪 + 水平投影方差纠偏）的专项用例。
//
// 全部夹具都是现画的点阵图：不依赖任何外部图片文件，换台机器跑结果也一样。
#include "ocr/pipeline/ImagePreprocess.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <QtMath>
#include <QtTest>

#include <cmath>

using namespace aipdf::ocr;

namespace {

// 一页「文字」：每行由若干黑色矩形（词块）拼成，行距 24 像素、行高 6 像素。
// 行内是实心块，行间全白——这正是水平投影方差法要找的结构。
QImage makeTextPage(int width, int height, int lineCount = 10) {
    QImage image(width, height, QImage::Format_Grayscale8);
    image.fill(255);
    const int lineHeight = 6;
    const int lineGap = 24;
    for (int i = 0; i < lineCount; ++i) {
        const int top = 20 + i * lineGap;
        if (top + lineHeight >= height) {
            break;
        }
        int x = 20;
        while (x < width - 40) {
            const int wordWidth = 18 + ((i * 7 + x) % 22);
            const int end = qMin(x + wordWidth, width - 10);
            for (int y = top; y < top + lineHeight; ++y) {
                uchar* line = image.scanLine(y);
                for (int dx = x; dx < end; ++dx) {
                    line[dx] = 0;
                }
            }
            x = end + 12;
        }
    }
    return image;
}

// 最近邻旋转夹具：正角度表示页面右倾（往下斜）。
// 不用插值，保证夹具是硬边二值，角度不会因为重采样而漂。
QImage rotateNearest(const QImage& source, double degrees) {
    const int width = source.width();
    const int height = source.height();
    const double radians = qDegreesToRadians(degrees);
    const double cosA = std::cos(radians);
    const double sinA = std::sin(radians);
    const double centerX = (width - 1) / 2.0;
    const double centerY = (height - 1) / 2.0;

    QImage result(width, height, QImage::Format_Grayscale8);
    result.fill(255);
    for (int y = 0; y < height; ++y) {
        uchar* dst = result.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const double dx = x - centerX;
            const double dy = y - centerY;
            const double sx = cosA * dx + sinA * dy + centerX;
            const double sy = -sinA * dx + cosA * dy + centerY;
            const int ix = static_cast<int>(std::floor(sx + 0.5));
            const int iy = static_cast<int>(std::floor(sy + 0.5));
            if (ix < 0 || iy < 0 || ix >= width || iy >= height) {
                continue;
            }
            dst[x] = source.constScanLine(iy)[ix];
        }
    }
    return result;
}

// 独立实现的连通域计数，用来交叉验证被测实现的删除效果。
int countComponents(const QImage& image) {
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }
    QVector<bool> visited(width * height, false);
    int components = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (image.constScanLine(y)[x] != 0) {
                continue;
            }
            const int start = y * width + x;
            if (visited[start]) {
                continue;
            }
            ++components;
            QVector<int> stack;
            stack.append(start);
            visited[start] = true;
            while (!stack.isEmpty()) {
                const int index = stack.takeLast();
                const int cy = index / width;
                const int cx = index % width;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        const int neighbor = ny * width + nx;
                        if (visited[neighbor] || image.constScanLine(ny)[nx] != 0) {
                            continue;
                        }
                        visited[neighbor] = true;
                        stack.append(neighbor);
                    }
                }
            }
        }
    }
    return components;
}

struct Speck {
    int x;
    int y;
    int size;
};

// 孤立的噪点位置：全部落在文字行以下的空白区，保证不与任何笔画相连。
QVector<Speck> makeSpecks() {
    return QVector<Speck>{
        {30, 372, 1},  {120, 379, 1}, {210, 371, 2}, {300, 378, 1},
        {390, 372, 2}, {480, 379, 1}, {560, 371, 1}, {90, 366, 1},
    };
}

void addSpecks(QImage& page, const QVector<Speck>& specks) {
    for (const Speck& speck : specks) {
        for (int dy = 0; dy < speck.size; ++dy) {
            for (int dx = 0; dx < speck.size; ++dx) {
                page.setPixel(speck.x + dx, speck.y + dy, 0);
            }
        }
    }
}

int pixelAt(const QImage& image, int x, int y) {
    if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
        return 255;
    }
    return static_cast<int>(image.constScanLine(y)[x]);
}

}  // namespace

class Wave21ImagePrepTests : public QObject {
    Q_OBJECT

private slots:
    void deskewDetectsTiltedTextAngle();
    void deskewIncreasesProjectionVariance();
    void deskewKeepsHorizontalPageAtZero();
    void deskewLeavesNoBlackBorder();
    void deskewHandlesLargePage();
    void deskewHandlesBlankAndSolidPages();
    void connectedDenoiseRemovesSpecks();
    void connectedDenoiseKeepsThinStrokes();
    void connectedDenoiseHandlesBlankAndSolidPages();
    void preprocessScannedPageRunsWholeChain();
    void preprocessForOcrKeepsLegacyBehavior();
    void preprocessDoesNotModifyInput();
};

void Wave21ImagePrepTests::deskewDetectsTiltedTextAngle() {
    const QImage page = makeTextPage(600, 400);
    const QImage tilted = rotateNearest(page, 3.0);

    const DeskewResult result = deskew(tilted);
    QVERIFY(result.valid);
    // 页面右倾 3.0°，要往回掰 3.0°，所以检出的角度是 -3.0。
    // 精修步长 0.1°，误差上限 0.05°；留 0.5° 的余量给夹具本身的量化误差。
    QVERIFY2(std::abs(result.angleDeg + 3.0) < 0.5, qPrintable(QString::number(result.angleDeg)));
}

void Wave21ImagePrepTests::deskewIncreasesProjectionVariance() {
    const QImage page = makeTextPage(600, 400);
    const QImage tilted = rotateNearest(page, 3.0);

    const DeskewResult result = deskew(tilted);
    QVERIFY(result.valid);
    QVERIFY(result.varianceBefore > 0.0);
    // 行结构被摆正之后，同一行的字应该重新聚在一起，投影方差必须明显变大。
    QVERIFY2(result.varianceAfter > result.varianceBefore * 1.5,
             qPrintable(QStringLiteral("before=%1 after=%2")
                            .arg(result.varianceBefore)
                            .arg(result.varianceAfter)));
}

void Wave21ImagePrepTests::deskewKeepsHorizontalPageAtZero() {
    const DeskewResult result = deskew(makeTextPage(600, 400));
    QVERIFY(result.valid);
    QVERIFY2(std::abs(result.angleDeg) < 0.2, qPrintable(QString::number(result.angleDeg)));
}

void Wave21ImagePrepTests::deskewLeavesNoBlackBorder() {
    const QImage page = makeTextPage(600, 400);
    const QImage tilted = rotateNearest(page, 3.0);
    const DeskewResult result = deskew(tilted);
    QVERIFY(result.valid);

    // 旋转会把页面撑大，多出来的四个角必须是白色：
    // 一片黑边会让后续二值化把角落当成正文，OCR 直接读出一堆乱码。
    QCOMPARE(pixelAt(result.image, 0, 0), 255);
    QCOMPARE(pixelAt(result.image, result.image.width() - 1, 0), 255);
    QCOMPARE(pixelAt(result.image, 0, result.image.height() - 1), 255);
    QCOMPARE(pixelAt(result.image, result.image.width() - 1, result.image.height() - 1), 255);

    // 墨量也不该暴涨：黑边会把黑像素数直接翻好几倍。
    int inkBefore = 0;
    for (int y = 0; y < tilted.height(); ++y) {
        for (int x = 0; x < tilted.width(); ++x) {
            if (pixelAt(tilted, x, y) == 0) {
                ++inkBefore;
            }
        }
    }
    int inkAfter = 0;
    for (int y = 0; y < result.image.height(); ++y) {
        for (int x = 0; x < result.image.width(); ++x) {
            if (pixelAt(result.image, x, y) == 0) {
                ++inkAfter;
            }
        }
    }
    QVERIFY2(inkAfter <= inkBefore * 3 / 2 + 100,
             qPrintable(QStringLiteral("before=%1 after=%2").arg(inkBefore).arg(inkAfter)));
}

void Wave21ImagePrepTests::deskewHandlesLargePage() {
    // 大页面的墨点数量会触发粗搜抽稀，这条路径也必须给出同样的角度。
    const QImage page = makeTextPage(1200, 1700, 60);
    const QImage tilted = rotateNearest(page, 2.0);

    const DeskewResult result = deskew(tilted);
    QVERIFY(result.valid);
    QVERIFY2(std::abs(result.angleDeg + 2.0) < 0.5, qPrintable(QString::number(result.angleDeg)));
}

void Wave21ImagePrepTests::deskewHandlesBlankAndSolidPages() {
    QImage blank(200, 120, QImage::Format_Grayscale8);
    blank.fill(255);
    const DeskewResult blankResult = deskew(blank);
    QVERIFY(blankResult.valid);
    QCOMPARE(blankResult.angleDeg, 0.0);
    QVERIFY(!blankResult.image.isNull());
    QCOMPARE(pixelAt(blankResult.image, 0, 0), 255);

    // 全黑图每行的墨量完全一样，投影方差恒为 0，没有「最佳角度」可言，必须退回 0。
    QImage solid(200, 120, QImage::Format_Grayscale8);
    solid.fill(0);
    const DeskewResult solidResult = deskew(solid);
    QVERIFY(solidResult.valid);
    QCOMPARE(solidResult.angleDeg, 0.0);
    QVERIFY(!solidResult.image.isNull());
    QCOMPARE(pixelAt(solidResult.image, 100, 60), 0);
}

void Wave21ImagePrepTests::connectedDenoiseRemovesSpecks() {
    QImage page = makeTextPage(600, 400);
    const QVector<Speck> specks = makeSpecks();
    addSpecks(page, specks);

    const int before = countComponents(page);
    const QImage denoised = connectedDenoise(page);
    const int after = countComponents(denoised);

    // 噪点整块消失。
    for (const Speck& speck : specks) {
        for (int dy = 0; dy < speck.size; ++dy) {
            for (int dx = 0; dx < speck.size; ++dx) {
                QCOMPARE(pixelAt(denoised, speck.x + dx, speck.y + dy), 255);
            }
        }
    }
    // 连通域数量按噪点数量下降。
    QVERIFY2(after <= before - static_cast<int>(specks.size()),
             qPrintable(QStringLiteral("before=%1 after=%2").arg(before).arg(after)));
    // 主干笔画一个都不能少。
    QCOMPARE(pixelAt(denoised, 21, 20), 0);
    QCOMPARE(pixelAt(denoised, 21, 44), 0);
    QCOMPARE(pixelAt(denoised, 21, 236), 0);
}

void Wave21ImagePrepTests::connectedDenoiseKeepsThinStrokes() {
    // 1 像素宽、20 像素长的下划线 + 1 像素宽、30 像素长的竖线。
    // 两者的面积都小于下面设置的 minArea=40，按面积判定都会被误删。
    QImage page(120, 80, QImage::Format_Grayscale8);
    page.fill(255);
    for (int x = 20; x < 40; ++x) {
        page.setPixel(x, 30, 0);
    }
    for (int y = 10; y < 40; ++y) {
        page.setPixel(80, y, 0);
    }

    ConnectedDenoiseOptions options;
    // 面积阈值调到 40：两条细线的面积都不到，按面积判定会被误删。
    options.minArea = 40;
    const QImage protected_ = connectedDenoise(page, options);
    QCOMPARE(pixelAt(protected_, 25, 30), 0);
    QCOMPARE(pixelAt(protected_, 80, 25), 0);

    // 关掉细长笔画保护，同样的阈值就会把下划线和竖线一起删掉——
    // 这证明「保护规则」这一条真的在起作用，而不是被面积阈值顺带保住的。
    options.protectStrokes = false;
    const QImage unprotected = connectedDenoise(page, options);
    QCOMPARE(pixelAt(unprotected, 25, 30), 255);
    QCOMPARE(pixelAt(unprotected, 80, 25), 255);
}

void Wave21ImagePrepTests::connectedDenoiseHandlesBlankAndSolidPages() {
    QImage blank(100, 80, QImage::Format_Grayscale8);
    blank.fill(255);
    const QImage blankOut = connectedDenoise(blank);
    QCOMPARE(countComponents(blankOut), 0);
    QCOMPARE(pixelAt(blankOut, 50, 40), 255);

    // 全黑图是一个大连通域，面积远超阈值，绝不能被当成噪点删掉。
    QImage solid(100, 80, QImage::Format_Grayscale8);
    solid.fill(0);
    const QImage solidOut = connectedDenoise(solid);
    QCOMPARE(countComponents(solidOut), 1);
    QCOMPARE(pixelAt(solidOut, 50, 40), 0);
}

void Wave21ImagePrepTests::preprocessScannedPageRunsWholeChain() {
    QImage page = makeTextPage(600, 400);
    const QVector<Speck> specks = makeSpecks();
    addSpecks(page, specks);
    const QImage tilted = rotateNearest(page, 3.0);

    const ImagePrepResult result = preprocessScannedPage(tilted);
    QVERIFY(result.valid);
    QCOMPARE(result.image.format(), QImage::Format_Grayscale8);
    QVERIFY(!result.image.isNull());
    QVERIFY2(std::abs(result.deskewAngleDeg + 3.0) < 0.5, qPrintable(QString::number(result.deskewAngleDeg)));
    for (const Speck& speck : specks) {
        for (int dy = 0; dy < speck.size; ++dy) {
            for (int dx = 0; dx < speck.size; ++dx) {
                QCOMPARE(pixelAt(result.image, speck.x + dx, speck.y + dy), 255);
            }
        }
    }
}

void Wave21ImagePrepTests::preprocessForOcrKeepsLegacyBehavior() {
    // 老接口一个字没改：同样大小、同样二值、null 进 null 出。
    const QImage page = makeTextPage(120, 80);
    const QImage output = preprocessForOcr(page);
    QCOMPARE(output.size(), page.size());
    QCOMPARE(output.format(), QImage::Format_Grayscale8);
    QVERIFY(preprocessForOcr(QImage()).isNull());

    // 新出口可以逐段关掉：关掉纠偏时不该再动角度。
    ImagePrepOptions options;
    options.deskew = false;
    const ImagePrepResult noDeskew = preprocessScannedPage(page, options);
    QVERIFY(noDeskew.valid);
    QCOMPARE(noDeskew.deskewAngleDeg, 0.0);
}

void Wave21ImagePrepTests::preprocessDoesNotModifyInput() {
    QImage page = makeTextPage(200, 120);
    const QImage pageCopy = page.copy();
    const QImage tilted = rotateNearest(page, 3.0);
    const QImage tiltedCopy = tilted.copy();

    connectedDenoise(tilted);
    deskew(tilted);
    preprocessScannedPage(tilted);

    QVERIFY(page == pageCopy);
    QVERIFY(tilted == tiltedCopy);
}

QTEST_MAIN(Wave21ImagePrepTests)
#include "Wave21ImagePrepTests.moc"