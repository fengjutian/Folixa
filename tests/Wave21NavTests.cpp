// 2026-11-19 wave21：自动创建书签（读标签树）+ 交叉引用自动识别的专项用例。
//
// 夹具说明：
//   ① 带结构标签的 PDF（H1 / H2 / H3 层级，其中一个标题的文字挂在后代上）；
//   ② 没有结构标签的 PDF；
//   ③ 正文里带交叉引用的 PDF（中文 + 英文）。
//
// 三个夹具都是自己造的：夹具的结构必须"说得清"，
// 用现成的文件就只能证明"能读"，证明不了"读对了哪一段"。
#include "doc/model/OutlineGenerator.h"

#include "pdf/compliance/StructTreeReader.h"
#include "pdf/kernel/Navigation.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/ops/CrossReference.h"
#include "pdf/ops/OutlineWriter.h"

#include <QHash>
#include <QTest>

using aipdf::doc::DocumentChunk;
using aipdf::pdf::CrossReference;
using aipdf::pdf::CrossReferenceCandidate;
using aipdf::pdf::CrossReferenceConfidence;
using aipdf::pdf::CrossReferenceLinkResult;
using aipdf::pdf::CrossReferenceLinkSpec;
using aipdf::pdf::CrossReferenceMatch;
using aipdf::pdf::CrossReferencePage;
using aipdf::pdf::CrossReferenceScanResult;
using aipdf::pdf::OutlineItem;
using aipdf::pdf::OutlineTitleSource;

namespace {

// ---------------------------------------------------------------------------
// 造 PDF 的零件
// ---------------------------------------------------------------------------

// 造一份经典 xref 表的 PDF。
//
// xref 表必须覆盖到最大的对象号：表短了的话，后面几个对象就成了"找不到的引用"，
// qpdf 会去重建 xref，行为就不确定了（同一个夹具两次读出来的树都可能不一样）。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects) {
    int maxId = 0;
    for (const auto& object : objects) {
        maxId = qMax(maxId, object.first);
    }
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

// 内容流对象。
QByteArray streamObject(const QByteArray& content) {
    return QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
        .arg(content.size())
        .arg(QString::fromLatin1(content))
        .toLatin1();
}

// 一个 Type0 / Identity-H 字体 + ToUnicode CMap。
//
// 为什么这么造：夹具里的文字要能被 PDFium 与标签树读取器都还原出来，
// 而标准 14 号字体里没有中文字形。Identity-H 让每个字符占一个 2 字节 CID，
// ToUnicode 把 CID 映射回 Unicode——不内嵌字体文件也够用了
// （文本提取只依赖 ToUnicode）。
struct CjkFont {
    int nextCid = 1;
    QString bfChars;

    // 返回这段文字的十六进制码串。
    QByteArray encode(const QString& text) {
        QByteArray codes;
        for (int index = 0; index < text.size(); ++index) {
            const QChar ch = text.at(index);
            QString utf16;
            if (ch.isHighSurrogate() && index + 1 < text.size() &&
                text.at(index + 1).isLowSurrogate()) {
                utf16 = QString(ch) + text.at(index + 1);
                ++index;
            } else {
                utf16 = QString(ch);
            }
            const int cid = nextCid++;
            codes += QStringLiteral("%1").arg(cid, 4, 16, QLatin1Char('0')).toUpper().toLatin1();
            QString target;
            for (const QChar unit : utf16) {
                target += QStringLiteral("%1").arg(unit.unicode(), 4, 16, QLatin1Char('0')).toUpper();
            }
            bfChars += QStringLiteral("<%1> <%2>\n")
                           .arg(QStringLiteral("%1").arg(cid, 4, 16, QLatin1Char('0')).toUpper())
                           .arg(target);
        }
        return codes;
    }

