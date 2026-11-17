#include "ai/AiImageGeneration.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace aipdf::ai {

namespace {

// 错误原因：领域层不依赖 i18n，所以原因直接写成中文字符串常量。
//
// 写成常量而不是散在代码里的字面量，是为了后面要改文案时只改一处，
// 也为了让单测能按固定文案核对。
const QString kErrEmptyPrompt = QStringLiteral("提示词不能为空：请写一句你想要的画面。");
const QString kErrBadJson = QStringLiteral("图像服务返回的内容不是合法的 JSON。");
const QString kErrNotObject = QStringLiteral("图像服务返回的内容不是一个 JSON 对象。");
const QString kErrNoDataArray = QStringLiteral("图像服务的响应里没有 data 数组。");
const QString kErrEmptyData = QStringLiteral("图像服务没有返回任何图片。");
const QString kErrEmptyImage = QStringLiteral("图像服务返回的图片数据是空的。");
const QString kErrBadBase64 = QStringLiteral("图像服务返回的图片数据不是合法的 base64。");
const QString kErrNoFetcher = QStringLiteral("响应里只有图片链接，但这次没有可用的下载方式。");
const QString kErrItemShape = QStringLiteral("图像响应里的一项既没有 b64_json 也没有 url。");
const QString kErrDownloadFailed = QStringLiteral("下载图片失败：%1");
const QString kErrEmptyDocument = QStringLiteral("文档正文为空，无法总结配图。");
const QString kErrNoSentence = QStringLiteral("文档正文里没有可用的句子，无法总结配图。");

// 默认尺寸。
const QString kDefaultImageSize = QStringLiteral("1024x1024");

// 太短的句子当不了要点：短到只有两个字，多半是标题残片或页码。
constexpr int kMinKeyPointChars = 4;

// 认不出魔数时用这个类型兜底。
//
// OpenAI 的 b64_json 默认就是 PNG，所以把它当兜底比留空更贴近实际。
const QString kFallbackImageMimeType = QStringLiteral("image/png");

// 尺寸只认 "宽x高" 的数字形式。
const QRegularExpression& sizePattern() {
    static const QRegularExpression pattern(QStringLiteral("^([0-9]{2,5})x([0-9]{2,5})$"));
    return pattern;
}

// 规范化尺寸。
//
// 认不出来就回落到默认尺寸，而不是报错：尺寸写错不该让整次生成失败，
// 但也不该把一个看不懂的值原样发给服务端。
QString normalizeSize(const QString& size) {
    const QString trimmed = size.trimmed().toLower();
    if (trimmed.isEmpty()) {
        return kDefaultImageSize;
    }
    return sizePattern().match(trimmed).hasMatch() ? trimmed : kDefaultImageSize;
}

// 严格解码 base64。
//
// 自己先校验字符集与长度，再交给 Qt 解码：
// 不同 Qt 版本的解码器宽容度不一样，靠它自己的报错会不稳定。
bool decodeBase64Image(const QString& text, QByteArray* out) {
    // 有些服务会把 base64 包成多行，先把空白去掉。
    const QByteArray raw = text.toLatin1();
    QByteArray encoded;
    encoded.reserve(raw.size());
    for (const char c : raw) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        encoded.append(c);
    }
    if (encoded.isEmpty()) {
        return false;
    }
    for (const char c : encoded) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        if (!allowed) {
            return false;
        }
    }
    // 去掉补位符再算长度：余 1 位一定是坏数据。
    QByteArray paddingless = encoded;
    while (paddingless.endsWith('=')) {
        paddingless.chop(1);
    }
    if (paddingless.size() % 4 == 1) {
        return false;
    }
    const QByteArray decoded =
        QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.isEmpty()) {
        return false;
    }
    *out = decoded;
    return true;
}

