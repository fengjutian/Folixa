#include "pdf/DocumentMerger.h"

#include "pdf/PdfiumAdapter.h"

#include <QFile>
#include <QHash>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <optional>
#include <vector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::MergeInput;
using aipdf::pdf::MergeOptions;
using aipdf::pdf::MergeOutcome;
using aipdf::pdf::PdfOpenResult;
using aipdf::pdf::SourceOutlineEntry;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 读一份夹具文件。读不到就跳过这一条用例（夹具是构建时生成的）。
QByteArray fixture(const QString& name) {
    QFile file(QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    file.close();
    return bytes;
}

// 一页的纯文本（PDFium 读出来的）。
QString pageText(const aipdf::pdf::PdfDocument& document, int pageIndex) {
    const std::optional<aipdf::pdf::PdfPage> page = document.page(pageIndex);
    if (!page.has_value()) return {};
    const std::optional<aipdf::pdf::TextPage> textPage = page->extractText();
    if (!textPage.has_value()) return {};
    return textPage->plainText();
}

// 合并结果里某一页上的内部链接目标（页序号），升序。
QVector<int> internalLinkTargets(const aipdf::pdf::PdfDocument& document, int pageIndex) {
    QVector<int> targets;
    const std::optional<aipdf::pdf::PdfPage> page = document.page(pageIndex);
    if (!page.has_value()) return targets;
    for (const aipdf::pdf::PageLink& link : page->links()) {
        if (link.isInternal()) targets.append(link.pageIndex);
    }
    std::sort(targets.begin(), targets.end());
    return targets;
}

// 合并结果里的大纲（摊平后）：标题 + 指向的页号 + 层级。
struct FlatEntry {
    QString title;
    int pageIndex = -1;
    int depth = 0;
};

QVector<FlatEntry> mergedOutline(const aipdf::pdf::PdfDocument& document) {
    QVector<FlatEntry> entries;
    const std::vector<aipdf::pdf::OutlineEntry> flat =
        aipdf::pdf::flattenOutline(document.outline());
    for (const aipdf::pdf::OutlineEntry& entry : flat) {
        FlatEntry item;
        item.title = entry.title;
        item.pageIndex = entry.pageIndex;
        item.depth = entry.depth;
        entries.append(item);
    }
    return entries;
}

// 标题写成 UTF-16BE 的十六进制串（PDF 里放中文的常规写法）。
QByteArray titleString(const QString& title) {
    QByteArray hex("FEFF");
    for (QChar ch : title) {
        const ushort value = ch.unicode();
        hex += QByteArray::number((value >> 8) & 255, 16).rightJustified(2, '0');
        hex += QByteArray::number(value & 255, 16).rightJustified(2, '0');
    }
    return '<' + hex.toUpper() + '>';
}

// 造一份带书签与内部链接的小 PDF：第 1 页有一个指向第 2 页的链接，
// 大纲里有一个指向第 1 页的顶层条目与一个指向第 2 页的子条目。
//
// 为什么要自己造：夹具 navigation.pdf 的页数与目标固定，
// 但自造的文件能精确控制「第 2 页」这类断言，不受夹具变动影响。
QByteArray buildLinkedPdf(int pageCount, const QString& topTitle, const QString& childTitle) {
    QList<QPair<int, QByteArray>> objects;
    const auto add = [&objects](int id, const QByteArray& body) {
        objects.append(qMakePair(id, body));
    };

    // 对象号连续排布：xref 表按对象号顺序写，中间不能有洞。
    const int catalogId = 1;
    const int pagesId = 2;
    const int firstPageId = 3;
    const int annotId = firstPageId + pageCount * 2;
    const int fontId = annotId + 1;
    const int outlineRootId = fontId + 1;
    const int topId = outlineRootId + 1;
    const int childId = topId + 1;
    const int secondPageId = firstPageId + 2;  // 第 2 页（0 基下标 1）

    QByteArray kids;
    for (int i = 0; i < pageCount; ++i) {
        if (!kids.isEmpty()) kids += " ";
        kids += QByteArray::number(firstPageId + i * 2) + " 0 R";
    }
    add(catalogId, QByteArray("<< /Type /Catalog /Pages ") + QByteArray::number(pagesId) +
                        " 0 R /Outlines " + QByteArray::number(outlineRootId) + " 0 R >>");
    add(pagesId, QByteArray("<< /Type /Pages /Kids [") + kids + "] /Count " +
                    QByteArray::number(pageCount) + " >>");
    for (int i = 0; i < pageCount; ++i) {
        const int pageId = firstPageId + i * 2;
        const int contentId = pageId + 1;
        QByteArray page = "<< /Type /Page /Parent " + QByteArray::number(pagesId) +
                          " 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 " +
                          QByteArray::number(fontId) + " 0 R >> >> /Contents " +
                          QByteArray::number(contentId) + " 0 R";
        // 只有第 1 页挂一个指向第 2 页的链接批注。
        if (i == 0 && pageCount > 1) {
            page += " /Annots [" + QByteArray::number(annotId) + " 0 R]";
        }
        add(pageId, page + " >>");
        const QString text = QStringLiteral("BT /F1 18 Tf 40 200 Td (Page %1) Tj ET").arg(i + 1);
        add(contentId, QByteArray("<< /Length ") + QByteArray::number(text.size()) +
                           " >>\nstream\n" + text.toLatin1() + "\nendstream");
    }
    add(annotId, QByteArray("<< /Type /Annot /Subtype /Link /Rect [40 190 200 220] "
                            "/Border [0 0 0] /A << /S /GoTo /D [") +
                     QByteArray::number(secondPageId) + " 0 R /Fit] >> >>");
    add(fontId, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));
    // 大纲：顶层条目指向第 1 页，子条目指向第 2 页。
    add(outlineRootId, QByteArray("<< /Type /Outlines /First ") + QByteArray::number(topId) +
                           " 0 R /Last " + QByteArray::number(topId) + " 0 R /Count 1 >>");
    add(topId, QByteArray("<< /Title ") + titleString(topTitle) + " /Parent " +
                  QByteArray::number(outlineRootId) + " 0 R /First " + QByteArray::number(childId) +
                  " 0 R /Last " + QByteArray::number(childId) + " 0 R /Count 1 /Dest [" +
                  QByteArray::number(firstPageId) + " 0 R /Fit] >>");
    add(childId, QByteArray("<< /Title ") + titleString(childTitle) + " /Parent " +
                    QByteArray::number(topId) + " 0 R /A << /S /GoTo /D [" +
                    QByteArray::number(secondPageId) + " 0 R /Fit] >> >>");

    QByteArray out("%PDF-1.7\n");
    QHash<int, int> offsets;
    int maxId = 0;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        maxId = qMax(maxId, object.first);
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (offsets.contains(id)) {
            out += QStringLiteral("%1 00000 n \n")
                       .arg(offsets.value(id), 10, 10, QLatin1Char('0'))
                       .toLatin1();
        } else {
            out += QByteArray("0000000000 65535 f \n");
        }
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R >>\nstartxref\n%3\n%%EOF\n")
               .arg(count)
               .arg(catalogId)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

}  // namespace