    QByteArray cmap() const {
        const int count = static_cast<int>(bfChars.count(QLatin1Char('\n')));
        return QStringLiteral("/CIDInit /ProcSet findresource begin\n"
                              "12 dict begin\n"
                              "begincmap\n"
                              "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
                              "/CMapName /Adobe-Identity-UCS def\n"
                              "/CMapType 2 def\n"
                              "1 begincodespacerange\n"
                              "<0000> <FFFF>\n"
                              "endcodespacerange\n"
                              "%1 beginbfchar\n"
                              "%2"
                              "endbfchar\n"
                              "endcmap\n"
                              "CMapName currentdict /CMap defineresource pop\n"
                              "end\n"
                              "end\n")
            .arg(count)
            .arg(bfChars)
            .toUtf8();
    }
};

// 字体相关的四个对象（Type0、CIDFont、ToUnicode、FontDescriptor）。
//
// toUnicodeObjectId 是 ToUnicode 流的对象号——夹具里对象号不能重。
QVector<QPair<int, QByteArray>> fontObjects(int fontObjectId, int cidFontObjectId,
                                           int toUnicodeObjectId, int descriptorObjectId,
                                           const CjkFont& font) {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(
        fontObjectId,
        QByteArray("<< /Type /Font /Subtype /Type0 /BaseFont /STSong /Encoding /Identity-H "
                   "/DescendantFonts [") +
            QByteArray::number(cidFontObjectId) + " 0 R] /ToUnicode " +
            QByteArray::number(toUnicodeObjectId) + " 0 R >>"));
    objects.append(qMakePair(
        cidFontObjectId,
        QByteArray("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /STSong /CIDSystemInfo "
                   "<< /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor ") +
            QByteArray::number(descriptorObjectId) + " 0 R /DW 1000 >>"));
    objects.append(qMakePair(toUnicodeObjectId, streamObject(font.cmap())));
    objects.append(qMakePair(
        descriptorObjectId,
        QByteArray("<< /Type /FontDescriptor /FontName /STSong /Flags 4 /FontBBox [0 0 1000 1000] "
                   "/ItalicAngle 0 /Ascent 880 /Descent -120 /CapHeight 880 /StemV 80 >>")));
    return objects;
}

// ---------------------------------------------------------------------------
// 夹具
// ---------------------------------------------------------------------------

