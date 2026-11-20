#include "app/dialogs/SemanticOrderDialog.h"
#include "pdf/compliance/RoleInference.h"
#include "pdf/compliance/SemanticReadingOrder.h"
#include "pdf/compliance/SemanticTagWriter.h"
#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QTest>
#include <QTimer>

#include <utility>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::SemanticOrderEntry;
using aipdf::pdf::SemanticOrderSlot;

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

// 一页带图像的夹具：页面用 /Im1 画一张 2×2 的灰度图。
QByteArray imagePdf() {
    const QByteArray content(
        "BT /F1 20 Tf 72 700 Td (Picture page) Tj ET\n"
        "q 120 0 0 90 72 500 cm /Im1 Do Q\n");
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 7 0 R >> "
                                           "/XObject << /Im1 10 0 R >> >> /Contents 4 0 R >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), content)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                           "/FontDescriptor 8 0 R >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 "
                                           "/FontFile 9 0 R >>")));
    objects.append(qMakePair(9, streamObject(QByteArray(), QByteArray(64, '\0'))));
    objects.append(qMakePair(10, streamObject(QByteArray("/Type /XObject /Subtype /Image /Width 2 "
                                                          "/Height 2 /ColorSpace /DeviceGray "
                                                          "/BitsPerComponent 8"),
                                             QByteArray(4, '\x80'))));
    return buildPdf(objects, 1);
}

// 一页带表单域的夹具：页上有一个文本框 widget（对象号 11）。
QByteArray formPdf() {
    const QByteArray content("BT /F1 20 Tf 72 700 Td (Form page) Tj ET\n");
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R "
                                           "/AcroForm << /Fields [11 0 R] >> >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                                           "/Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R "
                                           "/Annots [11 0 R] >>")));
    objects.append(qMakePair(4, streamObject(QByteArray(), content)));
    objects.append(qMakePair(7, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                                           "/FontDescriptor 8 0 R >>")));
    objects.append(qMakePair(8, QByteArray("<< /Type /FontDescriptor /FontName /Helvetica /Flags 32 "
                                           "/FontFile 9 0 R >>")));
    objects.append(qMakePair(9, streamObject(QByteArray(), QByteArray(64, '\0'))));
    objects.append(qMakePair(11, QByteArray("<< /Type /Annot /Subtype /Widget /FT /Tx "
                                            "/T (firstName) /TU (First name) /Rect [72 600 300 620] "
                                            "/P 3 0 R /DA (/F1 10 Tf 0 g) >>")));
    return buildPdf(objects, 1);
}

// 表单域对应的语义块（内容是 widget 注解，不是文字）。
aipdf::pdf::SemanticBlock formFieldBlock() {
    aipdf::pdf::SemanticBlock block;
    block.pageIndex = 0;
    block.rect = QRectF(72, 600, 228, 20);
    block.formField = true;
    block.fieldName = QStringLiteral("firstName");
    block.fieldTooltip = QStringLiteral("First name");
    aipdf::pdf::ObjectRef reference;
    reference.pageIndex = 0;
    reference.objectId = 11;
    reference.generation = 0;
    block.widgetRefs.append(reference);
    return block;
}

// 给一份 PDF 打上语义标签树（照 SemanticTagWriterTests 的做法：
// 扫内容流 → 按行切块 → 推断角色 → 用共用的写手写出语义树）。
//
// includeImages：把页面上的图像也当成块（图会写成 /OBJR 引用）。
// extraBlocks：额外补进来的块（例如表单域）。
QByteArray withSemanticTree(const QByteArray& bytes, QString* error, bool includeImages = false,
                             const QVector<aipdf::pdf::SemanticBlock>& extraBlocks = {}) {
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
        if (!includeImages) continue;
        for (const aipdf::pdf::ScannedImage& image : scan.pages.at(page).images) {
            aipdf::pdf::SemanticBlock block;
            block.pageIndex = page;
            block.rect = image.bounds;
            block.image = true;
            block.imageObjectId = image.objectId;
            block.imageGeneration = image.generation;
            blocks.append(block);
        }
    }
    blocks.append(extraBlocks);
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

