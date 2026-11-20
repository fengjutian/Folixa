// 页面 → Form XObject → 图片：验证递归与坐标换算。
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

QByteArray makeImageData() {
    QByteArray data;
    data.append('\x20');
    data.append('\x80');
    data.append('\xC0');
    return data;
}

QPDFObjectHandle makeImage(QPDF& pdf) {
    QPDFObjectHandle image = pdf.newStream(makeImageData().toStdString());
    QPDFObjectHandle dict = image.getDict();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dict.replaceKey("/Width", QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/Height", QPDFObjectHandle::newInteger(1));
    dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
    dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    return pdf.makeIndirectObject(image);
}

// 页面上把 Fm1 放在 (100,0) 的偏移处；Fm1 内部把图片放在 (100,500)，
// 所以图片的页面矩形是 (200,500,50,50)。
QByteArray makePdfWithFormXObject() {
    QPDF pdf;
    pdf.emptyPDF();
    const QPDFObjectHandle imageRef = makeImage(pdf);

    QPDFObjectHandle innerResources = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle innerXObjects = QPDFObjectHandle::newDictionary();
    innerXObjects.replaceKey("/Im1", imageRef);
    innerResources.replaceKey("/XObject", innerXObjects);

    QPDFObjectHandle form = pdf.newStream(
        QByteArray("q 50 0 0 50 100 500 cm /Im1 Do Q").toStdString());
    QPDFObjectHandle formDict = form.getDict();
    formDict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    formDict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
    bbox.appendItem(QPDFObjectHandle::newInteger(0));
    bbox.appendItem(QPDFObjectHandle::newInteger(0));
    bbox.appendItem(QPDFObjectHandle::newInteger(600));
    bbox.appendItem(QPDFObjectHandle::newInteger(800));
    formDict.replaceKey("/BBox", bbox);
    formDict.replaceKey("/Resources", innerResources);
    const QPDFObjectHandle formRef = pdf.makeIndirectObject(form);

    QPDFObjectHandle pageResources = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle pageXObjects = QPDFObjectHandle::newDictionary();
    pageXObjects.replaceKey("/Fm1", formRef);
    pageResources.replaceKey("/XObject", pageXObjects);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle box = QPDFObjectHandle::newArray();
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(0));
    box.appendItem(QPDFObjectHandle::newInteger(600));
    box.appendItem(QPDFObjectHandle::newInteger(800));
    page.replaceKey("/MediaBox", box);
    page.replaceKey("/Resources", pageResources);
    page.replaceKey(
        "/Contents",
        pdf.makeIndirectObject(
            pdf.newStream(QByteArray("q 1 0 0 1 100 0 cm /Fm1 Do Q").toStdString())));
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

QString formContentOf(const QByteArray& bytes) {
    QPDF pdf;
    pdf.processMemoryFile("form.pdf", bytes.constData(), static_cast<size_t>(bytes.size()));
    const QPDFObjectHandle page = pdf.getAllPages().at(0);
    QPDFObjectHandle form = page.getKey("/Resources").getKey("/XObject").getKey("/Fm1");
    const std::shared_ptr<Buffer> data = form.getStreamData(qpdf_dl_generalized);
    return QString::fromLatin1(reinterpret_cast<const char*>(data->getBuffer()),
                               static_cast<int>(data->getSize()));
}

}  // namespace

class AnchorXObjectTests : public QObject {
    Q_OBJECT

private slots:
    void movesImageInsideFormXObject();
};

void AnchorXObjectTests::movesImageInsideFormXObject() {
    const QByteArray pdf = makePdfWithFormXObject();
    AnchorImageMove move;
    move.pageIndex = 0;
    move.from = QRectF(200, 500, 50, 50);
    move.deltaY = 30.0;
    const AnchorMoveResult result = moveAnchoredImages(pdf, {move});
    QVERIFY2(result.ok(), qPrintable(result.errors.join(QStringLiteral("; "))));
    QCOMPARE(result.moved, 1);
    const QString content = formContentOf(result.bytes);
    // 图片在 XObject 内部，缩放 50：页面 30pt → 本地 0.6。
    QVERIFY2(content.contains(QStringLiteral("q 1 0 0 1 0 -0.6 cm")), qPrintable(content));
    QVERIFY2(content.contains(QStringLiteral("/Im1 Do")), qPrintable(content));
}

QTEST_MAIN(AnchorXObjectTests)
#include "AnchorXObjectTests.moc"
