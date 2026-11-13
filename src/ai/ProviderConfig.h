#pragma once

#include "ai/DataPolicy.h"

#include <QString>

namespace aipdf::ai {

// 模型服务配置。
//
// 这个文件里**没有秘密**：地址、模型名、单价、超时都是普通配置。
// 密钥单独放在 Windows 凭据管理器里（见 CredentialStore），
// 配置文件里只存「去哪个名字下取密钥」。
//
// 配置放在主程序与 Worker 都能找到的地方：主程序改设置，Worker 读设置。
// 注意不能用 QStandardPaths::AppLocalDataLocation：那个路径带应用名，
// 主程序叫 AiPdfEditor、Worker 叫 PdfAiWorker，两个进程会找到不同目录。
struct ProviderConfig {
    // 服务商标识："demo"（演示）或 "openai"（OpenAI 兼容）。
    QString providerId = QStringLiteral("demo");
    // 服务地址（到 /v1 为止）。
    QString baseUrl = QStringLiteral("https://api.openai.com/v1");
    // 模型名。
    QString model = QStringLiteral("gpt-4o-mini");
    // 单价（美元 / 每百万 token）。服务商改价时改这里，不改代码。
    double inputPricePerMillion = 0.0;
    double outputPricePerMillion = 0.0;
    // 单次请求超时（毫秒）。
    int timeoutMs = 60'000;

    // 数据策略（v0.12.0）：local / chunks / pages。
    // 默认 chunks，保持 v0.11.0 的行为；坏值由 dataModeFromId 回退到 local。
    QString dataMode = QStringLiteral("chunks");

    // 解析后的模式。配置里写了不认识的值时返回 LocalOnly。
    DataMode effectiveDataMode() const;

    // 配置文件路径。
    static QString configFilePath();
    // 主程序与 Worker 共用的数据目录。
    static QString sharedDataDirectory();

    // 读配置。文件不存在时返回默认配置（不是错误）。
    static ProviderConfig load();
    // 写配置。失败时返回 false。
    bool save() const;

    // 配置里指定的服务商是不是已经存过密钥。
    bool hasCredential() const;

    // 配置能不能用。返回空串表示可以，否则返回给用户看的原因。
    //
    // 放在这里而不是界面里：Worker 读配置时要用同一条判断，
    // 而且它不依赖界面，可以直接写单元测试。
    static QString validationError(const ProviderConfig& config);
};

}  // namespace aipdf::ai