int countEntries(const QVector<SemanticOrderSlot>& positions) {
    int total = 0;
    for (const SemanticOrderSlot& slot : positions) total += slot.entries.size();
    return total;
}

// 槽位里有没有某个角色的元素。
bool hasRole(const QVector<SemanticOrderSlot>& positions, const QString& role) {
    for (const SemanticOrderSlot& slot : positions) {
        for (const SemanticOrderEntry& entry : slot.entries) {
            if (entry.role == role) return true;
        }
    }
    return false;
}

#ifdef AIPDF_WITH_QPDF
// 结构树里的「非文字内容」引用：图像（/OBJR）与表单域 widget。
struct StructReferences {
    int objectRefCount = 0;    // /OBJR 的个数
    int imageRefCount = 0;     // /OBJR 指向的是图像 XObject 的个数
    int widgetRefCount = 0;    // 结构元素引用了 widget 注解的个数
};

// /OBJR 指向的对象是不是一张图像。
//
// 图像 XObject 是**流**，不是字典：取它的 /Subtype 要走流字典。
bool isImageObject(const QPDFObjectHandle& object) {
    if (!object.isIndirect()) return false;
    if (object.isStreamOfType("/XObject", "/Image")) return true;
    // 图像 XObject 是**流**，不是字典：取它的 /Subtype 要走流字典。
    const QPDFObjectHandle target = object.isStream() ? object.getDict() : object;
    if (!target.isDictionary()) return false;
    const QPDFObjectHandle subtype = target.getKey("/Subtype");
    return subtype.isName() && subtype.getName() == "/Image";
}

void collectStructReferences(const QPDFObjectHandle& node, StructReferences* refs) {
    if (!node.isDictionary()) return;
    const QPDFObjectHandle kids = node.getKey("/K");
    if (kids.isDictionary()) {
        const QPDFObjectHandle type = kids.getKey("/Type");
        const QPDFObjectHandle subtype = kids.getKey("/Subtype");
        if (type.isName() && type.getName() == "/OBJR") {
            ++refs->objectRefCount;
            if (isImageObject(kids.getKey("/Obj"))) ++refs->imageRefCount;
            return;
        }
        if (subtype.isName() && subtype.getName() == "/Widget") {
            ++refs->widgetRefCount;
            return;
        }
        collectStructReferences(kids, refs);
        return;
    }
    if (!kids.isArray()) return;
    for (const QPDFObjectHandle& item : kids.getArrayAsVector()) {
        if (!item.isDictionary()) continue;
        const QPDFObjectHandle type = item.getKey("/Type");
        const QPDFObjectHandle subtype = item.getKey("/Subtype");
        if (type.isName() && type.getName() == "/OBJR") {
            ++refs->objectRefCount;
            if (isImageObject(item.getKey("/Obj"))) ++refs->imageRefCount;
            continue;
        }
        if (subtype.isName() && subtype.getName() == "/Widget") {
            ++refs->widgetRefCount;
            continue;
        }
        collectStructReferences(item, refs);
    }
}

