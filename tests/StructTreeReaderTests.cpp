#include "pdf/compliance/StructTreeReader.h"

#include <QElapsedTimer>
#include <QTest>

using aipdf::pdf::readStructTree;
using aipdf::pdf::StructTreeReport;
using aipdf::pdf::StructElement;
using aipdf::pdf::MarkedContentText;

namespace {

// 造一份 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了「找不到的引用」，
// qpdf 会去重建 xref，行为就不确定了（同一个夹具两次读出来的树都可能不一样）。
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

// 一页普通的 PDF（没有标签树）。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject("BT /F1 12 Tf 40 250 Td (Plain) Tj ET")));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    return buildPdf(objects);
}

// 一份「别人写的」标签树：/K 用数组、嵌套两层、带 /Alt 与 /Lang，
// 元素的 /K 里既有子元素也有直接的 MCID 数字。
//
// 刻意与本仓库 pdf::writeTaggedStructure 的输出形状不同：
// 那边的树是 Document → P → Span，/K 里只有 MCID 数组；
// 这边的树混着数组、数字与字典，/Pg 也只出现在一部分元素上。
QByteArray taggedPdf() {
    const QByteArray pageOne =
        "BT /F1 12 Tf 40 250 Td (Not tagged) Tj ET\n"
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 220 Td (Second line) Tj ET\n"
        "EMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 12 Tf 40 190 Td (Third line) Tj ET\n"
        "EMC";
    const QByteArray pageTwo =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (Page two text) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /Lang (zh-CN) "
                                            "/DisplayDocTitle true /StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(pageOne)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 6 0 R /StructParents 1 >>")));
    objects.append(qMakePair(6, streamObject(pageTwo)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    // 根节点的 /K 是数组。
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [9 0 R] >>")));
    // 文档元素：/K 里混着子元素。
    objects.append(qMakePair(9, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                             "/K [10 0 R 12 0 R 13 0 R] /Lang (zh-CN) >>")));
    // 一级标题：/K 里是子元素。
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /H1 /P 9 0 R "
                                             "/T (First chapter) /K [11 0 R] >>")));
    // 段落：/K 直接是 MCID 数字，并且带 /Pg。
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /P /P 10 0 R "
                                             "/Pg 3 0 R /K 0 >>")));
    // 图片：/K 是数字，带 /Alt（中文用带 BOM 的 UTF-16BE 十六进制串写）。
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /Figure /P 9 0 R "
                                             "/Pg 3 0 R /Alt <FEFF4F60597D> /K 1 >>")));
    // 链接：带 /Lang 与 /ActualText，页码指向第二页。
    objects.append(qMakePair(13, QByteArray("<< /Type /StructElem /S /Link /P 9 0 R "
                                             "/Pg 5 0 R /Lang (en-US) /ActualText (Go to chapter two) "
                                             "/K 0 >>")));
    return buildPdf(objects);
}


// 一份带 /ToUnicode 的 PDF：字体的 0x41 映射到「你」。
//
// 回归用途：ToUnicode CMap 的切段长度曾经差一个字符（"beginbfchar" 是 11 个
// 字符，代码按 10 切），解析结果恒为空 —— 带 /ToUnicode 的字体会**静默**
// 退回 WinAnsi 解码，读出来的是 "A" 而不是「你」。
QByteArray toUnicodePdf() {
    const QByteArray cmap =
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CMapName /A-B-C def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n"
        "<00> <FF>\n"
        "endcodespacerange\n"
        "1 beginbfchar\n"
        "<41> <4F60>\n"
        "endbfchar\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\n"
        "end\n"
        "end";
    const QByteArray page =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 12 Tf 40 250 Td (A) Tj ET\n"
        "EMC";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R "
                                            "/StructTreeRoot 8 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(page)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                            "/ToUnicode 9 0 R >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /StructTreeRoot /K [10 0 R] >>")));
    objects.append(qMakePair(9, streamObject(cmap)));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructElem /S /Document /P 8 0 R "
                                            "/K [11 0 R] >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /P /P 10 0 R "
                                            "/Pg 3 0 R /K 0 >>")));
    return buildPdf(objects);
}