class DocumentMergerTests : public QObject {
    Q_OBJECT

private slots:
    void mergesTwoDocuments();
    void mergedDocumentOpensInPdfium();
    void outlinesAreCarriedOver();
    void outlineTitlesCarrySourcePrefix();
    void outlineDestPointsToMergedPage();
    void internalLinksAreRedirected();
    void mergedDocumentPassesStructuralCheck();
    void mergedDocumentPassesQpdfCheck();
    void singleDocumentMergeKeepsOutline();
    void emptyInputFails();
    void brokenInputFails();
    void mergedPageIndexMapsSourcePages();
    void readOutlineEntriesReportsSourcePages();
};

// 1. 合并两份夹具：页数等于两份之和，页码偏移正确。
void DocumentMergerTests::mergesTwoDocuments() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("multipage.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("多页文档")});
    inputs.append({second, QStringLiteral("导航文档")});

    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));
    QCOMPARE(outcome.pageCount, 15);  // 12 + 3
    QCOMPARE(outcome.pageOffsets.size(), 2);
    QCOMPARE(outcome.pageOffsets.at(0), 0);
    QCOMPARE(outcome.pageOffsets.at(1), 12);  // 第二份从第 12 页开始
    QVERIFY(!outcome.bytes.isEmpty());
    QVERIFY(outcome.error.isEmpty());
}

