#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkAccessManager;

namespace aipdf::ai {

// ------------------------------------------------------------------ 文生图 --

// 提示词上限（字符）。
//
// 定上限是为了让「用户以为发出去的」和「真的发出去的」一致：
// 超出就明确截断并在请求里记下 truncated，而不是悄悄发一个更短的提示词。
constexpr int kMaxImagePromptChars = 2000;

// 风格描述的上限。
//
// 风格只是修饰词，不该挤占提示词的主要配额；限死了，主画面描述才是完整的。
constexpr int kMaxImageStyleChars = 100;

// 一次生成的张数范围。
//
// 上限 4 是因为多数服务对单次生成张数有限制，而且一次出太多张用户也用不上。
constexpr int kMinImageCount = 1;
constexpr int kMaxImageCount = 4;

// 一次文生图请求的描述。
//
// 这是「准备发出去的那次请求」的全部内容：界面拿它做预览与审计，
// Provider 拿它拼请求体。领域层不碰密钥、不碰地址：那些属于 Provider 配置。
struct ImageGenerationRequest {
    // 请求能不能发。false 时 error 里是给用户看的中文原因。
    bool ok = false;
    // 失败原因（中文）。ok 为 true 时为空。
    QString error;
    // 最终提示词（已规范化、已拼上风格、已截断）。
    QString prompt;
    // 反向提示词，可空。
    QString negativePrompt;
    // 图像尺寸，形如 "1024x1024"。
    QString size = QStringLiteral("1024x1024");
    // 生成张数，已经夹到 kMinImageCount..kMaxImageCount。
    int count = 1;
    // 风格描述，可空。
    QString style;
    // 提示词是不是被截断过。
    //
    // 界面要能说出来：用户写的和发出去的不一样时，不能装作没发生。
    bool promptTruncated = false;
};

// 一张生成出来的图片。
struct GeneratedImage {
    // 图片类型，例如 "image/png"。
    QString mimeType = QStringLiteral("image/png");
    // 图片字节。
    QByteArray bytes;
    // 服务端改写后的提示词，可空。
    //
    // 有些服务会按自己的理解重写提示词，这份原文要留给用户看：
    // 「它把我的提示词改成了什么」是用户有权知道的事。
    QString revisedPrompt;
};

// 一次图像生成的结果。
struct ImageGenerationOutcome {
    // 解析成功与否。**解析不出来就是 false**，绝不能当成生成成功。
    bool ok = false;
    // 解析出来的图片（ok 为 true 时非空）。
    QVector<GeneratedImage> images;
    // 失败原因（中文）。ok 为 true 时为空。
    QString error;
};

// 取图片字节的回调：给一个图片链接，返回字节内容。
//
// 做成回调而不是在解析里直接联网，是因为「解析响应」这条链路必须能离线验证：
// 单测注入自己的回调，生产代码注入基于 QNetworkAccessManager 的实现。
// 返回 false 时 error 里写给用户看的中文原因。
using ImageBytesFetcher = std::function<bool(const QString& url, QByteArray* out, QString* error)>;

// 构造一次文生图请求。
//
// 规范化规则：去空白（提示词是给图像模型看的，多余空白只占配额）、
// 拼上风格、限长（超长截断并置 promptTruncated）、把张数夹到 1..4。
// 提示词为空时返回 ok=false，并写明中文原因。
ImageGenerationRequest buildImageGenerationRequest(const QString& userPrompt,
                                                    const QString& negativePrompt = QString(),
                                                    const QString& style = QString(),
                                                    const QString& size = QString(),
                                                    int count = 1);

// 把请求描述转成 OpenAI 兼容的 /v1/images/generations 请求体（JSON）。
//
// 放在领域层是因为「字段叫什么、带哪些字段」是协议知识：
// 界面只负责把这个字节块 POST 出去，不该自己拼 JSON。
QByteArray buildImageGenerationPayload(const ImageGenerationRequest& request);

// 解析图像服务的响应。
//
// 支持两种形状：
//   {"data":[{"b64_json":"<base64>"}]}   —— base64 直接解码
//   {"data":[{"url":"https://..."}]}      —— 走 fetcher 下载字节
//
// 任何一步失败都返回 ok=false 与中文原因；**空数组、坏 JSON、坏 base64
// 都算失败**，不能被当成「生成成功但没有图」。
ImageGenerationOutcome parseImageGenerationResponse(const QByteArray& jsonBytes,
                                                     const ImageBytesFetcher& fetcher = ImageBytesFetcher());

// 用网络管理器下载图片字节。
//
// 默认实现会阻塞到下载结束，所以只能在后台线程调用。
// 这里不附带任何凭据，因此跟随重定向不会泄露密钥；
// 重定向策略用 NoLessSafe，是为了让 https 不被降级成 http。
bool downloadImageBytes(QNetworkAccessManager* manager, const QString& url,
                        QByteArray* out, QString* error);

// -------------------------------------------------------------- 总结配图 --

// 总结配图的计划。
//
// 顺序是「先总结，再配图」：先把正文压成若干要点，
// 再把每条要点变成一条能直接发给图像模型的提示词。
struct SummaryImagePlan {
    // 计划能不能用。false 时 error 里是给用户看的中文原因。
    bool ok = false;
    // 整体摘要（由要点拼成，最长 kMaxSummaryChars）。
    QString summary;
    // 要点：每条一个短句，最多 kMaxSummaryKeyPoints 条。
    QVector<QString> keyPoints;
    // 图像提示词：每条要点对应一条，数量与要点数相同。
    QVector<QString> imagePrompts;
    // 失败原因（中文）。ok 为 true 时为空。
    QString error;
};

// 一次总结配图最多取几个要点。
constexpr int kMaxSummaryKeyPoints = 4;

// 摘要的最大长度（字符）。
constexpr int kMaxSummaryChars = 200;

// 把一段文档正文总结成「适合配图的要点」，并据此拼出图像提示词。
//
// 正文为空时返回 ok=false，并写明中文原因。
// instruction 是用户额外的画面要求（例如「偏商务风格」），会被拼进每条提示词。
SummaryImagePlan planSummaryImages(const QString& documentText,
                                   const QString& instruction = QString());

}  // namespace aipdf::ai
