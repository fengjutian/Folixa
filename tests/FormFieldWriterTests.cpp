// 建域写入器的单元测试。
//
// 这里只测"写出来的字节对不对"，不碰 PDFium、不碰界面：
//   1) 经典 trailer 的文件仍然能建域（回归）；
//   2) 交叉引用流（/Type /XRef）的文件也能建域 —— 这类文件里根本没有 "trailer" 关键字，
//      写出来的新交叉引用节也必须是交叉引用流（PDF 32000-1 §7.5.8.3 禁止混用）；
//   3) 单选分组（/Parent）与真条码（外观流里画的是 Code 128 条纹，不是文字）。
//
// 夹具全部手写：这里不依赖 qpdf（默认不开），也不依赖 PDFium 的输出，
// 这样交叉引用流那种"没有 trailer 关键字"的形态才能被真正覆盖到。

#include "pdf/forms/BarcodeEncoder.h"
#include "pdf/forms/FormFieldWriter.h"
#include "pdf/kernel/StandardFonts.h"

#include <QRectF>
#include <QTest>
#include <QVector>

using aipdf::pdf::BarcodeCode128;
using aipdf::pdf::BarcodeModule;
using aipdf::pdf::FormFieldInsert;
using aipdf::pdf::FormFieldKind;
using aipdf::pdf::FormFieldWriteResult;
using aipdf::pdf::appendFormField;
using aipdf::pdf::encodeCode128;