// 夹具①：带结构标签的 PDF。
//
// 标签树的形状刻意做得"不整齐"：
//   * 第一章的文字直接挂在 H1 元素上（/K 里既有 MCID 又有子元素）；
//   * 「第三章 结论」**没有**自己的文字，文字挂在它下面的 Span 上；
//   * 「2.1.1 小节」是 H3，却直接挂在 H1 下面（层级跳级）；
//   * 正文段落是 P，不该被当成标题。
QByteArray taggedPdf() {
    CjkFont font;
    const QByteArray h1 = font.encode(QStringLiteral("第一章 概述"));
    const QByteArray h2 = font.encode(QStringLiteral("1.1 背景"));
    const QByteArray body = font.encode(QStringLiteral("正文第一段。"));
    const QByteArray h1Second = font.encode(QStringLiteral("第二章 方法"));
    const QByteArray h3 = font.encode(QStringLiteral("2.1.1 小节"));
    const QByteArray spanText = font.encode(QStringLiteral("第三章 结论"));

    const QByteArray contentOne =
        "/Tag << /MCID 0 >> BDC\n"
        "BT /F1 24 Tf 40 250 Td <" + h1 + "> Tj ET\nEMC\n"
        "/Tag << /MCID 1 >> BDC\n"
        "BT /F1 16 Tf 40 220 Td <" + h2 + "> Tj ET\nEMC\n"
        "/Tag << /MCID 2 >> BDC\n"
        "BT /F1 12 Tf 40 190 Td <" + body + "> Tj ET\nEMC\n";
    const QByteArray contentTwo =
        "/Tag << /MCID 3 >> BDC\n"
        "BT /F1 24 Tf 40 250 Td <" + h1Second + "> Tj ET\nEMC\n"
        "/Tag << /MCID 4 >> BDC\n"
        "BT /F1 16 Tf 40 220 Td <" + h3 + "> Tj ET\nEMC\n"
        "/Tag << /MCID 5 >> BDC\n"
        "BT /F1 16 Tf 40 190 Td <" + spanText + "> Tj ET\nEMC\n";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(
        1, QByteArray("<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 11 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(
        3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R /StructParents 0 >>")));
    objects.append(qMakePair(4, streamObject(contentOne)));
    objects.append(qMakePair(
        5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R /StructParents 1 >>")));
    objects.append(qMakePair(6, streamObject(contentTwo)));
    objects += fontObjects(7, 8, 9, 10, font);
    objects.append(qMakePair(11, QByteArray("<< /Type /StructTreeRoot /K [12 0 R] >>")));
    objects.append(qMakePair(
        12, QByteArray("<< /Type /StructElem /S /Document /P 11 0 R "
                       "/K [13 0 R 15 0 R 16 0 R 18 0 R] >>")));
    // H1：/K 里既有自己的 MCID，也有子元素。
    objects.append(qMakePair(
        13, QByteArray("<< /Type /StructElem /S /H1 /P 12 0 R /Pg 3 0 R /K [0 14 0 R] >>")));
    objects.append(qMakePair(
        14, QByteArray("<< /Type /StructElem /S /H2 /P 13 0 R /Pg 3 0 R /K 1 >>")));
    objects.append(qMakePair(
        15, QByteArray("<< /Type /StructElem /S /P /P 12 0 R /Pg 3 0 R /K 2 >>")));
    // H1 下面直接挂 H3：层级跳级。
    objects.append(qMakePair(
        16, QByteArray("<< /Type /StructElem /S /H1 /P 12 0 R /Pg 5 0 R /K [3 17 0 R] >>")));
    objects.append(qMakePair(
        17, QByteArray("<< /Type /StructElem /S /H3 /P 16 0 R /Pg 5 0 R /K 4 >>")));
    // H1 自己没有文字，文字挂在 Span 上。
    objects.append(qMakePair(
        18, QByteArray("<< /Type /StructElem /S /H1 /P 12 0 R /Pg 5 0 R /K [19 0 R] >>")));
    objects.append(qMakePair(
        19, QByteArray("<< /Type /StructElem /S /Span /P 18 0 R /Pg 5 0 R /K 5 >>")));
    return buildPdf(objects);
}

// 夹具②：没有结构标签的 PDF。
QByteArray untaggedPdf() {
    CjkFont font;
    const QByteArray first = font.encode(QStringLiteral("第一章 概述"));
    const QByteArray body = font.encode(QStringLiteral("正文第一段。"));
    const QByteArray second = font.encode(QStringLiteral("第二章 方法"));

    const QByteArray contentOne =
        "BT /F1 24 Tf 40 250 Td <" + first + "> Tj ET\n"
        "BT /F1 12 Tf 40 190 Td <" + body + "> Tj ET\n";
    const QByteArray contentTwo = "BT /F1 24 Tf 40 250 Td <" + second + "> Tj ET\n";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(
        3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject(contentOne)));
    objects.append(qMakePair(
        5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>")));
    objects.append(qMakePair(6, streamObject(contentTwo)));
    objects += fontObjects(7, 8, 9, 10, font);
    return buildPdf(objects);
}

// 夹具③：正文里带交叉引用的 PDF（三页）。
//
// 每处引用单独一段文字：这样 PDFium 抽出来的文字里，引用不会被换行切开。
QByteArray referencePdf() {
    CjkFont font;
    const QByteArray pageOne = font.encode(QStringLiteral("见第 2 页。"));
    const QByteArray pageOneBoth = font.encode(QStringLiteral("见第 1 页和第 2 页。"));
    const QByteArray pageOneEnglish = font.encode(QStringLiteral("see page 2"));
    const QByteArray pageTwo = font.encode(QStringLiteral("详见第 3 页。"));
    const QByteArray pageTwoSection = font.encode(QStringLiteral("参见 3.2 节。"));
    const QByteArray pageTwoChapter = font.encode(QStringLiteral("详见第 2 章。"));
    const QByteArray pageThree = font.encode(QStringLiteral("见第 99 页。"));
    const QByteArray pageThreeEnglish = font.encode(QStringLiteral("on page 3"));

    const QByteArray contentOne =
        "BT /F1 12 Tf 40 260 Td <" + pageOne + "> Tj ET\n"
        "BT /F1 12 Tf 40 230 Td <" + pageOneBoth + "> Tj ET\n"
        "BT /F1 12 Tf 40 200 Td <" + pageOneEnglish + "> Tj ET\n";
    const QByteArray contentTwo =
        "BT /F1 12 Tf 40 260 Td <" + pageTwo + "> Tj ET\n"
        "BT /F1 12 Tf 40 230 Td <" + pageTwoSection + "> Tj ET\n"
        "BT /F1 12 Tf 40 200 Td <" + pageTwoChapter + "> Tj ET\n";
    const QByteArray contentThree =
        "BT /F1 12 Tf 40 260 Td <" + pageThree + "> Tj ET\n"
        "BT /F1 12 Tf 40 230 Td <" + pageThreeEnglish + "> Tj ET\n";

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R 6 0 R] /Count 3 >>")));
    objects.append(qMakePair(
        3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject(contentOne)));
    objects.append(qMakePair(
        5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 8 0 R >>")));
    objects.append(qMakePair(
        6, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300] "
                      "/Resources << /Font << /F1 7 0 R >> >> /Contents 9 0 R >>")));
    objects += fontObjects(7, 10, 11, 12, font);
    objects.append(qMakePair(8, streamObject(contentTwo)));
    objects.append(qMakePair(9, streamObject(contentThree)));
    return buildPdf(objects);
}

// 字高估算那条路的结果：块是按"字高比正文大"判出来的。
//
// 刻意做成**平的一层**：字高只能看出"这行比正文大"，
// 看不出 H1 与 H2 的区别——这正是标签树那条路要解决的问题。
QVector<DocumentChunk> heuristicChunks() {
    QVector<DocumentChunk> chunks;
    const QStringList headings = {
        QStringLiteral("第一章 概述"), QStringLiteral("1.1 背景"), QStringLiteral("第二章 方法"),
        QStringLiteral("2.1.1 小节"), QStringLiteral("第三章 结论"),
    };
    int pageIndex = 0;
    for (int index = 0; index < headings.size(); ++index) {
        DocumentChunk chunk;
        chunk.pageIndex = pageIndex;
        chunk.order = index;
        chunk.text = headings.at(index);
        chunk.headingLevel = 1;
        chunks.append(chunk);
        if (index == 2) {
            pageIndex = 1;
        }
    }
    return chunks;
}

// 一棵给交叉引用用的大纲（章节标题里带着编号）。
std::vector<OutlineItem> referenceOutline() {
    std::vector<OutlineItem> outline;
    OutlineItem chapterOne;
    chapterOne.title = QStringLiteral("第 1 章 引言");
    chapterOne.pageIndex = 0;
    OutlineItem chapterTwo;
    chapterTwo.title = QStringLiteral("第 2 章 方法");
    chapterTwo.pageIndex = 1;
    OutlineItem section;
    section.title = QStringLiteral("3.2 详细设计");
    section.pageIndex = 1;
    chapterTwo.children.push_back(section);
    outline.push_back(chapterOne);
    outline.push_back(chapterTwo);
    return outline;
}

// 找出扫描结果里文本最接近的那一条引用。
const CrossReference* findReference(const CrossReferenceScanResult& result, const QString& text) {
    for (const CrossReference& reference : result.references) {
        if (reference.text.contains(text)) {
            return &reference;
        }
    }
    return nullptr;
}

// 页面文字（PDFium 抽出来的）。
QString pageText(const aipdf::pdf::PdfDocument& document, int pageIndex) {
    const auto page = document.page(pageIndex);
    if (!page.has_value()) {
        return QString();
    }
    const auto text = page->extractText();
    if (!text.has_value()) {
        return QString();
    }
    return text->plainText();
}

// 往页列表里加一页文字。
void appendPage(QVector<CrossReferencePage>* pages, int pageIndex, const QString& text) {
    CrossReferencePage page;
    page.pageIndex = pageIndex;
    page.text = text;
    pages->append(page);
}

}  // namespace

class Wave21NavTests : public QObject {
    Q_OBJECT

private slots:
    // ---------- 读标签树 ----------
    void taggedDocumentUsesStructTreeHeadings();
    void taggedDocumentKeepsLevelJumpUnderNearestParent();
    void headingTextIsTakenFromDescendant();
    void paragraphIsNotAHeading();
    void structTreeOutlineWritesBackIntoDocument();

    // ---------- 没有标签树时的回退 ----------
    void untaggedDocumentFallsBackToFontHeuristic();
    void untaggedResultIsNotReportedAsStructTree();

    // ---------- 交叉引用 ----------
    void pageReferenceHitsPageWithHighConfidence();
    void englishPageReferenceHitsPage();
    void twoPagesInOneSentenceGiveTwoCandidates();
    void sectionReferenceHitsTitleWithMediumConfidence();
    void chapterReferenceHitsTitle();
    void pageReferenceOutOfRangeIsUnresolved();
    void sectionReferenceWithoutTitleIsUnresolved();
    void crossReferencesAreFoundInRealPdfText();
    void recognizedReferenceBecomesClickableLink();
};

// 有标签：大纲层级与标签层级一致，标题文字取自标签树。
void Wave21NavTests::taggedDocumentUsesStructTreeHeadings() {
    const QByteArray pdf = taggedPdf();
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(pdf);
    QVERIFY2(report.hasStructTree, "这份夹具应当有结构标签");

    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutlineFromStructTree(report);

    QVERIFY(result.fromStructTree());
    QVERIFY(!result.isEstimated());
    QVERIFY(result.note.isEmpty());
    QCOMPARE(result.outline.size(), size_t(3));

    const OutlineItem& first = result.outline.at(0);
    QCOMPARE(first.title, QStringLiteral("第一章 概述"));
    QCOMPARE(first.pageIndex, 0);
    QCOMPARE(first.children.size(), size_t(1));
    QCOMPARE(first.children.at(0).title, QStringLiteral("1.1 背景"));
    QCOMPARE(first.children.at(0).pageIndex, 0);

    const OutlineItem& second = result.outline.at(1);
    QCOMPARE(second.title, QStringLiteral("第二章 方法"));
    QCOMPARE(second.pageIndex, 1);
    QCOMPARE(second.children.size(), size_t(1));
    QCOMPARE(second.children.at(0).title, QStringLiteral("2.1.1 小节"));

    const OutlineItem& third = result.outline.at(2);
    QCOMPARE(third.title, QStringLiteral("第三章 结论"));
    QCOMPARE(third.pageIndex, 1);
}

// 层级跳级：H3 直接挂在 H1 下面时，挂在最近的可用父级下（不丢标题）。
void Wave21NavTests::taggedDocumentKeepsLevelJumpUnderNearestParent() {
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(taggedPdf());
    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutlineFromStructTree(report);

    QCOMPARE(result.outline.size(), size_t(3));
    const OutlineItem& second = result.outline.at(1);
    QCOMPARE(second.title, QStringLiteral("第二章 方法"));
    QCOMPARE(second.children.size(), size_t(1));
    QCOMPARE(second.children.at(0).title, QStringLiteral("2.1.1 小节"));
    QVERIFY(second.children.at(0).children.empty());
}

// 标题文字挂在后代上时也要取得到（「第三章 结论」自己一个字都没有）。
void Wave21NavTests::headingTextIsTakenFromDescendant() {
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(taggedPdf());
    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutlineFromStructTree(report);

    QCOMPARE(result.outline.size(), size_t(3));
    QCOMPARE(result.outline.at(2).title, QStringLiteral("第三章 结论"));
}

// 正文段落（P）不是标题，不该进大纲。
void Wave21NavTests::paragraphIsNotAHeading() {
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(taggedPdf());
    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutlineFromStructTree(report);

    for (const OutlineItem& item : result.outline) {
        QVERIFY2(item.title != QStringLiteral("正文第一段。"), "正文段落被当成了标题");
    }
}

// 标签树读出来的大纲要能写回文档。
void Wave21NavTests::structTreeOutlineWritesBackIntoDocument() {
    const QByteArray pdf = taggedPdf();
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(pdf);
    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutlineFromStructTree(report);
    QCOMPARE(result.outline.size(), size_t(3));

    const aipdf::pdf::OutlineWriteResult written =
        aipdf::pdf::rewriteOutline(pdf, result.outline);
    QVERIFY2(written.ok, qPrintable(written.error));

    const aipdf::pdf::PdfOpenResult reopened =
        aipdf::pdf::openDocumentFromBytes(written.bytes);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));

    const std::vector<OutlineItem> parsed = reopened.document.outline();
    QCOMPARE(parsed.size(), size_t(3));
    QCOMPARE(parsed.at(0).title, QStringLiteral("第一章 概述"));
    QCOMPARE(parsed.at(0).children.size(), size_t(1));
    QCOMPARE(parsed.at(0).children.at(0).title, QStringLiteral("1.1 背景"));
    QCOMPARE(parsed.at(1).title, QStringLiteral("第二章 方法"));
    QCOMPARE(parsed.at(1).children.at(0).title, QStringLiteral("2.1.1 小节"));
    QCOMPARE(parsed.at(2).title, QStringLiteral("第三章 结论"));
}

// 没有结构标签：走字高估算，并且**明确标记**这是估算的。
void Wave21NavTests::untaggedDocumentFallsBackToFontHeuristic() {
    const QByteArray pdf = untaggedPdf();
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(pdf);
    QVERIFY(!report.hasStructTree);

    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutline(report, heuristicChunks());

    QVERIFY(result.isEstimated());
    QCOMPARE(result.source, OutlineTitleSource::FontHeuristic);
    QVERIFY2(result.note.contains(QStringLiteral("没有结构标签")),
             qPrintable(QStringLiteral("说明里必须写明没有结构标签：%1").arg(result.note)));
    QVERIFY2(result.note.contains(QStringLiteral("字高")),
             qPrintable(QStringLiteral("说明里必须写明是按字高估算的：%1").arg(result.note)));
    // 估算结果确实出来了：五个标题，平的一层。
    QCOMPARE(result.outline.size(), size_t(5));
    QCOMPARE(result.outline.at(0).title, QStringLiteral("第一章 概述"));
    QCOMPARE(result.outline.at(0).children.size(), size_t(0));
}

// 估算的结果不能被说成"读标签树读出来的"。
void Wave21NavTests::untaggedResultIsNotReportedAsStructTree() {
    const aipdf::pdf::OutlineGenerationResult result =
        aipdf::pdf::generateOutline(untaggedPdf(), heuristicChunks());
    QVERIFY(!result.fromStructTree());
    QVERIFY(result.isEstimated());
    // 估算的层级是平的，标签树的层级是嵌套的——两者不能混为一谈。
    QCOMPARE(result.outline.size(), size_t(5));
    for (const OutlineItem& item : result.outline) {
        QVERIFY(item.children.empty());
    }
}

// 页码引用：文档里有这一页 → 高置信度命中。
void Wave21NavTests::pageReferenceHitsPageWithHighConfidence() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("见第 2 页。"));

    const CrossReferenceScanResult result = aipdf::pdf::scanCrossReferences(pages, {}, 3);
    QCOMPARE(result.references.size(), 1);
    const CrossReference& reference = result.references.at(0);
    QCOMPARE(reference.text, QStringLiteral("见第 2 页"));
    QCOMPARE(reference.sourcePageIndex, 0);
    QCOMPARE(reference.candidates.size(), 1);
    const CrossReferenceCandidate& candidate = reference.candidates.at(0);
    QCOMPARE(candidate.match, CrossReferenceMatch::PageNumber);
    QCOMPARE(candidate.confidence, CrossReferenceConfidence::High);
    QCOMPARE(candidate.referencedPageNumber, 2);
    QCOMPARE(candidate.pageIndex, 1);
    QCOMPARE(reference.confidence, CrossReferenceConfidence::High);
    QCOMPARE(result.resolvedCount, 1);
    QCOMPARE(result.unresolvedCount, 0);
}

