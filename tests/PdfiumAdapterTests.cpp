#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QImage>
#include <QtTest>

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QString goldenPath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_GOLDEN_DIR)).filePath(name);
}

bool goldenUpdateRequested() {
    return qEnvironmentVariableIsSet("AIPDF_UPDATE_GOLDEN");
}

// 统计非白色像素，用于判断页面是否真的画出了内容。
int countInkedPixels(const QImage& image) {
    int inked = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            if (qRed(pixel) != 0xFF || qGreen(pixel) != 0xFF || qBlue(pixel) != 0xFF) {
                ++inked;
            }
        }
    }
    return inked;
}

}  // namespace

class PdfiumAdapterTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void normalDocumentHasExpectedGeometry();
    void normalDocumentRendersContent();
    void emptyDocumentRendersBlank();
    void rotatedDocumentSwapsDimensions();
    void corruptDocumentFailsWithoutCrash();
    void truncatedDocumentFailsWithoutCrash();
    void missingFileIsReported();
    void encryptedDocumentNeedsPassword();
    void oversizedRenderIsRejected();
    void goldenImageMatches();

private:
    bool hasEncryptedFixture_ = false;
};

void PdfiumAdapterTests::initTestCase() {
    hasEncryptedFixture_ = QFile::exists(fixturePath(QStringLiteral("encrypted.pdf")));
}

void PdfiumAdapterTests::normalDocumentHasExpectedGeometry() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));
    QCOMPARE(result.document.pageCount(), 1);

    const auto page = result.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->widthPoints(), 200.0);
    QCOMPARE(page->heightPoints(), 100.0);
    QVERIFY(!result.document.page(1).has_value());
    QVERIFY(!result.document.page(-1).has_value());
}

void PdfiumAdapterTests::normalDocumentRendersContent() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const QImage image = page->render(1.0);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 200);
    QCOMPARE(image.height(), 100);
    QVERIFY2(countInkedPixels(image) > 0, "首页没有渲染出任何内容");
}

void PdfiumAdapterTests::emptyDocumentRendersBlank() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("empty.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const QImage image = page->render(1.0);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 120);
    QCOMPARE(image.height(), 160);
    QCOMPARE(countInkedPixels(image), 0);
}

void PdfiumAdapterTests::rotatedDocumentSwapsDimensions() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("rotated.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    QCOMPARE(page->rotationDegrees(), 90);
    QCOMPARE(page->widthPoints(), 100.0);
    QCOMPARE(page->heightPoints(), 200.0);

    const QImage image = page->render(1.0);
    QCOMPARE(image.width(), 100);
    QCOMPARE(image.height(), 200);
}

void PdfiumAdapterTests::corruptDocumentFailsWithoutCrash() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("corrupt.pdf")));
    QVERIFY(!result.document.isValid());
    QVERIFY(!result.errorMessage.isEmpty());
    QCOMPARE(result.document.pageCount(), 0);
}

void PdfiumAdapterTests::truncatedDocumentFailsWithoutCrash() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("truncated.pdf")));
    if (result.document.isValid()) {
        // PDFium 能修复部分截断文件：至少要能正常取页，不崩溃。
        QVERIFY(result.document.pageCount() >= 1);
        QVERIFY(result.document.page(0).has_value());
        return;
    }
    QVERIFY(!result.errorMessage.isEmpty());
}

void PdfiumAdapterTests::missingFileIsReported() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("does-not-exist.pdf")));
    QVERIFY(!result.document.isValid());
    QVERIFY(result.errorMessage.contains(QStringLiteral("不存在")));
}

void PdfiumAdapterTests::encryptedDocumentNeedsPassword() {
    if (!hasEncryptedFixture_) {
        QSKIP("缺少 encrypted.pdf（需要 pypdf 生成）");
    }

    const auto denied = aipdf::pdf::openDocument(fixturePath(QStringLiteral("encrypted.pdf")));
    QVERIFY(!denied.document.isValid());
    QVERIFY2(denied.passwordRequired, "加密文档应当报告需要密码");

    const auto wrong =
        aipdf::pdf::openDocument(fixturePath(QStringLiteral("encrypted.pdf")), QStringLiteral("wrong"));
    QVERIFY(!wrong.document.isValid());
    QVERIFY(wrong.passwordRequired);

    const auto granted = aipdf::pdf::openDocument(
        fixturePath(QStringLiteral("encrypted.pdf")), QStringLiteral("secret"));
    QVERIFY2(granted.document.isValid(), qPrintable(granted.errorMessage));
    QCOMPARE(granted.document.pageCount(), 1);
}

void PdfiumAdapterTests::oversizedRenderIsRejected() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    // 超过像素上限时必须返回空图，而不是分配超大内存。
    QVERIFY(page->render(100000.0).isNull());
    QVERIFY(page->render(0.0).isNull());
    QVERIFY(page->render(-1.0).isNull());
}

void PdfiumAdapterTests::goldenImageMatches() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("normal.pdf")));
    QVERIFY(result.document.isValid());
    const auto page = result.document.page(0);
    QVERIFY(page.has_value());

    const QImage rendered = page->render(1.0);
    QVERIFY(!rendered.isNull());

    const QString target = goldenPath(QStringLiteral("normal-page1.png"));
    if (goldenUpdateRequested()) {
        QVERIFY(rendered.save(target));
        QVERIFY(QFile::exists(target));
        return;
    }

    QVERIFY2(QFile::exists(target), qPrintable(QStringLiteral("缺少黄金图 %1").arg(target)));
    QImage golden;
    QVERIFY(golden.load(target));
    QCOMPARE(golden.size(), rendered.size());
    QCOMPARE(golden.format(), rendered.format());
    // 逐像素比较：PDFium 版本变化时需要用 AIPDF_UPDATE_GOLDEN=1 重新生成。
    QCOMPARE(countInkedPixels(golden), countInkedPixels(rendered));
    QVERIFY2(golden == rendered, "渲染结果与黄金图不一致");
}

QTEST_GUILESS_MAIN(PdfiumAdapterTests)
#include "PdfiumAdapterTests.moc"
