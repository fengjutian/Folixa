#include "ai/ChatTypes.h"
#include "ai/CredentialStore.h"
#include "ai/ProviderConfig.h"
#include "ai/SseParser.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace aipdf;
using namespace aipdf::ai;

// v0.10.0 的模型接入底座单测。
//
// 覆盖四块：
//   1. SSE 增量解析（流式最容易错的就是半行、半事件）；
//   2. 提示词与费用计算（范围约束不能被绕过，价格不能写死）；
//   3. 凭据管理器存取（密钥不落普通配置）；
//   4. 配置读写与校验。
//
// 关键前置：把 LOCALAPPDATA 指向临时目录，
// 否则测试会写到用户真实的 %LOCALAPPDATA%\Folixa\ai\config.json 上，
// 把用户自己的模型设置冲掉。
class AiProviderTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY2(sandbox_.isValid(), "临时目录建不出来");
        const QString root = QDir(sandbox_.path()).filePath(QStringLiteral("localappdata"));
        QVERIFY(QDir().mkpath(root));
        originalLocalAppData_ = qEnvironmentVariable("LOCALAPPDATA");
        qputenv("LOCALAPPDATA", root.toUtf8());
        // 凭据用的是专用名字，测完立刻删掉，不碰用户自己的密钥。
        QVERIFY(CredentialStore::remove(testTarget_));
    }

    void cleanupTestCase() {
        CredentialStore::remove(testTarget_);
        if (originalLocalAppData_.isEmpty()) {
            qunsetenv("LOCALAPPDATA");
        } else {
            qputenv("LOCALAPPDATA", originalLocalAppData_.toUtf8());
        }
    }

    // ------------------------------------------------------------ SSE ----

    void sseParsesSingleEvent() {
        SseParser parser;
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray("data: hello\n\n"));

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("hello"));
        QVERIFY(events.first().event.isEmpty());
    }

    void sseKeepsHalfLineUntilComplete() {
        SseParser parser;
        QVERIFY(parser.feed(QByteArray("data: hel")).isEmpty());
        QVERIFY(parser.hasPending());
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray("lo\n\n"));

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("hello"));
        QVERIFY(!parser.hasPending());
    }

    void sseIgnoresCommentLines() {
        SseParser parser;
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray(": keep-alive\ndata: x\n\n"));

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("x"));
    }

    void sseJoinsMultipleDataLines() {
        SseParser parser;
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray("data: a\ndata: b\n\n"));

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("a\nb"));
    }

    void sseReadsEventName() {
        SseParser parser;
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray("event: delta\ndata: x\n\n"));

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().event, QStringLiteral("delta"));
        QCOMPARE(events.first().data, QStringLiteral("x"));
    }

    void sseHandlesCarriageReturnLineFeed() {
        SseParser parser;
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray("data: x\r\n\r\n"));

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().data, QStringLiteral("x"));
    }

    void sseStripsOnlyOneLeadingSpace() {
        SseParser parser;
        const QVector<SseParser::Event> events =
            parser.feed(QByteArray("data:  x\n\n"));

        QCOMPARE(events.size(), 1);
        // 标准只去掉冒号后的一个空格：多出来的那个要留着。
        QCOMPARE(events.first().data, QStringLiteral(" x"));
    }

    void sseFlushEmitsTrailingEvent() {
        SseParser parser;
        QVERIFY(parser.feed(QByteArray("data: tail\n")).isEmpty());

        const SseParser::Event flushed = parser.flush();
        QCOMPARE(flushed.data, QStringLiteral("tail"));

        // 第二次 flush 应该是空的：不能重复吐同一段。
        const SseParser::Event again = parser.flush();
        QVERIFY(again.data.isEmpty());
    }

    void sseResetDropsPendingEvent() {
        SseParser parser;
        parser.feed(QByteArray("data: half"));
        QVERIFY(parser.hasPending());

        parser.reset();
        QVERIFY(!parser.hasPending());
        QVERIFY(parser.feed(QByteArray("\n\n")).isEmpty());
    }

    void sseFeedsManyEventsInOneChunk() {
        SseParser parser;
        const QVector<SseParser::Event> events = parser.feed(
            QByteArray("data: one\n\ndata: two\n\ndata: three\n\n"));

        QCOMPARE(events.size(), 3);
        QCOMPARE(events.at(0).data, QStringLiteral("one"));
        QCOMPARE(events.at(1).data, QStringLiteral("two"));
        QCOMPARE(events.at(2).data, QStringLiteral("three"));
    }

    // -------------------------------------------------------- 提示词 ----

    void systemPromptAlwaysConstrainsScope() {
        const QStringList tasks = {QString::fromLatin1(Task::Summary),
                                   QString::fromLatin1(Task::Explain),
                                   QString::fromLatin1(Task::Translate),
                                   QString::fromLatin1(Task::Chat)};
        for (const QString& task : tasks) {
            const QString prompt = systemPromptForTask(task);
            QVERIFY2(!prompt.isEmpty(), qPrintable(task));
            // 范围约束是这一版的底线：提示词必须要求「只依据给出的材料」。
            QVERIFY2(prompt.contains(QStringLiteral("只依据")),
                     qPrintable(task + QLatin1String(" → ") + prompt));
        }
    }

    void systemPromptFallsBackForUnknownTask() {
        const QString prompt = systemPromptForTask(QStringLiteral("unknown-task"));
        QVERIFY(!prompt.isEmpty());
        QVERIFY(prompt.contains(QStringLiteral("只依据")));
    }

    void userPromptCarriesMaterialAndQuestion() {
        const QString prompt = userPromptForTask(
            QString::fromLatin1(Task::Chat),
            QStringLiteral("Folixa 是一个 PDF 编辑器"),
            QStringLiteral("这个编辑器做什么的？"));

        QVERIFY(prompt.contains(QStringLiteral("Folixa 是一个 PDF 编辑器")));
        QVERIFY(prompt.contains(QStringLiteral("这个编辑器做什么的？")));
    }

    void userPromptWithoutQuestionStillCarriesMaterial() {
        const QString prompt = userPromptForTask(
            QString::fromLatin1(Task::Summary),
            QStringLiteral("页面正文内容"),
            QString());

        QVERIFY(prompt.contains(QStringLiteral("页面正文内容")));
    }

    void roleNamesFollowProtocol() {
        QCOMPARE(ChatMessage::system(QStringLiteral("s")).roleName(),
                 QStringLiteral("system"));
        QCOMPARE(ChatMessage::user(QStringLiteral("u")).roleName(),
                 QStringLiteral("user"));
        QCOMPARE(ChatMessage::assistant(QStringLiteral("a")).roleName(),
                 QStringLiteral("assistant"));
    }

    // ---------------------------------------------------------- 费用 ----

    void costUsesConfiguredPrices() {
        TokenUsage usage;
        usage.inputTokens = 1000;
        usage.outputTokens = 500;

        // 输入 1000 tokens × $0.15/百万 = $0.00015
        // 输出  500 tokens × $0.60/百万 = $0.00030
        const double cost = usage.estimatedCost(0.15, 0.60);
        QVERIFY(std::abs(cost - 0.00045) < 1e-9);
        QCOMPARE(usage.totalTokens(), 1500);
    }

    void costIsZeroWithoutPrices() {
        TokenUsage usage;
        usage.inputTokens = 1000;
        usage.outputTokens = 500;

        QCOMPARE(usage.estimatedCost(0.0, 0.0), 0.0);
    }

    // ---------------------------------------------------------- 凭据 ----

    void credentialTargetNamesAreDistinctPerProvider() {
        const QString openAi = CredentialStore::targetNameFor(
            QStringLiteral("openai"));
        const QString demo = CredentialStore::targetNameFor(QStringLiteral("demo"));

        QVERIFY(openAi != demo);
        QVERIFY(openAi.contains(QStringLiteral("openai")));
        // 名字本身不能带秘密，只能带标识。
        QVERIFY(!openAi.contains(QStringLiteral("sk-")));
    }

    void credentialRoundTripsThroughStore() {
        const QString secret = QStringLiteral("sk-selftest-0123456789");

        QVERIFY(CredentialStore::store(testTarget_, secret));
        QVERIFY(CredentialStore::exists(testTarget_));
        QCOMPARE(CredentialStore::load(testTarget_), secret);

        QVERIFY(CredentialStore::remove(testTarget_));
        QVERIFY(!CredentialStore::exists(testTarget_));
        QVERIFY(CredentialStore::load(testTarget_).isEmpty());
    }

    void loadingMissingCredentialReturnsEmpty() {
        QVERIFY(CredentialStore::load(
            QStringLiteral("Folixa/test/never-stored")).isEmpty());
        QVERIFY(!CredentialStore::exists(
            QStringLiteral("Folixa/test/never-stored")));
    }

    void storingUnderEmptyNameFails() {
        QString error;
        QVERIFY(!CredentialStore::store(QString(), QStringLiteral("x"), &error));
        QVERIFY(!error.isEmpty());
    }

    // ---------------------------------------------------------- 配置 ----

    void configRoundTripsThroughFile() {
        ProviderConfig config;
        config.providerId = QStringLiteral("openai");
        config.baseUrl = QStringLiteral("https://example.invalid/v1");
        config.model = QStringLiteral("test-model");
        config.inputPricePerMillion = 0.15;
        config.outputPricePerMillion = 0.60;
        config.timeoutMs = 45'000;

        QVERIFY(config.save());

        const ProviderConfig loaded = ProviderConfig::load();
        QCOMPARE(loaded.providerId, config.providerId);
        QCOMPARE(loaded.baseUrl, config.baseUrl);
        QCOMPARE(loaded.model, config.model);
        QCOMPARE(loaded.inputPricePerMillion, config.inputPricePerMillion);
        QCOMPARE(loaded.outputPricePerMillion, config.outputPricePerMillion);
        QCOMPARE(loaded.timeoutMs, config.timeoutMs);
    }

    void missingConfigFileFallsBackToDefaults() {
        QVERIFY(QFile::remove(ProviderConfig::configFilePath()));

        const ProviderConfig loaded = ProviderConfig::load();
        QCOMPARE(loaded.providerId, QStringLiteral("demo"));
        QCOMPARE(loaded.model, QStringLiteral("gpt-4o-mini"));
    }

    void configFileNeverContainsTheSecret() {
        const QString secret = QStringLiteral("sk-selftest-must-not-appear");
        QVERIFY(CredentialStore::store(testTarget_, secret));

        ProviderConfig config;
        config.providerId = QStringLiteral("openai");
        config.baseUrl = QStringLiteral("https://example.invalid/v1");
        config.model = QStringLiteral("test-model");
        QVERIFY(config.save());

        QFile file(ProviderConfig::configFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        QVERIFY2(!content.contains(secret),
                 "配置文件里出现了密钥——秘密必须只待在凭据管理器里");
        QVERIFY(!content.contains(QStringLiteral("apiKey")));
    }

    void hasCredentialFollowsTheStore() {
        ProviderConfig config;
        config.providerId = QStringLiteral("openai");
        QVERIFY(!config.hasCredential());

        QVERIFY(CredentialStore::store(
            CredentialStore::targetNameFor(QStringLiteral("openai")),
            QStringLiteral("sk-selftest-has-credential")));
        QVERIFY(config.hasCredential());

        QVERIFY(CredentialStore::remove(
            CredentialStore::targetNameFor(QStringLiteral("openai"))));
        QVERIFY(!config.hasCredential());
    }

    void validationAcceptsDemoMode() {
        ProviderConfig config;
        config.providerId = QStringLiteral("demo");
        config.baseUrl.clear();
        config.model.clear();

        QVERIFY(ProviderConfig::validationError(config).isEmpty());
    }

    void validationRejectsBrokenCloudConfig() {
        ProviderConfig config;
        config.providerId = QStringLiteral("openai");

        config.baseUrl.clear();
        QVERIFY(!ProviderConfig::validationError(config).isEmpty());

        config.baseUrl = QStringLiteral("ftp://example.invalid/v1");
        QVERIFY(!ProviderConfig::validationError(config).isEmpty());

        config.baseUrl = QStringLiteral("not a url");
        QVERIFY(!ProviderConfig::validationError(config).isEmpty());

        config.baseUrl = QStringLiteral("https://example.invalid/v1");
        config.model.clear();
        QVERIFY(!ProviderConfig::validationError(config).isEmpty());

        config.model = QStringLiteral("gpt-4o-mini");
        config.timeoutMs = 10;
        QVERIFY(!ProviderConfig::validationError(config).isEmpty());

        config.timeoutMs = 60'000;
        QVERIFY(ProviderConfig::validationError(config).isEmpty());
    }

private:
    QTemporaryDir sandbox_;
    QString originalLocalAppData_;
    const QString testTarget_ = QStringLiteral("Folixa/test/ai-selftest");
};

QTEST_MAIN(AiProviderTests)
#include "AiProviderTests.moc"
