#include "pdf/kernel/PageObjectModel.h"

#include <QTest>

using aipdf::pdf::DocumentObjectModel;
using aipdf::pdf::PageObjectKind;
using aipdf::pdf::PageTextBlockIndex;
using aipdf::pdf::PageTextRole;
using aipdf::pdf::readPageObjectModel;

namespace {

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) maxId = qMax(maxId, object.first);
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (!offsets.contains(id)) {
            out += "0000000000 65535 f \n";
            continue;
        }
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

QByteArray streamObject(const QByteArray& content) {
    return QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

// 2×2 的 RGB 位图（12 字节）。
QByteArray tinyImageData() {
    QByteArray data;
    data.append(char(0xFF)); data.append(char(0x00)); data.append(char(0x00));
    data.append(char(0x00)); data.append(char(0xFF)); data.append(char(0x00));
    data.append(char(0x00)); data.append(char(0x00)); data.append(char(0xFF));
    data.append(char(0xFF)); data.append(char(0xFF)); data.append(char(0xFF));
    return data;
}

// 一份带页眉、正文、2×2 表格与图片的样本。
//
// 版面（400×300）：
//   y=285  页眉「Running head」（MCID 0）
//   y=260  正文「Body paragraph text」（MCID 1）
//   y=220  表格第一行：A1（MCID 2）、B1（MCID 3）
//   y=200  表格第二行：A2（MCID 4）、B2（MCID 5）
//   图片：40,60 起，100×50（MCID 6）
QByteArray samplePdf() {
    const QByteArray pageContent =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 260 Td (Body paragraph text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 2 >> BDC\n"
        "BT /F1 10 Tf 40 220 Td (A1) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 10 Tf 200 220 Td (B1) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 4 >> BDC\n"
        "BT /F1 10 Tf 40 200 Td (A2) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 5 >> BDC\n"
        "BT /F1 10 Tf 200 200 Td (B2) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 6 >> BDC\n"
        "q 100 0 0 50 40 60 cm /Im1 Do Q\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> "
                                            "/XObject << /Im1 9 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageContent)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [10 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
                                            "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 12 >>\n"
                                            "stream\n") +
                              tinyImageData() + QByteArray("\nendstream")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                             "/K [11 0 R 12 0 R 13 0 R] >>")));
    // 页眉。
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /Head /P 10 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    // 正文段落。
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /P /P 10 0 R "
                                             "/Pg 3 0 R /K 1 >>")));
    // 表格：四个单元格。
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /Table /P 10 0 R "
                                             "/Pg 3 0 R /K [14 0 R 15 0 R 16 0 R 17 0 R] >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 2 >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 3 >>")));
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 4 >>")));
    objects.append(qMakePair(17, QByteArray("<< /Type /StructElem /S /TD /P 13 0 R /Pg 3 0 R /K 5 >>")));
    return buildPdf(objects);
}

// 与 samplePdf 相同，但多一个**位置不同**的图片。
// 用来验证：稳定 id 不依赖「页面对象数组的下标」。
QByteArray samplePdfWithExtraImage() {
    const QByteArray pageContent =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 285 Td (Running head) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 260 Td (Body paragraph text) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 2 >> BDC\n"
        "BT /F1 10 Tf 40 220 Td (A1) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 10 Tf 200 220 Td (B1) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 4 >> BDC\n"
        "BT /F1 10 Tf 40 200 Td (A2) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 5 >> BDC\n"
        "BT /F1 10 Tf 200 200 Td (B2) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 6 >> BDC\n"
        "q 100 0 0 50 40 60 cm /Im1 Do Q\n"
        "EMC\n"
        "q 20 0 0 20 300 150 cm /Im2 Do Q\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> "
                                            "/XObject << /Im1 9 0 R /Im2 11 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageContent)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [10 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
                                            "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 12 >>\n"
                                            "stream\n") +
                              tinyImageData() + QByteArray("\nendstream")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                             "/K [12 0 R 13 0 R 14 0 R] >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 "
                                            "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 12 >>\n"
                                            "stream\n") +
                              tinyImageData() + QByteArray("\nendstream")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /Head /P 10 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /P /P 10 0 R "
                                             "/Pg 3 0 R /K 1 >>")));
    objects.append(qMakePair(14, QByteArray("<< /Type /StructElem /S /Table /P 10 0 R "
                                             "/Pg 3 0 R /K [15 0 R 16 0 R 17 0 R 18 0 R] >>")));
    objects.append(qMakePair(15, QByteArray("<< /Type /StructElem /S /TD /P 14 0 R /Pg 3 0 R /K 2 >>")));
    objects.append(qMakePair(16, QByteArray("<< /Type /StructElem /S /TD /P 14 0 R /Pg 3 0 R /K 3 >>")));
    objects.append(qMakePair(17, QByteArray("<< /Type /StructElem /S /TD /P 14 0 R /Pg 3 0 R /K 4 >>")));
    objects.append(qMakePair(18, QByteArray("<< /Type /StructElem /S /TD /P 14 0 R /Pg 3 0 R /K 5 >>")));
    return buildPdf(objects);
}

