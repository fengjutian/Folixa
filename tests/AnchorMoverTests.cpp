#include "pdf/ops/AnchorMover.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <QTest>

using aipdf::pdf::AnchorImageMove;
using aipdf::pdf::AnchorMoveResult;
using aipdf::pdf::moveAnchoredImages;

namespace {

QByteArray toBytes(QPDF& pdf) {
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<qsizetype>(buffer->getSize()));
}

// 一页 + 一张 1×1 图片，用 50×50 的 cm 摆在 (100,500)。
QByteArray makePdfWithImage(double x = 100, double y = 500, double w = 50, double h = 50) {
    QPDF pdf;
    pdf.emptyPDF();

    QByteArray imageData;
    imageData.append('\x20');
    imageData.append('\x80');
    imageData.append('\xC0');
    QPDFObjectHandle image = pdf.newStream(imageData.toStdString());
    QPDFObjectHandle dict = image.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    const QPDFObjectHandle imageRef = pdf.makeIndirectObject(image);

    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/Im1", imageRef);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle box = QPDFObjectHandle::newArray();
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(600));
    box.appendItem(QPDFObjectHandle::newInteger(800));
    page.replaceKey("/MediaBox", box);
    page.replaceKey("/Resources", resources);
    const QByteArray content = QStringLiteral("q %1 0 0 %2 %3 %4 cm /Im1 Do Q")
                                  .arg(w).arg(h).arg(x).arg(y)
                                  .toLatin1();
    page.replaceKey("/Contents",
                    pdf.makeIndirectObject(pdf.newStream(content.toStdString())));
    const QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);

    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pdf.getRoot().replaceKey("/Pages", pdf.makeIndirectObject(pages));
    return toBytes(pdf);
}

QString contentOf(const QByteArray& bytes) {
    QPDF pdf;
    pdf.processMemoryFile("content.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const QPDFObjectHandle page = pdf.getAllPages().at(0);
    QPDFObjectHandle contents = page.getKey("/Contents");
    if (contents.isArray()) contents = contents.getArrayItem(0);
    const std::shared_ptr<Buffer> data = contents.getStreamData(qpdf_dl_generalized);
    return QString::fromLatin1(reinterpret_cast<const char*>(data->getBuffer()),
                               static_cast<int>(data->getSize()));
}

}  // namespace

class AnchorMoverTests : public QObject {
    Q_OBJECT

private slots:
    void movesImageAtMatchingRect();
    void reportsMissingImage();
    void noMovesKeepsBytes();
    void movesManyPlacements();
};

void AnchorMoverTests::movesImageAtMatchingRect() {
    const QByteArray pdf = makePdfWithImage();
    AnchorImageMove move;
    move.pageIndex = 0;
    move.from = QRectF(100, 500, 50, 50);
    move.deltaY = 30.0;
    const AnchorMoveResult result = moveAnchoredImages(pdf, {move});
    QVERIFY2(result.ok(), qPrintable(result.errors.join(QStringLiteral("; "))));
    QCOMPARE(result.moved, 1);
    const QString content = contentOf(result.bytes);
    QVERIFY2(content.contains(QStringLiteral("q 1 0 0 1 0 -30.000 cm")), qPrintable(content));
    // 原来的 cm 不动，只在图片前后包一层。
    QVERIFY2(content.contains(QStringLiteral("50 0 0 50 100 500 cm")), qPrintable(content));
    QVERIFY2(content.count(QStringLiteral("Do")) == 1, qPrintable(content));
}

void AnchorMoverTests::reportsMissingImage() {
    const QByteArray pdf = makePdfWithImage();
    AnchorImageMove move;
    move.from = QRectF(10, 10, 20, 20);
    const AnchorMoveResult result = moveAnchoredImages(pdf, {move});
    QVERIFY(!result.ok());
    QVERIFY2(result.errors.first().contains(QStringLiteral("没找到")),
             qPrintable(result.errors.join(QStringLiteral("; "))));
}

void AnchorMoverTests::noMovesKeepsBytes() {
    const QByteArray pdf = makePdfWithImage();
    const AnchorMoveResult result = moveAnchoredImages(pdf, {});
    QVERIFY(result.ok());
    QCOMPARE(result.bytes, pdf);
}

void AnchorMoverTests::movesManyPlacements() {
    // 一组不同位置/尺寸/位移，近似「多份含图语料」的回归。
    for (int i = 0; i < 12; ++i) {
        const double x = 40 + i * 35;
        const double y = 300 + i * 20;
        const double w = 20 + i * 3;
        const double h = 15 + i * 2;
        const double delta = 5 + i * 4;
        const QByteArray pdf = makePdfWithImage(x, y, w, h);
        AnchorImageMove move;
        move.pageIndex = 0;
        move.from = QRectF(x, y, w, h);
        move.deltaY = delta;
        const AnchorMoveResult result = moveAnchoredImages(pdf, {move});
        QVERIFY2(result.ok(), qPrintable(result.errors.join(QStringLiteral("; "))));
        QCOMPARE(result.moved, 1);
        const QString content = contentOf(result.bytes);
        const QString expected =
            QStringLiteral("q 1 0 0 1 0 %1 cm").arg(QString::number(-delta, 'f', 3));
        QVERIFY2(content.contains(expected), qPrintable(content));
    }
}
QTEST_MAIN(AnchorMoverTests)
#include "AnchorMoverTests.moc"
