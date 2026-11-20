#include "app/dialogs/SemanticOrderSelectionDialog.h"
#include "pdf/compliance/RoleInference.h"
#include "pdf/compliance/SemanticOrderSelection.h"
#include "pdf/compliance/SemanticReadingOrder.h"
#include "pdf/compliance/SemanticTagWriter.h"
#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/StructTreeReader.h"
#include "pdf/compliance/TagTreeWriter.h"
#include "pdf/content/ContentStream.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QTest>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <vector>
#endif

using aipdf::pdf::SemanticOrderScope;
using aipdf::pdf::SemanticOrderSlot;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 把对象列表拼成一份带正确 xref 的 PDF（照 SemanticReadingOrderTests 的做法）。
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

// 两页的夹具：
//   * 第 1 页：H1「Chapter One」（y=700）、P「Alpha line.」（y=660，x=72）、
//     P「Beta line.」（y=640，x=200——左边缘不同，所以不会并进上一段）；
//   * 第 2 页：H1「Chapter Two」、P「Gamma line.」。
//
// 为什么第 1 页要三个元素：只有一个元素时「重排」无从谈起；
// 三个才能验「选区内的换位」与「选区外的元素留在原位」。
QByteArray scopePdf() {
    const QByteArray pageOne(
        "BT /F1 20 Tf 72 700 Td (Chapter One) Tj ET\n"
        "BT /F1 11 Tf 72 660 Td (Alpha line.) Tj ET\n"
        "BT /F1 11 Tf 200 640 Td (Beta line.) Tj ET\n");
    const QByteArray pageTwo(
        "BT /F1 20 Tf 200 700 Td (Chapter Two) Tj ET\n"
        "BT /F1 11 Tf 200 660 Td (Gamma line.) Tj ET\n");

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

// 给一份 PDF 打上语义标签树（照 SemanticReadingOrderTests 的做法）。
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
            blocks.append(block);
        }
    }
    const aipdf::pdf::RoleInferenceResult inference = aipdf::pdf::inferSemanticRoles(blocks);
    aipdf::pdf::SemanticWriteOptions options;
    options.documentLanguage = QStringLiteral("zh-CN");
    options.documentTitle = QStringLiteral("Scope sample");
    options.displayDocTitle = true;
    const aipdf::pdf::SemanticWriteResult written =
        aipdf::pdf::writeSemanticStructure(bytes, inference.blocks, inference.tree, options);
    if (!written.ok) {
        if (error) *error = written.error;
        return {};
    }
    return written.bytes;
}

// 把一个槽位里的元素顺序整个倒过来。
//
// 为什么不用 std::reverse：Qt 的容器迭代器不满足 std::reverse 要求的
// 「可交换的引用」，编译直接报错。用 swapItemsAt 自己翻。
void reverseEntries(QVector<aipdf::pdf::SemanticOrderEntry>& entries) {
    for (int i = 0; i < entries.size() / 2; ++i) {
        entries.swapItemsAt(i, entries.size() - 1 - i);
    }
}

// 打上标签树，并且把两页的顺序**故意排错**（第 1 页 [Beta, Alpha, H1]、
// 第 2 页 [Gamma, Chapter Two]）。
//
// 为什么要有这份「排错」的文档：视觉顺序只有在树里的顺序不对时才有意义；
// 拿本来就排对的文档去测，测的是「没变化」。
QByteArray taggedWithWrongOrder(QString* error) {
    const QByteArray tagged = withSemanticTree(scopePdf(), error);
    if (tagged.isEmpty()) return {};

    QString readError;
    QVector<SemanticOrderSlot> positions = aipdf::pdf::readSemanticReadingOrder(tagged, &readError);
    if (positions.isEmpty()) {
        if (error) *error = readError;
        return {};
    }
    if (positions.size() < 2 || positions.at(0).entries.size() < 3) {
        if (error) *error = QStringLiteral("夹具的元素数量不对");
        return {};
    }
    // 第 1 页：把顺序整个倒过来 → [Beta, Alpha, Chapter One]（与视觉顺序相反）。
    reverseEntries(positions[0].entries);
    // 第 2 页也倒过来：[Gamma line., Chapter Two]。
    //
    // 为什么第 2 页也要排错：只有当它**本来就不是**视觉顺序时，
    // 「选区外的页没被动过」才是可验的——要是有人偷偷把整篇一起排了，
    // 测试立刻变红；排对了的话，这个不变量测不出来。
    if (positions.size() > 1 && positions.at(1).entries.size() >= 2) {
        reverseEntries(positions[1].entries);
    }

    QString writeError;
    const QByteArray written = aipdf::pdf::writeSemanticReadingOrder(tagged, positions, &writeError);
    if (written.isEmpty()) {
        if (error) *error = writeError;
        return {};
    }
    return written;
}