// 一段文字被拆成两个文字对象（同一个 MCID）。
// 用来验证：同一块里的多行文字仍然只算**一个**文字块。
QByteArray twoLineParagraphPdf() {
    const QByteArray pageContent =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 260 Td (First line of the) Tj ET\n"
        "BT /F1 12 Tf 40 245 Td (second line) Tj ET\n"
        "EMC";
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 6 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 5 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageContent)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(6, QByteArray("<< /Type /StructTreeRoot /K [8 0 R] >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructElem /S /Document /P 6 0 R /K [9 0 R] >>")));
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /P /P 8 0 R /Pg 3 0 R /K 0 >>")));
    return buildPdf(objects);
}

}  // namespace

// 与领域层同口径的「文字块归属索引」：页眉、正文、表格单元格各自成块。
PageTextBlockIndex sampleIndex() {
    PageTextBlockIndex index;
    index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, 0), 0);
    index.blockRoles.append(PageTextRole::Header);
    index.blockTexts.append(QStringLiteral("Running head"));
    index.blockTags.append(QStringLiteral("Head"));
    index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, 1), 1);
    index.blockRoles.append(PageTextRole::Body);
    index.blockTexts.append(QStringLiteral("Body paragraph text"));
    index.blockTags.append(QStringLiteral("P"));
    for (int mcid = 2; mcid <= 5; ++mcid) {
        const int block = mcid;
        index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, mcid), block);
        index.blockRoles.append(PageTextRole::Body);
        index.blockTexts.append(QString());
        index.blockTags.append(QStringLiteral("TD"));
    }
    return index;
}

class PageObjectModelTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- 计数 ----

    void theObjectsOfTheSampleAreCounted() {
        const DocumentObjectModel model = readPageObjectModel(samplePdf(), sampleIndex());
        QVERIFY2(model.ok(), qPrintable(model.error));
        QCOMPARE(model.pageCount, 1);
        // 一段正文、一个 2×2 表格、一张图片、一个页眉。
        QCOMPARE(model.counts.textBlocks, 1);
        QCOMPARE(model.counts.tables, 1);
        QCOMPARE(model.counts.images, 1);
        QCOMPARE(model.counts.headers, 1);
        QCOMPARE(model.counts.footers, 0);
    }

    void withoutAStructureTheHeaderIsJustBodyText() {
        // 没有归属索引时，谁也不知道「Running head」是页眉：
        // 它就是一段普通文字，如实算成正文文字块。
        const DocumentObjectModel model = readPageObjectModel(samplePdf());
        QVERIFY2(model.ok(), qPrintable(model.error));
        QCOMPARE(model.counts.headers, 0);
        QCOMPARE(model.counts.textBlocks, 2);
    }

    void theTableCellsAreNotCountedAsBodyText() {
        const DocumentObjectModel model = readPageObjectModel(samplePdf(), sampleIndex());
        QVERIFY2(model.ok(), qPrintable(model.error));
        // 表格里的四个单元格归表格，不算正文文字块。
        int tableCells = 0;
        for (const auto& object : model.objects) {
            if (object.kind != PageObjectKind::Table) continue;
            QCOMPARE(object.rowCount, 2);
            QCOMPARE(object.columnCount, 2);
            tableCells = object.cells.size();
        }
        QCOMPARE(tableCells, 4);
        QCOMPARE(model.counts.textBlocks, 1);
    }

    void theTextOfTheParagraphIsKeptInTheBlock() {
        const DocumentObjectModel model = readPageObjectModel(samplePdf(), sampleIndex());
        QVERIFY2(model.ok(), qPrintable(model.error));
        bool found = false;
        for (const auto& object : model.objects) {
            if (object.kind != PageObjectKind::Text) continue;
            found = true;
            QVERIFY2(object.text.contains(QStringLiteral("Body paragraph text")),
                     qPrintable(object.text));
            QVERIFY(!object.fingerprint.isEmpty());
        }
        QVERIFY(found);
    }

    void theImageIsCountedWithAStableFingerprint() {
        const DocumentObjectModel model = readPageObjectModel(samplePdf(), sampleIndex());
        QVERIFY2(model.ok(), qPrintable(model.error));
        int images = 0;
        for (const auto& object : model.objects) {
            if (object.kind != PageObjectKind::Image) continue;
            ++images;
            QVERIFY(!object.fingerprint.isEmpty());
            // 图片的自然尺寸按 DPI 换算：PDFium 给出的 DPI 是按它在页面上的
            // 实际大小反推的，所以自然尺寸就是它原来的尺寸（100×50 点）。
            QVERIFY2(qAbs(object.naturalSize.width() - 100.0) < 0.05,
                     qPrintable(QStringLiteral("宽=%1").arg(object.naturalSize.width())));
            QVERIFY2(qAbs(object.naturalSize.height() - 50.0) < 0.05,
                     qPrintable(QStringLiteral("高=%1").arg(object.naturalSize.height())));
        }
        QCOMPARE(images, 1);
    }

    // ---- 分组 ----

    void twoLinesOfOneParagraphAreOneTextBlock() {
        // 没有归属索引时，每个文字对象自成一块。
        const DocumentObjectModel loose = readPageObjectModel(twoLineParagraphPdf());
        QVERIFY2(loose.ok(), qPrintable(loose.error));
        QCOMPARE(loose.counts.textBlocks, 2);

        // 给了归属索引（同一个 MCID 属于同一块）之后，两行合成一块。
        PageTextBlockIndex index;
        index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, 0), 0);
        index.blockRoles.append(PageTextRole::Body);
        index.blockTexts.append(QStringLiteral("First line of the second line"));
        index.blockTags.append(QStringLiteral("P"));
        const DocumentObjectModel grouped = readPageObjectModel(twoLineParagraphPdf(), index);
        QVERIFY2(grouped.ok(), qPrintable(grouped.error));
        QCOMPARE(grouped.counts.textBlocks, 1);
    }

    void theHeaderIsCountedAsAHeaderWhenTheIndexSaysSo() {
        PageTextBlockIndex index;
        index.mcidToBlock.insert(PageTextBlockIndex::mcidKey(0, 0), 0);
        index.blockRoles.append(PageTextRole::Header);
        index.blockTexts.append(QStringLiteral("Running head"));
        index.blockTags.append(QStringLiteral("Head"));
        const DocumentObjectModel model = readPageObjectModel(samplePdf(), index);
        QVERIFY2(model.ok(), qPrintable(model.error));
        QCOMPARE(model.counts.headers, 1);
        QCOMPARE(model.counts.textBlocks, 1);
    }

    // ---- 稳定身份 ----

    void theObjectIdIsTheSameOnEveryRead() {
        const DocumentObjectModel first = readPageObjectModel(samplePdf());
        const DocumentObjectModel second = readPageObjectModel(samplePdf());
        QVERIFY2(first.ok(), qPrintable(first.error));
        QVERIFY2(second.ok(), qPrintable(second.error));
        QCOMPARE(first.objects.size(), second.objects.size());
        for (int index = 0; index < first.objects.size(); ++index) {
            QCOMPARE(first.objects.at(index).id, second.objects.at(index).id);
            QVERIFY(!first.objects.at(index).id.isEmpty());
        }
    }

    void theObjectIdDoesNotDependOnTheObjectIndex() {
        const DocumentObjectModel plain = readPageObjectModel(samplePdf());
        const DocumentObjectModel extra = readPageObjectModel(samplePdfWithExtraImage());
        QVERIFY2(plain.ok(), qPrintable(plain.error));
        QVERIFY2(extra.ok(), qPrintable(extra.error));
        // 多了一张图之后，原来的那些对象的身份必须不变。
        for (const auto& object : plain.objects) {
            bool found = false;
            for (const auto& other : extra.objects) {
                if (other.id != object.id) continue;
                found = true;
                QCOMPARE(other.kind, object.kind);
                QCOMPARE(other.text, object.text);
            }
            QVERIFY2(found, qPrintable(object.id));
        }
    }

    void theIdsOfDifferentObjectsAreDifferent() {
        const DocumentObjectModel model = readPageObjectModel(samplePdf());
        QVERIFY2(model.ok(), qPrintable(model.error));
        for (int left = 0; left < model.objects.size(); ++left) {
            for (int right = left + 1; right < model.objects.size(); ++right) {
                QVERIFY2(model.objects.at(left).id != model.objects.at(right).id,
                         "两个不同的对象不能有同一个身份");
            }
        }
    }

    // ---- 差异清单 ----

    void theMissingListNamesWhatIsMissing() {
        aipdf::pdf::PageObjectCounts source;
        source.textBlocks = 3;
        source.images = 2;
        source.tables = 1;
        aipdf::pdf::PageObjectCounts output;
        output.textBlocks = 1;
        output.images = 2;
        output.tables = 0;
        const auto missing = source.missingComparedWith(output);
        // 少了的：文字块 2 个、表格 1 个。图片一个没少，不算。
        QCOMPARE(missing.size(), 2);
        QCOMPARE(missing.at(0).first, PageObjectKind::Text);
        QCOMPARE(missing.at(0).second, 2);
        QCOMPARE(missing.at(1).first, PageObjectKind::Table);
        QCOMPARE(missing.at(1).second, 1);
    }
};

void PageObjectModelTests::initTestCase() {}

QTEST_MAIN(PageObjectModelTests)
#include "PageObjectModelTests.moc"