// 一份带环的标签树：两个元素互相指回对方。
QByteArray cyclicPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 10 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                                            "/Resources << /Font << /F1 7 0 R >> >> "
                                            "/Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject("BT /F1 12 Tf 40 250 Td (Cyclic) Tj ET")));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")));
    objects.append(qMakePair(10, QByteArray("<< /Type /StructTreeRoot /K [11 0 R] >>")));
    objects.append(qMakePair(11, QByteArray("<< /Type /StructElem /S /Document /P 10 0 R /K [12 0 R] >>")));
    objects.append(qMakePair(12, QByteArray("<< /Type /StructElem /S /P /P 11 0 R /K [11 0 R] >>")));
    return buildPdf(objects);
}

// 元素里按标签名找。
int indexOfTag(const StructTreeReport& report, const QString& tag) {
    for (int i = 0; i < report.elements.size(); ++i) {
        if (report.elements.at(i).tag == tag) return i;
    }
    return -1;
}

}  // namespace

class StructTreeReaderTests : public QObject {
    Q_OBJECT

private slots:
    // ---- 负例 ----

    // 回归：ToUnicode 必须真的被应用。
    //
    // 曾经的 bug：CMap 的切段长度差一个字符，映射表恒为空，
    // 于是带 /ToUnicode 的字体会静默退回 WinAnsi 解码 —— 读出来是 "A"，
    // 而文档里明明写的是「你」。这种失败没有任何报错，只能靠断言守住。
    void toUnicodeCMapIsActuallyApplied() {
        const StructTreeReport report = readStructTree(toUnicodePdf());
        QVERIFY2(report.ok(), qPrintable(report.error));

        QString allText;
        for (const StructElement& element : report.elements) {
            allText += element.text;
        }
        for (const MarkedContentText& marked : report.markedContent) {
            allText += marked.text;
        }
        const QString expected = QString(QChar(0x4F60));  // 「你」
        QVERIFY2(allText.contains(expected),
                 qPrintable(QStringLiteral("ToUnicode 没被应用，读到的文字：%1").arg(allText)));
    }

    void emptyInputFails() {
        const StructTreeReport report = readStructTree(QByteArray());
        QVERIFY(!report.ok());
        QVERIFY(report.error.contains(QStringLiteral("为空")));
    }

    void notAPdfFails() {
        const StructTreeReport report = readStructTree(QByteArray("not a pdf"));
        QVERIFY(!report.ok());
        QVERIFY(!report.error.isEmpty());
    }

    void missingStructTreeGivesAnEmptyResult() {
        const StructTreeReport report = readStructTree(plainPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        QVERIFY(!report.hasStructTree);
        QVERIFY(report.elements.isEmpty());
        QVERIFY2(!report.notes.isEmpty(), "必须说明为什么没有标签树");
        QVERIFY(report.notes.join(QLatin1Char('\n')).contains(QStringLiteral("没有标签树")));
    }

    void cyclicStructTreeIsReportedAndReturns() {
        QElapsedTimer timer;
        timer.start();
        const StructTreeReport report = readStructTree(cyclicPdf());
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(!report.ok(), "有环必须报错");
        QVERIFY2(report.error.contains(QStringLiteral("环")), qPrintable(report.error));
        // 关键：不能挂死。遍历有防环，秒级就该回来。
        QVERIFY2(elapsed < 5000, qPrintable(QStringLiteral("读标签树花了 %1 毫秒").arg(elapsed)));
    }

    void missingLanguageIsReportedHonestly() {
        const StructTreeReport report = readStructTree(plainPdf());
        QVERIFY(!report.hasDocumentLanguage);
        QVERIFY(report.documentLanguage.isEmpty());
        QVERIFY(report.notes.join(QLatin1Char('\n')).contains(QStringLiteral("语言")));
    }

    // ---- 文档级信息 ----

    void readsTheDocumentLanguage() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        QVERIFY(report.hasStructTree);
        QVERIFY(report.hasDocumentLanguage);
        QCOMPARE(report.documentLanguage, QStringLiteral("zh-CN"));
    }

