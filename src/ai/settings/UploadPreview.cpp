#include "ai/UploadPreview.h"

#include "ai/ProviderPreset.h"

#include <QUrl>

#include <algorithm>

namespace aipdf::ai {

QString hostFromBaseUrl(const QString& baseUrl) {
    const QUrl url(baseUrl.trimmed());
    return url.host();
}

UploadPreview buildUploadPreview(const ProviderConfig& config,
                                 const DataScope& scope,
                                 QVector<int> pages) {
    UploadPreview preview;
    preview.mode = config.effectiveDataMode();
    preview.host = hostFromBaseUrl(config.baseUrl);
    preview.chunkCount = scope.chunkCount;
    preview.charCount = scope.charCount;
    // 地址指向本机时内容不出本机，本地模式不该把它拦下来。
    preview.staysOnThisMachine = isLoopbackUrl(config.baseUrl);
    preview.decision = evaluate(preview.mode, scope, preview.staysOnThisMachine);

    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    preview.pages = pages;
    return preview;
}

}  // namespace aipdf::ai