// 第 slot 个槽位里的元素文字（按顺序）。
QStringList textsOfSlot(const QVector<SemanticOrderSlot>& positions, int slot) {
    QStringList result;
    if (slot < 0 || slot >= positions.size()) return result;
    for (const aipdf::pdf::SemanticOrderEntry& entry : positions.at(slot).entries) {
        result.append(entry.text);
    }
    return result;
}

// 某一页的内容流字节（解码后的原始数据）。
QByteArray pageContentBytes(const QByteArray& pdfBytes, int pageIndex) {
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("scope-content.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return {};
        QPDFObjectHandle page = pages.at(pageIndex);
        QByteArray content;
        for (const QPDFObjectHandle& streamHandle : page.getPageContents()) {
            QPDFObjectHandle stream = streamHandle;
            if (!stream.isStream()) continue;
            const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (!data) continue;
            content += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                  static_cast<qsizetype>(data->getSize()));
            content += '\n';
        }
        return content;
    } catch (const std::exception&) {
        return {};
    }
#else
    Q_UNUSED(pdfBytes)
    Q_UNUSED(pageIndex)
    return {};
#endif
}

// 某一页内容流里的记号（去掉空白与注释）。
//
// 为什么比记号而不比原始字节：共用写手每写一次都会在标记内容旁边多留一个空格
// （空白不影响渲染，也不影响内容）。要比「内容有没有被动过」，
// 比记号序列才是准的：显示操作符与 MCID 一个都不许变。
QStringList contentTokensOfPage(const QByteArray& pdfBytes, int pageIndex) {
    QStringList result;
    const QByteArray content = pageContentBytes(pdfBytes, pageIndex);
    if (content.isEmpty()) return result;
    const aipdf::pdf::ContentParseResult parsed = aipdf::pdf::ContentStream::tokenize(content);
    if (!parsed.isValid()) return result;
    for (const aipdf::pdf::ContentToken& token : parsed.tokens) {
        if (token.type == aipdf::pdf::ContentTokenType::Whitespace) continue;
        if (token.type == aipdf::pdf::ContentTokenType::Comment) continue;
        result.append(QString::fromLatin1(token.raw));
    }
    return result;
}

// 某一页的「MCID → 文字」对照（读结构树时顺带读出来的）。
QStringList markedTextsOfPage(const QByteArray& pdfBytes, int pageIndex) {
    QStringList result;
    const aipdf::pdf::StructTreeReport report = aipdf::pdf::readStructTree(pdfBytes);
    for (const aipdf::pdf::MarkedContentText& marked : report.markedContent) {
        if (marked.pageIndex != pageIndex) continue;
        result.append(QStringLiteral("%1:%2").arg(marked.mcid).arg(marked.text));
    }
    result.sort();
    return result;
}

// 只包含第 pageIndex 页的选区。
SemanticOrderScope scopeOfPage(int pageIndex) {
    SemanticOrderScope scope;
    scope.pageIndexes.append(pageIndex);
    scope.regions.append(QRectF());
    return scope;
}

}  // namespace

class SemanticOrderSelectionTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void readScopeListsPagesAndElements();
    void readScopeOnPlainDocumentExplainsWhy();
    void applyWithEmptyScopeDoesNothing();
    void applyWithScopeWithoutElementsKeepsBytes();
    void visualOrderIsAppliedInsideScope();
    void elementsOutsideScopeKeepTheirOrder();
    void elementsOutsideRegionKeepTheirPosition();
    void regionWithSingleElementChangesNothing();
    void rolesAndTextsSurviveTheRewrite();
    void outOfScopePageContentIsUnchanged();
    void writtenDocumentStillOpens();
    void dialogDefaultsToCurrentPage();
    void dialogCanPickAnotherPage();
    void dialogRegionModeUsesGivenRegion();
    void dialogWithoutRegionDisablesRegionOnly();
};

