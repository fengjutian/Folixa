#include "ipc/IpcProtocol.h"
#include "ipc/WorkerClient.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {

QString workerExecutablePath() {
    return QDir::fromNativeSeparators(QString::fromLocal8Bit(AIPDF_WORKER_EXE));
}

QString uniqueServerName(const QString& prefix) {
    return prefix + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool waitForServer(QLocalSocket& socket, const QString& serverName, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        socket.abort();
        socket.connectToServer(serverName);
        if (socket.waitForConnected(200)) {
            return true;
        }
        QTest::qWait(50);
    }
    return false;
}

QByteArray readFrame(QLocalSocket* socket, int timeoutMs) {
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        buffer.append(socket->readAll());
        const int newline = buffer.indexOf('\n');
        if (newline >= 0) {
            return buffer.left(newline);
        }
        if (!socket->waitForReadyRead(200)) {
            continue;
        }
    }
    return {};
}

bool startWorker(QProcess& worker, QLocalSocket& probe, const QString& serverName) {
    worker.start(workerExecutablePath(), {"--server-name", serverName});
    if (!worker.waitForStarted(5000)) {
        return false;
    }
    return waitForServer(probe, serverName, 8000);
}

}  // namespace

class WorkerIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void workerAnswersHealthCheck();
    void workerRejectsUnknownOperation();
    void workerRejectsProtocolMismatch();
    void missingWorkerExecutableIsReported();
    void requestTimeoutIsReported();
    void workerExitIsReported();
    void streamedChatEmitsEventsThenFinalResult();
    void streamedChatCanBeCancelled();
    void busyStreamIsReportedAsFailure();
};

void WorkerIntegrationTests::workerAnswersHealthCheck() {
    const QString serverName = uniqueServerName("aipdf-health");
    QProcess worker;
    QLocalSocket socket;
    QVERIFY2(startWorker(worker, socket, serverName), "worker did not accept a connection");

    const QString requestId = "it-health-1";
    socket.write(aipdf::ipc::encodeFrame(
        aipdf::ipc::makeRequest(requestId, "system.health")));
    QVERIFY(socket.waitForBytesWritten(2000));

    const QByteArray frame = readFrame(&socket, 5000);
    QVERIFY2(!frame.isEmpty(), "worker did not answer the health check");

    const QJsonObject response = QJsonDocument::fromJson(frame).object();
    QCOMPARE(response.value("type").toString(), QString("result"));
    QVERIFY(response.value("ok").toBool());
    QCOMPARE(response.value("protocol_version").toInt(), aipdf::ipc::kProtocolVersion);
    QCOMPARE(response.value("request_id").toString(), requestId);

    const QJsonObject payload = response.value("payload").toObject();
    QCOMPARE(payload.value("status").toString(), QString("ready"));
    QCOMPARE(payload.value("protocol_version").toInt(), aipdf::ipc::kProtocolVersion);
    QCOMPARE(payload.value("worker_version").toString(), QStringLiteral(AIPDF_VERSION));

    worker.kill();
    worker.waitForFinished(3000);
}

void WorkerIntegrationTests::workerRejectsUnknownOperation() {
    const QString serverName = uniqueServerName("aipdf-unknown");
    QProcess worker;
    QLocalSocket socket;
    QVERIFY2(startWorker(worker, socket, serverName), "worker did not accept a connection");

    socket.write(aipdf::ipc::encodeFrame(
        aipdf::ipc::makeRequest("it-unknown-1", "ai.not_available")));
    QVERIFY(socket.waitForBytesWritten(2000));

    const QByteArray frame = readFrame(&socket, 5000);
    QVERIFY2(!frame.isEmpty(), "worker did not answer the unknown operation");

    const QJsonObject response = QJsonDocument::fromJson(frame).object();
    QCOMPARE(response.value("type").toString(), QString("error"));
    QVERIFY(!response.value("ok").toBool());
    QCOMPARE(response.value("error").toObject().value("code").toString(),
             QString("operation_not_supported"));

    worker.kill();
    worker.waitForFinished(3000);
}