// 2. 合并结果用 PDFium 能打开：页数对，首尾页的内容与源文件一致。
void DocumentMergerTests::mergedDocumentOpensInPdfium() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("multipage.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("A")});
    inputs.append({second, QStringLiteral("B")});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));

    const PdfOpenResult merged = aipdf::pdf::openDocumentFromBytes(outcome.bytes);
    QVERIFY2(merged.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(merged.errorMessage)));
    QCOMPARE(merged.document.pageCount(), 15);

    const PdfOpenResult sourceFirst = aipdf::pdf::openDocumentFromBytes(first);
    const PdfOpenResult sourceSecond = aipdf::pdf::openDocumentFromBytes(second);
    QVERIFY(sourceFirst.document.isValid());
    QVERIFY(sourceSecond.document.isValid());

    // 第一页与源文件第一页一致。
    QCOMPARE(pageText(merged.document, 0), pageText(sourceFirst.document, 0));
    // 最后一页与最后一份源文件的最后一页一致。
    QCOMPARE(pageText(merged.document, 14), pageText(sourceSecond.document, 2));
    QVERIFY(pageText(merged.document, 0).contains(QStringLiteral("Page 1")));
    QVERIFY(pageText(merged.document, 12).contains(QStringLiteral("Navigation demo")));
}

// 3. 大纲搬运：两份都有书签时，条目数是两份之和，层级照搬。
void DocumentMergerTests::outlinesAreCarriedOver() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("navigation.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("甲")});
    inputs.append({second, QStringLiteral("乙")});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));

    const PdfOpenResult merged = aipdf::pdf::openDocumentFromBytes(outcome.bytes);
    QVERIFY2(merged.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(merged.errorMessage)));

    // navigation.pdf 的大纲是 2 个顶层条目 + 2 个子条目，共 4 条。
    const QVector<FlatEntry> entries = mergedOutline(merged.document);
    QCOMPARE(entries.size(), 8);
    QVector<int> depths;
    for (const FlatEntry& entry : entries) depths.append(entry.depth);
    QCOMPARE(depths, QVector<int>({0, 1, 1, 0, 0, 1, 1, 0}));

    // 领域层自己的读法也要对得上（说明写进文件的结构与读出来的一致）。
    const QVector<SourceOutlineEntry> read =
        aipdf::pdf::readOutlineEntries(outcome.bytes, &error);
    QCOMPARE(read.size(), 8);
    QCOMPARE(read.at(4).level, 0);
    QCOMPARE(read.at(5).level, 1);
}

// 4. 大纲标题前缀：开启时带来源名，关闭时不带。
void DocumentMergerTests::outlineTitlesCarrySourcePrefix() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("navigation.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("甲文件")});
    inputs.append({second, QStringLiteral("乙文件")});

    MergeOptions withPrefix;
    MergeOutcome prefixed;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, withPrefix, &prefixed, &error),
             qPrintable(error));
    const PdfOpenResult prefixedDocument = aipdf::pdf::openDocumentFromBytes(prefixed.bytes);
    QVERIFY(prefixedDocument.document.isValid());
    const QVector<FlatEntry> prefixedEntries = mergedOutline(prefixedDocument.document);
    QCOMPARE(prefixedEntries.size(), 8);
    QVERIFY(prefixedEntries.at(0).title.contains(QStringLiteral("甲文件")));
    QVERIFY(prefixedEntries.at(4).title.contains(QStringLiteral("乙文件")));
    QVERIFY(prefixedEntries.at(0).title.contains(QStringLiteral("第一章")));

    MergeOptions withoutPrefix;
    withoutPrefix.outlinePrefix = false;
    MergeOutcome plain;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, withoutPrefix, &plain, &error),
             qPrintable(error));
    const PdfOpenResult plainDocument = aipdf::pdf::openDocumentFromBytes(plain.bytes);
    QVERIFY(plainDocument.document.isValid());
    const QVector<FlatEntry> plainEntries = mergedOutline(plainDocument.document);
    QCOMPARE(plainEntries.size(), 8);
    QVERIFY(!plainEntries.at(0).title.contains(QStringLiteral("甲文件")));
    QVERIFY(!plainEntries.at(4).title.contains(QStringLiteral("乙文件")));
    QCOMPARE(plainEntries.at(0).title, QStringLiteral("第一章 概述"));
}

