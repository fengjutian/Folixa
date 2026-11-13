#include "app/RenderThread.h"
#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QtTest>

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 等线程退出，但只给一段有限的时间。
// 死等会把整个测试套件挂住，超时判失败更省事：
// 症状（卡死）照样被记下来，后面的用例还能跑。
bool waitForFinish(QThread* thread, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!thread->isFinished() && timer.elapsed() < timeoutMs) {
        QTest::qWait(50);
    }
    return thread->isFinished();
}

}  // namespace

// 渲染线程的收工路径。
//
// 这里守的是一次真实事故：页面结构一变（插入 / 删除 / 旋转 / 移动页），
// 视图要换掉渲染线程。旧实现是「cancel() 之后 wait()」，
// 而 cancel 只作废请求、不叫醒线程——线程正睡在条件变量上，
// 主线程就永远等下去，整个界面卡死（CI 的界面验收撞上，界面点不动）。
//
// 页面操作的单元测试当时测不到：那里没有渲染线程。
class RenderThreadTests final : public QObject {
    Q_OBJECT

private slots:
    void stopLetsThreadFinish();
    void stopAfterCancelStillFinishes();
    void requestsAfterStopAreIgnored();
};

void RenderThreadTests::stopLetsThreadFinish() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));

    auto* thread = new aipdf::app::RenderThread(result.document);
    thread->start();
    thread->requestRange(0, 1, 1.0);

    // 给它一点时间真的进到渲染循环，别在还没开始时就断言。
    QTest::qWait(300);

    thread->stop();
    QVERIFY2(waitForFinish(thread), "stop() 之后渲染线程没有退出：主线程会一直等下去");
    thread->wait();
    delete thread;
}

void RenderThreadTests::stopAfterCancelStillFinishes() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));

    auto* thread = new aipdf::app::RenderThread(result.document);
    thread->start();
    thread->requestRange(0, 1, 1.0);
    QTest::qWait(200);

    // 作废请求之后还得能叫醒线程：作废不等于收工。
    thread->cancel();
    thread->stop();
    QVERIFY2(waitForFinish(thread), "cancel() 之后 stop() 没有把线程叫醒");
    thread->wait();
    delete thread;
}

void RenderThreadTests::requestsAfterStopAreIgnored() {
    const auto result = aipdf::pdf::openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY2(result.document.isValid(), qPrintable(result.errorMessage));

    auto* thread = new aipdf::app::RenderThread(result.document);
    thread->start();
    QTest::qWait(100);

    thread->stop();
    // 收工之后再来请求：不能把已经收工的线程重新叫活。
    thread->requestRange(0, 1, 1.0);
    QVERIFY2(waitForFinish(thread), "stop() 之后又来请求，线程没有退出");
    thread->wait();
    delete thread;
}

QTEST_MAIN(RenderThreadTests)
#include "RenderThreadTests.moc"