    void readsTheDisplayDocTitleFlag() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY(report.hasDisplayDocTitle);
        QVERIFY(report.displayDocTitle);
    }

    // ---- 结构元素 ----

    void readsEveryElementOfTheTree() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        QCOMPARE(report.elements.size(), 5);
        QCOMPARE(report.elements.at(0).tag, QStringLiteral("Document"));
        QCOMPARE(report.elements.at(1).tag, QStringLiteral("H1"));
        QCOMPARE(report.elements.at(2).tag, QStringLiteral("P"));
        QCOMPARE(report.elements.at(3).tag, QStringLiteral("Figure"));
        QCOMPARE(report.elements.at(4).tag, QStringLiteral("Link"));
    }

    void keepsTheParentRelationAndTheDepth() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        QCOMPARE(report.elements.at(0).parentIndex, -1);
        QCOMPARE(report.elements.at(0).depth, 0);
        QCOMPARE(report.elements.at(1).parentIndex, 0);
        QCOMPARE(report.elements.at(1).depth, 1);
        QCOMPARE(report.elements.at(2).parentIndex, 1);
        QCOMPARE(report.elements.at(2).depth, 2);
        QCOMPARE(report.elements.at(3).parentIndex, 0);
        QCOMPARE(report.elements.at(3).depth, 1);
    }

    void readsTheAltText() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        const int figure = indexOfTag(report, QStringLiteral("Figure"));
        QVERIFY(figure >= 0);
        // /Alt 用 UTF-16BE 十六进制串写的中文，读回来要是「你好」。
        QCOMPARE(report.elements.at(figure).alt, QStringLiteral("你好"));
        // 别的元素没有 /Alt。
        QCOMPARE(report.elements.at(0).alt, QString());
    }

    void readsTheElementLanguageAndActualText() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        const int link = indexOfTag(report, QStringLiteral("Link"));
        QVERIFY(link >= 0);
        QCOMPARE(report.elements.at(link).language, QStringLiteral("en-US"));
        QCOMPARE(report.elements.at(link).actualText, QStringLiteral("Go to chapter two"));
        const int document = indexOfTag(report, QStringLiteral("Document"));
        QVERIFY(document >= 0);
        QCOMPARE(report.elements.at(document).language, QStringLiteral("zh-CN"));
    }

    void readsTheElementTitle() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        const int heading = indexOfTag(report, QStringLiteral("H1"));
        QVERIFY(heading >= 0);
        QCOMPARE(report.elements.at(heading).title, QStringLiteral("First chapter"));
    }

    void readsThePageOfEachElement() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        const int paragraph = indexOfTag(report, QStringLiteral("P"));
        QVERIFY(paragraph >= 0);
        QCOMPARE(report.elements.at(paragraph).pageIndex, 0);
        const int link = indexOfTag(report, QStringLiteral("Link"));
        QVERIFY(link >= 0);
        QCOMPARE(report.elements.at(link).pageIndex, 1);
    }

    void readsTheMcidsOfAnElement() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        const int paragraph = indexOfTag(report, QStringLiteral("P"));
        QVERIFY(paragraph >= 0);
        QCOMPARE(report.elements.at(paragraph).mcids, QVector<int>({0}));
        const int figure = indexOfTag(report, QStringLiteral("Figure"));
        QVERIFY(figure >= 0);
        QCOMPARE(report.elements.at(figure).mcids, QVector<int>({1}));
    }

    void readsTheTextBehindAnMcid() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        const int paragraph = indexOfTag(report, QStringLiteral("P"));
        QVERIFY(paragraph >= 0);
        QCOMPARE(report.elements.at(paragraph).text, QStringLiteral("Second line"));
        const int figure = indexOfTag(report, QStringLiteral("Figure"));
        QVERIFY(figure >= 0);
        QCOMPARE(report.elements.at(figure).text, QStringLiteral("Third line"));
        const int link = indexOfTag(report, QStringLiteral("Link"));
        QVERIFY(link >= 0);
        QCOMPARE(report.elements.at(link).text, QStringLiteral("Page two text"));
    }

    void listsTheMarkedContentOfEveryPage() {
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        QCOMPARE(report.markedContent.size(), 3);
        QCOMPARE(report.markedContent.at(0).pageIndex, 0);
        QCOMPARE(report.markedContent.at(0).mcid, 0);
        QCOMPARE(report.markedContent.at(0).text, QStringLiteral("Second line"));
        QCOMPARE(report.markedContent.at(2).pageIndex, 1);
        QCOMPARE(report.markedContent.at(2).text, QStringLiteral("Page two text"));
    }

    void readsAStructTreeWrittenBySomebodyElse() {
        // 这份树的结构与本仓库写出来的完全不同（数组、混合的 /K、两层嵌套）。
        const StructTreeReport report = readStructTree(taggedPdf());
        QVERIFY2(report.ok(), qPrintable(report.error));
        QVERIFY(report.hasStructTree);
        QVERIFY(report.elements.size() >= 5);
        QVERIFY(report.elements.at(2).depth == 2);
    }
};

QTEST_GUILESS_MAIN(StructTreeReaderTests)
#include "StructTreeReaderTests.moc"
