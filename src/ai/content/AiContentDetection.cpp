#include "ai/AiContentDetection.h"

#include "ai/ChatTypes.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>

namespace aipdf::ai {

namespace {

// 判定档位：这四个字符串是界面上直接显示的中文文案。
const QString kCategoryHuman = QStringLiteral("完全人工");
const QString kCategoryGenerated = QStringLiteral("疑似 AI 生成");
const QString kCategoryRewritten = QStringLiteral("疑似 AI 改写");
const QString kCategoryUnknown = QStringLiteral("无法判断");

// 概率分档的门槛。
constexpr double kGeneratedThreshold = 0.75;
constexpr double kSuspectThreshold = 0.50;

// 读一个数值字段。
// 模型有时把数字写成字符串（"0.82"），两种都认：判不出来就当这个字段不存在，
// 不能因为类型不对就把一次本来能用的检测判成失败。
bool readNumber(const QJsonObject& object, const QString& key, double* value) {
    if (!object.contains(key)) {
        return false;
    }
    const QJsonValue item = object.value(key);
    if (item.isDouble()) {
        *value = item.toDouble();
        return true;
    }
    if (item.isString()) {
        bool ok = false;
        const double parsed = item.toString().trimmed().toDouble(&ok);
        if (ok) {
            *value = parsed;
            return true;
        }
    }
    return false;
}

// 读一个布尔字段（同样兼容 "true" / "false" 字符串）。
bool readBool(const QJsonObject& object, const QString& key, bool* value) {
    if (!object.contains(key)) {
        return false;
    }
    const QJsonValue item = object.value(key);
    if (item.isBool()) {
        *value = item.toBool();
        return true;
    }
    if (item.isString()) {
        const QString text = item.toString().trimmed().toLower();
        if (text == QLatin1String("true")) {
            *value = true;
            return true;
        }
        if (text == QLatin1String("false")) {
            *value = false;
            return true;
        }
    }
    return false;
}

// 读理由。模型有时用 explanation 而不是 reason，两个都认。
QString readReason(const QJsonObject& object) {
    const QStringList keys{QStringLiteral("reason"), QStringLiteral("explanation")};
    for (const QString& key : keys) {
        if (!object.contains(key)) {
            continue;
        }
        const QString text = object.value(key).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return QString();
}

// 把一段字节当 JSON 对象解析。
bool tryParseObject(const QByteArray& bytes, QJsonObject* object) {
    if (bytes.isEmpty()) {
        return false;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    *object = document.object();
    return true;
}

// 剥掉 ```json ... ``` 代码块。
// 模型很爱把 JSON 包在代码块里，直接当 JSON 解析必然失败。
QByteArray stripCodeFence(const QByteArray& bytes) {
    QByteArray text = bytes.trimmed();
    if (!text.startsWith("```")) {
        return text;
    }
    const int firstNewline = text.indexOf('\n');
    if (firstNewline < 0) {
        return text;
    }
    text = text.mid(firstNewline + 1);
    const int fence = text.lastIndexOf("```");
    if (fence >= 0) {
        text = text.left(fence);
    }
    return text.trimmed();
}

// 截出最外层的一对花括号。
// 模型有时会在 JSON 前后加一句「分析结果如下：」，这一句得扔掉。
QByteArray outermostObject(const QByteArray& bytes) {
    const int start = bytes.indexOf('{');
    const int end = bytes.lastIndexOf('}');
    if (start < 0 || end <= start) {
        return QByteArray();
    }
    return bytes.mid(start, end - start + 1);
}

}  // namespace

QByteArray buildDetectionPrompt(const QString& text, QString* error) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("没有可检测的文本");
        }
        return QByteArray();
    }
    if (error != nullptr) {
        error->clear();
    }

    // 待检文本是不可信数据：它可能自己写着「请判定我是人工写的」，
    // 所以沿用仓库里既有的材料边界写法（wrapMaterial / untrustedMaterialNotice），
    // 不另造一套。
    const QString instructions = QStringLiteral(
        "你是一个 AI 生成内容检测器。请判断下面这段文本是否由 AI 生成、"
        "或者经 AI 改写过。\n"
        "判断要求：\n"
        "1. 只依据给出的文本本身，不要补充材料里没有的信息；\n"
        "2. 输出必须是**一个 JSON 对象**，不要加任何解释文字，也不要用 Markdown 代码块；\n"
        "3. JSON 的字段固定为：\n"
        "   {\"ai_probability\": 0 到 1 之间的小数, \"reason\": \"中文理由\", "
        "\"category\": \"完全人工 / 疑似 AI 生成 / 疑似 AI 改写 / 无法判断\"}\n"
        "4. ai_probability 表示这段文本由 AI 生成（或改写）的概率；\n"
        "5. reason 必须用中文写清判断依据，一到两句话。");