namespace {

// 写一个对象并记下它的偏移。
void appendObject(QByteArray* pdf, QVector<QPair<int, QByteArray>>* objects,
                  QVector<qsizetype>* offsets, int number, const QByteArray& body) {
    offsets->append(pdf->size());
    objects->append({number, body});
    *pdf += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
}

// 经典 xref 表的条目（10 位偏移 + 5 位代数 + 类型）。
QByteArray classicEntry(qsizetype offset, bool free) {
    if (free) return QByteArray("0000000000 65535 f \n");
    return QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
}

// 交叉引用流的条目：1 字节类型 + 4 字节偏移 + 2 字节代数（/W [1 4 2]）。
void appendXrefStreamEntry(QByteArray* data, int type, quint32 offset, quint16 generation) {
    data->append(char(type));
    data->append(char((offset >> 24) & 0xff));
    data->append(char((offset >> 16) & 0xff));
    data->append(char((offset >> 8) & 0xff));
    data->append(char(offset & 0xff));
    data->append(char((generation >> 8) & 0xff));
    data->append(char(generation & 0xff));
}

// 一个最小的经典 xref 表 PDF：一页、一个目录。
QByteArray buildClassicPdf() {
    QByteArray pdf = "%PDF-1.4\n";
    QVector<QPair<int, QByteArray>> objects;
    QVector<qsizetype> offsets;
    const QByteArray content = "BT /F1 24 Tf 72 700 Td (Hello) Tj ET";
    appendObject(&pdf, &objects, &offsets, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    appendObject(&pdf, &objects, &offsets, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    appendObject(&pdf, &objects, &offsets, 3,
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>");
    appendObject(&pdf, &objects, &offsets, 4,
                 "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                     "\nendstream");
    const qsizetype xrefOffset = pdf.size();
    pdf += "xref\n0 5\n";
    pdf += classicEntry(0, true);
    for (const qsizetype offset : offsets) pdf += classicEntry(offset, false);
    pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefOffset) +
           "\n%%EOF\n";
    return pdf;
}

// 一个最小的交叉引用流 PDF：文件里没有 "trailer" 关键字。
QByteArray buildXrefStreamPdf() {
    QByteArray pdf = "%PDF-1.5\n";
    QVector<QPair<int, QByteArray>> objects;
    QVector<qsizetype> offsets;
    appendObject(&pdf, &objects, &offsets, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    appendObject(&pdf, &objects, &offsets, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    appendObject(&pdf, &objects, &offsets, 3,
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>");
    QByteArray data;
    appendXrefStreamEntry(&data, 0, 0, 65535);
    for (const qsizetype offset : offsets)
        appendXrefStreamEntry(&data, 1, static_cast<quint32>(offset), 0);
    const qsizetype xrefOffset = pdf.size();
    const QByteArray dict = "<< /Type /XRef /Size 5 /Root 1 0 R /W [1 4 2] /Index [0 5] /Length " +
                             QByteArray::number(data.size()) + " >>";
    pdf += "4 0 obj\n" + dict + "\nstream\n" + data + "\nendstream\nendobj\n";
    pdf += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return pdf;
}

// 页面对象藏在对象流里的 PDF：扫描器看不到它，必须报明确的错。
QByteArray buildObjectStreamPdf() {
    QByteArray pdf = "%PDF-1.5\n";
    QVector<QPair<int, QByteArray>> objects;
    QVector<qsizetype> offsets;
    appendObject(&pdf, &objects, &offsets, 1, "<< /Type /Catalog /Pages 2 0 R >>");
    appendObject(&pdf, &objects, &offsets, 2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    // 对象 3 是对象流：页面对象压缩在里面，扫不到。
    const QByteArray inner = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>";
    const QByteArray compressed = qCompress(inner);
    const qsizetype objStmOffset = pdf.size();
    pdf += "3 0 obj\n<< /Type /ObjStm /N 1 /First " + QByteArray::number(compressed.size() + 2) +
           " /Length " + QByteArray::number(compressed.size()) + " /Filter /FlateDecode >>\nstream\n" +
           compressed + "\nendstream\nendobj\n";
    QByteArray data;
    appendXrefStreamEntry(&data, 0, 0, 65535);
    appendXrefStreamEntry(&data, 1, static_cast<quint32>(offsets.at(0)), 0);
    appendXrefStreamEntry(&data, 1, static_cast<quint32>(offsets.at(1)), 0);
    appendXrefStreamEntry(&data, 2, static_cast<quint32>(objStmOffset), 0);  // 类型 2 = 在对象流里
    const qsizetype xrefOffset = pdf.size();
    const QByteArray dict = "<< /Type /XRef /Size 5 /Root 1 0 R /W [1 4 2] /Index [0 5] /Length " +
                             QByteArray::number(data.size()) + " >>";
    pdf += "4 0 obj\n" + dict + "\nstream\n" + data + "\nendstream\nendobj\n";
    pdf += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return pdf;
}

// 取某个对象的字典体（"N 0 obj" 与 "endobj" 之间）。取最后一个版本，就是增量更新后的最新版本。
QByteArray objectBody(const QByteArray& pdf, int number) {
    const QByteArray head = QByteArray::number(number) + " 0 obj";
    qsizetype at = pdf.lastIndexOf(head);
    while (at >= 0) {
        const bool boundary = at == 0 || !(pdf.at(at - 1) >= '0' && pdf.at(at - 1) <= '9');
        if (boundary) break;
        at = pdf.lastIndexOf(head, at - 1);
    }
    if (at < 0) return {};
    const qsizetype bodyStart = at + head.size();
    const qsizetype end = pdf.indexOf("endobj", bodyStart);
    if (end < 0) return {};
    return pdf.mid(bodyStart, end - bodyStart);
}

// 读回最后一版交叉引用流的条目（对象号 -> 文件偏移）。
// 用它验证"写出来的字节真的能被重新解析"，而不是只检查字符串里有没有某个关键字。
bool readLastXrefStreamEntries(const QByteArray& pdf, QVector<QPair<int, qsizetype>>* entries) {
    entries->clear();
    const qsizetype startxrefAt = pdf.lastIndexOf("startxref");
    if (startxrefAt < 0) return false;
    qsizetype cursor = startxrefAt + 9;
    while (cursor < pdf.size() && (pdf.at(cursor) < '0' || pdf.at(cursor) > '9')) ++cursor;
    const qsizetype begin = cursor;
    while (cursor < pdf.size() && pdf.at(cursor) >= '0' && pdf.at(cursor) <= '9') ++cursor;
    if (begin == cursor) return false;
    const qsizetype xrefOffset = pdf.mid(begin, cursor - begin).toLongLong();
    const qsizetype dictStart = pdf.indexOf("<<", xrefOffset);
    if (dictStart < 0) return false;
    const qsizetype dictEnd = pdf.indexOf(">>", dictStart);
    if (dictEnd < 0) return false;
    const QByteArray dict = pdf.mid(dictStart, dictEnd - dictStart + 2);
    if (!dict.contains("/Type /XRef")) return false;

    const qsizetype wAt = dict.indexOf("/W [");
    const qsizetype wEnd = dict.indexOf(']', wAt);
    if (wAt < 0 || wEnd < 0) return false;
    const QVector<QByteArray> widths = dict.mid(wAt + 4, wEnd - wAt - 4).simplified().split(' ');
    if (widths.size() != 3) return false;
    const int typeWidth = widths.at(0).toInt();
    const int offsetWidth = widths.at(1).toInt();
    const int generationWidth = widths.at(2).toInt();
    const int entryWidth = typeWidth + offsetWidth + generationWidth;

    QVector<QPair<int, int>> sections;
    const qsizetype indexAt = dict.indexOf("/Index [");
    if (indexAt >= 0) {
        const qsizetype indexEnd = dict.indexOf(']', indexAt);
        const QVector<QByteArray> numbers =
            dict.mid(indexAt + 7, indexEnd - indexAt - 7).simplified().split(' ');
        for (int i = 0; i + 1 < numbers.size(); i += 2)
            sections.append({numbers.at(i).toInt(), numbers.at(i + 1).toInt()});
    } else {
        const qsizetype sizeAt = dict.indexOf("/Size ");
        if (sizeAt < 0) return false;
        sections.append({0, dict.mid(sizeAt + 6).simplified().split(' ').first().toInt()});
    }

    const qsizetype streamAt = pdf.indexOf("stream\n", dictEnd);
    if (streamAt < 0) return false;
    int total = 0;
    for (const auto& section : sections) total += section.second * entryWidth;
    const QByteArray data = pdf.mid(streamAt + 7, total);
    qsizetype position = 0;
    for (const auto& section : sections) {
        for (int i = 0; i < section.second; ++i) {
            const int type = data.mid(position, typeWidth).toInt();
            position += typeWidth;
            quint32 offset = 0;
            for (int k = 0; k < offsetWidth; ++k)
                offset = (offset << 8) | static_cast<quint8>(data.at(position + k));
            position += offsetWidth + generationWidth;
            if (type == 1) entries->append({section.first + i, static_cast<qsizetype>(offset)});
        }
    }
    return true;
}

// 判断 "/Fields [...]" 这样的数组里是不是已经有某个对象的引用。
bool arrayHasReference(const QByteArray& body, const QByteArray& key, int number) {
    const qsizetype keyAt = body.indexOf(key);
    if (keyAt < 0) return false;
    const qsizetype open = body.indexOf('[', keyAt + key.size());
    if (open < 0) return false;
    int depth = 0;
    qsizetype close = -1;
    for (qsizetype i = open; i < body.size(); ++i) {
        if (body.at(i) == '[') ++depth;
        else if (body.at(i) == ']' && --depth == 0) { close = i; break; }
    }
    if (close < 0) return false;
    const QVector<QByteArray> tokens = body.mid(open + 1, close - open - 1).simplified().split(' ');
    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens.at(i) == QByteArray::number(number) && tokens.at(i + 1) == "0" &&
            tokens.at(i + 2) == "R")
            return true;
    }
    return false;
}

// 字段名在 PDF 里写成 UTF-16BE 字符串，检查时要按同样的编码去找。
bool containsFieldName(const QByteArray& body, const QString& name) {
    QByteArray utf16("\xFE\xFF", 2);
    for (const QChar ch : name) {
        const ushort code = ch.unicode();
        utf16.append(char((code >> 8) & 0xff));
        utf16.append(char(code & 0xff));
    }
    return body.contains(utf16);
}

// widget 字典里的 /Parent 指向谁；没有分组时返回 0。
int parentReference(const QByteArray& body) {
    const qsizetype at = body.indexOf("/Parent ");
    if (at < 0) return 0;
    return body.mid(at + 8).simplified().split(' ').first().toInt();
}

// widget 字典里 /AP /N 指向的外观流对象号。
int appearanceReference(const QByteArray& body) {
    const qsizetype apAt = body.indexOf("/AP");
    if (apAt < 0) return 0;
    const qsizetype nAt = body.indexOf("/N ", apAt);
    if (nAt < 0) return 0;
    return body.mid(nAt + 3).simplified().split(' ').first().toInt();
}

// 外观流对象里的内容流。
QByteArray streamContent(const QByteArray& body) {
    const qsizetype start = body.indexOf("stream\n");
    if (start < 0) return {};
    const qsizetype end = body.indexOf("endstream", start);
    if (end < 0) return {};
    return body.mid(start + 7, end - start - 7);
}

// 去掉字符串字面量，免得说明文字里的 "re" 之类被当成操作符。
QByteArray stripStrings(const QByteArray& content) {
    QByteArray result;
    bool inString = false;
    for (const char ch : content) {
        if (ch == '(') { inString = true; continue; }
        if (ch == ')') { inString = false; result.append(' '); continue; }
        if (!inString) result.append(ch);
    }
    return result;
}

// 外观流里所有用黑色填充的矩形（就是条码的竖条）。
QVector<QRectF> blackRects(const QByteArray& content) {
    QVector<QRectF> rects;
    const QVector<QByteArray> tokens = stripStrings(content).simplified().split(' ');
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i) == "rg" && i >= 3) {
            red = tokens.at(i - 3).toDouble();
            green = tokens.at(i - 2).toDouble();
            blue = tokens.at(i - 1).toDouble();
        } else if (tokens.at(i) == "re" && i >= 4) {
            if (red != 0.0 || green != 0.0 || blue != 0.0) continue;
            rects.append(QRectF(tokens.at(i - 4).toDouble(), tokens.at(i - 3).toDouble(),
                                 tokens.at(i - 2).toDouble(), tokens.at(i - 1).toDouble()));
        }
    }
    return rects;
}

FormFieldInsert makeField(const QString& name, FormFieldKind kind, double x, double y) {
    FormFieldInsert field;
    field.pageIndex = 0;
    field.name = name;
    field.rect = QRectF(x, y, 120.0, 18.0);
    field.kind = kind;
    return field;
}

}  // namespace

class FormFieldWriterTests : public QObject {
    Q_OBJECT

private slots:
    void classicTrailerStillAcceptsField();
    void xrefStreamPdfAcceptsField();
    void xrefStreamUpdateKeepsStreamFormat();
    void objectStreamPagesReportChineseError();
    void radioGroupSharesOneParent();
    void differentRadioGroupsUseDifferentParents();
    void ungroupedRadioKeepsLegacyShape();
    void radioGroupParentIsRegisteredInAcroForm();
    void barcodeAppearanceDrawsRealBars();
    void barcodeFallsBackToFieldName();
    void barcodeRejectsNonAsciiContent();
    void acroFormRegistersAllStandardFonts();
};

// 回归：经典 trailer 的 PDF 仍然走 xref 表 + trailer 的老路。
void FormFieldWriterTests::classicTrailerStillAcceptsField() {
    const QByteArray source = buildClassicPdf();
    const FormFieldWriteResult written =
        appendFormField(source, makeField(QStringLiteral("customerName"), FormFieldKind::Text, 10, 100));
    QVERIFY2(written.ok, qPrintable(written.error));
    // 增量更新：原字节一个都不动。
    QVERIFY(written.bytes.startsWith(source));
    QVERIFY(written.bytes.contains("\nxref\n"));
    QVERIFY(written.bytes.contains("trailer"));
    QVERIFY(!written.bytes.contains("/Type /XRef"));
    const QByteArray widget = objectBody(written.bytes, written.fieldObject);
    QVERIFY(widget.contains("/Subtype /Widget"));
    QVERIFY(containsFieldName(widget, QStringLiteral("customerName")));
}

// 交叉引用流的 PDF 也能建域：这类文件里没有 "trailer" 关键字。
void FormFieldWriterTests::xrefStreamPdfAcceptsField() {
    const QByteArray source = buildXrefStreamPdf();
    QVERIFY(!source.contains("trailer"));
    const FormFieldWriteResult written =
        appendFormField(source, makeField(QStringLiteral("customerName"), FormFieldKind::Text, 10, 100));
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY(written.bytes.startsWith(source));

    // 产出必须能被重新解析：最后一版交叉引用流里能找回这个域。
    QVector<QPair<int, qsizetype>> entries;
    QVERIFY2(readLastXrefStreamEntries(written.bytes, &entries), "产出的交叉引用流无法解析");
    bool found = false;
    for (const auto& entry : entries) {
        if (entry.first != written.fieldObject) continue;
        found = true;
        const QByteArray body = objectBody(written.bytes, entry.first);
        QVERIFY(body.contains("/Subtype /Widget"));
        QVERIFY(containsFieldName(body, QStringLiteral("customerName")));
    }
    QVERIFY(found);
}

// 交叉引用流的文件，后续更新必须继续用交叉引用流（§7.5.8.3 禁止混用）。
void FormFieldWriterTests::xrefStreamUpdateKeepsStreamFormat() {
    QByteArray bytes = buildXrefStreamPdf();
    for (int i = 0; i < 2; ++i) {
        const FormFieldWriteResult written = appendFormField(
            bytes, makeField(QStringLiteral("field%1").arg(i), FormFieldKind::Text, 10, 100 + i * 20));
        QVERIFY2(written.ok, qPrintable(written.error));
        bytes = written.bytes;
    }
    QVERIFY(bytes.contains("/Type /XRef"));
    QVERIFY(!bytes.contains("\nxref\n"));
    QVERIFY(bytes.contains("/W [1 4 2]"));
    QVERIFY(bytes.contains("/Index ["));
}

// 页面在对象流里：必须给明确的中文错误，不能默默写出坏文件。
void FormFieldWriterTests::objectStreamPagesReportChineseError() {
    const FormFieldWriteResult written = appendFormField(
        buildObjectStreamPdf(), makeField(QStringLiteral("customerName"), FormFieldKind::Text, 10, 100));
    QVERIFY(!written.ok);
    QVERIFY(written.bytes.isEmpty());
    QVERIFY2(written.error.contains(QStringLiteral("对象流")), qPrintable(written.error));
}

// 同名分组的单选按钮共享同一个父字段。
void FormFieldWriterTests::radioGroupSharesOneParent() {
    QByteArray bytes = buildClassicPdf();
    FormFieldInsert first = makeField(QStringLiteral("genderMale"), FormFieldKind::RadioButton, 10, 200);
    first.groupName = QStringLiteral("gender");
    FormFieldWriteResult written = appendFormField(bytes, first);
    QVERIFY2(written.ok, qPrintable(written.error));
    const int firstWidget = written.fieldObject;

    FormFieldInsert second = makeField(QStringLiteral("genderFemale"), FormFieldKind::RadioButton, 30, 200);
    second.groupName = QStringLiteral("gender");
    written = appendFormField(written.bytes, second);
    QVERIFY2(written.ok, qPrintable(written.error));
    const int secondWidget = written.fieldObject;

    const QByteArray firstBody = objectBody(written.bytes, firstWidget);
    const QByteArray secondBody = objectBody(written.bytes, secondWidget);
    const int parent = parentReference(firstBody);
    QVERIFY(parent > 0);
    QCOMPARE(parentReference(secondBody), parent);

    // 导出名按组内顺序编号：gender_1、gender_2。
    QVERIFY(firstBody.contains("/gender_1"));
    QVERIFY(secondBody.contains("/gender_2"));
    // 还没勾选：父字段的 /V 是 /Off，按钮的 /AS 是 /Off。
    const QByteArray parentBody = objectBody(written.bytes, parent);
    QVERIFY(parentBody.contains("/V /Off"));
    QVERIFY(firstBody.contains("/AS /Off"));
    // 父字段带 /FT /Btn 与 radio 位。
    QVERIFY(parentBody.contains("/FT /Btn"));
    QVERIFY(parentBody.contains("/Ff 32768"));
    // 父字段把两个按钮都收在 /Kids 里。
    QVERIFY(arrayHasReference(parentBody, "/Kids", firstWidget));
    QVERIFY(arrayHasReference(parentBody, "/Kids", secondWidget));
}

// 不同名的分组不能共用父字段。
void FormFieldWriterTests::differentRadioGroupsUseDifferentParents() {
    QByteArray bytes = buildClassicPdf();
    FormFieldInsert first = makeField(QStringLiteral("a1"), FormFieldKind::RadioButton, 10, 200);
    first.groupName = QStringLiteral("alpha");
    FormFieldWriteResult written = appendFormField(bytes, first);
    QVERIFY2(written.ok, qPrintable(written.error));
    const int firstWidget = written.fieldObject;

    FormFieldInsert second = makeField(QStringLiteral("b1"), FormFieldKind::RadioButton, 30, 200);
    second.groupName = QStringLiteral("beta");
    written = appendFormField(written.bytes, second);
    QVERIFY2(written.ok, qPrintable(written.error));
    const int secondWidget = written.fieldObject;

    const int firstParent = parentReference(objectBody(written.bytes, firstWidget));
    const int secondParent = parentReference(objectBody(written.bytes, secondWidget));
    QVERIFY(firstParent > 0);
    QVERIFY(secondParent > 0);
    QVERIFY(firstParent != secondParent);
}

// 回归：不写 groupName 时，行为与原来完全一致 —— 独立按钮，不带 /Parent。
void FormFieldWriterTests::ungroupedRadioKeepsLegacyShape() {
    const FormFieldWriteResult written = appendFormField(
        buildClassicPdf(), makeField(QStringLiteral("choice"), FormFieldKind::RadioButton, 10, 200));
    QVERIFY2(written.ok, qPrintable(written.error));
    const QByteArray widget = objectBody(written.bytes, written.fieldObject);
    QVERIFY(!widget.contains("/Parent"));
    QVERIFY(widget.contains("/FT /Btn"));
    QVERIFY(widget.contains("/Ff 32768"));
    QVERIFY(widget.contains("/AS /Off"));
    QVERIFY(widget.contains("/Yes"));
    QVERIFY(widget.contains("/V /Off"));
}

// 父字段必须进 AcroForm /Fields（Acrobat 是从这儿认出单选组的），
// 但它不是页面上的批注，不能进 /Annots。
void FormFieldWriterTests::radioGroupParentIsRegisteredInAcroForm() {
    QByteArray bytes = buildClassicPdf();
    FormFieldInsert first = makeField(QStringLiteral("genderMale"), FormFieldKind::RadioButton, 10, 200);
    first.groupName = QStringLiteral("gender");
    FormFieldWriteResult written = appendFormField(bytes, first);
    QVERIFY2(written.ok, qPrintable(written.error));

    FormFieldInsert second = makeField(QStringLiteral("genderFemale"), FormFieldKind::RadioButton, 30, 200);
    second.groupName = QStringLiteral("gender");
    written = appendFormField(written.bytes, second);
    QVERIFY2(written.ok, qPrintable(written.error));

    const int parent = parentReference(objectBody(written.bytes, written.fieldObject));
    QVERIFY(parent > 0);
    const QByteArray root = objectBody(written.bytes, 1);
    QVERIFY2(arrayHasReference(root, "/Fields", parent), "父字段没有进 AcroForm /Fields");
    const QByteArray page = objectBody(written.bytes, 3);
    QVERIFY(arrayHasReference(page, "/Annots", written.fieldObject));
    QVERIFY(!arrayHasReference(page, "/Annots", parent));
}

// 条码字段的外观流里必须画的是真条码：黑条数与 modules 对得上，条宽没有被拉伸。
void FormFieldWriterTests::barcodeAppearanceDrawsRealBars() {
    const QString text = QStringLiteral("12345678");
    FormFieldInsert field = makeField(QStringLiteral("code"), FormFieldKind::Barcode, 10, 300);
    field.rect = QRectF(10, 300, 220, 60);
    field.barcodeText = text;
    field.barcodeCaption = QStringLiteral("SCAN ME");
    const FormFieldWriteResult written = appendFormField(buildClassicPdf(), field);
    QVERIFY2(written.ok, qPrintable(written.error));

    const QByteArray widget = objectBody(written.bytes, written.fieldObject);
    // 条形码字段在 PDF 里本来就是文本域。
    QVERIFY(widget.contains("/FT /Tx"));
    QVERIFY(widget.contains("/MaxLen 128"));
    const int appearance = appearanceReference(widget);
    QVERIFY(appearance > 0);
    const QByteArray content = streamContent(objectBody(written.bytes, appearance));
    QVERIFY(!content.isEmpty());

    const BarcodeCode128 code = encodeCode128(text);
    QVERIFY(code.ok);
    int totalModules = 0;
    int expectedBars = 0;
    for (const BarcodeModule& module : code.modules) {
        totalModules += module.width;
        if (module.bar) ++expectedBars;
    }
    const QVector<QRectF> bars = blackRects(content);
    QCOMPARE(bars.size(), expectedBars);

    // 条宽 = 域宽 / 总模块数，取整且至少 1 份点；余量留成白边，不拉伸条码。
    const double unit = std::max(1.0, std::floor(field.rect.width() / totalModules));
    const double left = bars.isEmpty() ? 0.0 : bars.first().left();
    double cursor = left;
    int index = 0;
    for (const BarcodeModule& module : code.modules) {
        const double moduleWidth = unit * module.width;
        if (module.bar) {
            const QRectF bar = bars.at(index++);
            QVERIFY2(qAbs(bar.width() - moduleWidth) < 0.01,
                     qPrintable(QStringLiteral("第 %1 条宽度 %2 与期望 %3 不符")
                                    .arg(index).arg(bar.width()).arg(moduleWidth)));
            QVERIFY2(qAbs(bar.left() - cursor) < 0.01, "条码条没有首尾相接");
        }
        cursor += moduleWidth;
    }
    QVERIFY(left >= 0.0);
    QVERIFY(cursor <= field.rect.width() + 0.01);  // 富余量留成白边
    QVERIFY(content.contains("Tj"));               // 说明文字画出来了
}

// barcodeText 留空时回退用字段名当条码内容。
void FormFieldWriterTests::barcodeFallsBackToFieldName() {
    const QString name = QStringLiteral("ABC123");
    FormFieldInsert field = makeField(name, FormFieldKind::Barcode, 10, 300);
    field.rect = QRectF(10, 300, 200, 50);
    const FormFieldWriteResult written = appendFormField(buildClassicPdf(), field);
    QVERIFY2(written.ok, qPrintable(written.error));
    const QByteArray widget = objectBody(written.bytes, written.fieldObject);
    const QByteArray content = streamContent(objectBody(written.bytes, appearanceReference(widget)));
    const BarcodeCode128 code = encodeCode128(name);
    QVERIFY(code.ok);
    int expectedBars = 0;
    for (const BarcodeModule& module : code.modules)
        if (module.bar) ++expectedBars;
    QCOMPARE(blackRects(content).size(), expectedBars);
}

// 条码内容编不出来时必须报中文错误，不能画一张空白条码。
void FormFieldWriterTests::barcodeRejectsNonAsciiContent() {
    FormFieldInsert field = makeField(QStringLiteral("code"), FormFieldKind::Barcode, 10, 300);
    field.barcodeText = QStringLiteral("条码-123");
    const FormFieldWriteResult written = appendFormField(buildClassicPdf(), field);
    QVERIFY(!written.ok);
    QVERIFY(written.bytes.isEmpty());
    QVERIFY2(written.error.contains(QStringLiteral("条形码")), qPrintable(written.error));
    QVERIFY2(written.error.contains(QStringLiteral("ASCII")), qPrintable(written.error));
}

// 新建 AcroForm 时要把标准 14 号字体全登记进 /DR /Font。
//
// 为什么钉这条：域的 /DA 里写的是**资源名**（例如 "/HeBo 10 Tf"），
// 阅读器按这个名字去 /DR /Font 里找。少登记一个，用户选的粗体 / 斜体
// 在别的阅读器里就会退回默认字体 —— 这正是「表单字体只给三档」的老毛病。
void FormFieldWriterTests::acroFormRegistersAllStandardFonts() {
    const FormFieldWriteResult written =
        appendFormField(buildClassicPdf(),
                        makeField(QStringLiteral("customerName"), FormFieldKind::Text, 10, 100));
    QVERIFY2(written.ok, qPrintable(written.error));
    // 目录对象（对象号 1）在增量更新里被重写，/AcroForm 挂在那儿。
    const QByteArray root = objectBody(written.bytes, 1);
    QVERIFY(root.contains("/AcroForm"));
    QVERIFY(root.contains("/DR"));
    QVERIFY(root.contains("/Font"));
    // 14 个资源名与它们的 BaseFont 都要在。
    for (const QString& name : aipdf::pdf::standardFormFontNames()) {
        const QByteArray key = "/" + name.toLatin1();
        QVERIFY2(root.contains(key),
                 qPrintable(QStringLiteral("/DR 里没有登记资源名 %1").arg(name)));
        const QString base = aipdf::pdf::standardFormBaseFont(name);
        QVERIFY2(root.contains("/" + base.toLatin1()),
                 qPrintable(QStringLiteral("/DR 里没有 %1 的 BaseFont %2").arg(name, base)));
    }
    // 登记的必须是 14 个，不是 15 个（多出来的说明表里混进了不该有的名字）。
    int fontEntries = 0;
    for (const QString& name : aipdf::pdf::standardFormFontNames()) {
        if (root.contains("/" + name.toLatin1() + " << /Type /Font")) ++fontEntries;
    }
    QCOMPARE(fontEntries, 14);
}

QTEST_MAIN(FormFieldWriterTests)
#include "FormFieldWriterTests.moc"
