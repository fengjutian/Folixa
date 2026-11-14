// v0.13.0：OCR 服务（后台线程 + 队列 + 取消）的单元测试。
//
// 用假引擎把「什么时候回来」变成确定的：真引擎一页几百毫秒，
// 拿它测取消只会测出机器快慢。
#include "ocr/OcrService.h"

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
    explicit FakeEngine(int delayMs = 0) : delayMs_(delayMs) {}

    QString id() const override { return QStringLiteral("fake"); }
    bool isAvailable() const override { return true; }

    OcrPageResult recognize(const QImage& image, int pageIndex) override {
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

private:
    int delayMs_ = 0;
    std::atomic<int> calls_{0};
    std::atomic<bool> cancelled_{false};
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

QTEST_MAIN(OcrServiceTests)
#include "OcrServiceTests.moc"