// 5. 跨文件书签跳转：第二份文件的大纲项落到合并结果里的正确页。
void DocumentMergerTests::outlineDestPointsToMergedPage() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("multipage.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("A")});
    inputs.append({second, QStringLiteral("B")});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));

    const PdfOpenResult merged = aipdf::pdf::openDocumentFromBytes(outcome.bytes);
    QVERIFY2(merged.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(merged.errorMessage)));
    const QVector<FlatEntry> entries = mergedOutline(merged.document);
    QCOMPARE(entries.size(), 4);

    // navigation.pdf 的条目原本指向源页 0 / 1 / 2 / 2；
    // 它在合并结果里从第 12 页开始，所以应当指向 12 / 13 / 14 / 14。
    QCOMPARE(entries.at(0).pageIndex, 12);
    QCOMPARE(entries.at(1).pageIndex, 13);
    QCOMPARE(entries.at(2).pageIndex, 14);
    QCOMPARE(entries.at(3).pageIndex, 14);

    // 领域层读出来的是「这份文档里的页号」，也就是合并结果里的页号。
    const QVector<SourceOutlineEntry> read =
        aipdf::pdf::readOutlineEntries(outcome.bytes, &error);
    QCOMPARE(read.size(), 4);
    QCOMPARE(read.at(0).sourcePage, 12);
    QCOMPARE(read.at(3).sourcePage, 14);

    // 页号换算函数给出的答案必须与上面一致。
    QCOMPARE(aipdf::pdf::mergedPageIndex(outcome.pageOffsets, 1, 0), 12);
    QCOMPARE(aipdf::pdf::mergedPageIndex(outcome.pageOffsets, 1, 2), 14);
}

// 6. 内部链接重定向：源文件里指向第 2 页的链接，合并后指向合并结果里的那一页。
void DocumentMergerTests::internalLinksAreRedirected() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("multipage.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("A")});
    inputs.append({second, QStringLiteral("B")});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));

    const PdfOpenResult merged = aipdf::pdf::openDocumentFromBytes(outcome.bytes);
    QVERIFY2(merged.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(merged.errorMessage)));

    // navigation.pdf 第 1 页的两个内部链接原本指向源页 1 与源页 2，
    // 合并后它们在合并结果里是第 13 页与第 14 页。
    const QVector<int> targets = internalLinkTargets(merged.document, 12);
    QCOMPARE(targets, QVector<int>({13, 14}));

    // 自造的文件再验一遍：第 1 页的链接指向源第 2 页 → 合并后指向第 13 页。
    const QByteArray linked = buildLinkedPdf(2, QStringLiteral("顶层"), QStringLiteral("子层"));
    QVector<MergeInput> synthetic;
    synthetic.append({first, QStringLiteral("A")});
    synthetic.append({linked, QStringLiteral("B")});
    MergeOutcome syntheticOutcome;
    QVERIFY2(aipdf::pdf::mergeDocuments(synthetic, MergeOptions(), &syntheticOutcome, &error),
             qPrintable(error));
    const PdfOpenResult syntheticMerged =
        aipdf::pdf::openDocumentFromBytes(syntheticOutcome.bytes);
    QVERIFY(syntheticMerged.document.isValid());
    QCOMPARE(internalLinkTargets(syntheticMerged.document, 12), QVector<int>({13}));
    // 书签的目标页也要跟着走：顶层条目 → 12，子条目 → 13。
    const QVector<FlatEntry> syntheticEntries = mergedOutline(syntheticMerged.document);
    QCOMPARE(syntheticEntries.size(), 2);
    QCOMPARE(syntheticEntries.at(0).pageIndex, 12);
    QCOMPARE(syntheticEntries.at(1).pageIndex, 13);
}