// 英文写法："see page 2" / "on page 3" 一样认。
void Wave21NavTests::englishPageReferenceHitsPage() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("As shown on page 3, the result follows."));

    const CrossReferenceScanResult result = aipdf::pdf::scanCrossReferences(pages, {}, 5);
    QCOMPARE(result.references.size(), 1);
    const CrossReference& reference = result.references.at(0);
    QCOMPARE(reference.text, QStringLiteral("shown on page 3"));
    QCOMPARE(reference.candidates.at(0).confidence, CrossReferenceConfidence::High);
    QCOMPARE(reference.candidates.at(0).pageIndex, 2);
}

// 一句话里提到两页 → 两个候选，仍然是一处引用。
void Wave21NavTests::twoPagesInOneSentenceGiveTwoCandidates() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("见第 1 页和第 2 页。"));

    const CrossReferenceScanResult result = aipdf::pdf::scanCrossReferences(pages, {}, 3);
    QCOMPARE(result.references.size(), 1);
    const CrossReference& reference = result.references.at(0);
    QCOMPARE(reference.candidates.size(), 2);
    QCOMPARE(reference.candidates.at(0).pageIndex, 0);
    QCOMPARE(reference.candidates.at(1).pageIndex, 1);
    QCOMPARE(reference.candidates.at(0).confidence, CrossReferenceConfidence::High);
    QCOMPARE(reference.candidates.at(1).confidence, CrossReferenceConfidence::High);
}