// 按魔数认出图片类型；认不出返回空串。
QString detectImageMimeType(const QByteArray& bytes) {
    if (bytes.size() >= 8 && bytes.startsWith("\x89PNG\r\n\x1a\n")) {
        return QStringLiteral("image/png");
    }
    if (bytes.size() >= 3 && bytes.startsWith("\xFF\xD8\xFF")) {
        return QStringLiteral("image/jpeg");
    }
    if (bytes.size() >= 6 && (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a"))) {
        return QStringLiteral("image/gif");
    }
    if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") {
        return QStringLiteral("image/webp");
    }
    if (bytes.size() >= 2 && bytes.startsWith("BM")) {
        return QStringLiteral("image/bmp");
    }
    return QString();
}

// 服务端声明的输出格式（OpenAI 新接口的 output_format）。
QString mimeTypeForFormatHint(const QString& hint) {
    const QString normalized = hint.trimmed().toLower();
    if (normalized == QLatin1String("jpeg") || normalized == QLatin1String("jpg")) {
        return QStringLiteral("image/jpeg");
    }
    if (normalized == QLatin1String("webp")) {
        return QStringLiteral("image/webp");
    }
    if (normalized == QLatin1String("png")) {
        return QStringLiteral("image/png");
    }
    return QString();
}

// 按链接后缀猜类型：下载回来的字节认不出魔数时才用得上。
QString mimeTypeForUrl(const QString& url) {
    const QString path = QUrl(url).path().toLower();
    if (path.endsWith(QLatin1String(".jpg")) || path.endsWith(QLatin1String(".jpeg"))) {
        return QStringLiteral("image/jpeg");
    }
    if (path.endsWith(QLatin1String(".webp"))) {
        return QStringLiteral("image/webp");
    }
    if (path.endsWith(QLatin1String(".gif"))) {
        return QStringLiteral("image/gif");
    }
    if (path.endsWith(QLatin1String(".png"))) {
        return QStringLiteral("image/png");
    }
    return QString();
}

// 决定这张图的类型：先信字节（事实），再信服务商的声明，最后才看链接后缀。
QString resolveMimeType(const QByteArray& bytes, const QString& formatHint,
                         const QString& url) {
    const QString detected = detectImageMimeType(bytes);
    if (!detected.isEmpty()) {
        return detected;
    }
    const QString hinted = mimeTypeForFormatHint(formatHint);
    if (!hinted.isEmpty()) {
        return hinted;
    }
    const QString fromUrl = mimeTypeForUrl(url);
    if (!fromUrl.isEmpty()) {
        return fromUrl;
    }
    return kFallbackImageMimeType;
}

// 这个字符是不是句末标点。
//
// 中文标点按码位判断：它们不是 Latin-1 字符，
// 写成字符常量会变成多字符常量，值是错的。
bool isSentenceTerminator(QChar ch) {
    switch (ch.unicode()) {
    case 0x3002:  // 。
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0xFF1B:  // ；
        return true;
    case '!':
    case '?':
    case ';':
        return true;
    default:
        return false;
    }
}

// 把正文切成句子。
//
// 只在换行与句末标点处断句，不在英文句点处断：
// 「3.5」「Fig.1」这类写法会被断成两半，断出来的碎片当不了要点。
QStringList splitSentences(const QString& text) {
    QStringList sentences;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")));
    for (const QString& line : lines) {
        QString current;
        for (const QChar ch : line) {
            current.append(ch);
            if (isSentenceTerminator(ch)) {
                const QString trimmed = current.trimmed();
                if (!trimmed.isEmpty()) {
                    sentences.append(trimmed);
                }
                current.clear();
            }
        }
        const QString tail = current.trimmed();
        if (!tail.isEmpty()) {
            sentences.append(tail);
        }
    }
    return sentences;
}

// 把一条要点变成一条能直接发给图像模型的提示词。
//
// 画面描述要包住要点本身：要点是「画什么」，包装句是「怎么画」，
// 两者合成一句，模型才知道要画的是这件事而不是这句话。
QString buildIllustrationPrompt(const QString& point, const QString& instruction) {
    QString prompt = QStringLiteral("为文档要点「") + point +
                     QStringLiteral("」画一张配图：画面直接表现「") + point +
                     QStringLiteral("」，主体突出、构图简洁、色彩协调，适合排版进 PDF。");
    if (!instruction.isEmpty()) {
        prompt += QStringLiteral("额外要求：") + instruction + QStringLiteral("。");
    }
    return prompt;
}

}  // namespace

