// 图像生成领域层的单元测试。
//
// 这些用例守的是三件事：
//   1. 请求构造：提示词会被规范化、限长，张数会被夹住；
//   2. 响应解析：坏 JSON、坏 base64、空数组都**必须**是失败，不是「成功但没图」；
//   3. 总结配图：正文能变成要点，要点能变成可发送的提示词。
//
// 整组测试不联网：图片链接那一路走的是注入的取字节回调。
#include "ai/AiImageGeneration.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtTest>

using namespace aipdf::ai;

namespace {

// 造一段最小的 PNG 字节：只要魔数对得上就行，不必是能解码的真图。
QByteArray fakePngBytes() {
    return QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray("fake-png-body");
}

// 造一段最小的 JPEG 字节。
QByteArray fakeJpegBytes() {
    return QByteArray("\xFF\xD8\xFF", 3) + QByteArray("fake-jpeg-body");
}

// 造一个 {"data":[...]} 的响应体。
QByteArray responseWithItems(const QJsonArray& items) {
    QJsonObject root;
    root.insert(QStringLiteral("data"), items);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// 造一个只含 b64_json 的响应体。
QByteArray responseWithBase64(const QByteArray& bytes) {
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), QString::fromLatin1(bytes.toBase64()));
    return responseWithItems(QJsonArray{item});
}

}  // namespace

class AiImageGenerationTests : public QObject {
    Q_OBJECT

private slots:
    // 请求构造
    void normalPromptProducesRequest();
    void emptyPromptFails();
    void whitespaceOnlyPromptFails();
    void longPromptTruncated();
    void negativePromptAndStyleCarried();
    void countClampedToRange();
    void sizeDefaultsAndFallsBack();
    void payloadCarriesPromptAndParameters();

    // 响应解析
    void b64ResponseDecodesBytes();
    void urlResponseUsesFetcher();
    void urlResponseWithoutFetcherFails();
    void fetchFailureIsReported();
    void invalidJsonFails();
    void nonObjectJsonFails();
    void missingDataArrayFails();
    void emptyDataArrayFails();
    void invalidBase64Fails();
    void revisedPromptCarried();
    void multipleImagesParsed();
    void jpegMimeDetected();
    void itemWithoutImageFieldsFails();

    // 总结配图
    void documentProducesKeyPoints();
    void emptyDocumentFails();
    void keyPointBecomesImagePrompt();
    void promptCountMatchesPointCount();
    void instructionAppearsInImagePrompts();
    void summaryUsesKeyPoints();
};

void AiImageGenerationTests::normalPromptProducesRequest() {
    const ImageGenerationRequest request = buildImageGenerationRequest(QStringLiteral("一只戴帽子的猫"));
    QVERIFY(request.ok);
    QVERIFY(request.error.isEmpty());
    QVERIFY(!request.prompt.isEmpty());
    QVERIFY(request.prompt.contains(QStringLiteral("戴帽子的猫")));
    // 默认值要真的落到位，否则界面会拿一个空尺寸去发请求。
    QCOMPARE(request.size, QStringLiteral("1024x1024"));
    QCOMPARE(request.count, 1);
    QVERIFY(!request.promptTruncated);
}

void AiImageGenerationTests::emptyPromptFails() {
    const ImageGenerationRequest request = buildImageGenerationRequest(QString());
    QVERIFY(!request.ok);
    QVERIFY(!request.error.isEmpty());
    // 原因要给用户看，不能只是一句「失败」。
    QVERIFY(request.error.contains(QStringLiteral("提示词")));
    QVERIFY(request.prompt.isEmpty());
}

void AiImageGenerationTests::whitespaceOnlyPromptFails() {
    // 只有空白等同于没有提示词：直接发出去只会被服务端拒。
    const ImageGenerationRequest request =
        buildImageGenerationRequest(QStringLiteral("   \n\t  "));
    QVERIFY(!request.ok);
    QVERIFY(!request.error.isEmpty());
    QVERIFY(request.prompt.isEmpty());
}