StructReferences scanStructReferences(const QByteArray& bytes) {
    StructReferences refs;
    try {
        QPDF pdf;
        pdf.processMemoryFile("semantic-order-refs-test.pdf", bytes.constData(),
                              static_cast<size_t>(bytes.size()));
        const QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) return refs;
        const QPDFObjectHandle root = catalog.getKey("/StructTreeRoot");
        if (!root.isDictionary()) return refs;
        collectStructReferences(root, &refs);
    } catch (const std::exception&) {
        return refs;
    }
    return refs;
}
#endif  // AIPDF_WITH_QPDF

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
    void crossPageMoveKeepsContentOnItsPage();
    void crossPageMoveKeepsOtherElementsInPlace();
    void documentWithImageKeepsItsObjectReference();
    void documentWithFormFieldKeepsItsWidgetReference();
    void dialogMovesEntryToAnotherSlot();
    void dialogRejectsDuplicatedElement();
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
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(positions.size(), 2);
    QCOMPARE(positions.at(0).pageIndex, 0);
    QCOMPARE(positions.at(1).pageIndex, 1);

    // 每个槽位：标题 + 一个段落（两行正文合成一段）。
    QCOMPARE(positions.at(0).entries.size(), 2);
    QCOMPARE(positions.at(0).entries.at(0).role, QStringLiteral("H1"));
    QCOMPARE(positions.at(0).entries.at(0).text, QStringLiteral("Chapter One"));
    QCOMPARE(positions.at(0).entries.at(1).role, QStringLiteral("P"));
    QVERIFY(positions.at(0).entries.at(1).text.contains(QStringLiteral("First body line.")));
    QVERIFY(positions.at(0).entries.at(1).text.contains(QStringLiteral("Second body line.")));

    QCOMPARE(positions.at(1).entries.size(), 2);
    QCOMPARE(positions.at(1).entries.at(0).role, QStringLiteral("H1"));
    QCOMPARE(positions.at(1).entries.at(0).text, QStringLiteral("Chapter Two"));
    QCOMPARE(positions.at(1).entries.at(1).role, QStringLiteral("P"));
    QVERIFY(positions.at(1).entries.at(1).text.contains(QStringLiteral("Third body line.")));

    // 每个元素都要能指回结构树里的下标（写回时靠它对上号）。
    QSet<int> nodeIndexes;
    for (const SemanticOrderSlot& slot : positions) {
        for (const SemanticOrderEntry& entry : slot.entries) {
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
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(plain, &error);
    // 没有语义树就返回空，并且把原因写清楚——不静默退化成行级。
    QVERIFY(positions.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("语义标签树")), qPrintable(error));
}

void SemanticReadingOrderTests::writeWithoutStructTreeFails() {
    REQUIRE_QPDF();
    const QByteArray plain = readFixture(QStringLiteral("normal.pdf"));
    QVERIFY(!plain.isEmpty());

    QString error;
    const QByteArray written =
        aipdf::pdf::writeSemanticReadingOrder(plain, QVector<SemanticOrderSlot>(), &error);
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
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(positions.at(0).entries.size(), 2);

    // 记住写之前的角色与文字，写回来之后逐条比对。
    const QString roleBefore = positions.at(0).entries.at(0).role;
    const QString textBefore = positions.at(0).entries.at(0).text;
    const QString roleAfter = positions.at(0).entries.at(1).role;
    const QString textAfter = positions.at(0).entries.at(1).text;

    // 把第一个元素挪到最后。
    positions[0].entries.swapItemsAt(0, 1);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderSlot> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.size(), positions.size());
    QCOMPARE(reread.at(0).entries.size(), 2);
    // 顺序变了。
    QCOMPARE(reread.at(0).entries.at(0).role, roleAfter);
    QCOMPARE(reread.at(0).entries.at(0).text, textAfter);
    QCOMPARE(reread.at(0).entries.at(1).role, roleBefore);
    QCOMPARE(reread.at(0).entries.at(1).text, textBefore);
    // 第 2 页没动。
    QCOMPARE(reread.at(1).entries.size(), positions.at(1).entries.size());
    QCOMPARE(reread.at(1).entries.at(0).role, positions.at(1).entries.at(0).role);
    QCOMPARE(reread.at(1).entries.at(0).text, positions.at(1).entries.at(0).text);
}

void SemanticReadingOrderTests::writtenDocumentStillOpens() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    positions[0].entries.swapItemsAt(0, 1);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
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
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    positions[0].entries.swapItemsAt(0, 1);

    const aipdf::pdf::StructTreeReport before = aipdf::pdf::readStructTree(tagged);
    QVERIFY(before.hasStructTree);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
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
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 把第 1 页的第一个元素挪到最后。
    //
    // 记住**挪之前**的两个元素：挪之后的槽位已经被改过了，
    // 拿它当期望值就是自己骗自己。
    const SemanticOrderEntry originalFirst = positions.at(0).entries.at(0);
    const SemanticOrderEntry originalSecond = positions.at(0).entries.at(1);
    const SemanticOrderEntry first = positions[0].entries.takeFirst();
    positions[0].entries.append(first);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderSlot> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.at(0).entries.size(), positions.at(0).entries.size());
    QCOMPARE(reread.at(0).entries.at(0).role, originalSecond.role);
    QCOMPARE(reread.at(0).entries.at(0).text, originalSecond.text);
    QCOMPARE(reread.at(0).entries.at(1).role, originalFirst.role);
    QCOMPARE(reread.at(0).entries.at(1).text, originalFirst.text);
    // 别的页一点没动。
    QCOMPARE(reread.at(1).entries.size(), positions.at(1).entries.size());
    QCOMPARE(reread.at(1).entries.at(0).text, positions.at(1).entries.at(0).text);
    QCOMPARE(reread.at(1).entries.at(0).role, positions.at(1).entries.at(0).role);
    QCOMPARE(reread.at(1).entries.at(1).text, positions.at(1).entries.at(1).text);
}

