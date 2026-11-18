// AI 服务商预设的单元测试。
//
// 这些用例守的是「服务商这件事只有一处说了算」：
// 下拉框、要不要密钥、地址指不指向本机、本地模式放不放手，
// 全都读同一张预设表（ai/settings/ProviderPreset.h）。
//
// 特别要盯住两件容易被人改坏的事：
// 1. 云端服务商的地址与模型不能退回过时的那一套（api.minimax.chat 已经没了）；
// 2. 本机模型（Ollama）既不要密钥，也不该被「本地模式」那条禁令拦下来。
#include "ai/settings/DataPolicy.h"
#include "ai/settings/ProviderConfig.h"
#include "ai/settings/ProviderPreset.h"
#include "ai/settings/ProviderSelection.h"
#include "ai/settings/UploadPreview.h"

#include <QSet>
#include <QUrl>
#include <QtTest>

using namespace aipdf::ai;

class ProviderPresetTests : public QObject {
    Q_OBJECT

private slots:
    void presetsHaveUniqueIds();
    void presetUrlsAreValid();
    void cloudPresetsRequireKey();
    void ollamaPresetIsLocalAndKeyless();
    void minimaxPresetUsesCurrentEndpoint();

    void loopbackDetection();
    void loopbackEndpointsNeedNoKey();
    void unknownProviderNeedsKeyAndFallsBackToDemo();

    void ollamaWithoutKeyUsesOpenAiProvider();
    void ollamaSurvivesLocalDataMode();
    void localModelIsNotBlockedByLocalMode();
    void uploadPreviewAllowsLocalModelInLocalMode();

    void validationExplainsMissingLocalModel();
    void validationRejectsUnknownProvider();
};

void ProviderPresetTests::presetsHaveUniqueIds() {
    const QVector<ProviderPreset> presets = providerPresets();
    QVERIFY(!presets.isEmpty());
    QSet<QString> ids;
    for (const ProviderPreset& preset : presets) {
        QVERIFY2(!preset.id.isEmpty(), "预设必须有 id");
        QVERIFY2(!ids.contains(preset.id), qPrintable(QStringLiteral("id 重复：%1").arg(preset.id)));
        ids.insert(preset.id);
    }
    // 界面上要能选到这几家。
    for (const QString& id : {QStringLiteral("minimax"), QStringLiteral("deepseek"),
                              QStringLiteral("qwen"), QStringLiteral("kimi"),
                              QStringLiteral("ollama")}) {
        QVERIFY2(ids.contains(id), qPrintable(QStringLiteral("缺预设：%1").arg(id)));
    }
}

void ProviderPresetTests::presetUrlsAreValid() {
    for (const ProviderPreset& preset : providerPresets()) {
        if (preset.baseUrl.isEmpty()) {
            // 演示模式与「自定义」本来就由用户填地址。
            continue;
        }
        const QUrl url(preset.baseUrl);
        QVERIFY2(url.isValid(), qPrintable(QStringLiteral("%1 地址非法").arg(preset.id)));
        const QString scheme = url.scheme().toLower();
        QVERIFY2(scheme == QLatin1String("http") || scheme == QLatin1String("https"),
                 qPrintable(QStringLiteral("%1 地址协议不对").arg(preset.id)));
        QVERIFY2(!url.host().isEmpty(),
                 qPrintable(QStringLiteral("%1 地址没有主机名").arg(preset.id)));
        // 除本机模型外，每家都得有默认模型名，否则开箱就发不出去。
        if (!preset.runsLocally) {
            QVERIFY2(!preset.model.isEmpty(),
                     qPrintable(QStringLiteral("%1 没有默认模型").arg(preset.id)));
        }
    }
}

void ProviderPresetTests::cloudPresetsRequireKey() {
    for (const QString& id : {QStringLiteral("minimax"), QStringLiteral("deepseek"),
                              QStringLiteral("qwen"), QStringLiteral("kimi"),
                              QStringLiteral("openai")}) {
        const std::optional<ProviderPreset> preset = findProviderPreset(id);
        QVERIFY(preset.has_value());
        QVERIFY2(preset->requiresApiKey, qPrintable(id));
        QVERIFY2(!preset->runsLocally, qPrintable(id));
        QVERIFY2(providerRequiresApiKey(id, preset->baseUrl), qPrintable(id));
    }
}

void ProviderPresetTests::ollamaPresetIsLocalAndKeyless() {
    const std::optional<ProviderPreset> preset = findProviderPreset(QStringLiteral("ollama"));
    QVERIFY(preset.has_value());
    QVERIFY(!preset->requiresApiKey);
    QVERIFY(preset->runsLocally);
    // 地址必须指向本机：这是「文档内容不出本机」的机械保证。
    QVERIFY(isLoopbackUrl(preset->baseUrl));
    // 本机模型不预置模型名：装了什么只有用户自己知道。
    QVERIFY(preset->model.isEmpty());
}

void ProviderPresetTests::minimaxPresetUsesCurrentEndpoint() {
    const std::optional<ProviderPreset> preset = findProviderPreset(QStringLiteral("minimax"));
    QVERIFY(preset.has_value());
    // api.minimax.chat 与 MiniMax-Text-01 都已经过时了，别退回去。
    QVERIFY(!preset->baseUrl.contains(QStringLiteral("minimax.chat")));
    QCOMPARE(preset->baseUrl, QStringLiteral("https://api.minimaxi.com/v1"));
    QCOMPARE(preset->model, QStringLiteral("MiniMax-M3.1-Flash"));
}

