#include "ipc/IpcProtocol.h"

#include <QJsonDocument>
#include <QtTest>

class IpcProtocolTests final : public QObject {
    Q_OBJECT

private slots:
    void requestContainsStableEnvelope() {
        const QJsonObject request = aipdf::ipc::makeRequest(
            "request-1", "system.health", {{"sample", 7}});

        QCOMPARE(request.value("protocol_version").toInt(), aipdf::ipc::kProtocolVersion);
        QCOMPARE(request.value("request_id").toString(), QString("request-1"));
        QCOMPARE(request.value("operation").toString(), QString("system.health"));
        QCOMPARE(request.value("payload").toObject().value("sample").toInt(), 7);
    }

    void encodedFrameIsCompactJsonLine() {
        const QByteArray frame = aipdf::ipc::encodeFrame(
            aipdf::ipc::makeResult("request-2", {{"status", "ready"}}));

        QVERIFY(frame.endsWith('\n'));
        QCOMPARE(frame.count('\n'), 1);
        QJsonParseError error;
        const QJsonDocument parsed = QJsonDocument::fromJson(frame.trimmed(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(parsed.object().value("ok").toBool());
    }

    // v0.10.0：流式对话要靠事件帧承载增量。
    void eventFrameIsNotAFinalResult() {
        const QJsonObject event = aipdf::ipc::makeEvent(
            "request-3", "ai.stream.delta", {{"text", "增量"}});

        QCOMPARE(event.value("protocol_version").toInt(), aipdf::ipc::kProtocolVersion);
        QCOMPARE(event.value("request_id").toString(), QString("request-3"));
        QCOMPARE(event.value("type").toString(), QString("event"));
        QCOMPARE(event.value("event").toString(), QString("ai.stream.delta"));
        QCOMPARE(event.value("payload").toObject().value("text").toString(),
                 QString("增量"));
        // 事件帧不能自带 ok：客户端看到它就知道「请求还没结束」。
        QVERIFY(!event.contains("ok"));
    }

    void eventFrameEncodesAsOneLine() {
        const QByteArray frame = aipdf::ipc::encodeFrame(
            aipdf::ipc::makeEvent("request-4", "ai.stream.done"));

        QCOMPARE(frame.count('\n'), 1);
        QJsonParseError error;
        const QJsonDocument parsed = QJsonDocument::fromJson(frame.trimmed(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(parsed.object().value("type").toString(), QString("event"));
    }

    void protocolVersionIsTwo() {
        // v0.10.0 加了事件帧，旧客户端处理不了，版本号必须跟着涨。
        QCOMPARE(aipdf::ipc::kProtocolVersion, 2);
    }
};

QTEST_MAIN(IpcProtocolTests)
#include "IpcProtocolTests.moc"

