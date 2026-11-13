// v0.12.0 数据策略与审计的单元测试。
//
// 这些用例守的是「发不发、发多少」这条规则本身：不依赖界面、不依赖网络，
// 所以能在毫秒级跑完，也就能在每次改动时都跑。
#include "ai/AuditLog.h"
#include "ai/DataPolicy.h"
#include "ai/ProviderSelection.h"
#include "ai/UploadPreview.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace aipdf::ai;

class DataPolicyTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void localModeNeverUsesCloud();
    void localModeRejectsEvenWithContent();
    void chunksAllowWithinLimits();
    void chunksRejectTooManyChunks();
    void chunksRejectTooManyChars();
    void chunksRejectEmptyContent();
    void pagesRequireExplicitSelection();
    void pagesAllowWithSelection();
    void modeIdsRoundTrip();
    void unknownModeFallsBackToLocal();

    void auditWritesMetadataOnly();
    void auditNeverContainsSecrets();

    void previewParsesHost();
    void previewDedupesAndSortsPages();
    void previewCarriesPolicyDecision();

    // Provider 选择：本地模式的机械保证，不依赖本机是否存了密钥。
    void localModeForcesDemoEvenWithCredential();
    void cloudOnlyWhenConfiguredAndCredentialPresent();

private:
    QTemporaryDir dataDir_;
};

void DataPolicyTests::initTestCase() {
    QVERIFY(dataDir_.isValid());
    // 审计写到临时目录，别污染真实用户数据。
    qputenv("AIPDF_DATA_DIR", dataDir_.path().toUtf8());
}

void DataPolicyTests::localModeNeverUsesCloud() {
    QVERIFY(!mayUseCloud(DataMode::LocalOnly));
    QVERIFY(mayUseCloud(DataMode::SelectedChunks));
    QVERIFY(mayUseCloud(DataMode::SelectedPages));
}

void DataPolicyTests::localModeRejectsEvenWithContent() {
    DataScope scope;
    scope.chunkCount = 1;
    scope.charCount = 100;
    const PolicyDecision decision = evaluate(DataMode::LocalOnly, scope);
    QVERIFY(!decision.allowed);
    QVERIFY(decision.reason.contains(QStringLiteral("本地")));
}

void DataPolicyTests::chunksAllowWithinLimits() {
    DataScope scope;
    scope.chunkCount = 5;
    scope.charCount = 3000;
    const PolicyDecision decision = evaluate(DataMode::SelectedChunks, scope);
    QVERIFY(decision.allowed);
    QVERIFY(decision.reason.isEmpty());
}

void DataPolicyTests::chunksRejectTooManyChunks() {
    DataScope scope;
    scope.chunkCount = kMaxChunksPerRequest + 1;
    scope.charCount = 1000;
    const PolicyDecision decision = evaluate(DataMode::SelectedChunks, scope);
    QVERIFY(!decision.allowed);
    QVERIFY(decision.reason.contains(QString::number(kMaxChunksPerRequest)));
}

void DataPolicyTests::chunksRejectTooManyChars() {
    DataScope scope;
    scope.chunkCount = 2;
    scope.charCount = kMaxCharsPerRequest + 1;
    const PolicyDecision decision = evaluate(DataMode::SelectedChunks, scope);
    QVERIFY(!decision.allowed);
    QVERIFY(decision.reason.contains(QString::number(kMaxCharsPerRequest)));
}

void DataPolicyTests::chunksRejectEmptyContent() {
    DataScope scope;
    scope.charCount = 0;
    const PolicyDecision decision = evaluate(DataMode::SelectedChunks, scope);
    QVERIFY(!decision.allowed);
    QVERIFY(decision.reason.contains(QStringLiteral("没有可发送")));
}

void DataPolicyTests::pagesRequireExplicitSelection() {
    DataScope scope;
    scope.chunkCount = 1;
    scope.charCount = 200;
    scope.hasExplicitPageSelection = false;
    const PolicyDecision decision = evaluate(DataMode::SelectedPages, scope);
    QVERIFY(!decision.allowed);
    QVERIFY(decision.reason.contains(QStringLiteral("勾选")));
}

void DataPolicyTests::pagesAllowWithSelection() {
    DataScope scope;
    scope.chunkCount = 1;
    scope.charCount = 200;
    scope.hasExplicitPageSelection = true;
    QVERIFY(evaluate(DataMode::SelectedPages, scope).allowed);
}

void DataPolicyTests::modeIdsRoundTrip() {
    for (const DataMode mode :
         {DataMode::LocalOnly, DataMode::SelectedChunks, DataMode::SelectedPages}) {
        bool ok = false;
        QCOMPARE(dataModeFromId(dataModeId(mode), &ok), mode);
        QVERIFY(ok);
    }
}

