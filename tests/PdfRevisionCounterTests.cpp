// 数文档自己的修订数（xref 链长度）的单元测试。
//
// 判据不是「函数没崩」，而是**数出来的数对不对**：
//   - 单修订文档是 1，不是 0（0 是「不是 PDF」）；
//   - 增量更新过 n 次是 n + 1；
//   - 把另一份 PDF 嵌进来**不改变**这个数（这正是旧实现错的地方：
//     旧实现数字节里的 %%EOF，内层 PDF 的 %%EOF 被一起数进去）；
//   - classic xref 表与 xref 流两种写法都要走通；
//   - 解析不出来时返回 0，不编数。

#include "pdf/kernel/PdfRevisionCounter.h"

#include <QByteArray>
#include <QTest>
#include <QVector>

using aipdf::pdf::countPdfRevisions;

namespace {

// PDF 里的一个间接对象。
struct PdfObject {
    int number = 0;
    QByteArray body;
};

// classic xref 表的条目必须正好 20 字节：10 位偏移 + 空格 + 5 位代数 + 空格 + 类型 + 换行。
//
// 补零用 rightJustified：QString::arg 的第三个参数是**进制**不是填充字符，
// 写错了会把偏移按 48 进制输出，qpdf 会直接判定 xref 损坏。
QByteArray xrefEntry(int offset) {
    QByteArray entry = QByteArray::number(offset).rightJustified(10, '0');
    entry += " ";
    entry += QByteArray::number(0).rightJustified(5, '0');
    entry += " n \n";
    return entry;
}

// 造一份最小的单修订 PDF（classic xref 表）。
//
// extraObject：可选的第 4 号对象（用来把另一份 PDF 嵌进去）。
QByteArray buildMinimalPdf(const QByteArray& extraObject = QByteArray()) {
    QVector<PdfObject> objects;
    objects.push_back({1, QByteArrayLiteral("<< /Type /Catalog /Pages 2 0 R >>")});
    objects.push_back({2, QByteArrayLiteral("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")});
    objects.push_back({3, QByteArrayLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>")});

    QByteArray out = QByteArrayLiteral("%PDF-1.7\n");
    QVector<int> offsets;
    for (const PdfObject& object : objects) {
        offsets.append(out.size());
        out += QByteArray::number(object.number) + " 0 obj\n" + object.body + "\nendobj\n";
    }
    if (!extraObject.isEmpty()) {
        offsets.append(out.size());
        out += extraObject;
    }

    const int xrefOffset = out.size();
    out += QByteArrayLiteral("xref\n0 ") + QByteArray::number(offsets.size() + 1) + "\n";
    out += QByteArrayLiteral("0000000000 65535 f \n");
    for (int offset : offsets) {
        out += xrefEntry(offset);
    }
    out += QByteArrayLiteral("trailer\n<< /Size ") + QByteArray::number(offsets.size() + 1) +
           QByteArrayLiteral(" /Root 1 0 R >>\n");
    out += QByteArrayLiteral("startxref\n") + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return out;
}

// 在 base 之上追加一次增量更新（classic xref 表，trailer 里带 /Prev）。
QByteArray appendIncrementalUpdate(const QByteArray& base, const QByteArray& newObjectBody) {
    const int marker = base.lastIndexOf(QByteArrayLiteral("startxref"));
    if (marker < 0) {
        return {};
    }
    int pos = marker + 9;
    while (pos < base.size() && (base.at(pos) == ' ' || base.at(pos) == '\r' || base.at(pos) == '\n')) {
        ++pos;
    }
    int previousXref = 0;
    bool haveDigits = false;
    while (pos < base.size() && base.at(pos) >= '0' && base.at(pos) <= '9') {
        previousXref = previousXref * 10 + (base.at(pos) - '0');
        ++pos;
        haveDigits = true;
    }
    if (!haveDigits) {
        return {};
    }

    QByteArray out = base;
    const int objectOffset = out.size();
    out += QByteArrayLiteral("4 0 obj\n") + newObjectBody + "\nendobj\n";
    const int xrefOffset = out.size();
    out += QByteArrayLiteral("xref\n4 1\n");
    out += xrefEntry(objectOffset);
    out += QByteArrayLiteral("trailer\n<< /Size 5 /Root 1 0 R /Prev ") +
           QByteArray::number(previousXref) + QByteArrayLiteral(" >>\n");
    out += QByteArrayLiteral("startxref\n") + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return out;
}

// 把另一份 PDF 作为嵌入文件塞进第 4 号对象。
//
// 关键：内层 PDF 的字节里也有 %%EOF 与 startxref。
// 旧的「数字节里的 %%EOF」实现会把内层的也算进去。
QByteArray embeddedPdfObject(const QByteArray& embedded) {
    QByteArray body = QByteArrayLiteral("<< /Type /EmbeddedFile /Subtype /application#2Fpdf /Length ") +
                      QByteArray::number(embedded.size()) + " >>\nstream\n";
    body += embedded;
    body += QByteArrayLiteral("\nendstream");
    return QByteArrayLiteral("4 0 obj\n") + body + "\nendobj\n";
}

// 四字节大端（xref 流的 /W [1 4 1] 里偏移是 4 字节大端）。
QByteArray bigEndian32(int value) {
    QByteArray out(4, '\0');
    out[0] = static_cast<char>((value >> 24) & 0xFF);
    out[1] = static_cast<char>((value >> 16) & 0xFF);
    out[2] = static_cast<char>((value >> 8) & 0xFF);
    out[3] = static_cast<char>(value & 0xFF);
    return out;
}

// 造一份用 xref **流**的 PDF（PDF 1.5 起的写法）。
QByteArray buildXrefStreamPdf() {
    QByteArray out = QByteArrayLiteral("%PDF-1.7\n");
    QVector<int> offsets;
    const QVector<QByteArray> bodies = {
        QByteArrayLiteral("<< /Type /Catalog /Pages 2 0 R >>"),
        QByteArrayLiteral("<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        QByteArrayLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>"),
    };
    for (int index = 0; index < bodies.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + bodies.at(index) + "\nendobj\n";
    }
    const int streamOffset = out.size();
    offsets.append(streamOffset);

    // xref 流的数据：每个对象 6 字节（1 字节类型 + 4 字节偏移 + 1 字节代数）。
    QByteArray data;
    data += QByteArrayLiteral("0") + bigEndian32(0) + QByteArray(1, '\0');
    for (int index = 0; index < offsets.size(); ++index) {
        data += QByteArrayLiteral("1") + bigEndian32(offsets.at(index)) + QByteArray(1, '\0');
    }
    out += QByteArrayLiteral("4 0 obj\n<< /Type /XRef /Size 5 /W [1 4 1] /Root 1 0 R /Length ") +
           QByteArray::number(data.size()) + " >>\nstream\n";
    out += data;
    out += QByteArrayLiteral("\nendstream\nendobj\n");
    out += QByteArrayLiteral("startxref\n") + QByteArray::number(streamOffset) + "\n%%EOF\n";
    return out;
}

// 造一份 /Prev 互相指着的文件（两节 xref 成环）。
//
// 坏文件会这样。不记走过的偏移就会一直绕到上限，把修订数数成几千。
QByteArray buildCyclicXrefPdf() {
    QByteArray out = QByteArrayLiteral("%PDF-1.7\n");
    QVector<int> offsets;
    const QVector<QByteArray> bodies = {
        QByteArrayLiteral("<< /Type /Catalog /Pages 2 0 R >>"),
        QByteArrayLiteral("<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        QByteArrayLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>"),
    };
    for (int index = 0; index < bodies.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + bodies.at(index) + "\nendobj\n";
    }

    // 两节 xref 的 /Prev 先写占位（10 位），写完再回填真实偏移。
    QByteArray entries;
    entries += QByteArrayLiteral("0000000000 65535 f \n");
    for (int offset : offsets) {
        entries += xrefEntry(offset);
    }
    const int firstXref = out.size();
    out += QByteArrayLiteral("xref\n0 4\n") + entries +
           QByteArrayLiteral("trailer\n<< /Size 4 /Root 1 0 R /Prev 0000000000 >>\n");
    const int secondXref = out.size();
    out += QByteArrayLiteral("xref\n0 4\n") + entries +
           QByteArrayLiteral("trailer\n<< /Size 4 /Root 1 0 R /Prev 0000000000 >>\n");
    out += QByteArrayLiteral("startxref\n") + QByteArray::number(secondXref) + "\n%%EOF\n";

    // 回填：第一节的 /Prev 指向第二节，第二节指向第一节（成环）。
    const QByteArray firstTarget = QByteArray::number(secondXref).rightJustified(10, '0');
    const QByteArray secondTarget = QByteArray::number(firstXref).rightJustified(10, '0');
    out.replace(QByteArrayLiteral("/Prev 0000000000"), QByteArrayLiteral("/Prev ") + firstTarget);
    out.replace(QByteArrayLiteral("/Prev 0000000000"), QByteArrayLiteral("/Prev ") + secondTarget);
    return out;
}

}  // namespace

class PdfRevisionCounterTests : public QObject {
    Q_OBJECT

private slots:
    void singleRevisionDocumentCountsOne();
    void incrementalUpdateCountsTwoRevisions();
    void embeddedPdfDoesNotInflateTheCount();
    void xrefStreamDocumentCountsOne();
    void garbageInputCountsZero();
    void truncatedDocumentCountsZero();
    void prevInsideAStringIsNotMistakenForAKey();
    void cyclicPrevChainDoesNotLoopForever();
    void linearizedFirstPageXrefIsNotAnOldRevision();
};

void PdfRevisionCounterTests::singleRevisionDocumentCountsOne() {
    QCOMPARE(countPdfRevisions(buildMinimalPdf()), 1);
}

void PdfRevisionCounterTests::incrementalUpdateCountsTwoRevisions() {
    const QByteArray base = buildMinimalPdf();
    const QByteArray updated = appendIncrementalUpdate(
        base, QByteArrayLiteral("<< /Title (REVISED) >>"));
    QVERIFY(!updated.isEmpty());
    QCOMPARE(countPdfRevisions(updated), 2);
    // 再更新一次就是三节。
    const QByteArray again = appendIncrementalUpdate(
        updated, QByteArrayLiteral("<< /Title (REVISED AGAIN) >>"));
    QVERIFY(!again.isEmpty());
    QCOMPARE(countPdfRevisions(again), 3);
}

void PdfRevisionCounterTests::embeddedPdfDoesNotInflateTheCount() {
    const QByteArray inner = buildMinimalPdf();
    QVERIFY(inner.contains("%%EOF"));
    const QByteArray outer = buildMinimalPdf(embeddedPdfObject(inner));
    // 外层自己只有一节 xref：内层 PDF 的 %%EOF 与 startxref 都不算数。
    QCOMPARE(countPdfRevisions(outer), 1);
    // 而字节里确实有两个 %%EOF —— 旧的「数字节」实现会把这里算成 1 份增量历史。
    QCOMPARE(outer.count("%%EOF"), 2);
}

void PdfRevisionCounterTests::xrefStreamDocumentCountsOne() {
    QCOMPARE(countPdfRevisions(buildXrefStreamPdf()), 1);
}

void PdfRevisionCounterTests::garbageInputCountsZero() {
    QCOMPARE(countPdfRevisions(QByteArray()), 0);
    QCOMPARE(countPdfRevisions(QByteArrayLiteral("not a pdf at all")), 0);
    QCOMPARE(countPdfRevisions(QByteArrayLiteral("%PDF-1.7\nstartxref\n999999\n%%EOF\n")), 0);
}

void PdfRevisionCounterTests::truncatedDocumentCountsZero() {
    QByteArray pdf = buildMinimalPdf();
    pdf.truncate(pdf.size() / 2);
    QCOMPARE(countPdfRevisions(pdf), 0);
}

void PdfRevisionCounterTests::prevInsideAStringIsNotMistakenForAKey() {
    // 文档标题里写着 "(/Prev 999)"：那是字符串内容，不是 /Prev 键。
    QByteArray pdf = buildMinimalPdf();
    pdf.replace(QByteArrayLiteral("/Root 1 0 R >>"),
                QByteArrayLiteral("/Root 1 0 R /Info << /Title (/Prev 999) >> >>"));
    QCOMPARE(countPdfRevisions(pdf), 1);
}

// /Prev 成环：不能绕到上限去（那会把修订数数成几千）。
void PdfRevisionCounterTests::cyclicPrevChainDoesNotLoopForever() {
    const QByteArray pdf = buildCyclicXrefPdf();
    const int count = countPdfRevisions(pdf);
    QVERIFY2(count < 10, qPrintable(QStringLiteral("成环的 xref 链数出了 %1 节").arg(count)));
}

// 线性化文件：首页 xref 的 trailer 带 /Linearized，它不是「旧修订」。
void PdfRevisionCounterTests::linearizedFirstPageXrefIsNotAnOldRevision() {
    QByteArray pdf = buildMinimalPdf();
    pdf.replace(QByteArrayLiteral("/Root 1 0 R >>"),
                QByteArrayLiteral("/Root 1 0 R /Linearized 1.0 >>"));
    QCOMPARE(countPdfRevisions(pdf), 0);
}

QTEST_MAIN(PdfRevisionCounterTests)
#include "PdfRevisionCounterTests.moc"