void SemanticOrderSelectionTests::initTestCase() {
    // 这一组会建界面控件（选区对话框）。
    //
    // 为什么每个对话框都要开 WA_DontShowOnScreen：界面单测不许抢前台——
    // 本地绝对不允许跑会操控桌面的验收。这条属性让控件只在内存里存在，
    // 不真正映射到屏幕上。
}

void SemanticOrderSelectionTests::readScopeListsPagesAndElements() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = withSemanticTree(scopePdf(), &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const aipdf::pdf::SemanticOrderScopeReport report =
        aipdf::pdf::readSemanticOrderScope(tagged, scopeOfPage(0));
    QVERIFY2(report.ok(), qPrintable(report.error));
    QCOMPARE(report.pages.size(), 2);
    QCOMPARE(report.pages.at(0), 0);
    QCOMPARE(report.pages.at(1), 1);
    // 第 1 页三个元素、第 2 页两个。
    QCOMPARE(report.elements.size(), 5);
    // 选区是第 1 页 → 三个元素在选区里。
    QCOMPARE(report.scopedCount, 3);

    // 视觉名次：从上到下（Chapter One → Alpha line. → Beta line.）。
    QVector<int> ranks;
    for (const aipdf::pdf::SemanticOrderScopeElement& element : report.elements) {
        if (!element.inScope) continue;
        ranks.append(element.visualRank);
    }
    std::sort(ranks.begin(), ranks.end());
    QCOMPARE(ranks, QVector<int>({0, 1, 2}));

    // 每个在选区里的元素都要有位置（取不到位置的元素不会被重排）。
    for (const aipdf::pdf::SemanticOrderScopeElement& element : report.elements) {
        if (!element.inScope) continue;
        QVERIFY(!element.bounds.isEmpty());
    }
}

void SemanticOrderSelectionTests::readScopeOnPlainDocumentExplainsWhy() {
    REQUIRE_QPDF();
    const aipdf::pdf::SemanticOrderScopeReport report =
        aipdf::pdf::readSemanticOrderScope(scopePdf(), scopeOfPage(0));
    QVERIFY(!report.ok());
    QVERIFY2(report.error.contains(QStringLiteral("语义标签树")), qPrintable(report.error));
}

void SemanticOrderSelectionTests::applyWithEmptyScopeDoesNothing() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = withSemanticTree(scopePdf(), &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, SemanticOrderScope(), &error);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.bytes.isEmpty());
}

void SemanticOrderSelectionTests::applyWithScopeWithoutElementsKeepsBytes() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    // 选区指向一个没有语义元素的页（这份夹具只有两页）。
    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scopeOfPage(7), &error);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.scopedCount, 0);
    QCOMPARE(result.movedCount, 0);
    QVERIFY(!result.changed);
    // 一个字节都没变。
    QCOMPARE(result.bytes, tagged);
}

void SemanticOrderSelectionTests::visualOrderIsAppliedInsideScope() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    // 先确认夹具确实是「排错的」：第 1 页的顺序是 [Beta, Alpha, Chapter One]。
    QVector<SemanticOrderSlot> before = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(textsOfSlot(before, 0),
             QStringList({QStringLiteral("Beta line."), QStringLiteral("Alpha line."),
                          QStringLiteral("Chapter One")}));

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scopeOfPage(0), &error);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.changed);
    // 动了两个位置：Chapter One 从下标 2 换到 0，Beta 从 0 换到 2；
    // Alpha 本来就在中间（下标 1），不算动了。
    QCOMPARE(result.movedCount, 2);
    QVERIFY(!result.bytes.isEmpty());

    // 读回来：第 1 页按视觉顺序（从上到下）。
    QVector<SemanticOrderSlot> after = aipdf::pdf::readSemanticReadingOrder(result.bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(textsOfSlot(after, 0),
             QStringList({QStringLiteral("Chapter One"), QStringLiteral("Alpha line."),
                          QStringLiteral("Beta line.")}));
}