void DataPolicyTests::unknownModeFallsBackToLocal() {
    bool ok = true;
    QCOMPARE(dataModeFromId(QStringLiteral("chunkz"), &ok), DataMode::LocalOnly);
    QVERIFY(!ok);
    // 空串、大小写混乱也一律按最严格处理。
    QCOMPARE(dataModeFromId(QString(), nullptr), DataMode::LocalOnly);
    QCOMPARE(dataModeFromId(QStringLiteral("  CHUNKS  "), nullptr), DataMode::SelectedChunks);
}

void DataPolicyTests::auditWritesMetadataOnly() {
    AuditEntry entry;
    entry.time = QDateTime::fromString(QStringLiteral("2026-11-13T12:00:00Z"), Qt::ISODate);
    entry.mode = QStringLiteral("chunks");
    entry.task = QStringLiteral("qa");
    entry.chunkCount = 3;
    entry.charCount = 900;
    entry.host = QStringLiteral("api.openai.com");
    entry.allowed = true;
    QVERIFY(appendAudit(entry));

    QFile file(auditLogPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    file.close();

    QVERIFY(content.contains("\"mode\":\"chunks\""));
    QVERIFY(content.contains("\"host\":\"api.openai.com\""));
    QVERIFY(content.contains("\"chunks\":3"));
    // 审计里不该出现正文字段。
    QVERIFY(!content.contains("\"text\""));
    QVERIFY(!content.contains("\"content\""));
}

void DataPolicyTests::auditNeverContainsSecrets() {
    AuditEntry entry;
    entry.time = QDateTime::currentDateTimeUtc();
    entry.mode = QStringLiteral("local");
    entry.task = QStringLiteral("summary");
    QVERIFY(appendAudit(entry));

    QFile file(auditLogPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    file.close();
    QVERIFY(!content.contains("sk-"));
    QVERIFY(!QString::fromUtf8(content)
                 .contains(QStringLiteral("Authorization"), Qt::CaseInsensitive));
}

void DataPolicyTests::previewParsesHost() {
    QCOMPARE(hostFromBaseUrl(QStringLiteral("https://api.openai.com/v1")),
             QStringLiteral("api.openai.com"));
    QCOMPARE(hostFromBaseUrl(QStringLiteral("http://127.0.0.1:8080/v1")),
             QStringLiteral("127.0.0.1"));
    QVERIFY(hostFromBaseUrl(QStringLiteral("not a url")).isEmpty());
}

void DataPolicyTests::previewDedupesAndSortsPages() {
    ProviderConfig config;
    config.dataMode = QStringLiteral("chunks");
    DataScope scope;
    scope.chunkCount = 3;
    scope.charCount = 500;
    const UploadPreview preview =
        buildUploadPreview(config, scope, QVector<int>{5, 2, 5, 1});
    QCOMPARE(preview.pages, (QVector<int>{1, 2, 5}));
}

void DataPolicyTests::previewCarriesPolicyDecision() {
    ProviderConfig config;
    config.dataMode = QStringLiteral("chunks");
    DataScope small;
    small.chunkCount = 1;
    small.charCount = 100;
    QVERIFY(buildUploadPreview(config, small, {}).decision.allowed);

    config.dataMode = QStringLiteral("local");
    const UploadPreview blocked = buildUploadPreview(config, small, {});
    QVERIFY(!blocked.decision.allowed);
    QCOMPARE(blocked.mode, DataMode::LocalOnly);
}

void DataPolicyTests::localModeForcesDemoEvenWithCredential() {
    ProviderConfig config;
    config.providerId = QStringLiteral("openai");
    config.dataMode = QStringLiteral("local");
    // 关键：凭据存在也必须走演示。界面禁用按钮不算数，这里才是真的发不出去。
    QVERIFY(resolveProviderKind(config, true) == ProviderKind::Demo);
    QVERIFY(resolveProviderKind(config, false) == ProviderKind::Demo);
}

void DataPolicyTests::cloudOnlyWhenConfiguredAndCredentialPresent() {
    ProviderConfig config;
    config.providerId = QStringLiteral("openai");
    config.dataMode = QStringLiteral("chunks");
    QVERIFY(resolveProviderKind(config, true) == ProviderKind::OpenAi);
    // 没有密钥：演示模式接管，不能拿空密钥去联网。
    QVERIFY(resolveProviderKind(config, false) == ProviderKind::Demo);

    config.providerId = QStringLiteral("demo");
    QVERIFY(resolveProviderKind(config, true) == ProviderKind::Demo);

    // 坏模式回退到 local，也就回退到演示。
    config.providerId = QStringLiteral("openai");
    config.dataMode = QStringLiteral("chunkz");
    QVERIFY(resolveProviderKind(config, true) == ProviderKind::Demo);
}

QTEST_MAIN(DataPolicyTests)
#include "DataPolicyTests.moc"
