#include "app/dialogs/SemanticOrderDialog.h"
#include "pdf/compliance/RoleInference.h"
#include "pdf/compliance/SemanticReadingOrder.h"
#include "pdf/compliance/SemanticTagWriter.h"
#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QTest>

#include <utility>

using aipdf::pdf::SemanticOrderEntry;
using aipdf::pdf::SemanticOrderPage;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QByteArray readFixture(const QString& name) {
    QFile file(fixturePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// 把对象列表拼成一份带正确 xref 的 PDF（照 SemanticTagWriterTests 的做法）。
QByteArray buildPdf(const QVector<QPair<int, QByteArray>>& objects, int rootId) {
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
               .arg(rootId)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一个流对象（prefix 是字典里 /Length 之前的内容）。
QByteArray streamObject(const QByteArray& prefix, const QByteArray& data) {
    QByteArray out = "<<";
    out += prefix;
    if (!out.endsWith(' ')) out += ' ';
    out += "/Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
    return out;
}

// 一份两页的夹具：每页一个标题 + 一段正文。
//
// 为什么自己造，而不用夹具目录里的多页文档：角色推断把**相邻两行**合成一段，
// 而它的「相邻」不看页边界——上一页最后一行与下一页第一行左边缘相同时，
// 正文会跨页并成一段，结构树里一个元素就指向了两页的 MCID，
// 写标签树时直接失败（共用写手按页给 MCID，跨页的块对不上）。
// 这里让两页的左边缘不同，段落就不会跨页。
QByteArray twoPagePdf() {
    const QByteArray pageOne(
        "BT /F1 20 Tf 72 700 Td (Chapter One) Tj ET\n"
        "BT /F1 11 Tf 72 670 Td (First body line.) Tj ET\n"
        "BT /F1 11 Tf 72 655 Td (Second body line.) Tj ET\n");
    const QByteArray pageTwo(
        "BT /F1 20 Tf 200 700 Td (Chapter Two) Tj ET\n"
        "BT /F1 11 Tf 200 670 Td (Third body line.) Tj ET\n");

    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), pageOne)));
    objects.append(qMakePair(5, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>")));
    objects.append(qMakePair(6, streamObject(QByteArray(), pageTwo)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                           "/FontDescriptor 8 0 R >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 "
                                           "/FontFile 9 0 R >>")));
    objects.append(qMakePair(9, streamObject(QByteArray(), QByteArray(64, '\0'))));
    return buildPdf(objects, 1);
}

// 给一份 PDF 打上语义标签树（照 SemanticTagWriterTests 的做法：
// 扫内容流 → 按行切块 → 推断角色 → 用共用的写手写出语义树）。
QByteArray withSemanticTree(const QByteArray& bytes, QString* error) {
    aipdf::pdf::ScannedDocument scan;
    if (!aipdf::pdf::scanDocumentContent(bytes, &scan, error)) {
        return {};
    }
    QVector<aipdf::pdf::SemanticBlock> blocks;
    for (int page = 0; page < scan.pages.size(); ++page) {
        for (const aipdf::pdf::ScannedTextLine& line : scan.pages.at(page).lines) {
            aipdf::pdf::SemanticBlock block;
            block.pageIndex = page;
            block.rect = line.bounds;
            block.text = line.text.trimmed();
            block.fontSize = line.fontSize;
            block.runIndices = line.runIndices;
            // 一行里的每个文本运行当成一列。
            //
            // 为什么自己分列：真实界面里列边界来自表格结构识别，
            // 这里按运行的位置粗分，好让夹具能造出一张带 TH / TD 的表，
            // 验「整张表是一个单元，格与行跟着它走」。
            if (line.runIndices.size() >= 2) {
                for (int runIndex : line.runIndices) {
                    const aipdf::pdf::ScannedTextRun& run = scan.pages.at(page).runs.at(runIndex);
                    block.columnStarts.append(run.origin.x());
                    block.columnTexts.append(run.text);
                    block.columnRunIndices.append(QVector<int>{runIndex});
                }
            }
            blocks.append(block);
        }
    }
    const aipdf::pdf::RoleInferenceResult inference = aipdf::pdf::inferSemanticRoles(blocks);
    aipdf::pdf::SemanticWriteOptions options;
    options.documentLanguage = QStringLiteral("zh-CN");
    options.documentTitle = QStringLiteral("Semantic order sample");
    options.displayDocTitle = true;
    const aipdf::pdf::SemanticWriteResult written =
        aipdf::pdf::writeSemanticStructure(bytes, inference.blocks, inference.tree, options);
    if (!written.ok) {
        if (error) *error = written.error;
        return {};
    }
    return written.bytes;
}

