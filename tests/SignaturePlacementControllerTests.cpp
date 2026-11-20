#include "app/controllers/SignaturePlacementController.h"

#include <QBuffer>
#include <QImage>
#include <QtTest>

class SignaturePlacementControllerTests final : public QObject {
    Q_OBJECT

private slots:
    void splitsImageAcrossPages();
    void rejectsInvalidImage();
};

void SignaturePlacementControllerTests::splitsImageAcrossPages() {
    QImage image(120, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));

    aipdf::app::SignaturePlacementController controller;
    const auto result = controller.planRidingSeam(
        bytes, {QSizeF(600, 800), QSizeF(500, 700), QSizeF(400, 600)});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.placements.size(), 3);
    for (int index = 0; index < result.placements.size(); ++index) {
        const auto& placement = result.placements.at(index);
        QCOMPARE(placement.pageIndex, index);
        QCOMPARE(placement.rect.width(), 18.0);
        QImage slice;
        QVERIFY(slice.loadFromData(placement.pngBytes));
        QCOMPARE(slice.width(), 40);
        QCOMPARE(slice.height(), 40);
    }
    QCOMPARE(result.placements.at(0).rect.right(), 600.0);
    QCOMPARE(result.placements.at(2).rect.right(), 400.0);
}

void SignaturePlacementControllerTests::rejectsInvalidImage() {
    const auto result = aipdf::app::SignaturePlacementController().planRidingSeam(
        QByteArrayLiteral("not-an-image"), {QSizeF(600, 800)});
    QVERIFY(!result.ok());
    QVERIFY(result.placements.isEmpty());
}

QTEST_GUILESS_MAIN(SignaturePlacementControllerTests)
#include "SignaturePlacementControllerTests.moc"
