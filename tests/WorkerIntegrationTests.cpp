#include "ipc/IpcProtocol.h"
#include "ipc/WorkerClient.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
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
    QVERIFY(failures.first().at(1).toString().contains("timed out"));
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

QTEST_GUILESS_MAIN(WorkerIntegrationTests)
#include "WorkerIntegrationTests.moc"