// 7. 合并结果再被 qpdf 打开：结构没问题，页数与大纲条目数都对。
void DocumentMergerTests::mergedDocumentPassesStructuralCheck() {
    REQUIRE_QPDF();
    const QByteArray first = fixture(QStringLiteral("navigation.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("甲")});
    inputs.append({second, QStringLiteral("乙")});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));

    QPDF pdf;
    pdf.processMemoryFile("merged.pdf", outcome.bytes.constData(),
                           static_cast<size_t>(outcome.bytes.size()));
    QCOMPARE(static_cast<int>(pdf.getAllPages().size()), 6);
    const QPDFObjectHandle root = pdf.getRoot();
    QVERIFY(root.isDictionary());
    const QPDFObjectHandle outlines = root.getKey("/Outlines");
    QVERIFY(outlines.isDictionary());
    QCOMPARE(outlines.getKey("/Count").getIntValue(), 8LL);
}

// 8. qpdf --check：合并结果没有结构错误。
void DocumentMergerTests::mergedDocumentPassesQpdfCheck() {
    REQUIRE_QPDF();
    if (QString::fromUtf8(AIPDF_QPDF_CLI).isEmpty()) QSKIP("没有 qpdf 命令行，跳过");
    const QByteArray first = fixture(QStringLiteral("multipage.pdf"));
    const QByteArray second = fixture(QStringLiteral("navigation.pdf"));
    if (first.isEmpty() || second.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({first, QStringLiteral("A")});
    inputs.append({second, QStringLiteral("B")});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("merged.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(outcome.bytes);
    file.close();

    QProcess process;
    process.start(QString::fromUtf8(AIPDF_QPDF_CLI),
                  {QStringLiteral("--check"), path});
    QVERIFY(process.waitForFinished(60000));
    const QString output = QString::fromUtf8(process.readAllStandardOutput()) +
                           QString::fromUtf8(process.readAllStandardError());
    QCOMPARE(process.exitCode(), 0);
    QVERIFY2(!output.contains(QStringLiteral("WARNING")), qPrintable(output));
    QVERIFY2(!output.contains(QStringLiteral("ERROR")), qPrintable(output));
}

// 9. 只给一份文件：结果等价于原文件（页数、书签都在）。
void DocumentMergerTests::singleDocumentMergeKeepsOutline() {
    REQUIRE_QPDF();
    const QByteArray navigation = fixture(QStringLiteral("navigation.pdf"));
    if (navigation.isEmpty()) QSKIP("夹具不可用");

    QVector<MergeInput> inputs;
    inputs.append({navigation, QString()});
    MergeOutcome outcome;
    QString error;
    QVERIFY2(aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error),
             qPrintable(error));
    QCOMPARE(outcome.pageCount, 3);
    QCOMPARE(outcome.pageOffsets.size(), 1);
    QCOMPARE(outcome.pageOffsets.at(0), 0);

    const PdfOpenResult merged = aipdf::pdf::openDocumentFromBytes(outcome.bytes);
    QVERIFY2(merged.document.isValid(),
             qPrintable(QStringLiteral("PDFium 打不开：%1").arg(merged.errorMessage)));
    QCOMPARE(merged.document.pageCount(), 3);

    const QVector<FlatEntry> entries = mergedOutline(merged.document);
    QCOMPARE(entries.size(), 4);
    QCOMPARE(entries.at(0).title, QStringLiteral("第一章 概述"));
    QCOMPARE(entries.at(0).pageIndex, 0);
    QCOMPARE(entries.at(1).pageIndex, 1);
    QCOMPARE(entries.at(2).pageIndex, 2);
    QCOMPARE(entries.at(3).pageIndex, 2);
    // 源文件里的内部链接也还在，且指向的还是原来那一页。
    QCOMPARE(internalLinkTargets(merged.document, 0), QVector<int>({1, 2}));
}

// 10. 空输入 / 空字节：失败并给出原因，不崩、不产出半成品。
void DocumentMergerTests::emptyInputFails() {
    MergeOutcome outcome;
    QString error;
    QVERIFY(!aipdf::pdf::mergeDocuments(QVector<MergeInput>(), MergeOptions(), &outcome, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outcome.bytes.isEmpty());
    QCOMPARE(outcome.pageCount, 0);

    const QByteArray navigation = fixture(QStringLiteral("navigation.pdf"));
    if (navigation.isEmpty()) QSKIP("夹具不可用");
    QVector<MergeInput> inputs;
    inputs.append({navigation, QStringLiteral("A")});
    inputs.append({QByteArray(), QStringLiteral("B")});
    MergeOutcome secondOutcome;
    QString secondError;
    QVERIFY(!aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &secondOutcome, &secondError));
    QVERIFY(!secondError.isEmpty());
    QVERIFY(secondOutcome.bytes.isEmpty());
    QCOMPARE(secondOutcome.pageCount, 0);
}

// 11. 坏文件：整份合并失败，不产出半成品。
void DocumentMergerTests::brokenInputFails() {
    const QByteArray navigation = fixture(QStringLiteral("navigation.pdf"));
    if (navigation.isEmpty()) QSKIP("夹具不可用");
    QVector<MergeInput> inputs;
    inputs.append({navigation, QStringLiteral("A")});
    inputs.append({QByteArray("this is not a pdf at all"), QStringLiteral("B")});
    MergeOutcome outcome;
    QString error;
    QVERIFY(!aipdf::pdf::mergeDocuments(inputs, MergeOptions(), &outcome, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outcome.bytes.isEmpty());
    QCOMPARE(outcome.pageCount, 0);
}

// 12. 页号换算：源页号 → 合并结果里的页号，越界返回 -1。
void DocumentMergerTests::mergedPageIndexMapsSourcePages() {
    const QVector<int> offsets = {0, 12};
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 0, 0), 0);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 0, 11), 11);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 1, 0), 12);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 1, 2), 14);
    // 越过下一份文件的起点：这个页号不属于这份源文件。
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 0, 12), -1);
    // 文件序号越界、页号为负。
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 2, 0), -1);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, -1, 0), -1);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 0, -1), -1);
    QCOMPARE(aipdf::pdf::mergedPageIndex(QVector<int>(), 0, 0), -1);

    // 最后一份文件的区间没有下界：只知道页码偏移时判不出越界，
    // 知道总页数才判得出来。
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 1, 12), 24);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 1, 2, 15), 14);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 1, 3, 15), -1);
    QCOMPARE(aipdf::pdf::mergedPageIndex(offsets, 0, 12, 15), -1);
}