// 章节号引用：书签里有同名标题 → 中置信度命中。
void Wave21NavTests::sectionReferenceHitsTitleWithMediumConfidence() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("参见 3.2 节。"));

    const CrossReferenceScanResult result =
        aipdf::pdf::scanCrossReferences(pages, referenceOutline(), 3);
    QCOMPARE(result.references.size(), 1);
    const CrossReference& reference = result.references.at(0);
    QCOMPARE(reference.text, QStringLiteral("参见 3.2 节"));
    QCOMPARE(reference.candidates.size(), 1);
    const CrossReferenceCandidate& candidate = reference.candidates.at(0);
    QCOMPARE(candidate.match, CrossReferenceMatch::Title);
    QCOMPARE(candidate.confidence, CrossReferenceConfidence::Medium);
    QCOMPARE(candidate.sectionNumber, QStringLiteral("3.2"));
    QCOMPARE(candidate.title, QStringLiteral("3.2 详细设计"));
    QCOMPARE(candidate.pageIndex, 1);
    QCOMPARE(reference.confidence, CrossReferenceConfidence::Medium);
}

// 章号引用：书签标题里带着「第 N 章」也能对上。
void Wave21NavTests::chapterReferenceHitsTitle() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("详见第 2 章。"));

    const CrossReferenceScanResult result =
        aipdf::pdf::scanCrossReferences(pages, referenceOutline(), 3);
    QCOMPARE(result.references.size(), 1);
    const CrossReferenceCandidate& candidate = result.references.at(0).candidates.at(0);
    QCOMPARE(candidate.match, CrossReferenceMatch::Title);
    QCOMPARE(candidate.confidence, CrossReferenceConfidence::Medium);
    QCOMPARE(candidate.title, QStringLiteral("第 2 章 方法"));
    QCOMPARE(candidate.pageIndex, 1);
}