void AiImageGenerationTests::longPromptTruncated() {
    const QString longPrompt(3000, QChar('a'));
    const ImageGenerationRequest request = buildImageGenerationRequest(longPrompt);
    QVERIFY(request.ok);
    QCOMPARE(request.prompt.size(), kMaxImagePromptChars);
    QVERIFY(request.promptTruncated);
    // 截断了就要说清楚上限是多少，用户才知道自己丢了什么。
    QVERIFY(QString::number(kMaxImagePromptChars) == QStringLiteral("2000"));
}

void AiImageGenerationTests::negativePromptAndStyleCarried() {
    const ImageGenerationRequest request = buildImageGenerationRequest(
        QStringLiteral("一只戴帽子的猫"), QStringLiteral("模糊, 变形"),
        QStringLiteral("水彩"));
    QVERIFY(request.ok);
    QCOMPARE(request.negativePrompt, QStringLiteral("模糊, 变形"));
    QCOMPARE(request.style, QStringLiteral("水彩"));
    // 风格要真的进到提示词里：只存在一个独立字段上等于没生效。
    QVERIFY(request.prompt.contains(QStringLiteral("水彩")));
    QVERIFY(request.prompt.contains(QStringLiteral("戴帽子的猫")));
}

void AiImageGenerationTests::countClampedToRange() {
    const ImageGenerationRequest tooFew =
        buildImageGenerationRequest(QStringLiteral("猫"), QString(), QString(), QString(), 0);
    QCOMPARE(tooFew.count, kMinImageCount);
    const ImageGenerationRequest tooMany =
        buildImageGenerationRequest(QStringLiteral("猫"), QString(), QString(), QString(), 99);
    QCOMPARE(tooMany.count, kMaxImageCount);
    const ImageGenerationRequest justRight =
        buildImageGenerationRequest(QStringLiteral("猫"), QString(), QString(), QString(), 3);
    QCOMPARE(justRight.count, 3);
}

void AiImageGenerationTests::sizeDefaultsAndFallsBack() {
    const ImageGenerationRequest fallback =
        buildImageGenerationRequest(QStringLiteral("猫"), QString(), QString(), QStringLiteral("大"));
    // 认不出来的尺寸回落到默认，而不是把看不懂的值原样发出去。
    QCOMPARE(fallback.size, QStringLiteral("1024x1024"));

    const ImageGenerationRequest explicitSize = buildImageGenerationRequest(
        QStringLiteral("猫"), QString(), QString(), QStringLiteral("512x512"));
    QCOMPARE(explicitSize.size, QStringLiteral("512x512"));
}

void AiImageGenerationTests::payloadCarriesPromptAndParameters() {
    const ImageGenerationRequest request = buildImageGenerationRequest(
        QStringLiteral("一只戴帽子的猫"), QStringLiteral("模糊"), QStringLiteral("水彩"));
    QVERIFY(request.ok);
    const QJsonObject body =
        QJsonDocument::fromJson(buildImageGenerationPayload(request)).object();
    QCOMPARE(body.value(QStringLiteral("prompt")).toString(), request.prompt);
    QCOMPARE(body.value(QStringLiteral("n")).toInt(), request.count);
    QCOMPARE(body.value(QStringLiteral("size")).toString(), request.size);
    QCOMPARE(body.value(QStringLiteral("negative_prompt")).toString(),
             QStringLiteral("模糊"));
    QCOMPARE(body.value(QStringLiteral("style")).toString(), QStringLiteral("水彩"));
}

void AiImageGenerationTests::b64ResponseDecodesBytes() {
    const QByteArray png = fakePngBytes();
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithBase64(png));
    QVERIFY(outcome.ok);
    QVERIFY(outcome.error.isEmpty());
    QCOMPARE(outcome.images.size(), 1);
    QCOMPARE(outcome.images.first().bytes, png);
    QCOMPARE(outcome.images.first().mimeType, QStringLiteral("image/png"));
}