void SemanticOrderSelectionTests::elementsOutsideScopeKeepTheirOrder() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    QVector<SemanticOrderSlot> before = aipdf::pdf::readSemanticReadingOrder(tagged, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QStringList beforeSecondPage = textsOfSlot(before, 1);
    QVERIFY(!beforeSecondPage.isEmpty());

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scopeOfPage(0), &error);
    QVERIFY2(result.ok, qPrintable(result.error));

    QVector<SemanticOrderSlot> after = aipdf::pdf::readSemanticReadingOrder(result.bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // 选区外的页：元素个数、文字、角色一个都不变。
    QCOMPARE(textsOfSlot(after, 1), beforeSecondPage);
    for (int i = 0; i < before.at(1).entries.size(); ++i) {
        QCOMPARE(after.at(1).entries.at(i).role, before.at(1).entries.at(i).role);
        QCOMPARE(after.at(1).entries.at(i).pageIndex, before.at(1).entries.at(i).pageIndex);
        QCOMPARE(after.at(1).entries.at(i).mcids, before.at(1).entries.at(i).mcids);
    }
    // 第 2 页本来就不是视觉顺序（夹具故意排错的）：
    // 它**仍然是**错序，说明这一页真的一个字节都没被动过。
    QCOMPARE(textsOfSlot(after, 1),
             QStringList({QStringLiteral("Gamma line."), QStringLiteral("Chapter Two")}));
}

void SemanticOrderSelectionTests::elementsOutsideRegionKeepTheirPosition() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    // 选区只圈住第 1 页下半部分（Alpha 与 Beta），不圈 Chapter One。
    //
    // 位置：Chapter One 的基线在 700（框是 [700, 720]），
    // Alpha 在 660、Beta 在 640；区域取 [630, 670] 只罩住后两个。
    SemanticOrderScope scope = scopeOfPage(0);
    scope.regions[0] = QRectF(0.0, 630.0, 612.0, 40.0);

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scope, &error);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.changed);

    QVector<SemanticOrderSlot> after = aipdf::pdf::readSemanticReadingOrder(result.bytes, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // 选区内的两个按视觉顺序换了位；选区外的 Chapter One 留在原来的下标上。
    QCOMPARE(textsOfSlot(after, 0),
             QStringList({QStringLiteral("Alpha line."), QStringLiteral("Beta line."),
                          QStringLiteral("Chapter One")}));
}

void SemanticOrderSelectionTests::regionWithSingleElementChangesNothing() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    // 只圈住 Chapter One（基线 700）：选区里只有一个元素，换不了位。
    SemanticOrderScope scope = scopeOfPage(0);
    scope.regions[0] = QRectF(0.0, 690.0, 612.0, 60.0);

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scope, &error);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.scopedCount, 1);
    QCOMPARE(result.movedCount, 0);
    QVERIFY(!result.changed);
    QCOMPARE(result.bytes, tagged);
}

void SemanticOrderSelectionTests::rolesAndTextsSurviveTheRewrite() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scopeOfPage(0), &error);
    QVERIFY2(result.ok, qPrintable(result.error));

    const aipdf::pdf::StructTreeReport before = aipdf::pdf::readStructTree(tagged);
    const aipdf::pdf::StructTreeReport after = aipdf::pdf::readStructTree(result.bytes);
    QVERIFY(after.error.isEmpty());
    QCOMPARE(after.elements.size(), before.elements.size());

    // 「角色 + 文字」的多重集不变：只有先后变了。
    QStringList beforeShape;
    QStringList afterShape;
    for (const aipdf::pdf::StructElement& element : before.elements) {
        beforeShape.append(element.tag + QLatin1Char('\t') + element.text);
    }
    for (const aipdf::pdf::StructElement& element : after.elements) {
        afterShape.append(element.tag + QLatin1Char('\t') + element.text);
    }
    beforeShape.sort();
    afterShape.sort();
    QCOMPARE(afterShape, beforeShape);
}

