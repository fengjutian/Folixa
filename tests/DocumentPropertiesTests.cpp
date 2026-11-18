#include "pdf/document/DocumentProperties.h"

#include "pdf/kernel/PdfiumAdapter.h"

#include <QTest>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::DocumentProperties;

namespace {

// 造一个最小的 PDF（经典 xref 表），可选地带 /Info。
//
// 为什么自己造：这里要控制 /Info 里的确切内容（哪个键存在、值是什么），
// 夹具里不会正好有这些；而且"没有 /Info"这一种情况根本没有夹具。
QByteArray buildPdf(const QByteArray& infoBody, bool referenceInfo) {
    const QByteArray content = "BT /F1 12 Tf 72 72 Td (Hello) Tj ET\n";
    QVector<QByteArray> bodies;
    bodies << "<< /Type /Catalog /Pages 2 0 R >>";
    bodies << "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    bodies << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 5 0 R "
              "/Resources << /Font << /F1 4 0 R >> >> >>";
    bodies << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    bodies << "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
               "endstream";
    bodies << infoBody;  // 对象 6：/Info

    QByteArray pdf = "%PDF-1.7\n";
    QVector<int> offsets;
    for (int index = 0; index < bodies.size(); ++index) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(index + 1) + " 0 obj\n" + bodies.at(index) + "\nendobj\n";
    }
    const int xrefOffset = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(bodies.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (int offset : offsets) {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(bodies.size() + 1) + " /Root 1 0 R";
    if (referenceInfo) {
        pdf += " /Info 6 0 R";
    }
    pdf += " >>\n";
    pdf += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return pdf;
}

// 一份带全字段的 /Info。
QByteArray fullInfoBody() {
    return QByteArray("<< /Title (Sample Title) /Author (Alice) /Subject (Testing) "
                      "/Keywords (alpha, beta) /Creator (AIPDF Tests) /Producer (qpdf) "
                      "/CreationDate (D:20260101120000+08'00') "
                      "/ModDate (D:20260202030405Z) >>");
}

// UTF-16BE 的十六进制串（PDF 里另一种常见写法）。
QByteArray utf16Hex(const QString& text) {
    QByteArray hex("FEFF");
    for (const QChar ch : text) {
        const ushort unit = ch.unicode();
        hex += QByteArray::number((unit >> 8) & 255, 16).rightJustified(2, '0');
        hex += QByteArray::number(unit & 255, 16).rightJustified(2, '0');
    }
    return hex.toUpper();
}

#ifdef AIPDF_WITH_QPDF
// 直接看文件里的 /Info 原始值：用来验证"我们没碰过的键真的没被碰"。
QString rawInfoValue(const QByteArray& pdf, const char* key) {
    QPDF qpdf;
    qpdf.processMemoryFile("raw.pdf", pdf.constData(), static_cast<size_t>(pdf.size()));
    const QPDFObjectHandle info = qpdf.getTrailer().getKey("/Info");
    if (!info.isDictionary()) {
        return {};
    }
    const QPDFObjectHandle value = info.getKey(key);
    if (!value.isString()) {
        return {};
    }
    return QString::fromUtf8(value.getUTF8Value().c_str());
}
#endif

}  // namespace

class DocumentPropertiesTests : public QObject {
    Q_OBJECT

private slots:
    void readsInfoDictionaryFields();
    void readsDocumentWithoutInfo();
    void readsUtf16EncodedTitle();
    void writesAndReadsBack();
    void emptyFieldDoesNotClearExistingValue();
    void writesKeyThatWasMissing();
    void keepsUnrelatedInfoKeys();
    void writtenFileStillOpensInPdfium();
    void withoutQpdfReportsReason();
    void emptyInputIsRejected();
};

// 读：带 /Info 的 PDF，字段逐个对上。
void DocumentPropertiesTests::readsInfoDictionaryFields() {
    const QByteArray pdf = buildPdf(fullInfoBody(), true);
    DocumentProperties properties;
    QString error;
    QVERIFY2(aipdf::pdf::readDocumentProperties(pdf, &properties, &error), qPrintable(error));
    QCOMPARE(properties.title, QStringLiteral("Sample Title"));
    QCOMPARE(properties.author, QStringLiteral("Alice"));
    QCOMPARE(properties.subject, QStringLiteral("Testing"));
    QCOMPARE(properties.keywords, QStringLiteral("alpha, beta"));
    QCOMPARE(properties.creator, QStringLiteral("AIPDF Tests"));
    QCOMPARE(properties.producer, QStringLiteral("qpdf"));
    // 日期原样保留：不自己转格式，免得丢时区。
    QCOMPARE(properties.creationDate, QStringLiteral("D:20260101120000+08'00'"));
    QCOMPARE(properties.modificationDate, QStringLiteral("D:20260202030405Z"));
    QVERIFY(!properties.isEmpty());
}

// 读：没有 /Info 的 PDF 不是错误，字段全空。
void DocumentPropertiesTests::readsDocumentWithoutInfo() {
    const QByteArray pdf = buildPdf(fullInfoBody(), false);
    DocumentProperties properties;
    QString error;
    QVERIFY2(aipdf::pdf::readDocumentProperties(pdf, &properties, &error), qPrintable(error));
    QVERIFY(properties.isEmpty());
    QVERIFY(error.isEmpty());
}

// 读：UTF-16 编码的标题也要读成一样的字。
void DocumentPropertiesTests::readsUtf16EncodedTitle() {
    const QByteArray body =
        QByteArray("<< /Title <") + utf16Hex(QStringLiteral("标题")) + "> /Author <" +
        utf16Hex(QStringLiteral("作者")) + "> >>";
    const QByteArray pdf = buildPdf(body, true);
    DocumentProperties properties;
    QString error;
    QVERIFY2(aipdf::pdf::readDocumentProperties(pdf, &properties, &error), qPrintable(error));
    QCOMPARE(properties.title, QStringLiteral("标题"));
    QCOMPARE(properties.author, QStringLiteral("作者"));
}

// 写：写完再读回来，值一致。
void DocumentPropertiesTests::writesAndReadsBack() {
    const QByteArray pdf = buildPdf(fullInfoBody(), true);
    DocumentProperties wanted;
    wanted.title = QStringLiteral("新标题");
    wanted.author = QStringLiteral("新作者");
    wanted.subject = QStringLiteral("新主题");
    wanted.keywords = QStringLiteral("k1, k2");
    wanted.creator = QStringLiteral("AI PDF Editor");
    wanted.producer = QStringLiteral("qpdf");
    wanted.creationDate = QStringLiteral("D:20260304050607+08'00'");
    wanted.modificationDate = QStringLiteral("D:20260304050608Z");

    QString error;
    const QByteArray written = aipdf::pdf::writeDocumentProperties(pdf, wanted, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    DocumentProperties readBack;
    QVERIFY2(aipdf::pdf::readDocumentProperties(written, &readBack, &error), qPrintable(error));
    QCOMPARE(readBack.title, wanted.title);
    QCOMPARE(readBack.author, wanted.author);
    QCOMPARE(readBack.subject, wanted.subject);
    QCOMPARE(readBack.keywords, wanted.keywords);
    QCOMPARE(readBack.creator, wanted.creator);
    QCOMPARE(readBack.producer, wanted.producer);
    QCOMPARE(readBack.creationDate, wanted.creationDate);
    QCOMPARE(readBack.modificationDate, wanted.modificationDate);
}

// 写：空字符串的字段不写，原有的值不能被清空。
void DocumentPropertiesTests::emptyFieldDoesNotClearExistingValue() {
    const QByteArray pdf = buildPdf(fullInfoBody(), true);
    DocumentProperties wanted;
    wanted.title = QStringLiteral("只改标题");
    wanted.author = QString();  // 这次不想动作者

    QString error;
    const QByteArray written = aipdf::pdf::writeDocumentProperties(pdf, wanted, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    DocumentProperties readBack;
    QVERIFY2(aipdf::pdf::readDocumentProperties(written, &readBack, &error), qPrintable(error));
    QCOMPARE(readBack.title, QStringLiteral("只改标题"));
    QCOMPARE(readBack.author, QStringLiteral("Alice"));
    QCOMPARE(readBack.subject, QStringLiteral("Testing"));
}

// 写：原本不存在的键要补上。
void DocumentPropertiesTests::writesKeyThatWasMissing() {
    const QByteArray pdf = buildPdf(QByteArray("<< >>"), false);
    DocumentProperties wanted;
    wanted.author = QStringLiteral("Bob");

    QString error;
    const QByteArray written = aipdf::pdf::writeDocumentProperties(pdf, wanted, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    DocumentProperties readBack;
    QVERIFY2(aipdf::pdf::readDocumentProperties(written, &readBack, &error), qPrintable(error));
    QCOMPARE(readBack.author, QStringLiteral("Bob"));
    QVERIFY(readBack.title.isEmpty());
}

// 写：/Info 里我们没碰过的键要原样留着。
void DocumentPropertiesTests::keepsUnrelatedInfoKeys() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray pdf =
        buildPdf(QByteArray("<< /Title (Old) /Trapped (False) /Custom (keep me) >>"), true);
    DocumentProperties wanted;
    wanted.title = QStringLiteral("New");

    QString error;
    const QByteArray written = aipdf::pdf::writeDocumentProperties(pdf, wanted, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QCOMPARE(rawInfoValue(written, "/Custom"), QStringLiteral("keep me"));
    QCOMPARE(rawInfoValue(written, "/Trapped"), QStringLiteral("False"));
#else
    QSKIP("本次构建未启用 qpdf");
#endif
}

// 写：写出来的文件仍然能被 PDFium 打开（文件没写坏）。
void DocumentPropertiesTests::writtenFileStillOpensInPdfium() {
    const QByteArray pdf = buildPdf(fullInfoBody(), true);
    DocumentProperties wanted;
    wanted.title = QStringLiteral("写完还能打开");

    QString error;
    const QByteArray written = aipdf::pdf::writeDocumentProperties(pdf, wanted, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const aipdf::pdf::PdfOpenResult reopened = aipdf::pdf::openDocumentFromBytes(written);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 1);
}

// 没有 qpdf 的构建：读要失败并说明原因。
void DocumentPropertiesTests::withoutQpdfReportsReason() {
    DocumentProperties properties;
    QString error;
#ifdef AIPDF_WITH_QPDF
    // 本机构建启用了 qpdf：这一条退化成"成功时不报错"。
    const QByteArray pdf = buildPdf(fullInfoBody(), true);
    QVERIFY(aipdf::pdf::readDocumentProperties(pdf, &properties, &error));
    QVERIFY(error.isEmpty());
#else
    QVERIFY(!aipdf::pdf::readDocumentProperties(QByteArray("not a pdf"), &properties, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(aipdf::pdf::writeDocumentProperties(QByteArray("not a pdf"), properties, &error).isEmpty());
    QVERIFY(!error.isEmpty());
#endif
}

// 空输入：直接报错，不当成"没有属性"。
void DocumentPropertiesTests::emptyInputIsRejected() {
    DocumentProperties properties;
    QString error;
    QVERIFY(!aipdf::pdf::readDocumentProperties(QByteArray(), &properties, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(DocumentPropertiesTests)
#include "DocumentPropertiesTests.moc"
