#include "ai/DataPolicy.h"

#include "i18n/Translate.h"

namespace aipdf::ai {

QString dataModeId(DataMode mode) {
    switch (mode) {
    case DataMode::LocalOnly:
        return QStringLiteral("local");
    case DataMode::SelectedChunks:
        return QStringLiteral("chunks");
    case DataMode::SelectedPages:
        return QStringLiteral("pages");
    }
    return QStringLiteral("local");
}

DataMode dataModeFromId(const QString& id, bool* ok) {
    const QString normalized = id.trimmed().toLower();
    if (ok != nullptr) {
        *ok = true;
    }
    if (normalized == QLatin1String("local")) {
        return DataMode::LocalOnly;
    }
    if (normalized == QLatin1String("chunks")) {
        return DataMode::SelectedChunks;
    }
    if (normalized == QLatin1String("pages")) {
        return DataMode::SelectedPages;
    }
    // 坏配置回退到最严格的模式：宁可少发，也不要因为一个拼写错误把文档发出去。
    if (ok != nullptr) {
        *ok = false;
    }
    return DataMode::LocalOnly;
}

bool mayUseCloud(DataMode mode) {
    return mode != DataMode::LocalOnly;
}

PolicyDecision evaluate(DataMode mode, const DataScope& scope, bool staysOnThisMachine) {
    PolicyDecision decision;

    // 「本地模式」拦的是「把文档内容发到本机之外」。
    // 终点就在本机的模型（Ollama 这类）内容根本没离开这台机器，
    // 这条禁令对它没有意义，拦了只会让用户选了本地模型却用不上。
    // 其余规则（规模上限、空内容）照旧生效。
    if (mode == DataMode::LocalOnly && !staysOnThisMachine) {
        decision.allowed = false;
        decision.reason =
            i18n::tr("aipdf::ai::DataPolicy", "本地模式：文档内容不会离开本机");
        return decision;
    }

    if (scope.charCount <= 0) {
        decision.allowed = false;
        decision.reason = i18n::tr("aipdf::ai::DataPolicy", "没有可发送的内容");
        return decision;
    }

    if (mode == DataMode::SelectedPages && !scope.hasExplicitPageSelection) {
        decision.allowed = false;
        decision.reason = i18n::tr("aipdf::ai::DataPolicy",
                                   "选定页面模式需要先勾选要发送的页面");
        return decision;
    }

    if (scope.chunkCount > kMaxChunksPerRequest) {
        decision.allowed = false;
        decision.reason =
            i18n::tr("aipdf::ai::DataPolicy", "一次最多发送 %1 块，本次 %2 块")
                .arg(kMaxChunksPerRequest)
                .arg(scope.chunkCount);
        return decision;
    }

    if (scope.charCount > kMaxCharsPerRequest) {
        decision.allowed = false;
        decision.reason =
            i18n::tr("aipdf::ai::DataPolicy", "一次最多发送 %1 字，本次 %2 字")
                .arg(kMaxCharsPerRequest)
                .arg(scope.charCount);
        return decision;
    }

    decision.allowed = true;
    return decision;
}

}  // namespace aipdf::ai