// 页码超出范围：列为未解析，**不许瞎猜一个页码**。
void Wave21NavTests::pageReferenceOutOfRangeIsUnresolved() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("见第 99 页。"));

    const CrossReferenceScanResult result = aipdf::pdf::scanCrossReferences(pages, {}, 3);
    QCOMPARE(result.references.size(), 1);
    const CrossReference& reference = result.references.at(0);
    QCOMPARE(reference.candidates.size(), 1);
    const CrossReferenceCandidate& candidate = reference.candidates.at(0);
    QCOMPARE(candidate.match, CrossReferenceMatch::Unresolved);
    QCOMPARE(candidate.confidence, CrossReferenceConfidence::None);
    QCOMPARE(candidate.pageIndex, -1);
    QCOMPARE(reference.confidence, CrossReferenceConfidence::None);
    QVERIFY(!reference.isResolved());
    QCOMPARE(result.unresolvedCount, 1);
    QCOMPARE(result.resolvedCount, 0);
    QVERIFY(!candidate.note.isEmpty());
}

// 章节号在书签里找不到：未解析。
void Wave21NavTests::sectionReferenceWithoutTitleIsUnresolved() {
    QVector<CrossReferencePage> pages;
    appendPage(&pages, 0, QStringLiteral("参见 9.9 节。"));

    const CrossReferenceScanResult result =
        aipdf::pdf::scanCrossReferences(pages, referenceOutline(), 3);
    QCOMPARE(result.references.size(), 1);
    const CrossReferenceCandidate& candidate = result.references.at(0).candidates.at(0);
    QCOMPARE(candidate.match, CrossReferenceMatch::Unresolved);
    QCOMPARE(candidate.confidence, CrossReferenceConfidence::None);
    QCOMPARE(candidate.pageIndex, -1);
}