void SemanticReadingOrderTests::tableKeepsItsInnerStructure() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(readFixture(QStringLiteral("table.pdf")), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(positions.size(), 1);
    QCOMPARE(positions.at(0).entries.size(), 2);
    // 标题的字号只比正文大一点，按相对字号算出来是 H2。
    QCOMPARE(positions.at(0).entries.at(0).role, QStringLiteral("H2"));
    QCOMPARE(positions.at(0).entries.at(1).role, QStringLiteral("Table"));

    // 表格整张是一个单元：里面的行与格跟着走，不能散。
    const aipdf::pdf::StructTreeReport before = aipdf::pdf::readStructTree(tagged);
    QVERIFY(before.hasStructTree);
    int cellsBefore = 0;
    for (const aipdf::pdf::StructElement& element : before.elements) {
        if (element.tag == QLatin1String("TD") || element.tag == QLatin1String("TH")) ++cellsBefore;
    }
    QVERIFY(cellsBefore > 0);

    positions[0].entries.swapItemsAt(0, 1);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderSlot> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
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
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 少给一个元素：必须失败，不能写出一份丢元素的树。
    QVector<SemanticOrderSlot> broken = positions;
    broken[0].entries.removeLast();
    error.clear();
    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, broken, &error);
    QVERIFY(written.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("数量对不上")), qPrintable(error));

    // 重复一个元素：同样要失败。
    QVector<SemanticOrderSlot> duplicated = positions;
    duplicated[0].entries[1] = duplicated[0].entries[0];
    error.clear();
    QVERIFY(aipdf::pdf::writeSemanticReadingOrder(tagged, duplicated, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 塞一个文档里根本没有的元素：也要失败。
    //
    // 为什么还要这条：跨页移动之后，「这个元素不属于这一页」是**合法的**，
    // 但「这个元素不属于这份文档」仍然是错的——两条要分清。
    QVector<SemanticOrderSlot> foreign = positions;
    SemanticOrderEntry stranger = foreign.at(0).entries.at(0);
    stranger.nodeIndex = 99999;
    foreign[0].entries.append(stranger);
    error.clear();
    QVERIFY(aipdf::pdf::writeSemanticReadingOrder(tagged, foreign, &error).isEmpty());
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

    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(!positions.isEmpty(), qPrintable(error));
    QCOMPARE(positions.size(), 3);

    // 顺序不变也要写得进去（这一条正是跨页段落缺陷会踩到的地方）。
    const QByteArray rewritten = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!rewritten.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderSlot> after = aipdf::pdf::readSemanticReadingOrder(rewritten, &error);
    QVERIFY(!after.isEmpty());
    QCOMPARE(after.size(), positions.size());
}

void SemanticReadingOrderTests::dialogMovesEntryUpAndDown() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // 界面测试不许抢用户桌面：窗口不落到屏幕上。
    aipdf::app::SemanticOrderDialog dialog(positions);
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
    const QVector<SemanticOrderSlot> moved = dialog.order();
    QCOMPARE(moved.at(0).entries.size(), 2);
    QCOMPARE(moved.at(0).entries.at(0).role, positions.at(0).entries.at(1).role);
    QCOMPARE(moved.at(0).entries.at(0).text, positions.at(0).entries.at(1).text);
    QCOMPARE(moved.at(0).entries.at(1).role, positions.at(0).entries.at(0).role);

    // 再往下挪，顺序复原。
    moveDown->click();
    const QVector<SemanticOrderSlot> restored = dialog.order();
    QCOMPARE(restored.at(0).entries.at(0).role, positions.at(0).entries.at(0).role);
    QCOMPARE(restored.at(0).entries.at(0).text, positions.at(0).entries.at(0).text);
    QCOMPARE(restored.at(0).entries.at(1).role, positions.at(0).entries.at(1).role);
    QCOMPARE(restored.at(0).entries.at(1).text, positions.at(0).entries.at(1).text);

    // 元素个数没变，确认对话框不会拦下（拦下会弹提示框）。
    QCOMPARE(restored.at(0).entries.size(), positions.at(0).entries.size());
    QCOMPARE(restored.at(1).entries.size(), positions.at(1).entries.size());
    acceptButton->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
}

