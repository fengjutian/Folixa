// 本机模型（Ollama 这类）的端到端请求链路测试。
//
// 本机没装 Ollama 时，光看代码说「支持」是没有说服力的：
// 真正要证明的是**我们发出去的请求长什么样**，以及**回来的流能不能读懂**。
// 所以这里起一个假的 OpenAI 兼容服务器（就在 127.0.0.1 上），
// 让程序按真实路径把请求打出去，再逐项核对：
//
// - 路径是 /v1/chat/completions（少一个斜杠就 404）；
// - body 里 model / messages / stream / stream_options 都在；
// - **没有 Authorization 头**：本机模型不需要密钥，多发一个头只是把本机的东西泄露出去；
// - SSE 分片能读成完整回答，token 用量能读出来（费用行靠它）。
//
// 这条链路（OpenAiProvider）与云端服务商是同一套实现，
// 所以本机模型能用，就说明云端那几家走的也是同一条路。
#include "ai/chat/ChatTypes.h"
#include "ai/settings/ProviderConfig.h"
#include "ai/provider/ProviderFactory.h"
#include "ai/settings/ProviderPreset.h"

#include <QByteArray>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <memory>

using namespace aipdf::ai;

namespace {

// 一个只会说 OpenAI 兼容流式话术的假服务器。
class FakeOpenAiServer : public QTcpServer {
public:
    // 收到的那次请求（原始字节）。
    QByteArray request;
    // 回给客户端的 SSE 内容（行尾用 CRLF，和真服务器一样）。
    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Connection: close\r\n"
        "\r\n"
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"你好\"}}]}\r\n"
        "\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"，本机模型\"}}],"
        "\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":5}}\r\n"
        "\r\n"
        "data: [DONE]\r\n"
        "\r\n";

protected:
    void incomingConnection(qintptr socketDescriptor) override {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            request += socket->readAll();
            // 收齐 header + body（Content-Length 到了才回）。
            const int headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const QByteArray header = request.left(headerEnd);
            const int length = header.contains("Content-Length:")
                                   ? header.mid(header.indexOf("Content-Length:") + 15)
                                         .split('\r')[0].trimmed().toInt()
                                   : 0;
            if (request.size() - (headerEnd + 4) < length) {
                return;
            }
            socket->write(response);
            socket->flush();
            // 等数据真的写出去了再断开：提前关会把响应截断。
            // 计数器用 shared_ptr 带着走：lambda 比这个栈帧活得久。
            auto written = std::make_shared<qint64>(0);
            const qint64 expected = response.size();
            connect(socket, &QTcpSocket::bytesWritten, this, [socket, written, expected](qint64 count) {
                *written += count;
                if (*written >= expected) {
                    socket->disconnectFromHost();
                }
            });
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};

}  // namespace

class OllamaProviderTests : public QObject {
    Q_OBJECT

private slots:
    void localModelRequestNeedsNoKeyAndReachesTheEndpoint();
    void localModelWithoutKeyIsNotBlockedByLocalMode();
};

void OllamaProviderTests::localModelRequestNeedsNoKeyAndReachesTheEndpoint() {
    FakeOpenAiServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();

    ProviderConfig config;
    config.providerId = QStringLiteral("ollama");
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    config.model = QStringLiteral("qwen2.5:7b");
    config.dataMode = QStringLiteral("chunks");
    config.timeoutMs = 5000;

    // 走工厂这条真实路径：没有密钥也必须造出真 Provider，而不是演示模型。
    const std::unique_ptr<ModelProvider> provider = createProvider(config);
    QVERIFY(provider != nullptr);
    QVERIFY(provider->isConfigured());

    ChatRequest request;
    request.task = Task::Chat;
    request.messages = {ChatMessage::user(QStringLiteral("你好"))};

    QSignalSpy deltas(provider.get(), &ModelProvider::streamDelta);
    QSignalSpy finished(provider.get(), &ModelProvider::streamFinished);
    QSignalSpy failed(provider.get(), &ModelProvider::streamFailed);

    provider->startChat(request);
    QVERIFY2(finished.wait(10000), "模型流没有结束");
    QCOMPARE(failed.count(), 0);

    // 请求确实打到了 /v1/chat/completions。
    const QByteArray raw = server.request;
    QVERIFY2(raw.startsWith("POST /v1/chat/completions"),
             qPrintable(QString::fromLatin1(raw.left(120))));
    QVERIFY2(raw.contains("\"model\":\"qwen2.5:7b\""), qPrintable(QString::fromLatin1(raw)));
    QVERIFY2(raw.contains("\"stream\":true"), qPrintable(QString::fromLatin1(raw)));
    // Ollama 的兼容层支持 stream_options.include_usage，费用行靠它。
    QVERIFY2(raw.contains("\"include_usage\":true"), qPrintable(QString::fromLatin1(raw)));
    // 本机模型不需要密钥：不发 Authorization 头。
    QVERIFY2(!raw.toLower().contains("authorization:"),
             qPrintable(QString::fromLatin1(raw.left(400))));

    // 回答被读成完整文本，用量也读到了。
    QString answer;
    for (const QList<QVariant>& call : deltas) {
        answer += call.at(0).toString();
    }
    QCOMPARE(answer, QStringLiteral("你好，本机模型"));
    QCOMPARE(finished.at(0).at(1).toInt(), 12);   // input tokens
    QCOMPARE(finished.at(0).at(2).toInt(), 5);    // output tokens
}

void OllamaProviderTests::localModelWithoutKeyIsNotBlockedByLocalMode() {
    // 本机模型 + 本地模式：既不要密钥，也不该被「内容不许出本机」拦下来。
    ProviderConfig config;
    config.providerId = QStringLiteral("ollama");
    config.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    config.model = QStringLiteral("qwen2.5:7b");
    config.dataMode = QStringLiteral("local");
    QVERIFY(resolveProviderKind(config, false) == ProviderKind::OpenAi);
    QVERIFY(!providerRequiresApiKey(config.providerId, config.baseUrl));
}

QTEST_MAIN(OllamaProviderTests)
#include "OllamaProviderTests.moc"
