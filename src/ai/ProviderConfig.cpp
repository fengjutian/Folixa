#include "ai/ProviderConfig.h"

#include "ai/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace aipdf::ai {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyProvider = "provider";
constexpr auto kKeyBaseUrl = "baseUrl";
constexpr auto kKeyModel = "model";
constexpr auto kKeyInputPrice = "inputPricePerMillion";
constexpr auto kKeyOutputPrice = "outputPricePerMillion";
constexpr auto kKeyTimeoutMs = "timeoutMs";
constexpr int kFormatVersion = 1;

}  // namespace

QString ProviderConfig::sharedDataDirectory() {
    // 主程序与 Worker 都要找到同一个目录，所以不能带应用名。
#ifdef Q_OS_WIN
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        return QDir(localAppData).filePath(QStringLiteral("Folixa"));
    }
#endif
    const QString generic =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(generic).filePath(QStringLiteral("Folixa"));
}

QString ProviderConfig::configFilePath() {
    return QDir(sharedDataDirectory()).filePath(QStringLiteral("ai/config.json"));
}

ProviderConfig ProviderConfig::load() {
    ProviderConfig config;
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return config;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return config;
    }
    const QJsonObject root = document.object();
    if (root.value(QString::fromLatin1(kKeyVersion)).toInt() != kFormatVersion) {
        return config;
    }
    config.providerId = root.value(QString::fromLatin1(kKeyProvider)).toString(config.providerId);
    config.baseUrl = root.value(QString::fromLatin1(kKeyBaseUrl)).toString(config.baseUrl);
    config.model = root.value(QString::fromLatin1(kKeyModel)).toString(config.model);
    config.inputPricePerMillion =
        root.value(QString::fromLatin1(kKeyInputPrice)).toDouble(config.inputPricePerMillion);
    config.outputPricePerMillion =
        root.value(QString::fromLatin1(kKeyOutputPrice)).toDouble(config.outputPricePerMillion);
    config.timeoutMs = root.value(QString::fromLatin1(kKeyTimeoutMs)).toInt(config.timeoutMs);
    return config;
}

bool ProviderConfig::save() const {
    const QString path = configFilePath();
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        return false;
    }
    QJsonObject root;
    root.insert(QString::fromLatin1(kKeyVersion), kFormatVersion);
    root.insert(QString::fromLatin1(kKeyProvider), providerId);
    root.insert(QString::fromLatin1(kKeyBaseUrl), baseUrl);
    root.insert(QString::fromLatin1(kKeyModel), model);
    root.insert(QString::fromLatin1(kKeyInputPrice), inputPricePerMillion);
    root.insert(QString::fromLatin1(kKeyOutputPrice), outputPricePerMillion);
    root.insert(QString::fromLatin1(kKeyTimeoutMs), timeoutMs);

    // 先写临时文件再替换：写到一半崩了，旧的配置还在。
    const QString temporaryPath = path + QStringLiteral(".tmp");
    QFile file(temporaryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        file.close();
        QFile::remove(temporaryPath);
        return false;
    }
    file.close();
    if (!QFile::remove(path) && QFile::exists(path)) {
        QFile::remove(temporaryPath);
        return false;
    }
    return QFile::rename(temporaryPath, path);
}

bool ProviderConfig::hasCredential() const {
    return CredentialStore::exists(CredentialStore::targetNameFor(providerId));
}

QString ProviderConfig::validationError(const ProviderConfig& config) {
    if (config.providerId.trimmed().isEmpty()) {
        return QStringLiteral("没有选服务商");
    }
    // 演示模式不联网，地址、模型、密钥都不需要。
    if (config.providerId == QLatin1String("demo")) {
        return QString();
    }
    const QString url = config.baseUrl.trimmed();
    if (url.isEmpty()) {
        return QStringLiteral("服务地址不能为空");
    }
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() ||
        (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        return QStringLiteral("服务地址必须以 http:// 或 https:// 开头");
    }
    if (parsed.host().isEmpty()) {
        return QStringLiteral("服务地址里没有主机名");
    }
    if (config.model.trimmed().isEmpty()) {
        return QStringLiteral("模型名不能为空");
    }
    if (config.timeoutMs < 1000) {
        return QStringLiteral("超时时间不能小于 1 秒");
    }
    if (config.inputPricePerMillion < 0.0 || config.outputPricePerMillion < 0.0) {
        return QStringLiteral("单价不能是负数");
    }
    return QString();
}

}  // namespace aipdf::ai