// 读回的结构树 → 语义树模型（用来跑 semanticReadingOrder / semanticReadingText）。
aipdf::pdf::SemanticTree toSemanticTree(const aipdf::pdf::StructTreeReport& report) {
    aipdf::pdf::SemanticTree tree;
    for (const aipdf::pdf::StructElement& element : report.elements) {
        aipdf::pdf::SemanticNode node;
        node.role = aipdf::pdf::nameToRole(element.tag);
        node.text = element.text;
        node.pageIndex = element.pageIndex;
        node.mcids = element.mcids;
        node.title = element.title;
        node.alt = element.alt;
        const int index = aipdf::pdf::appendSemanticNode(&tree, node);
        if (element.parentIndex >= 0 && element.parentIndex < index) {
            aipdf::pdf::appendSemanticChild(&tree, element.parentIndex, index);
        }
    }
    if (!tree.nodes.isEmpty()) {
        tree.rootIndex = 0;
    }
    return tree;
}

// 语义树按阅读顺序念出来的每一段文字。
QStringList readingPieces(const aipdf::pdf::SemanticTree& tree) {
    QStringList pieces;
    for (int index : aipdf::pdf::semanticReadingOrder(tree)) {
        pieces.append(tree.nodes.at(index).text);
    }
    return pieces;
}

}  // namespace

class SemanticReadingOrderTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void readsUnitsGroupedByPage();
    void readWithoutStructTreeExplainsWhy();
    void writeWithoutStructTreeFails();
    void reorderKeepsRolesAndTexts();
    void writtenDocumentStillOpens();
    void structTreeStaysConsistent();
    void moveFirstToLastKeepsOtherPages();
    void tableKeepsItsInnerStructure();
    void mismatchedOrderIsRejected();
    void multiPageDocumentCanBeTaggedAndReordered();
    void dialogMovesEntryUpAndDown();
};

void SemanticReadingOrderTests::initTestCase() {
    if (!QFile::exists(fixturePath(QStringLiteral("normal.pdf")))) {
        QSKIP("缺少 normal.pdf 夹具");
    }
}

void SemanticReadingOrderTests::readsUnitsGroupedByPage() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    const QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(pages.size(), 2);
    QCOMPARE(pages.at(0).pageIndex, 0);
    QCOMPARE(pages.at(1).pageIndex, 1);

    // 每页：标题 + 一个段落（两行正文合成一段）。
    QCOMPARE(pages.at(0).entries.size(), 2);
    QCOMPARE(pages.at(0).entries.at(0).role, QStringLiteral("H1"));
    QCOMPARE(pages.at(0).entries.at(0).text, QStringLiteral("Chapter One"));
    QCOMPARE(pages.at(0).entries.at(1).role, QStringLiteral("P"));
    QVERIFY(pages.at(0).entries.at(1).text.contains(QStringLiteral("First body line.")));
    QVERIFY(pages.at(0).entries.at(1).text.contains(QStringLiteral("Second body line.")));

    QCOMPARE(pages.at(1).entries.size(), 2);
    QCOMPARE(pages.at(1).entries.at(0).role, QStringLiteral("H1"));
    QCOMPARE(pages.at(1).entries.at(0).text, QStringLiteral("Chapter Two"));
    QCOMPARE(pages.at(1).entries.at(1).role, QStringLiteral("P"));
    QVERIFY(pages.at(1).entries.at(1).text.contains(QStringLiteral("Third body line.")));

    // 每个元素都要能指回结构树里的下标（写回时靠它对上号）。
    QSet<int> nodeIndexes;
    for (const SemanticOrderPage& page : pages) {
        for (const SemanticOrderEntry& entry : page.entries) {
            QVERIFY(entry.nodeIndex >= 0);
            QVERIFY(!entry.role.isEmpty());
            // 身份要唯一：文字会重复，靠文字认元素会把两个同名元素搞混。
            QVERIFY(!nodeIndexes.contains(entry.nodeIndex));
            nodeIndexes.insert(entry.nodeIndex);
        }
    }
}