void SemanticReadingOrderTests::crossPageMoveKeepsContentOnItsPage() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(positions.size(), 2);

    // 把第 1 页的第一个元素挪到第 2 页的槽位（放在最前面）。
    const SemanticOrderEntry moved = positions[0].entries.takeFirst();
    positions[1].entries.prepend(moved);
    QCOMPARE(countEntries(positions), 4);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    // 读回来：第 1 页的槽位少一个，第 2 页的槽位多一个。
    const QVector<SemanticOrderSlot> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.size(), 2);
    QCOMPARE(reread.at(0).entries.size(), 1);
    QCOMPARE(reread.at(1).entries.size(), 3);
    // 被挪走的那个排在第 2 页槽位的最前面。
    QCOMPARE(reread.at(1).entries.at(0).role, moved.role);
    QCOMPARE(reread.at(1).entries.at(0).text, moved.text);
    // 元素总数没变。
    QCOMPARE(countEntries(reread), 4);

    // 关键：内容还在原来的物理页上，MCID 也没变。
    const SemanticOrderEntry& after = reread.at(1).entries.at(0);
    QCOMPARE(after.pageIndex, moved.pageIndex);
    QCOMPARE(after.mcids, moved.mcids);
    QVERIFY(!after.mcids.isEmpty());
}

void SemanticReadingOrderTests::crossPageMoveKeepsOtherElementsInPlace() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(positions.size(), 2);

    // 记住每个元素原来的文字。
    QStringList before;
    for (const SemanticOrderSlot& slot : positions) {
        for (const SemanticOrderEntry& entry : slot.entries) before.append(entry.text);
    }

    // 把第 2 页的最后一个元素挪到第 1 页的槽位末尾。
    const SemanticOrderEntry moved = positions[1].entries.takeLast();
    positions[0].entries.append(moved);

    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

    const QVector<SemanticOrderSlot> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(countEntries(reread), before.size());

    // 第 1 页：原来那两个元素顺序不变，搬过来的那个排在最后。
    QCOMPARE(reread.at(0).entries.size(), 3);
    QCOMPARE(reread.at(0).entries.at(0).text, before.at(0));
    QCOMPARE(reread.at(0).entries.at(1).text, before.at(1));
    QCOMPARE(reread.at(0).entries.at(2).text, moved.text);
    // 第 2 页剩下的元素没动。
    QCOMPARE(reread.at(1).entries.size(), 1);
    QCOMPARE(reread.at(1).entries.at(0).text, before.at(2));
}

void SemanticReadingOrderTests::documentWithImageKeepsItsObjectReference() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(imagePdf(), &fixtureError, true);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    // 含图像的文档以前会被**整篇拒绝**（读结构树时遇到没有 /S 的 /OBJR 字典）。
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!positions.isEmpty());
    QVERIFY(hasRole(positions, QStringLiteral("Figure")));

#ifdef AIPDF_WITH_QPDF
    const StructReferences before = scanStructReferences(tagged);
    QCOMPARE(before.objectRefCount, 1);
    QCOMPARE(before.imageRefCount, 1);
#endif

    // 重排之后，图像的 /OBJR 引用还在（不能因为重写结构树就把图丢了）。
    positions[0].entries.swapItemsAt(0, 1);
    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

#ifdef AIPDF_WITH_QPDF
    const StructReferences after = scanStructReferences(written);
    QCOMPARE(after.objectRefCount, 1);
    QCOMPARE(after.imageRefCount, 1);
