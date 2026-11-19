#include "pdf/content/ObjectInventory.h"

#include <QTest>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>

using aipdf::pdf::PageObjectInventory;
using aipdf::pdf::objectInventoryLoss;
using aipdf::pdf::pageObjectInventory;

namespace {

QByteArray toPdfBytes(QPDF& pdf) {
    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer) return {};
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<qsizetype>(buffer->getSize()));
}

// 一页：一段文字 + 一条矩形路径 + 一张 2x2 图片。
QByteArray makePdfWithObjects() {
    QPDF pdf;
    pdf.emptyPDF();

    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));

    QPDFObjectHandle image = pdf.newStream(std::string());
    QByteArray pixels(2 * 2 * 3, '\x80');
    image.replaceStreamData(pixels.toStdString(), QPDFObjectHandle::newNull(),
                            QPDFObjectHandle::newNull());
    QPDFObjectHandle imageDict = image.getDict();
    imageDict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    imageDict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    imageDict.replaceKey("/Width", QPDFObjectHandle::newInteger(2));
    imageDict.replaceKey("/Height", QPDFObjectHandle::newInteger(2));
    imageDict.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
    imageDict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
    const QPDFObjectHandle imageRef = pdf.makeIndirectObject(image);

    QPDFObjectHandle fontDict = QPDFObjectHandle::newDictionary();
    fontDict.replaceKey("/F1", pdf.makeIndirectObject(font));
    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/Im1", imageRef);
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fontDict);
    resources.replaceKey("/XObject", xobjects);

    const QByteArray content =
        "q 0 0 100 100 re f Q\n"
        "q 50 0 0 50 100 500 cm /Im1 Do Q\n"
        "BT /F1 12 Tf 72 720 Td (Hello) Tj ET\n";

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    QPDFObjectHandle mediaBox = QPDFObjectHandle::newArray();
    mediaBox.appendItem(QPDFObjectHandle::newInteger(0));
    mediaBox.appendItem(QPDFObjectHandle::newInteger(0));
    mediaBox.appendItem(QPDFObjectHandle::newInteger(612));
    mediaBox.appendItem(QPDFObjectHandle::newInteger(792));
    page.replaceKey("/MediaBox", mediaBox);
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", pdf.makeIndirectObject(pdf.newStream(content.toStdString())));
    const QPDFObjectHandle pageRef = pdf.makeIndirectObject(page);

    QPDFObjectHandle pages = QPDFObjectHandle::newDictionary();
    pages.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(pageRef);
    pages.replaceKey("/Kids", kids);
    pages.replaceKey("/Count", QPDFObjectHandle::newInteger(1));
    pdf.getRoot().replaceKey("/Pages", pdf.makeIndirectObject(pages));

    return toPdfBytes(pdf);
}

}  // namespace

class ObjectInventoryTests : public QObject {
    Q_OBJECT

private slots:
    void countsTextImagesAndPaths();
    void detectsLostImage();
    void detectsLostPath();
    void detectsPageCountChange();
    void noLossReturnsEmpty();
};

void ObjectInventoryTests::countsTextImagesAndPaths() {
    const QByteArray pdf = makePdfWithObjects();
    QVERIFY(!pdf.isEmpty());
    QString error;
    const QVector<PageObjectInventory> inventory = pageObjectInventory(pdf, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(inventory.size(), 1);
    QCOMPARE(inventory.first().pageIndex, 0);
    QVERIFY2(inventory.first().textChars >= 5, qPrintable(QString::number(inventory.first().textChars)));
    QCOMPARE(inventory.first().images, 1);
    QVERIFY(inventory.first().paths >= 1);
}

void ObjectInventoryTests::detectsLostImage() {
    QVector<PageObjectInventory> before{{0, 10, 3, 4}};
    QVector<PageObjectInventory> after{{0, 10, 2, 4}};
    const QString loss = objectInventoryLoss(before, after);
    QVERIFY2(loss.contains(QStringLiteral("图片")), qPrintable(loss));
    QVERIFY2(loss.contains(QStringLiteral("第 1 页")), qPrintable(loss));
}

void ObjectInventoryTests::detectsLostPath() {
    QVector<PageObjectInventory> before{{1, 10, 0, 8}};
    QVector<PageObjectInventory> after{{1, 10, 0, 6}};
    const QString loss = objectInventoryLoss(before, after);
    QVERIFY2(loss.contains(QStringLiteral("矢量")), qPrintable(loss));
    QVERIFY2(loss.contains(QStringLiteral("第 2 页")), qPrintable(loss));
}

void ObjectInventoryTests::detectsPageCountChange() {
    const QString loss = objectInventoryLoss({{0, 1, 0, 0}}, {});
    QVERIFY2(loss.contains(QStringLiteral("页数")), qPrintable(loss));
}

void ObjectInventoryTests::noLossReturnsEmpty() {
    QVector<PageObjectInventory> before{{0, 10, 2, 3}, {1, 5, 0, 1}};
    QVector<PageObjectInventory> after{{0, 12, 2, 4}, {1, 5, 0, 1}};
    QCOMPARE(objectInventoryLoss(before, after), QString());
}

QTEST_MAIN(ObjectInventoryTests)
#include "ObjectInventoryTests.moc"