void SemanticReadingOrderTests::readWithoutStructTreeExplainsWhy() {
    REQUIRE_QPDF();
    const QByteArray plain = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!plain.isEmpty());

    QString error;
    const QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(plain, &error);
    // 没有语义树就返回空，并且把原因写清楚——不静默退化成行级。
    QVERIFY(pages.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("语义标签树")), qPrintable(error));
}

void SemanticReadingOrderTests::writeWithoutStructTreeFails() {
    REQUIRE_QPDF();
    const QByteArray plain = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!plain.isEmpty());

    QString error;
    const QByteArray written =
        aipdf::pdf::writeSemanticReadingOrder(plain, QVector<SemanticOrderPage>(), &error);
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("语义标签树")), qPrintable(error));
}

void SemanticReadingOrderTests::reorderKeepsRolesAndTexts() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(pages.at(0).entries.size(), 2);

    // 记住写之前的角色与文字，写回来之后逐条比对。
    const QString roleBefore = pages.at(0).entries.at(0).role;
    const QString textBefore = pages.at(0).entries.at(0).text;
    const QString roleAfter = pages.at(0).entries.at(1).role;
    const QString textAfter = pages.at(0).entries.at(1).text;

    // 把第一个元素挪到最后。
    SemanticOrderPage first = pages.at(0);
    first.entries.swapItemsAt(0, 1);
    QVector<SemanticOrderPage> order = pages;
    order[0] = first;

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, order, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderPage> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.size(), pages.size());
    QCOMPARE(reread.at(0).entries.size(), 2);
    // 顺序变了。
    QCOMPARE(reread.at(0).entries.at(0).role, roleAfter);
    QCOMPARE(reread.at(0).entries.at(0).text, textAfter);
    QCOMPARE(reread.at(0).entries.at(1).role, roleBefore);
    QCOMPARE(reread.at(0).entries.at(1).text, textBefore);
    // 第 2 页没动。
    QCOMPARE(reread.at(1).entries.size(), pages.at(1).entries.size());
    QCOMPARE(reread.at(1).entries.at(0).role, pages.at(1).entries.at(0).role);
    QCOMPARE(reread.at(1).entries.at(0).text, pages.at(1).entries.at(0).text);
}

void SemanticReadingOrderTests::writtenDocumentStillOpens() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    SemanticOrderPage first = pages.at(0);
    first.entries.swapItemsAt(0, 1);
    QVector<SemanticOrderPage> order = pages;
    order[0] = first;

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, order, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(written);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 2);
}

void SemanticReadingOrderTests::structTreeStaysConsistent() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    SemanticOrderPage first = pages.at(0);
    first.entries.swapItemsAt(0, 1);
    QVector<SemanticOrderPage> order = pages;
    order[0] = first;

    const aipdf::pdf::StructTreeReport before = aipdf::pdf::readStructTree(tagged);
    QVERIFY(before.hasStructTree);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, order, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const aipdf::pdf::StructTreeReport after = aipdf::pdf::readStructTree(written);
    QVERIFY2(after.error.isEmpty(), qPrintable(after.error));
    QVERIFY(after.hasStructTree);
    // 元素数量不变。
    QCOMPARE(after.elements.size(), before.elements.size());

    // 文字集合不变（只是顺序变了）。
    const aipdf::pdf::SemanticTree beforeTree = toSemanticTree(before);
    const aipdf::pdf::SemanticTree afterTree = toSemanticTree(after);
    QStringList beforePieces = readingPieces(beforeTree);
    QStringList afterPieces = readingPieces(afterTree);
    beforePieces.sort();
    afterPieces.sort();
    QCOMPARE(afterPieces, beforePieces);
    QVERIFY(!aipdf::pdf::semanticReadingText(afterTree).isEmpty());

    // 每个元素指向的 MCID 数量不变（文字没有搬家）。
    // 比的是「角色 + MCID 个数」的多重集：顺序变了，按下标逐个比会自己骗自己。
    QStringList beforeShape;
    QStringList afterShape;
    for (const aipdf::pdf::StructElement& element : before.elements) {
        beforeShape.append(element.tag + QStringLiteral(":%1").arg(element.mcids.size()));
    }
    for (const aipdf::pdf::StructElement& element : after.elements) {
        afterShape.append(element.tag + QStringLiteral(":%1").arg(element.mcids.size()));
    }
    beforeShape.sort();
    afterShape.sort();
    QCOMPARE(afterShape, beforeShape);
}