void SemanticOrderSelectionTests::outOfScopePageContentIsUnchanged() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scopeOfPage(0), &error);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 选区外的页：内容流里的记号（显示操作符 + MCID）一个都不变，
    // 标记内容与文字的对应也不变。
    const QStringList beforeTokens = contentTokensOfPage(tagged, 1);
    const QStringList afterTokens = contentTokensOfPage(result.bytes, 1);
    QVERIFY(!beforeTokens.isEmpty());
    QCOMPARE(afterTokens, beforeTokens);
    QCOMPARE(markedTextsOfPage(result.bytes, 1), markedTextsOfPage(tagged, 1));
}

void SemanticOrderSelectionTests::writtenDocumentStillOpens() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = taggedWithWrongOrder(&error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const aipdf::pdf::SemanticOrderApplyResult result =
        aipdf::pdf::applySemanticOrderScope(tagged, scopeOfPage(0), &error);
    QVERIFY2(result.ok, qPrintable(result.error));

    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(result.bytes);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 2);
}

void SemanticOrderSelectionTests::dialogDefaultsToCurrentPage() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = withSemanticTree(scopePdf(), &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    aipdf::app::SemanticOrderSelectionDialog dialog(tagged, 1, -1, QRectF());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    const SemanticOrderScope scope = dialog.scope();
    QCOMPARE(scope.pageIndexes.size(), 1);
    QCOMPARE(scope.pageIndexes.at(0), 1);
    // 没有区域限制 → 整页。
    QVERIFY(scope.regionAt(0).isEmpty());
}

void SemanticOrderSelectionTests::dialogCanPickAnotherPage() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = withSemanticTree(scopePdf(), &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    aipdf::app::SemanticOrderSelectionDialog dialog(tagged, 0, -1, QRectF());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    // 控件要按名字找：findChild 的接收者必须是**被测的对话框**，
    // 在测试类里直接调 findChild 会搜到测试类自己身上的同名控件。
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("semanticOrderSelection.pages"));
    QVERIFY(pages != nullptr);
    QCOMPARE(pages->count(), 2);

    // 取消第 1 页、勾上第 2 页。
    pages->item(0)->setCheckState(Qt::Unchecked);
    pages->item(1)->setCheckState(Qt::Checked);

    const SemanticOrderScope scope = dialog.scope();
    QCOMPARE(scope.pageIndexes.size(), 1);
    QCOMPARE(scope.pageIndexes.at(0), 1);
}

void SemanticOrderSelectionTests::dialogRegionModeUsesGivenRegion() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = withSemanticTree(scopePdf(), &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    const QRectF region(0.0, 630.0, 612.0, 40.0);
    aipdf::app::SemanticOrderSelectionDialog dialog(tagged, 0, 0, region);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto* regionOnly =
        dialog.findChild<QCheckBox*>(QStringLiteral("semanticOrderSelection.regionOnly"));
    QVERIFY(regionOnly != nullptr);
    QVERIFY(regionOnly->isEnabled());
    QVERIFY(regionOnly->isChecked());

    const SemanticOrderScope scope = dialog.scope();
    QCOMPARE(scope.pageIndexes.size(), 1);
    QCOMPARE(scope.pageIndexes.at(0), 0);
    QCOMPARE(scope.regionAt(0), region);
}

void SemanticOrderSelectionTests::dialogWithoutRegionDisablesRegionOnly() {
    REQUIRE_QPDF();
    QString error;
    const QByteArray tagged = withSemanticTree(scopePdf(), &error);
    QVERIFY2(!tagged.isEmpty(), qPrintable(error));

    aipdf::app::SemanticOrderSelectionDialog dialog(tagged, 0, -1, QRectF());
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    auto* regionOnly =
        dialog.findChild<QCheckBox*>(QStringLiteral("semanticOrderSelection.regionOnly"));
    QVERIFY(regionOnly != nullptr);
    QVERIFY(!regionOnly->isEnabled());

    const SemanticOrderScope scope = dialog.scope();
    QCOMPARE(scope.pageIndexes.size(), 1);
    QCOMPARE(scope.pageIndexes.at(0), 0);
}

QTEST_MAIN(SemanticOrderSelectionTests)
#include "SemanticOrderSelectionTests.moc"