// 13. 读大纲：条目（标题 + 指向的源页号 + 层级）读得出来。
void DocumentMergerTests::readOutlineEntriesReportsSourcePages() {
    REQUIRE_QPDF();
    const QByteArray navigation = fixture(QStringLiteral("navigation.pdf"));
    if (navigation.isEmpty()) QSKIP("夹具不可用");

    QString error;
    const QVector<SourceOutlineEntry> entries =
        aipdf::pdf::readOutlineEntries(navigation, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), 4);
    QCOMPARE(entries.at(0).title, QStringLiteral("第一章 概述"));
    QCOMPARE(entries.at(0).level, 0);
    QCOMPARE(entries.at(0).sourcePage, 0);
    QCOMPARE(entries.at(1).title, QStringLiteral("第一节 背景"));
    QCOMPARE(entries.at(1).level, 1);
    QCOMPARE(entries.at(1).sourcePage, 1);
    QCOMPARE(entries.at(2).title, QStringLiteral("第二节 方法"));
    QCOMPARE(entries.at(2).level, 1);
    QCOMPARE(entries.at(2).sourcePage, 2);
    QCOMPARE(entries.at(3).title, QStringLiteral("第二章 结果"));
    QCOMPARE(entries.at(3).level, 0);
    QCOMPARE(entries.at(3).sourcePage, 2);

    // 没有书签的文件：空列表，不是错误。
    const QByteArray normal = fixture(QStringLiteral("normal.pdf"));
    if (!normal.isEmpty()) {
        const QVector<SourceOutlineEntry> none =
            aipdf::pdf::readOutlineEntries(normal, &error);
        QVERIFY(none.isEmpty());
    }
}

QTEST_GUILESS_MAIN(DocumentMergerTests)
#include "DocumentMergerTests.moc"