// 端到端：从 PDF 里抽出文字再扫描。
void Wave21NavTests::crossReferencesAreFoundInRealPdfText() {
    const aipdf::pdf::PdfOpenResult opened =
        aipdf::pdf::openDocumentFromBytes(referencePdf());
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 3);

    QVector<CrossReferencePage> pages;
    for (int index = 0; index < opened.document.pageCount(); ++index) {
        appendPage(&pages, index, pageText(opened.document, index));
    }
    // 夹具的文字确实被抽出来了（否则后面的断言就没意义）。
    QVERIFY2(pages.at(0).text.contains(QStringLiteral("见第 2 页")),
             qPrintable(QStringLiteral("第一页文字：%1").arg(pages.at(0).text)));
    QVERIFY2(pages.at(2).text.contains(QStringLiteral("见第 99 页")),
             qPrintable(QStringLiteral("第三页文字：%1").arg(pages.at(2).text)));

    const CrossReferenceScanResult result =
        aipdf::pdf::scanCrossReferences(pages, referenceOutline(), opened.document.pageCount());

    const CrossReference* pageHit = findReference(result, QStringLiteral("见第 2 页"));
    QVERIFY(pageHit != nullptr);
    QCOMPARE(pageHit->sourcePageIndex, 0);
    QCOMPARE(pageHit->candidates.at(0).confidence, CrossReferenceConfidence::High);
    QCOMPARE(pageHit->candidates.at(0).pageIndex, 1);

    const CrossReference* englishHit = findReference(result, QStringLiteral("see page 2"));
    QVERIFY(englishHit != nullptr);
    QCOMPARE(englishHit->candidates.at(0).confidence, CrossReferenceConfidence::High);
    QCOMPARE(englishHit->candidates.at(0).pageIndex, 1);

    const CrossReference* bothPages = findReference(result, QStringLiteral("见第 1 页和第 2 页"));
    QVERIFY(bothPages != nullptr);
    QCOMPARE(bothPages->candidates.size(), 2);

    const CrossReference* sectionHit = findReference(result, QStringLiteral("参见 3.2 节"));
    QVERIFY(sectionHit != nullptr);
    QCOMPARE(sectionHit->candidates.at(0).match, CrossReferenceMatch::Title);
    QCOMPARE(sectionHit->candidates.at(0).confidence, CrossReferenceConfidence::Medium);
    QCOMPARE(sectionHit->candidates.at(0).title, QStringLiteral("3.2 详细设计"));

    const CrossReference* chapterHit = findReference(result, QStringLiteral("详见第 2 章"));
    QVERIFY(chapterHit != nullptr);
    QCOMPARE(chapterHit->candidates.at(0).confidence, CrossReferenceConfidence::Medium);

    const CrossReference* unresolved = findReference(result, QStringLiteral("见第 99 页"));
    QVERIFY(unresolved != nullptr);
    QCOMPARE(unresolved->candidates.at(0).confidence, CrossReferenceConfidence::None);
    QCOMPARE(unresolved->candidates.at(0).pageIndex, -1);
}