ImageGenerationRequest buildImageGenerationRequest(const QString& userPrompt,
                                                    const QString& negativePrompt,
                                                    const QString& style,
                                                    const QString& size,
                                                    int count) {
    ImageGenerationRequest request;
    request.count = qBound(kMinImageCount, count, kMaxImageCount);
    request.size = normalizeSize(size);
    request.style = style.simplified();
    if (request.style.size() > kMaxImageStyleChars) {
        request.style = request.style.left(kMaxImageStyleChars);
    }
    request.negativePrompt = negativePrompt.simplified();
    if (request.negativePrompt.size() > kMaxImagePromptChars) {
        request.negativePrompt = request.negativePrompt.left(kMaxImagePromptChars);
    }

    QString prompt = userPrompt.simplified();
    if (prompt.isEmpty()) {
        request.error = kErrEmptyPrompt;
        return request;
    }

    // 风格后缀也占配额：整体提示词永远不超过上限，
    // 免得「带了风格就超长」这种意外。
    if (!request.style.isEmpty()) {
        const QString suffix = QStringLiteral("，风格：") + request.style;
        const int room = kMaxImagePromptChars - suffix.size();
        if (prompt.size() > room) {
            prompt = prompt.left(room);
            request.promptTruncated = true;
        }
        prompt += suffix;
    }
    if (prompt.size() > kMaxImagePromptChars) {
        prompt = prompt.left(kMaxImagePromptChars);
        request.promptTruncated = true;
    }

    request.prompt = prompt;
    request.ok = true;
    return request;
}

