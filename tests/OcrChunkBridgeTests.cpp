// v0.13.0：OCR 结果接入索引的单元测试。
//
// 守两件事：OCR 页产出与文本页同样形状的块；fromOcr 标记能落盘再读回来。
#include "doc/LocalIndex.h"
#include "ocr/OcrChunkBridge.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace aipdf::doc;
using namespace aipdf::ocr;

namespace {

OcrLine makeLine(const QString& text, double y, double height = 10.0) {
    OcrLine line;
    line.text = text;
    line.rect = QRectF(72.0, y, 300.0, height);
    return line;
}

OcrPageResult makePage(int pageIndex, const QVector<OcrLine>& lines) {
    OcrPageResult page;
    page.pageIndex = pageIndex;
    page.engineId = QStringLiteral("fake");
    page.lines = lines;
    return page;
}

}  // namespace

class OcrChunkBridgeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void closeLinesBecomeOneChunk();
    void paragraphGapSplitsChunks();
    void chunksCarryPageIndexAndOcrFlag();
    void emptyResultProducesNoChunks();
    void chunksRoundTripThroughLocalIndex();

private:
    QTemporaryDir dataDir_;
};

void OcrChunkBridgeTests::initTestCase() {
    QVERIFY(dataDir_.isValid());
    qputenv("AIPDF_INDEX_DIR", (dataDir_.path() + QStringLiteral("/index")).toUtf8());
}

void OcrChunkBridgeTests::closeLinesBecomeOneChunk() {
    const OcrPageResult page =
        makePage(0, {makeLine(QStringLiteral("第一行正文"), 700), makeLine(QStringLiteral("第二行正文"), 685)});
    const auto chunks = chunksFromOcrPage(page, 0);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.first().text, QStringLiteral("第一行正文\n第二行正文"));
    QCOMPARE(chunks.first().lineBoxes.size(), 2);
}

void OcrChunkBridgeTests::paragraphGapSplitsChunks() {
    // 至少三行：行距基准取的是行间距的中位数，两行时唯一那个大间距
    // 会被当成「正常行距」，段落规则就永远不触发。
    const OcrPageResult page = makePage(0,
                                        {makeLine(QStringLiteral("上一段第一行"), 700),
                                         makeLine(QStringLiteral("上一段第二行"), 685),
                                         makeLine(QStringLiteral("上一段第三行"), 670),
                                         makeLine(QStringLiteral("下一段"), 500)});
    // 关掉「太短的块并进相邻块」：这里要验的是行距规则本身。
    ChunkBuilder::Options options;
    options.minChunkChars = 0;
    const auto chunks = chunksFromOcrPage(page, 0, options);
    QCOMPARE(chunks.size(), 2);
    QVERIFY(chunks.at(0).text.startsWith(QStringLiteral("上一段第一行")));
    QCOMPARE(chunks.at(1).text, QStringLiteral("下一段"));
    // 顺序要接着 startOrder 排，多页拼接时不会撞车。
    QCOMPARE(chunks.at(1).order, 1);
}

void OcrChunkBridgeTests::chunksCarryPageIndexAndOcrFlag() {
    const OcrPageResult page = makePage(4, {makeLine(QStringLiteral("扫描页上的字"), 500)});
    const auto chunks = chunksFromOcrPage(page, 17);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.first().pageIndex, 4);
    QCOMPARE(chunks.first().order, 17);
    QVERIFY(chunks.first().fromOcr);
    QVERIFY(!chunks.first().text.isEmpty());
}

void OcrChunkBridgeTests::emptyResultProducesNoChunks() {
    OcrPageResult page;
    page.pageIndex = 2;
    QVERIFY(chunksFromOcrPage(page, 0).isEmpty());
}

void OcrChunkBridgeTests::chunksRoundTripThroughLocalIndex() {
    // fromOcr 必须能落盘再读回来：重启后引用还要能说「这句是识别来的」。
    const OcrPageResult page = makePage(0, {makeLine(QStringLiteral("识别出来的一段话"), 400)});
    const auto chunks = chunksFromOcrPage(page, 0);
    QVERIFY(!chunks.isEmpty());

    LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/tmp/scan.pdf"));
    index.setPageCount(1);
    index.replacePage(0, chunks, QStringLiteral("fingerprint"));
    QVERIFY(index.save());

    LocalIndex loaded;
    QVERIFY(loaded.load(QStringLiteral("C:/tmp/scan.pdf")));
    const auto restored = loaded.chunks();
    QCOMPARE(restored.size(), chunks.size());
    QVERIFY(restored.first().fromOcr);
    QCOMPARE(restored.first().text, chunks.first().text);
}

QTEST_MAIN(OcrChunkBridgeTests)
#include "OcrChunkBridgeTests.moc"