void SemanticReadingOrderTests::moveFirstToLastKeepsOtherPages() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 把第 1 页的第一个元素挪到最后。
    SemanticOrderPage moved = pages.at(0);
    const SemanticOrderEntry first = moved.entries.takeFirst();
    moved.entries.append(first);
    QVector<SemanticOrderPage> order = pages;
    order[0] = moved;

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, order, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderPage> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.at(0).entries.size(), pages.at(0).entries.size());
    QCOMPARE(reread.at(0).entries.at(0).role, pages.at(0).entries.at(1).role);
    QCOMPARE(reread.at(0).entries.at(0).text, pages.at(0).entries.at(1).text);
    QCOMPARE(reread.at(0).entries.at(1).role, pages.at(0).entries.at(0).role);
    QCOMPARE(reread.at(0).entries.at(1).text, pages.at(0).entries.at(0).text);
    // 别的页一点没动。
    QCOMPARE(reread.at(1).entries.size(), pages.at(1).entries.size());
    QCOMPARE(reread.at(1).entries.at(0).text, pages.at(1).entries.at(0).text);
    QCOMPARE(reread.at(1).entries.at(0).role, pages.at(1).entries.at(0).role);
    QCOMPARE(reread.at(1).entries.at(1).text, pages.at(1).entries.at(1).text);
}

void SemanticReadingOrderTests::tableKeepsItsInnerStructure() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(readFixture(QStringLiteral("table.pdf")), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    const QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).entries.size(), 2);
    // 标题的字号只比正文大一点，按相对字号算出来是 H2。
    QCOMPARE(pages.at(0).entries.at(0).role, QStringLiteral("H2"));
    QCOMPARE(pages.at(0).entries.at(1).role, QStringLiteral("Table"));

    // 表格整张是一个单元：里面的行与格跟着走，不能散。
    const aipdf::pdf::StructTreeReport before = aipdf::pdf::readStructTree(tagged);
    QVERIFY(before.hasStructTree);
    int cellsBefore = 0;
    for (const aipdf::pdf::StructElement& element : before.elements) {
        if (element.tag == QLatin1String("TD") || element.tag == QLatin1String("TH")) ++cellsBefore;
    }
    QVERIFY(cellsBefore > 0);

    SemanticOrderPage swapped = pages.at(0);
    swapped.entries.swapItemsAt(0, 1);
    QVector<SemanticOrderPage> order = pages;
    order[0] = swapped;

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, order, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderPage> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.at(0).entries.size(), 2);
    QCOMPARE(reread.at(0).entries.at(0).role, QStringLiteral("Table"));
    QCOMPARE(reread.at(0).entries.at(1).role, QStringLiteral("H2"));

    const aipdf::pdf::StructTreeReport after = aipdf::pdf::readStructTree(written);
    int cellsAfter = 0;
    for (const aipdf::pdf::StructElement& element : after.elements) {
        if (element.tag == QLatin1String("TD") || element.tag == QLatin1String("TH")) ++cellsAfter;
    }
    QCOMPARE(cellsAfter, cellsBefore);
}

