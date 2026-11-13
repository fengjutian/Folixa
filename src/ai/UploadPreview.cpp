#include "ai/UploadPreview.h"

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
    preview.decision = evaluate(preview.mode, scope);

    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    preview.pages = pages;
    return preview;
}

}  // namespace aipdf::ai