// 识别出来的引用要能一键变成可点击的跳转。
void Wave21NavTests::recognizedReferenceBecomesClickableLink() {
    const QByteArray pdf = referencePdf();
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(pdf);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));

    QVector<CrossReferencePage> pages;
    for (int index = 0; index < opened.document.pageCount(); ++index) {
        appendPage(&pages, index, pageText(opened.document, index));
    }
    const CrossReferenceScanResult result =
        aipdf::pdf::scanCrossReferences(pages, referenceOutline(), opened.document.pageCount());
    const CrossReference* reference = findReference(result, QStringLiteral("见第 2 页"));
    QVERIFY(reference != nullptr);

    const CrossReferenceLinkSpec spec =
        aipdf::pdf::crossReferenceLinkSpec(*reference, QRectF(40.0, 250.0, 120.0, 16.0));
    QCOMPARE(spec.sourcePageIndex, 0);
    QCOMPARE(spec.targetPageIndex, 1);

    const CrossReferenceLinkResult written = aipdf::pdf::insertCrossReferenceLink(pdf, spec);
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY(!written.bytes.isEmpty());

    const aipdf::pdf::PdfOpenResult reopened =
        aipdf::pdf::openDocumentFromBytes(written.bytes);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    const auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    const QVector<aipdf::pdf::PageLink> links = page->links();
    QCOMPARE(links.size(), 1);
    QVERIFY(links.at(0).isInternal());
    QCOMPARE(links.at(0).pageIndex, 1);
}

QTEST_MAIN(Wave21NavTests)
#include "Wave21NavTests.moc"
