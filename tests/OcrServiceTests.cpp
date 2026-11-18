// v0.13.0：OCR 服务（后台线程 + 队列 + 取消）的单元测试。
//
// 用假引擎把「什么时候回来」变成确定的：真引擎一页几百毫秒，
// 拿它测取消只会测出机器快慢。
#include "ocr/pipeline/OcrService.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <memory>

using namespace aipdf::ocr;

namespace {

class FakeEngine final : public OcrEngine {
public:
    explicit FakeEngine(int delayMs = 0, bool blockUntilCancelled = false)
        : delayMs_(delayMs), blockUntilCancelled_(blockUntilCancelled) {}

    QString id() const override { return QStringLiteral("fake"); }
    bool isAvailable() const override { return true; }

    OcrPageResult recognize(const OcrJob& job) override {
        const int pageIndex = job.pageIndex;
        if (blockUntilCancelled_) {
            // 模拟真实的命令行引擎：识别过程很长，只有 cancel() 能让它提前回来。
            started_.store(true);
            for (int i = 0; i < 1000 && !cancelled_.load(); ++i) {  // 上限 5s
                QThread::msleep(5);
            }
            OcrPageResult blocked;
            blocked.pageIndex = pageIndex;
            blocked.engineId = id();
            blocked.cancelled = cancelled_.load();
            calls_.fetch_add(1);
            return blocked;
        }
        if (delayMs_ > 0) {
            QThread::msleep(static_cast<unsigned long>(delayMs_));
        }
        OcrPageResult result;
        result.pageIndex = pageIndex;
        result.engineId = id();
        OcrLine line;
        line.text = QStringLiteral("第 %1 页").arg(pageIndex + 1);
        line.rect = QRectF(10.0, 20.0, 100.0, 12.0);
        line.words.append({QStringLiteral("第"), line.rect, 0.9});
        result.lines.append(line);
        result.meanConfidence = 0.9;
        calls_.fetch_add(1);
        return result;
    }

    void cancel() override { cancelled_.store(true); }

    int calls() const { return calls_.load(); }
    bool started() const { return started_.load(); }

private:
    int delayMs_ = 0;
    bool blockUntilCancelled_ = false;
    std::atomic<int> calls_{0};
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> started_{false};
};

OcrJob makeJob(int page) {
    OcrJob job;
    job.pageIndex = page;
    job.image = QImage(100, 100, QImage::Format_ARGB32);
    job.pageSize = QSizeF(200.0, 300.0);
    return job;
}

}  // namespace

class OcrServiceTests : public QObject {
    Q_OBJECT

private slots:
    void requiresEngineBeforeEnqueue();
    void processesQueuedPagesInOrder();
    void duplicatePageIsIgnored();
    void cancelDropsQueuedPages();
    void cancelInterruptsRunningRecognition();
};

void OcrServiceTests::requiresEngineBeforeEnqueue() {
    OcrService service;
    QVERIFY(!service.hasEngine());
    // 没有引擎时入队不应该崩，只是什么都不发生。
    service.enqueue(makeJob(0));
    QCOMPARE(service.pendingCount(), 0);
}

void OcrServiceTests::processesQueuedPagesInOrder() {
    OcrService service;
    service.setEngine(std::make_unique<FakeEngine>());
    QSignalSpy spy(&service, &OcrService::pageReady);

    for (int page = 0; page < 3; ++page) {
        service.enqueue(makeJob(page));
    }
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 5000);

    QList<int> pages;
    for (const QList<QVariant>& call : spy) {
        const auto result = call.at(0).value<OcrPageResult>();
        pages.append(result.pageIndex);
        QVERIFY(!result.cancelled);
        QCOMPARE(result.lines.size(), 1);
        QVERIFY(result.plainText().contains(QStringLiteral("第")));
    }
    QCOMPARE(pages, (QList<int>{0, 1, 2}));
}

void OcrServiceTests::duplicatePageIsIgnored() {
    OcrService service;
    service.setEngine(std::make_unique<FakeEngine>(30));
    QSignalSpy spy(&service, &OcrService::pageReady);

    service.enqueue(makeJob(7));
    service.enqueue(makeJob(7));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QTest::qWait(200);
    QCOMPARE(spy.count(), 1);
}

void OcrServiceTests::cancelDropsQueuedPages() {
    OcrService service;
    service.setEngine(std::make_unique<FakeEngine>(5));
    QSignalSpy spy(&service, &OcrService::pageReady);

    // 先取消、再入队：取消标记对之后入队的页同样生效。
    service.cancelAll();
    for (int page = 0; page < 3; ++page) {
        service.enqueue(makeJob(page));
    }

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 5000);
    for (const QList<QVariant>& call : spy) {
        const auto result = call.at(0).value<OcrPageResult>();
        QVERIFY2(result.cancelled, qPrintable(QString::number(result.pageIndex)));
    }
    QCOMPARE(service.pendingCount(), 0);
}

void OcrServiceTests::cancelInterruptsRunningRecognition() {
    // C-10 回归：正在跑的那一页必须真的被打断，而不是等它识别完再丢结果。
    // 假引擎在没收到 cancel() 时会一直卡着（上限 5 秒），所以这里能靠耗时区分。
    auto engine = std::make_unique<FakeEngine>(0, true);
    FakeEngine* raw = engine.get();
    OcrService service;
    service.setEngine(std::move(engine));
    QSignalSpy spy(&service, &OcrService::pageReady);

    service.enqueue(makeJob(3));
    QTRY_VERIFY_WITH_TIMEOUT(raw->started(), 3000);

    QElapsedTimer timer;
    timer.start();
    service.cancelAll();
    // 没修复时假引擎会卡满 5 秒才回来，这里 3 秒内没结果就说明没打断。
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 3000);
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(elapsed < 2000, qPrintable(QStringLiteral("取消后仍等了 %1ms，说明没打断").arg(elapsed)));
    const auto result = spy.at(0).at(0).value<OcrPageResult>();
    QVERIFY(result.cancelled);
    QCOMPARE(result.pageIndex, 3);
}

QTEST_MAIN(OcrServiceTests)
#include "OcrServiceTests.moc"