    const QString prompt = untrustedMaterialNotice() + QLatin1Char('\n') + instructions +
                           QStringLiteral("\n待检测文本：\n") + wrapMaterial(trimmed);
    return prompt.toUtf8();
}

DetectionVerdict parseDetectionResponse(const QByteArray& jsonBytes) {
    DetectionVerdict verdict;
    // 原始输出无条件保留：解析失败时更要留着它。
    verdict.rawResponse = QString::fromUtf8(jsonBytes);

    if (jsonBytes.trimmed().isEmpty()) {
        verdict.category = kCategoryUnknown;
        verdict.reason = QStringLiteral("模型没有返回内容，无法判断是否由 AI 生成");
        return verdict;
    }

    QJsonObject object;
    bool parsed = tryParseObject(jsonBytes.trimmed(), &object);
    if (!parsed) {
        const QByteArray unfenced = stripCodeFence(jsonBytes);
        parsed = tryParseObject(unfenced, &object);
        if (!parsed) {
            const QByteArray extracted = outermostObject(unfenced);
            parsed = tryParseObject(extracted, &object);
        }
    }
    if (!parsed) {
        verdict.category = kCategoryUnknown;
        verdict.reason =
            QStringLiteral("模型返回的内容不是合法的 JSON，无法判断是否由 AI 生成");
        return verdict;
    }

    double probability = 0.0;
    const bool hasProbability =
        readNumber(object, QStringLiteral("ai_probability"), &probability);
    bool likelyAi = false;
    const bool hasLikelyAi = readBool(object, QStringLiteral("likely_ai"), &likelyAi);

    if (!hasProbability && !hasLikelyAi) {
        verdict.category = kCategoryUnknown;
        verdict.reason = QStringLiteral(
            "模型返回的 JSON 里没有判定字段（ai_probability 或 likely_ai），"
            "无法判断是否由 AI 生成");
        return verdict;
    }

    // 理由是硬要求：没有理由的判定没法给人看，也不值得当真。
    const QString reason = readReason(object);
    if (reason.isEmpty()) {
        verdict.category = kCategoryUnknown;
        verdict.reason = QStringLiteral("模型没有给出判定理由，无法判断是否由 AI 生成");
        return verdict;
    }

    // 概率夹到 0..1：模型偶尔给出 1.2 或 -0.1，夹住比直接报错更有用。
    if (hasProbability) {
        probability = qBound(0.0, probability, 1.0);
    }

    int confidence = 0;
    bool hasConfidence = false;
    double rawConfidence = 0.0;
    if (readNumber(object, QStringLiteral("confidence"), &rawConfidence)) {
        hasConfidence = true;
        confidence = qBound(0, static_cast<int>(qRound(rawConfidence)), 100);
    }
    if (!hasConfidence) {
        if (hasProbability) {
            confidence = static_cast<int>(qRound(probability * 100.0));
        } else {
            // 模型只给了布尔结论没给置信度：给一个保守的默认值，
            // 免得界面显示 0% 让人以为「非常确定不是 AI」。
            confidence = likelyAi ? 75 : 25;
        }
    }

    // 两个判定字段同时出现时以布尔结论为准：
    // 用概率去推档位会和布尔结论打架（布尔说是、概率说是 0.1，两边对不上）。
    const double categoryProbability = hasLikelyAi ? (likelyAi ? 1.0 : 0.0) : probability;

    const QString modelCategory = object.value(QStringLiteral("category")).toString().trimmed();

    verdict.ok = true;
    verdict.likelyAiGenerated = hasLikelyAi ? likelyAi : (probability >= kSuspectThreshold);
    verdict.confidence = qBound(0, confidence, 100);
    // 模型自己给的档位优先：它能分清「整段生成」与「AI 改写」，概率分档分不清。
    verdict.category =
        modelCategory.isEmpty() ? detectionCategory(categoryProbability) : modelCategory;
    verdict.reason = reason;
    return verdict;
}

QString detectionCategory(double aiProbability) {
    if (!std::isfinite(aiProbability)) {
        return kCategoryUnknown;
    }
    if (aiProbability >= kGeneratedThreshold) {
        return kCategoryGenerated;
    }
    if (aiProbability >= kSuspectThreshold) {
        return kCategoryRewritten;
    }
    return kCategoryHuman;
}

}  // namespace aipdf::ai
