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
};

QTEST_MAIN(IpcProtocolTests)
#include "IpcProtocolTests.moc"