#endif

    // 文档还能打开，页数没变。
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(written);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
}

void SemanticReadingOrderTests::documentWithFormFieldKeepsItsWidgetReference() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(formPdf(), &fixtureError, false, {formFieldBlock()});
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    // 含表单域的文档以前也会被整篇拒绝（而且报错文案误称「含表单域不支持」）。
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!positions.isEmpty());
    QVERIFY(hasRole(positions, QStringLiteral("Form")));

#ifdef AIPDF_WITH_QPDF
    const StructReferences before = scanStructReferences(tagged);
    QCOMPARE(before.widgetRefCount, 1);
#endif

    positions[0].entries.swapItemsAt(0, 1);
    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));

#ifdef AIPDF_WITH_QPDF
    const StructReferences after = scanStructReferences(written);
    QCOMPARE(after.widgetRefCount, 1);
#endif

    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(written);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
}

void SemanticReadingOrderTests::dialogMovesEntryToAnotherSlot() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    aipdf::app::SemanticOrderDialog dialog(positions);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("semanticOrder.list"));
    auto* toNextPage = dialog.findChild<QPushButton*>(QStringLiteral("semanticOrder.moveToNextPage"));
    auto* acceptButton = dialog.findChild<QPushButton*>(QStringLiteral("semanticOrder.accept"));
    QVERIFY(list != nullptr);
    QVERIFY(toNextPage != nullptr);
    QVERIFY(acceptButton != nullptr);
    QCOMPARE(list->count(), 6);

    // 选中第 1 页的第一个元素，挪到下一页的开头。
    list->setCurrentRow(1);
    toNextPage->click();

    const QVector<SemanticOrderSlot> moved = dialog.order();
    // 第 1 页的槽位少一个，第 2 页的槽位多一个。
    QCOMPARE(moved.at(0).entries.size(), 1);
    QCOMPARE(moved.at(1).entries.size(), 3);
    QCOMPARE(moved.at(1).entries.at(0).text, positions.at(0).entries.at(0).text);
    QCOMPARE(moved.at(1).entries.at(0).role, positions.at(0).entries.at(0).role);
    // 元素内容所在的物理页没变（它还印在第 1 页上）。
    QCOMPARE(moved.at(1).entries.at(0).pageIndex, positions.at(0).entries.at(0).pageIndex);

    // 这份顺序是合法的：确认按钮应当放行。
    QString reason;
    QVERIFY2(dialog.isOrderValid(&reason), qPrintable(reason));
    acceptButton->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));

    // 而且它真的写得回去。
    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, moved, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    const QVector<SemanticOrderSlot> reread = aipdf::pdf::readSemanticReadingOrder(written, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reread.at(1).entries.size(), 3);
    QCOMPARE(reread.at(1).entries.at(0).text, positions.at(0).entries.at(0).text);
}

void SemanticReadingOrderTests::dialogRejectsDuplicatedElement() {
    REQUIRE_QPDF();
    QString fixtureError;
    const QByteArray tagged = withSemanticTree(twoPagePdf(), &fixtureError);
    QVERIFY2(!tagged.isEmpty(), qPrintable(fixtureError));

    QString error;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    aipdf::app::SemanticOrderDialog dialog(positions);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("semanticOrder.list"));
    auto* acceptButton = dialog.findChild<QPushButton*>(QStringLiteral("semanticOrder.accept"));
    QVERIFY(list != nullptr);
    QVERIFY(acceptButton != nullptr);

    // 人为制造重复：把第一个元素复制一份塞进列表。
    QListWidgetItem* duplicate = list->item(1)->clone();
    QVERIFY(duplicate != nullptr);
    list->insertItem(2, duplicate);

    QString reason;
    QVERIFY(!dialog.isOrderValid(&reason));
    QVERIFY(!reason.isEmpty());

    // 点「写入」也要被拦下：提示框由定时器关掉（不许在测试里卡住）。
    QTimer::singleShot(0, [] {
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
    });
    acceptButton->click();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
}

QTEST_MAIN(SemanticReadingOrderTests)
#include "SemanticReadingOrderTests.moc"
