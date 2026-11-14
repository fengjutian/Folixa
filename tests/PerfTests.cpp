// v0.15.0：性能基线。
//
// 这里量的是「不依赖界面与 PDFium 的纯计算路径」：分块、检索、空白页判定、
// 提案校验、图像预处理。选它们做基线有两个原因：
//   1. 它们的耗时直接决定大文档打开、搜索、OCR 的手感；
//   2. 它们在 CI 上足够稳定——不涉及渲染线程、磁盘与网络。
//
// 预算刻意给得宽（实测的 10 倍以上）：这里要抓的是「数量级退化」，
// 不是把机器快慢当成回归。具体数值随构建打印出来，方便对照。
#include "app/BlankPageDetector.h"
#include "doc/ChunkBuilder.h"
#include "doc/LocalIndex.h"
#include "ocr/ImagePreprocess.h"
#include "tools/ProposalValidator.h"

#include <QElapsedTimer>
#include <QImage>
#include <QtTest>

using namespace aipdf;

namespace {

double measureMs(const std::function<void()>& work) {
    QElapsedTimer timer;
    timer.start();
    work();
    return static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
}

QVector<doc::ChunkLine> makeLines(int count) {
    QVector<doc::ChunkLine> lines;
    lines.reserve(count);
    for (int i = 0; i < count; ++i) {
        doc::ChunkLine line;
        line.text = QStringLiteral("第 %1 行：这是一段用于性能基线的中文正文，包含数字 %2。")
                        .arg(i)
                        .arg(i * 7);
        line.box = QRectF(72.0, 700.0 - i * 16.0, 460.0, 12.0);
        line.charHeight = 12.0;
        lines.append(line);
    }
    return lines;
}

}  // namespace

class PerfTests : public QObject {
    Q_OBJECT

private slots:
    void chunkBuilderBaseline();
    void indexSearchBaseline();
    void blankPageDetectorBaseline();
    void proposalValidationBaseline();
    void imagePreprocessBaseline();
};

void PerfTests::chunkBuilderBaseline() {
    const QVector<doc::ChunkLine> lines = makeLines(600);
    const double elapsed = measureMs([&lines] {
        for (int page = 0; page < 20; ++page) {
            const auto chunks = doc::ChunkBuilder::build(page, lines, QString(), 0);
            QVERIFY(!chunks.isEmpty());
        }
    });
    // 20 页 × 600 行：预算 400ms（实测约 20–40ms）。
    qInfo("chunk builder: 20 pages x 600 lines = %.1f ms", elapsed);
    QVERIFY2(elapsed < 400.0, qPrintable(QString::number(elapsed)));
}

void PerfTests::indexSearchBaseline() {
    doc::LocalIndex index;
    index.setDocumentPath(QStringLiteral("C:/perf/doc.pdf"));
    index.setPageCount(120);
    // 每行一块：模拟「块很小、数量很多」的最坏情况（3600 块），
    // 这比真实文档更容易暴露检索的复杂度问题。
    doc::ChunkBuilder::Options options;
    options.maxChunkChars = 120;
    options.minChunkChars = 0;
    for (int page = 0; page < 120; ++page) {
        index.replacePage(
            page, doc::ChunkBuilder::build(page, makeLines(30), QString(), 0, options),
            QStringLiteral("fp%1").arg(page));
    }
    QVERIFY2(index.chunkCount() > 800, qPrintable(QString::number(index.chunkCount())));
    qInfo("index corpus: %d chunks", index.chunkCount());

    const QStringList queries{QStringLiteral("性能"), QStringLiteral("中文"),
                              QStringLiteral("数字 42"), QStringLiteral("不存在的词")};
    const double elapsed = measureMs([&index, &queries] {
        for (int round = 0; round < 10; ++round) {
            for (const QString& query : queries) {
                const auto hits = index.search(query);
                Q_UNUSED(hits);
            }
        }
    });
    // 40 次检索（每次扫 3600 块）：预算 400ms（实测约 30–60ms）。
    qInfo("index search: 40 queries = %.1f ms", elapsed);
    QVERIFY2(elapsed < 300.0, qPrintable(QString::number(elapsed)));
}

void PerfTests::blankPageDetectorBaseline() {
    QImage page(1000, 1400, QImage::Format_Grayscale8);
    page.fill(255);
    for (int y = 100; y < 400; ++y) {
        for (int x = 100; x < 500; ++x) {
            page.setPixel(x, y, 30);
        }
    }
    const double elapsed = measureMs([&page] {
        for (int i = 0; i < 20; ++i) {
            const double ratio = app::inkRatio(page);
            QVERIFY(ratio > 0.0);
        }
    });
    // 20 次 140 万像素统计：预算 400ms（实测约 40–80ms）。
    qInfo("ink ratio: 20 pages = %.1f ms", elapsed);
    QVERIFY2(elapsed < 400.0, qPrintable(QString::number(elapsed)));
}

void PerfTests::proposalValidationBaseline() {
    QJsonArray operations;
    for (int i = 0; i < 20; ++i) {
        operations.append(QJsonObject{
            {QStringLiteral("tool"), QStringLiteral("doc.delete_pages")},
            {QStringLiteral("args"), QJsonObject{{QStringLiteral("pages"), QJsonArray{1, 2}}}}});
    }
    const double elapsed = measureMs([&operations] {
        for (int i = 0; i < 200; ++i) {
            const auto proposal =
                tools::validateProposal(operations, tools::DocumentSnapshot{50, 1});
            QVERIFY(proposal.ok());
        }
    });
    // 200 次 20 条操作的校验：预算 300ms（实测约 20ms）。
    qInfo("proposal validation: 200 x 20 ops = %.1f ms", elapsed);
    QVERIFY2(elapsed < 300.0, qPrintable(QString::number(elapsed)));
}

void PerfTests::imagePreprocessBaseline() {
    // 与 OCR 实际输入同量级：A4 @150dpi 左右。
    QImage scan(1240, 1754, QImage::Format_RGB32);
    scan.fill(Qt::white);
    for (int y = 100; y < 500; y += 3) {
        for (int x = 100; x < 900; ++x) {
            scan.setPixel(x, y, qRgb(20, 20, 20));
        }
    }
    const double elapsed = measureMs([&scan] {
        for (int i = 0; i < 5; ++i) {
            const QImage binary = ocr::preprocessForOcr(scan);
            QVERIFY(!binary.isNull());
        }
    });
    // 5 次 217 万像素的灰度+中值+Otsu+二值：预算 3000ms（实测约 400–700ms）。
    qInfo("ocr preprocess: 5 pages = %.1f ms", elapsed);
    QVERIFY2(elapsed < 3000.0, qPrintable(QString::number(elapsed)));
}

QTEST_MAIN(PerfTests)
#include "PerfTests.moc"