void AiImageGenerationTests::urlResponseUsesFetcher() {
    QJsonObject item;
    item.insert(QStringLiteral("url"), QStringLiteral("https://example.com/a.png"));
    const QByteArray response = responseWithItems(QJsonArray{item});

    QString seenUrl;
    const QByteArray downloaded = fakePngBytes();
    ImageBytesFetcher fetcher = [&seenUrl, &downloaded](const QString& url, QByteArray* out,
                                                         QString* error) {
        seenUrl = url;
        if (out) {
            *out = downloaded;
        }
        return true;
    };

    const ImageGenerationOutcome outcome = parseImageGenerationResponse(response, fetcher);
    QVERIFY(outcome.ok);
    QCOMPARE(seenUrl, QStringLiteral("https://example.com/a.png"));
    QCOMPARE(outcome.images.size(), 1);
    QCOMPARE(outcome.images.first().bytes, downloaded);
    QCOMPARE(outcome.images.first().mimeType, QStringLiteral("image/png"));
}

void AiImageGenerationTests::urlResponseWithoutFetcherFails() {
    QJsonObject item;
    item.insert(QStringLiteral("url"), QStringLiteral("https://example.com/a.png"));
    // 没有下载方式就不能装作拿到了图片。
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithItems(QJsonArray{item}));
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::fetchFailureIsReported() {
    QJsonObject item;
    item.insert(QStringLiteral("url"), QStringLiteral("https://example.com/a.png"));
    ImageBytesFetcher fetcher = [](const QString&, QByteArray*, QString* error) {
        if (error) {
            *error = QStringLiteral("连接被拒绝");
        }
        return false;
    };
    const ImageGenerationOutcome outcome = parseImageGenerationResponse(
        responseWithItems(QJsonArray{item}), fetcher);
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.images.isEmpty());
    // 失败原因要带上下游的具体原因，用户才知道是网络问题还是参数问题。
    QVERIFY(outcome.error.contains(QStringLiteral("连接被拒绝")));
}

void AiImageGenerationTests::invalidJsonFails() {
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(QByteArray("{not json"));
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.error.contains(QStringLiteral("JSON")));
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::nonObjectJsonFails() {
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(QByteArray("[1,2,3]"));
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::missingDataArrayFails() {
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(QByteArray("{\"error\":\"bad request\"}"));
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.error.contains(QStringLiteral("data")));
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::emptyDataArrayFails() {
    // 关键回归：空数组不是「成功但没有图」，它是失败。
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithItems(QJsonArray{}));
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::invalidBase64Fails() {
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), QStringLiteral("这不是 base64 !!!"));
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithItems(QJsonArray{item}));
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.error.contains(QStringLiteral("base64")));
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::revisedPromptCarried() {
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), QString::fromLatin1(fakePngBytes().toBase64()));
    item.insert(QStringLiteral("revised_prompt"), QStringLiteral("改成：一只戴草帽的猫"));
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithItems(QJsonArray{item}));
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.images.first().revisedPrompt,
             QStringLiteral("改成：一只戴草帽的猫"));
}

void AiImageGenerationTests::multipleImagesParsed() {
    QJsonObject first;
    first.insert(QStringLiteral("b64_json"), QString::fromLatin1(fakePngBytes().toBase64()));
    QJsonObject second;
    second.insert(QStringLiteral("b64_json"), QString::fromLatin1(fakeJpegBytes().toBase64()));
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithItems(QJsonArray{first, second}));
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.images.size(), 2);
    QCOMPARE(outcome.images.at(0).mimeType, QStringLiteral("image/png"));
    QCOMPARE(outcome.images.at(1).mimeType, QStringLiteral("image/jpeg"));
}

void AiImageGenerationTests::jpegMimeDetected() {
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithBase64(fakeJpegBytes()));
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.images.first().mimeType, QStringLiteral("image/jpeg"));
}