void WorkerIntegrationTests::workerRejectsProtocolMismatch() {
    const QString serverName = uniqueServerName("aipdf-protocol");
    QProcess worker;
    QLocalSocket socket;
    QVERIFY2(startWorker(worker, socket, serverName), "worker did not accept a connection");

    QJsonObject request = aipdf::ipc::makeRequest("it-protocol-1", "system.health");
    request.insert("protocol_version", aipdf::ipc::kProtocolVersion + 1);
    socket.write(aipdf::ipc::encodeFrame(request));
    QVERIFY(socket.waitForBytesWritten(2000));

    const QByteArray frame = readFrame(&socket, 5000);
    QVERIFY2(!frame.isEmpty(), "worker did not answer the mismatched request");

    const QJsonObject response = QJsonDocument::fromJson(frame).object();
    QCOMPARE(response.value("type").toString(), QString("error"));
    QCOMPARE(response.value("error").toObject().value("code").toString(),
             QString("protocol_version_unsupported"));

    worker.kill();
    worker.waitForFinished(3000);
}

void WorkerIntegrationTests::missingWorkerExecutableIsReported() {
    aipdf::ipc::WorkerClient client;
    QSignalSpy failures(&client, &aipdf::ipc::WorkerClient::requestFailed);
    QSignalSpy states(&client, &aipdf::ipc::WorkerClient::stateChanged);

    client.setWorkerExecutable(QDir::temp().filePath("aipdf-missing-worker"));
    client.start();

    QCOMPARE(failures.size(), 1);
    QVERIFY(failures.first().at(1).toString().contains("not found"));
    QCOMPARE(states.last().at(0).toString(), QString("unavailable"));
}