void SemanticReadingOrderTests::mismatchedOrderIsRejected() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 少给一个元素：必须失败，不能写出一份丢元素的树。
    QVector<SemanticOrderPage> broken = pages;
    broken[0].entries.removeLast();
    error.clear();
    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, broken, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("数量对不上")), qPrintable(error));

    // 重复一个元素：同样要失败。
    QVector<SemanticOrderPage> duplicated = pages;
    duplicated[0].entries[1] = duplicated[0].entries[0];
    error.clear();
    QVERIFY(aipdf::pdf::writeSemanticReadingOrder(tagged, duplicated, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void SemanticReadingOrderTests::multiPageDocumentCanBeTaggedAndReordered() {
    // 多页文档要能自动打标签，也要能再写回语义顺序。
    //
    // 这条用例钉住一个真缺陷：段落元素挂在**一页**的 Part 之下，
    // 而角色推断在合并段落时没检查页码 —— 两页的行并成一个元素后，
    // 写回时那些行无处安放（报「第 N 页有 M 处文字不归属任何语义元素」）。
    REQUIRE_QPDF();
    const QByteArray source = readFixture(QStringLiteral("navigation.pdf"));
    QVERIFY2(!source.isEmpty(), "缺少 navigation.pdf 夹具");

    QString error;
    const QByteArray tagged = withSemanticTree(source, &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderPage> pages =
        aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(!pages.isEmpty(), qPrintable(error));
    QCOMPARE(pages.size(), 3);

    // 顺序不变也要写得进去（这一条正是跨页段落缺陷会踩到的地方）。
    const QByteArray rewritten =
        aipdf::pdf::writeSemanticReadingOrder(tagged, pages, &error);
    QVERIFY2(!rewritten.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderPage> after =
        aipdf::pdf::readSemanticReadingOrder(rewritten, &error);
    QVERIFY(!after.isEmpty());
    QCOMPARE(after.size(), pages.size());
}

void SemanticReadingOrderTests::dialogMovesEntryUpAndDown() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    const QVector<SemanticOrderPage> pages = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 界面测试不许抢用户桌面：窗口不落到屏幕上。
    aipdf::app::SemanticOrderDialog dialog(pages);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    QCOMPARE(dialog.objectName(), QStringLiteral("semanticOrderDialog"));

    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("semanticOrder.list"));
    QVERIFY(list != nullptr);
    auto* moveUp = dialog.findChild<QPushButton*>(QStringLiteral("semanticOrder.moveUp"));
    auto* moveDown = dialog.findChild<QPushButton*>(QStringLiteral("semanticOrder.moveDown"));
    auto* acceptButton = dialog.findChild<QPushButton*>(QStringLiteral("semanticOrder.accept"));
    QVERIFY(moveUp != nullptr);
    QVERIFY(moveDown != nullptr);
    QVERIFY(acceptButton != nullptr);

    // 列表里：第 1 页的标题 + 两个元素，第 2 页的标题 + 两个元素。
    QCOMPARE(list->count(), 6);

    // 选中第 1 页的第二个元素，往上挪。
    list->setCurrentRow(2);
    moveUp->click();
    const QVector<SemanticOrderPage> moved = dialog.order();
    QCOMPARE(moved.at(0).entries.size(), 2);
    QCOMPARE(moved.at(0).entries.at(0).role, pages.at(0).entries.at(1).role);
    QCOMPARE(moved.at(0).entries.at(0).text, pages.at(0).entries.at(1).text);
    QCOMPARE(moved.at(0).entries.at(1).role, pages.at(0).entries.at(0).role);

    // 再往下挪，顺序复原。
    moveDown->click();
    const QVector<SemanticOrderPage> restored = dialog.order();
    QCOMPARE(restored.at(0).entries.at(0).role, pages.at(0).entries.at(0).role);
    QCOMPARE(restored.at(0).entries.at(0).text, pages.at(0).entries.at(0).text);
    QCOMPARE(restored.at(0).entries.at(1).role, pages.at(0).entries.at(1).role);
    QCOMPARE(restored.at(0).entries.at(1).text, pages.at(0).entries.at(1).text);

    // 元素个数没变，确认对话框不会拦下（拦下会弹提示框）。
    QCOMPARE(restored.at(0).entries.size(), pages.at(0).entries.size());
    QCOMPARE(restored.at(1).entries.size(), pages.at(1).entries.size());
    acceptButton->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
}

QTEST_MAIN(SemanticReadingOrderTests)
#include "SemanticReadingOrderTests.moc"