void AiImageGenerationTests::itemWithoutImageFieldsFails() {
    QJsonObject item;
    item.insert(QStringLiteral("id"), QStringLiteral("abc"));
    const ImageGenerationOutcome outcome =
        parseImageGenerationResponse(responseWithItems(QJsonArray{item}));
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outcome.images.isEmpty());
}

void AiImageGenerationTests::documentProducesKeyPoints() {
    const QString document = QStringLiteral(
        "本文介绍 AI PDF Editor 的核心能力。\n"
        "它支持在 PDF 里直接编辑文字与批注。\n"
        "所有修改都可以撤销。\n"
        "还可以对整份文档提问并核验引用。\n"
        "最后还能把页面导出成图片。");
    const SummaryImagePlan plan = planSummaryImages(document);
    QVERIFY(plan.ok);
    QVERIFY(plan.error.isEmpty());
    QVERIFY(plan.keyPoints.size() >= 1);
    QVERIFY(plan.keyPoints.size() <= kMaxSummaryKeyPoints);
    // 五句话只取前四条：一次配图最多四张。
    QCOMPARE(plan.keyPoints.size(), kMaxSummaryKeyPoints);
    // 要点是完整的短句，不是被切碎的碎片。
    QVERIFY(plan.keyPoints.first().endsWith(QStringLiteral("。")));
}

void AiImageGenerationTests::emptyDocumentFails() {
    const SummaryImagePlan plan = planSummaryImages(QString());
    QVERIFY(!plan.ok);
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(plan.error.contains(QStringLiteral("正文")));
    QVERIFY(plan.keyPoints.isEmpty());
    QVERIFY(plan.imagePrompts.isEmpty());
}

void AiImageGenerationTests::keyPointBecomesImagePrompt() {
    const QString document =
        QStringLiteral("本文介绍 AI PDF Editor 的核心能力。它支持直接编辑文字。");
    const SummaryImagePlan plan = planSummaryImages(document);
    QVERIFY(plan.ok);
    QVERIFY(!plan.imagePrompts.isEmpty());
    const QString prompt = plan.imagePrompts.first();
    QVERIFY(!prompt.isEmpty());
    // 提示词要包住要点本身：模型得知道要画的是哪件事。
    QVERIFY(prompt.contains(plan.keyPoints.first()));
    QVERIFY(prompt.contains(QStringLiteral("配图")));
}

void AiImageGenerationTests::promptCountMatchesPointCount() {
    const QString document = QStringLiteral(
        "第一句讲的是排版。第二句讲的是批注。第三句讲的是撤销。第四句讲的是导出。第五句不讲。");
    const SummaryImagePlan plan = planSummaryImages(document);
    QVERIFY(plan.ok);
    QCOMPARE(plan.imagePrompts.size(), plan.keyPoints.size());
}

void AiImageGenerationTests::instructionAppearsInImagePrompts() {
    const SummaryImagePlan plan =
        planSummaryImages(QStringLiteral("本文介绍核心能力。"), QStringLiteral("偏商务风格"));
    QVERIFY(plan.ok);
    QVERIFY(!plan.imagePrompts.isEmpty());
    QVERIFY(plan.imagePrompts.first().contains(QStringLiteral("偏商务风格")));
}

void AiImageGenerationTests::summaryUsesKeyPoints() {
    const QString document =
        QStringLiteral("本文介绍 AI PDF Editor 的核心能力。它支持直接编辑文字与批注。");
    const SummaryImagePlan plan = planSummaryImages(document);
    QVERIFY(plan.ok);
    QVERIFY(!plan.summary.isEmpty());
    // 摘要出自要点，不另编一段话。
    QVERIFY(plan.summary.contains(plan.keyPoints.first()));
    QVERIFY(plan.summary.size() <= kMaxSummaryChars);
}

QTEST_GUILESS_MAIN(AiImageGenerationTests)
#include "AiImageGenerationTests.moc"