void WorkerIntegrationTests::requestTimeoutIsReported() {
    QLocalServer server;
    QVERIFY(server.listen(uniqueServerName("aipdf-silent")));

    // 接受连接但从不回应，用于触发请求超时路径。
    QLocalSocket* accepted = nullptr;
    connect(&server, &QLocalServer::newConnection, this, [&server, &accepted] {
        accepted = server.nextPendingConnection();
    });

    aipdf::ipc::WorkerClient client;
    QSignalSpy failures(&client, &aipdf::ipc::WorkerClient::requestFailed);
    QSignalSpy states(&client, &aipdf::ipc::WorkerClient::stateChanged);

    client.setAutoLaunchWorker(false);
    client.setServerName(server.serverName());
    client.start();

    QTRY_VERIFY_WITH_TIMEOUT(states.size() >= 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(states.last().at(0).toString(), QString("ready"), 5000);

    client.sendRequest("system.health", {}, 400);
    QTRY_COMPARE_WITH_TIMEOUT(failures.size(), 1, 5000);
    // 超时提示是直接显示给用户的，文案用中文。
    QVERIFY(failures.first().at(1).toString().contains(QStringLiteral("超时")));
}

void WorkerIntegrationTests::workerExitIsReported() {
    // 使用客户端自己拉起的 Worker 走真实产品路径：请求关闭后必须给出可见状态变化。
    aipdf::ipc::WorkerClient client;
    QSignalSpy responses(&client, &aipdf::ipc::WorkerClient::responseReceived);
    QSignalSpy failures(&client, &aipdf::ipc::WorkerClient::requestFailed);
    QSignalSpy states(&client, &aipdf::ipc::WorkerClient::stateChanged);

    client.setWorkerExecutable(workerExecutablePath());
    client.start();

    QTRY_VERIFY_WITH_TIMEOUT(states.size() >= 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(states.last().at(0).toString(), QString("ready"), 5000);

    client.sendRequest("system.shutdown");
    QTRY_COMPARE_WITH_TIMEOUT(responses.size(), 1, 5000);
    QVERIFY(responses.first().at(1).value<QJsonObject>().value("ok").toBool());

    // Worker 进程退出后，客户端必须切换到不可用状态（可见错误）。
    QTRY_VERIFY_WITH_TIMEOUT(
        states.size() >= 2 && states.last().at(0).toString() == "unavailable", 5000);
    QVERIFY(failures.size() <= 1);
}

// 流式对话的端到端路径：Worker 真的把增量一段段发回来。
//
// 两个前置条件不能省：
//   1. LOCALAPPDATA 指向临时目录——Worker 从这里读模型配置，
//      不指的话会读到用户自己配好的云端 Provider，测试就变成真联网了；
//   2. 演示模式的吐字速度调慢一点——取消要发生在流还没结束的时候。
namespace {

// 把 Worker 固定在演示模式（不联网）。
bool forceDemoMode(QTemporaryDir& sandbox, const char* delayMs) {
    if (!sandbox.isValid()) {
        return false;
    }
    QDir(sandbox.path()).mkpath(QStringLiteral("localappdata"));
    qputenv("LOCALAPPDATA",
            QDir(sandbox.path())
                .filePath(QStringLiteral("localappdata"))
                .toUtf8());
    qputenv("AIPDF_AI_MOCK_DELAY_MS", delayMs);
    return true;
}

QJsonObject summaryPayload() {
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", "你是一个 PDF 阅读助手。只依据给出的文本回答。"}});
    messages.append(QJsonObject{
        {"role", "user"},
        {"content", "以下是这一页的文本，请总结：\n\nFolixa 是一个 PDF 编辑器。"}});
    return QJsonObject{
        {"task", "summary"},
        {"data_scope", "第 1 页 · 20 字"},
        {"messages", messages}};
}

// 从 QSignalSpy 里找出最后一个指定名字的事件。
QJsonObject lastEvent(const QSignalSpy& events, const QString& name) {
    QJsonObject found;
    for (const QList<QVariant>& call : events) {
        if (call.size() < 3) {
            continue;
        }
        if (call.at(1).toString() == name) {
            found = call.at(2).toJsonObject();
        }
    }
    return found;
}

}  // namespace

void WorkerIntegrationTests::streamedChatEmitsEventsThenFinalResult() {
    QTemporaryDir sandbox;
    QVERIFY(forceDemoMode(sandbox, "10"));

    aipdf::ipc::WorkerClient client;
    QSignalSpy responses(&client, &aipdf::ipc::WorkerClient::responseReceived);
    QSignalSpy events(&client, &aipdf::ipc::WorkerClient::eventReceived);
    QSignalSpy states(&client, &aipdf::ipc::WorkerClient::stateChanged);

    client.setWorkerExecutable(workerExecutablePath());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(states.last().at(0).toString(), QString("ready"), 5000);

    const QString requestId = client.sendStreamRequest("ai.chat.stream", summaryPayload());
    QVERIFY(!requestId.isEmpty());
    QVERIFY(client.isStreamActive(requestId));

    // 「开始了」的回执：先来一个 result（status=streaming）。
    QTRY_VERIFY_WITH_TIMEOUT(responses.size() >= 1, 5000);
    const QJsonObject started = responses.first().at(1).value<QJsonObject>();
    QCOMPARE(started.value("payload").toObject().value("status").toString(),
             QString("streaming"));
    QCOMPARE(started.value("payload").toObject().value("demo").toBool(), true);

    // 增量事件：一段一段地来。
    // 先等结束事件：到这时候所有增量都到齐了，
    // 只等到第一个增量的话，拼出来的文本还断在词中间。
    QTRY_VERIFY_WITH_TIMEOUT(
        !lastEvent(events, QStringLiteral("ai.stream.done")).isEmpty(), 8000);

    QString text;
    for (const QList<QVariant>& call : events) {
        if (call.size() < 3 || call.at(1).toString() != QLatin1String("ai.stream.delta")) {
            continue;
        }
        text += call.at(2).toJsonObject().value("text").toString();
    }
    QVERIFY2(text.contains(QStringLiteral("演示模式")), qPrintable(text));

    // 结束事件的载荷带 token 用量。
    // lastEvent 拿到的是事件帧的 payload 本身。
    const QJsonObject done = lastEvent(events, QStringLiteral("ai.stream.done"));
    QVERIFY(done.contains("input_tokens"));
    QVERIFY(done.contains("output_tokens"));

    // 客户端要能在最终结果帧之后把这次流清掉：
    // 只发事件不发结果的话，activeStreams_ 会一直留着这个 id。
    QTRY_VERIFY_WITH_TIMEOUT(!client.isStreamActive(requestId), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(responses.size() >= 2, 5000);
    const QJsonObject finished = responses.last().at(1).value<QJsonObject>();
    QCOMPARE(finished.value("type").toString(), QString("result"));
    QCOMPARE(finished.value("payload").toObject().value("status").toString(),
             QString("finished"));
}

void WorkerIntegrationTests::streamedChatCanBeCancelled() {
    QTemporaryDir sandbox;
    // 慢一点吐字，取消才发生在流还没结束的时候。
    QVERIFY(forceDemoMode(sandbox, "40"));

    aipdf::ipc::WorkerClient client;
    QSignalSpy responses(&client, &aipdf::ipc::WorkerClient::responseReceived);
    QSignalSpy events(&client, &aipdf::ipc::WorkerClient::eventReceived);
    QSignalSpy states(&client, &aipdf::ipc::WorkerClient::stateChanged);

    client.setWorkerExecutable(workerExecutablePath());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(states.last().at(0).toString(), QString("ready"), 5000);

    const QString requestId = client.sendStreamRequest("ai.chat.stream", summaryPayload());
    QVERIFY(!requestId.isEmpty());

    // 等第一段增量到达：说明流已经跑起来了。
    QTRY_VERIFY_WITH_TIMEOUT(events.size() >= 1, 5000);
    client.cancelStreamRequest(requestId);

    QTRY_VERIFY_WITH_TIMEOUT(
        !lastEvent(events, QStringLiteral("ai.stream.cancelled")).isEmpty(), 5000);
    // 取消之后这次流也要收尾，不能一直挂着。
    QTRY_VERIFY_WITH_TIMEOUT(!client.isStreamActive(requestId), 5000);
}

void WorkerIntegrationTests::busyStreamIsReportedAsFailure() {
    QTemporaryDir sandbox;
    // 慢一点吐字，第二条请求才撞得上「上一次还没结束」。
    QVERIFY(forceDemoMode(sandbox, "60"));

    aipdf::ipc::WorkerClient client;
    QSignalSpy events(&client, &aipdf::ipc::WorkerClient::eventReceived);
    QSignalSpy failures(&client, &aipdf::ipc::WorkerClient::requestFailed);
    QSignalSpy states(&client, &aipdf::ipc::WorkerClient::stateChanged);

    client.setWorkerExecutable(workerExecutablePath());
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(states.last().at(0).toString(), QString("ready"), 5000);

    const QString first = client.sendStreamRequest("ai.chat.stream", summaryPayload());
    QVERIFY(!first.isEmpty());
    // 等第一条流真的跑起来：否则第二条可能排在第一条后面。
    QTRY_VERIFY_WITH_TIMEOUT(events.size() >= 1, 5000);

    client.sendStreamRequest("ai.chat.stream", summaryPayload());

    // 第二条撞上「忙」：Worker 回一个错误帧，客户端必须把它报成失败。
    // 以前错误帧只是被读掉了，界面收下了这个响应却什么都不显示。
    QTRY_VERIFY_WITH_TIMEOUT(failures.size() >= 1, 5000);
    const QString message = failures.first().at(1).toString();
    QVERIFY2(!message.isEmpty(), "失败没有带原因");
    QVERIFY2(message.contains(QStringLiteral("还没结束")), qPrintable(message));
}

QTEST_GUILESS_MAIN(WorkerIntegrationTests)
#include "WorkerIntegrationTests.moc"