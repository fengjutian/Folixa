// v0.9.0 索引控制器的集成测试：拿真实 PDF 走一遍建索引、检索与增量重建。
//
// 分块器与索引文件本身的纯逻辑在 DocumentIndexTests 里；
// 这里测的是「真的打开一个 PDF、后台线程真的把块切出来」这条链路。

#include "app/IndexController.h"
#include "doc/ChunkBuilder.h"
#include "doc/DocumentChunk.h"
#include "doc/LocalIndex.h"
#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QEventLoop>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace aipdf::app;
using namespace aipdf::doc;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 打开一个夹具，失败就报警。
aipdf::pdf::PdfDocument openFixture(const QString& name) {
    const auto result = aipdf::pdf::openDocument(fixturePath(name));
    if (!result.document.isValid()) {
        qWarning("打开夹具失败 %s：%s", qPrintable(name), qPrintable(result.errorMessage));
    }
    return result.document;
}

// 打开一个路径、建索引、等它跑完。
//
// 返回 nullptr 表示打不开；状态不是 Completed 时由调用方自己断言。
std::unique_ptr<IndexController> buildIndexPath(const QString& documentPath,
                                                int timeoutMs = 60000) {
    const auto document = aipdf::pdf::openDocument(documentPath);
    if (!document.document.isValid()) {
        return nullptr;
    }
    auto controller = std::make_unique<IndexController>(document.document);
    controller->setDocumentPath(documentPath);

    QEventLoop loop;
    QObject::connect(controller.get(), &IndexController::indexFinished, &loop, &QEventLoop::quit);
    QObject::connect(controller.get(), &IndexController::indexCancelled, &loop, &QEventLoop::quit);
    QObject::connect(controller.get(), &IndexController::indexFailed, &loop, &QEventLoop::quit);
    // 超时兜底：没有它，索引卡住时整个测试会永远挂着。
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    controller->startIndexing();
    loop.exec();
    return controller;
}

// 同上，但按夹具名来。
std::unique_ptr<IndexController> buildIndex(const QString& fixture,
                                            int timeoutMs = 60000) {
    return buildIndexPath(fixturePath(fixture), timeoutMs);
}

}  // namespace

class PdfIndexTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void buildsChunksFromRealPdf();
    void chunkTextMatchesPageContent();
    void headingDetectedOnFirstPage();
    void chunkCoordinatesStayOnPage();
    void searchFindsTextAcrossPages();
    void secondRunReusesUnchangedPages();
    void cancelStopsIndexing();

private:
    // 索引目录的临时目录：测试不该往用户的真实数据目录里写东西。
    // 它必须活到测试结束，所以用成员变量保活。
    std::unique_ptr<QTemporaryDir> indexDir_;
};

void PdfIndexTests::initTestCase() {
    indexDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(indexDir_->isValid());
    qputenv("AIPDF_INDEX_DIR", indexDir_->path().toUtf8());
}

void PdfIndexTests::buildsChunksFromRealPdf() {
    const auto controller = buildIndex(QStringLiteral("text.pdf"));
    QVERIFY(controller != nullptr);
    QCOMPARE(controller->state(), IndexController::State::Completed);
    QCOMPARE(controller->totalPages(), 2);
    QCOMPARE(controller->processedPages(), 2);
    QVERIFY(controller->index().chunkCount() > 0);
    // 状态文字要说清楚建了多少块。
    QVERIFY(controller->statusText().contains(QStringLiteral("索引完成")));
}

void PdfIndexTests::chunkTextMatchesPageContent() {
    const auto controller = buildIndex(QStringLiteral("text.pdf"));
    QVERIFY(controller != nullptr);

    const QVector<DocumentChunk> chunks = controller->index().chunks();
    QString all;
    for (const DocumentChunk& chunk : chunks) {
        all += chunk.text;
        all += QLatin1Char('\n');
    }
    // 两页的正文都进来了。
    QVERIFY(all.contains(QStringLiteral("Folixa")));
    QVERIFY(all.contains(QStringLiteral("quick brown fox")));
    QVERIFY(all.contains(QStringLiteral("second page")));
    // 每一块都要有页码、id 与坐标。
    for (const DocumentChunk& chunk : chunks) {
        QVERIFY(chunk.isValid());
        QVERIFY(!chunk.id.isEmpty());
        QVERIFY(chunk.pageIndex >= 0 && chunk.pageIndex < 2);
        QVERIFY(!chunk.lineBoxes.isEmpty());
    }
}

void PdfIndexTests::headingDetectedOnFirstPage() {
    const auto controller = buildIndex(QStringLiteral("text.pdf"));
    QVERIFY(controller != nullptr);

    const QVector<DocumentChunk> firstPage = controller->index().chunksForPage(0);
    QVERIFY(!firstPage.isEmpty());
    // 第 1 页第一行是 32pt 的大标题，正文是 14pt → 标题层级 1。
    QVERIFY(firstPage.at(0).isHeading());
    QCOMPARE(firstPage.at(0).headingLevel, 1);
    QVERIFY(firstPage.at(0).text.contains(QStringLiteral("Folixa")));

    // 第 2 页第一行是 18pt：正文 14pt 的 1.29 倍 → 是标题，但层级更低。
    const QVector<DocumentChunk> secondPage = controller->index().chunksForPage(1);
    QVERIFY(!secondPage.isEmpty());
    QVERIFY(secondPage.at(0).isHeading());
    QVERIFY(secondPage.at(0).headingLevel >= firstPage.at(0).headingLevel);
}