void ProviderPresetTests::loopbackDetection() {
    QVERIFY(isLoopbackUrl(QStringLiteral("http://127.0.0.1:11434/v1")));
    QVERIFY(isLoopbackUrl(QStringLiteral("http://localhost:11434/v1")));
    QVERIFY(isLoopbackUrl(QStringLiteral("http://[::1]:11434/v1")));
    QVERIFY(isLoopbackUrl(QStringLiteral("  http://127.0.0.1:11434/v1  ")));
    QVERIFY(!isLoopbackUrl(QStringLiteral("https://api.deepseek.com")));
    QVERIFY(!isLoopbackUrl(QStringLiteral("https://api.minimaxi.com/v1")));
    QVERIFY(!isLoopbackUrl(QString()));
}

void ProviderPresetTests::loopbackEndpointsNeedNoKey() {
    // 地址在本机 → 不用密钥。不管预设里写的是哪家。
    QVERIFY(!providerRequiresApiKey(QStringLiteral("openai"),
                                    QStringLiteral("http://127.0.0.1:8000/v1")));
    QVERIFY(!providerRequiresApiKey(QStringLiteral("deepseek"),
                                    QStringLiteral("http://localhost:1234/v1")));
    // 云端 → 要密钥。
    QVERIFY(providerRequiresApiKey(QStringLiteral("openai"),
                                   QStringLiteral("https://api.example.com/v1")));
}

void ProviderPresetTests::unknownProviderNeedsKeyAndFallsBackToDemo() {
    // 认不出来的服务商：按最严处理要密钥，并且绝不拿空地址去联网。
    QVERIFY(providerRequiresApiKey(QStringLiteral("no-such-provider"),
                                   QStringLiteral("https://api.example.com/v1")));
    ProviderConfig config;
    config.providerId = QStringLiteral("no-such-provider");
    config.baseUrl = QStringLiteral("https://api.example.com/v1");
    config.dataMode = QStringLiteral("chunks");
    QVERIFY(resolveProviderKind(config, true) == ProviderKind::Demo);
}

void ProviderPresetTests::ollamaWithoutKeyUsesOpenAiProvider() {
    ProviderConfig config;
    config.providerId = QStringLiteral("ollama");
    config.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    config.model = QStringLiteral("qwen2.5:7b");
    config.dataMode = QStringLiteral("chunks");
    // 没有密钥也照样能用：OpenAI 兼容层不需要 Authorization 头。
    QVERIFY(resolveProviderKind(config, false) == ProviderKind::OpenAi);
    QVERIFY(resolveProviderKind(config, true) == ProviderKind::OpenAi);
}

void ProviderPresetTests::ollamaSurvivesLocalDataMode() {
    ProviderConfig config;
    config.providerId = QStringLiteral("ollama");
    config.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    config.model = QStringLiteral("qwen2.5:7b");
    config.dataMode = QStringLiteral("local");
    // 内容根本没离开这台机器，「本地模式」不该把它拦下来。
    QVERIFY(resolveProviderKind(config, false) == ProviderKind::OpenAi);
}

void ProviderPresetTests::localModelIsNotBlockedByLocalMode() {
    DataScope scope;
    scope.chunkCount = 1;
    scope.charCount = 200;
    // 终点在本机：允许。
    QVERIFY(evaluate(DataMode::LocalOnly, scope, true).allowed);
    // 终点在云端：仍然拦（这条是机械保证，不能因为加了一个参数就松掉）。
    const PolicyDecision blocked = evaluate(DataMode::LocalOnly, scope, false);
    QVERIFY(!blocked.allowed);
    QVERIFY(blocked.reason.contains(QStringLiteral("本地")));
}

void ProviderPresetTests::uploadPreviewAllowsLocalModelInLocalMode() {
    ProviderConfig config;
    config.providerId = QStringLiteral("ollama");
    config.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    config.model = QStringLiteral("qwen2.5:7b");
    config.dataMode = QStringLiteral("local");
    DataScope scope;
    scope.chunkCount = 2;
    scope.charCount = 300;
    const UploadPreview preview = buildUploadPreview(config, scope, {1});
    QVERIFY(preview.decision.allowed);
    QVERIFY(preview.staysOnThisMachine);
    QCOMPARE(preview.host, QStringLiteral("127.0.0.1"));

    // 云端地址在本地模式下照样拦。
    config.providerId = QStringLiteral("deepseek");
    config.baseUrl = QStringLiteral("https://api.deepseek.com");
    const UploadPreview cloud = buildUploadPreview(config, scope, {1});
    QVERIFY(!cloud.decision.allowed);
    QVERIFY(!cloud.staysOnThisMachine);
}

void ProviderPresetTests::validationExplainsMissingLocalModel() {
    ProviderConfig config;
    config.providerId = QStringLiteral("ollama");
    config.baseUrl = QStringLiteral("http://127.0.0.1:11434/v1");
    config.model.clear();
    const QString error = ProviderConfig::validationError(config);
    QVERIFY(!error.isEmpty());
    // 提示要能指导下一步：告诉用户模型名从哪来。
    QVERIFY(error.contains(QStringLiteral("模型名")));
}

void ProviderPresetTests::validationRejectsUnknownProvider() {
    ProviderConfig config;
    config.providerId = QStringLiteral("no-such-provider");
    config.baseUrl = QStringLiteral("https://api.example.com/v1");
    const QString error = ProviderConfig::validationError(config);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("不认识")));
}

QTEST_MAIN(ProviderPresetTests)
#include "ProviderPresetTests.moc"