QByteArray buildImageGenerationPayload(const ImageGenerationRequest& request) {
    QJsonObject body;
    body.insert(QStringLiteral("prompt"), request.prompt);
    body.insert(QStringLiteral("n"), request.count);
    body.insert(QStringLiteral("size"), request.size);
    // 要 base64：图片链接会过期，也会让服务端多一次回源。
    body.insert(QStringLiteral("response_format"), QStringLiteral("b64_json"));
    if (!request.negativePrompt.isEmpty()) {
        body.insert(QStringLiteral("negative_prompt"), request.negativePrompt);
    }
    if (!request.style.isEmpty()) {
        body.insert(QStringLiteral("style"), request.style);
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

ImageGenerationOutcome parseImageGenerationResponse(const QByteArray& jsonBytes,
                                                     const ImageBytesFetcher& fetcher) {
    ImageGenerationOutcome outcome;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        outcome.error = kErrBadJson;
        return outcome;
    }
    if (!document.isObject()) {
        outcome.error = kErrNotObject;
        return outcome;
    }

    const QJsonObject root = document.object();
    const QJsonValue dataValue = root.value(QStringLiteral("data"));
    if (!dataValue.isArray()) {
        outcome.error = kErrNoDataArray;
        return outcome;
    }
    const QJsonArray data = dataValue.toArray();
    if (data.isEmpty()) {
        outcome.error = kErrEmptyData;
        return outcome;
    }

    // 服务端声明的输出格式，作为魔数认不出来时的备份。
    const QString formatHint =
        root.value(QStringLiteral("output_format")).toString().toLower();
    // 有些服务把 revised_prompt 放在根上，而不是每一项里。
    const QString rootRevisedPrompt =
        root.value(QStringLiteral("revised_prompt")).toString();

    QVector<GeneratedImage> images;
    for (const QJsonValue& value : data) {
        if (!value.isObject()) {
            outcome.error = kErrItemShape;
            return outcome;
        }
        const QJsonObject item = value.toObject();
        GeneratedImage image;
        image.revisedPrompt = item.value(QStringLiteral("revised_prompt")).toString();
        if (image.revisedPrompt.isEmpty()) {
            image.revisedPrompt = rootRevisedPrompt;
        }

        const QJsonValue b64Value = item.value(QStringLiteral("b64_json"));
        const QJsonValue urlValue = item.value(QStringLiteral("url"));
        if (b64Value.isString()) {
            const QString encoded = b64Value.toString();
            if (encoded.isEmpty()) {
                outcome.error = kErrEmptyImage;
                return outcome;
            }
            if (!decodeBase64Image(encoded, &image.bytes)) {
                outcome.error = kErrBadBase64;
                return outcome;
            }
            image.mimeType = resolveMimeType(image.bytes, formatHint, QString());
        } else if (urlValue.isString()) {
            const QString link = urlValue.toString();
            if (link.isEmpty()) {
                outcome.error = kErrEmptyImage;
                return outcome;
            }
            if (!fetcher) {
                outcome.error = kErrNoFetcher;
                return outcome;
            }
            QByteArray bytes;
            QString fetchError;
            if (!fetcher(link, &bytes, &fetchError)) {
                outcome.error = kErrDownloadFailed.arg(
                    fetchError.isEmpty() ? QStringLiteral("未知原因") : fetchError);
                return outcome;
            }
            if (bytes.isEmpty()) {
                outcome.error = kErrEmptyImage;
                return outcome;
            }
            image.bytes = bytes;
            image.mimeType = resolveMimeType(image.bytes, formatHint, link);
        } else {
            outcome.error = kErrItemShape;
            return outcome;
        }
        images.append(image);
    }

    outcome.ok = true;
    outcome.images = images;
    return outcome;
}

bool downloadImageBytes(QNetworkAccessManager* manager, const QString& url,
                        QByteArray* out, QString* error) {
    if (!out) {
        if (error) {
            *error = QStringLiteral("没有可以写入结果的容器。");
        }
        return false;
    }
    if (!manager) {
        if (error) {
            *error = QStringLiteral("没有可用的网络管理器。");
        }
        return false;
    }
    const QUrl target(url);
    if (!target.isValid() || target.isEmpty()) {
        if (error) {
            *error = QStringLiteral("图片链接不是有效的网址。");
        }
        return false;
    }

    QNetworkRequest request(target);
    // 这条请求不附带任何凭据，所以跟随重定向不会泄露密钥；
    // NoLessSafe 是为了不让 https 被降级成 http。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // 同步等待：这个函数会阻塞调用线程，所以只能在后台线程用。
    loop.exec();

    const bool failed = reply->error() != QNetworkReply::NoError;
    const QString detail = failed ? reply->errorString() : QString();
    if (failed) {
        if (error) {
            *error = QStringLiteral("网络请求失败：") + detail;
        }
        reply->deleteLater();
        return false;
    }
    *out = reply->readAll();
    reply->deleteLater();
    return true;
}

SummaryImagePlan planSummaryImages(const QString& documentText, const QString& instruction) {
    SummaryImagePlan plan;

    const QString text = documentText.simplified();
    if (text.isEmpty()) {
        plan.error = kErrEmptyDocument;
        return plan;
    }

    QStringList sentences = splitSentences(text);
    // 去掉太短的碎片：它们当不了「适合配图的要点」。
    sentences.removeIf([](const QString& sentence) {
        return sentence.size() < kMinKeyPointChars;
    });
    if (sentences.isEmpty()) {
        plan.error = kErrNoSentence;
        return plan;
    }

    const int take = qMin(sentences.size(), kMaxSummaryKeyPoints);
    for (int i = 0; i < take; ++i) {
        plan.keyPoints.append(sentences.at(i));
    }

    // 摘要取前几个要点拼起来：它本来就出自这些要点，不另编一段话。
    QStringList summaryParts;
    for (int i = 0; i < qMin(plan.keyPoints.size(), 3); ++i) {
        summaryParts.append(plan.keyPoints.at(i));
    }
    QString summary = summaryParts.join(QStringLiteral("；"));
    if (summary.size() > kMaxSummaryChars) {
        summary = summary.left(kMaxSummaryChars);
    }
    plan.summary = summary;

    const QString extra = instruction.simplified();
    for (const QString& point : plan.keyPoints) {
        plan.imagePrompts.append(buildIllustrationPrompt(point, extra));
    }

    plan.ok = true;
    return plan;
}

}  // namespace aipdf::ai