void PdfIndexTests::chunkCoordinatesStayOnPage() {
    const auto controller = buildIndex(QStringLiteral("text.pdf"));
    QVERIFY(controller != nullptr);

    // text.pdf 的页面是 612 x 400，坐标是 PDF user space（左下原点）。
    for (const DocumentChunk& chunk : controller->index().chunks()) {
        const QRectF bounds = chunk.bounds();
        QVERIFY(!bounds.isNull());
        QVERIFY(bounds.x() >= -1.0);
        QVERIFY(bounds.y() >= -1.0);
        QVERIFY(bounds.right() <= 613.0);
        QVERIFY(bounds.top() <= 401.0);
    }
}

void PdfIndexTests::searchFindsTextAcrossPages() {
    const auto controller = buildIndex(QStringLiteral("text.pdf"));
    QVERIFY(controller != nullptr);

    const QVector<IndexHit> hits = controller->search(QStringLiteral("Folixa"));
    // 两页各一次。
    QCOMPARE(hits.size(), 2);
    QSet<int> pages;
    for (const IndexHit& hit : hits) {
        pages.insert(hit.pageIndex);
        QVERIFY(!hit.chunkId.isEmpty());
        QVERIFY(!hit.snippet.isEmpty());
    }
    QCOMPARE(pages.size(), 2);

    // 按页取块要取得到（定位高亮靠它）。
    DocumentChunk chunk;
    QVERIFY(controller->chunkByIdOnPage(hits.at(0).pageIndex, hits.at(0).chunkId, &chunk));
    QVERIFY(!chunk.lineBoxes.isEmpty());
}

void PdfIndexTests::secondRunReusesUnchangedPages() {
    // 用一份独有的副本：别的用例可能已经给同一个夹具建过索引，
    // 那样第一轮就会命中增量，「第一轮重建 2 页」这条断言就不成立了。
    const QString fixture = QStringLiteral("text.pdf");
    const QString uniquePath =
        QDir(indexDir_->path()).filePath(QStringLiteral("reuse-case.pdf"));
    QVERIFY(QFile::copy(fixturePath(fixture), uniquePath));

    QStringList firstIds;
    int firstChunkCount = 0;
    {
        const auto controller = buildIndexPath(uniquePath);
        QVERIFY(controller != nullptr);
        QCOMPARE(controller->state(), IndexController::State::Completed);
        firstChunkCount = controller->index().chunkCount();
        for (const DocumentChunk& chunk : controller->index().chunks()) {
            firstIds.append(chunk.id);
        }
        // 第一轮：没有旧索引，两页都要重建。
        QCOMPARE(controller->rebuiltPages(), 2);
    }

    // 第二轮：内容没变，应该一页都不重建。
    const auto second = buildIndexPath(uniquePath);
    QVERIFY(second != nullptr);
    QCOMPARE(second->state(), IndexController::State::Completed);
    QCOMPARE(second->rebuiltPages(), 0);
    QCOMPARE(second->index().chunkCount(), firstChunkCount);

    QStringList secondIds;
    for (const DocumentChunk& chunk : second->index().chunks()) {
        secondIds.append(chunk.id);
    }
    // 块 id 与内容绑定：重建之后 id 不变，外部引用不会错位。
    QCOMPARE(secondIds, firstIds);
}

void PdfIndexTests::cancelStopsIndexing() {
    // long.pdf 有 500 页：跑到一半取消，剩下的页不再处理。
    const auto document = openFixture(QStringLiteral("long.pdf"));
    QVERIFY(document.isValid());
    IndexController controller(document);
    controller.setDocumentPath(fixturePath(QStringLiteral("long.pdf")));

    QEventLoop loop;
    bool cancelRequested = false;
    bool finished = false;
    QObject::connect(&controller, &IndexController::indexProgress, &loop, [&]() {
        // 跑完第一页就取消：后面还有 499 页，肯定来得及。
        if (!cancelRequested) {
            controller.cancelIndexing();
            cancelRequested = true;
        }
    });
    QObject::connect(&controller, &IndexController::indexCancelled, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QObject::connect(&controller, &IndexController::indexFinished, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);

    controller.startIndexing();
    loop.exec();

    QVERIFY(cancelRequested);
    QVERIFY(finished);
    QCOMPARE(controller.state(), IndexController::State::Cancelled);
    // 不该把整本文档都处理完。
    QVERIFY(controller.processedPages() < controller.totalPages());
}

QTEST_MAIN(PdfIndexTests)
#include "PdfIndexTests.moc"
