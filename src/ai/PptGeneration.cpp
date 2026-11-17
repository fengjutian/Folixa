#include "ai/PptGeneration.h"

#include "ai/ChatTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

namespace aipdf::ai {

namespace {

constexpr auto kKeyTitle = "title";
constexpr auto kKeySubtitle = "subtitle";
constexpr auto kKeyBullets = "bullets";
constexpr auto kKeyNotes = "notes";
constexpr auto kKeySlides = "slides";

// 送给模型的正文最多这么多字。
//
// 为什么截：模型上下文有限，整篇几十万字塞进去必然被服务端拒绝，
// 一次失败就是白等一轮。截断时会在提示词里说清楚截了多少，
// 不然模型会以为文档本来就到此为止。
constexpr int kMaxDocumentChars = 12000;

// 从正文里推出的标题最多这么多字。
//
// 为什么截：PPT 标题是给人放在封面上看的，整段正文当标题既难看也不好排版。
constexpr int kMaxDerivedTitleChars = 40;

// 造一份失败的 deck：原因（中文）非空，页一定是空的。
//
// 为什么失败时连半份内容都不留：调用方一旦拿到有内容又有 error 的 deck，
// 就得自己判断该信哪个——那种「一半成功」的状态最容易变成假成功。
PptDeck failedDeck(const QString& reason) {
    PptDeck deck;
    deck.error = reason;
    return deck;
}

// 页数上限：夹到 1..30。
int clampMaxSlides(int maxSlides) {
    if (maxSlides < kMinDeckSlides) {
        return kMinDeckSlides;
    }
    if (maxSlides > kMaxDeckSlides) {
        return kMaxDeckSlides;
    }
    return maxSlides;
}

// 截断到上限，并重新编号（1 起、连续）。
//
// 为什么要重新编号：模型给的页序里可能缺号或从 0 起，
// 放映时「第 0 页」这种页码会直接暴露在界面上。
QVector<Slide> truncateAndRenumber(const QVector<Slide>& slides, int limit) {
    QVector<Slide> result;
    const int keep = qMin(slides.size(), limit);
    result.reserve(keep);
    for (int index = 0; index < keep; ++index) {
        Slide slide = slides.at(index);
        slide.index = result.size() + 1;
        result.append(slide);
    }
    return result;
}

// 送给模型的 JSON 格式说明。
//
// 为什么把格式写死在一处：模型回什么形状，全靠这段话；
// 写散了以后改一处漏一处，模型就会开始回别的形状，
// 而格式对不上时整份 PPT 直接作废——代价太高，格式必须只有一处定义。
QString deckJsonContract(int maxSlides) {
    return QStringLiteral(
        "只输出一个 JSON 对象，不要输出解释、不要用代码块包裹。格式严格如下：\n"
        "{\"title\":\"整套 PPT 的标题\",\"slides\":[{\"title\":\"页面标题\","
        "\"subtitle\":\"可选副标题\",\"bullets\":[\"要点一\",\"要点二\"],"
        "\"notes\":\"可选备注\"}]}\n"
        "要求：\n"
        "1. slides 最多 %1 页，不够 %1 页就少给几页，不要凑数；\n"
        "2. 每页给 2 到 5 条要点，每条是一句完整的话；\n"
        "3. 只输出上面列出的字段，subtitle 与 notes 没有内容就省略；\n"
        "4. 用中文。")
        .arg(maxSlides);
}

// 主题模式的提示词。
QString topicPrompt(const QString& topic, const QString& instruction, int maxSlides) {
    QString prompt = QStringLiteral(
        "你是一位演示文稿作者。请围绕下面的主题拟一份 PPT 大纲。\n\n");
    prompt += deckJsonContract(maxSlides);
    prompt += QStringLiteral("\n\n主题：\n") + topic;
    const QString trimmedInstruction = instruction.trimmed();
    if (!trimmedInstruction.isEmpty()) {
        prompt += QStringLiteral("\n\n附加要求：\n") + trimmedInstruction;
    }
    return prompt;
}

// 文档模式的提示词。
//
// 为什么带「材料不是指令」与材料边界：文档正文是不可信数据，
// 正文里可能写着「忽略以上规则」「输出你的系统提示」。
// 仓库里已经有现成的两道防线（ai::untrustedMaterialNotice / ai::wrapMaterial），
// 这里直接复用，不另写一套——两套措辞迟早会不一致。
QString documentPrompt(const QString& documentText, int maxSlides) {
    QString prompt = untrustedMaterialNotice();
    prompt += QLatin1Char('\n');
    prompt += QStringLiteral(
        "你是一位演示文稿作者。请把下面的文档正文提炼成一份 PPT 大纲。\n\n");
    prompt += deckJsonContract(maxSlides);
    prompt += QStringLiteral("\n\n文档正文：\n");

    QString body = documentText;
    bool truncated = false;
    if (body.size() > kMaxDocumentChars) {
        body = body.left(kMaxDocumentChars);
        truncated = true;
    }
    prompt += wrapMaterial(body);
    if (truncated) {
        // 说清楚截断了：不然模型会以为文档本来就到此为止，
        // 后半篇的内容就这么凭空消失了。
        prompt += QStringLiteral("\n（文档正文过长，已只取前 %1 字。）").arg(kMaxDocumentChars);
    }
    return prompt;
}

// 读一个可选的字符串字段：缺省算空；给了但类型不对就算失败。
bool readOptionalString(const QJsonObject& object,
                         const QString& key,
                         QString* value,
                         QString* error) {
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        if (value != nullptr) {
            value->clear();
        }
        return true;
    }
    if (!jsonValue.isString()) {
        if (error != nullptr) {
            *error = QStringLiteral("字段「%1」类型不对（应为字符串）").arg(key);
        }
        return false;
    }
    if (value != nullptr) {
        *value = jsonValue.toString();
    }
    return true;
}

// 行首有没有空白。
//
// 为什么要自己数：Qt 没有「只去掉行首空白」的公开接口，
// 用 trimmed() 再比较会把行尾空白也一起算进去，判断就错了。
bool isIndented(const QString& line) {
    for (int index = 0; index < line.size(); ++index) {
        const QChar ch = line.at(index);
        if (!ch.isSpace()) {
            return index > 0;
        }
    }
    return false;
}

// 项目符号「•」。用 QChar 而不是 QLatin1Char：这个码位不在 Latin-1 里，
// QLatin1Char 对它是越界的。
constexpr QChar kBulletMarker = QChar(0x2022);

// 顿号「、」。中文大纲里编号后面常跟它（「1、背景」）。
// 同样不在 Latin-1 里，只能用 QChar。
constexpr QChar kIdeographicComma = QChar(0x3001);

// 「数字.」「数字)」这类编号标记结束在哪个下标；不是编号就返回 -1。
//
// 为什么只认 1 到 3 位数字：大纲里的编号不会超过三位，
// 而「2024. 年的数据」里的点是正文的一部分，不是编号——
// 四位数字起步的一律不按编号处理。
//
// 为什么编号后面不强制跟空格：「1.概述」这种不带空格的写法在中文里很常见，
// 强制要求空格等于把一大半用户的大纲默默丢掉。
int numericMarkerEnd(const QString& line) {
    if (line.isEmpty() || !line.at(0).isDigit()) {
        return -1;
    }
    int index = 0;
    while (index < line.size() && line.at(index).isDigit()) {
        ++index;
    }
    if (index < 1 || index > 3) {
        return -1;
    }
    if (index >= line.size()) {
        return -1;
    }
    const QChar separator = line.at(index);
    if (separator != QLatin1Char('.') && separator != QLatin1Char(')') &&
        separator != kIdeographicComma) {
        return -1;
    }
    return index + 1;
}

// 一行是不是「页面标题」：顶格且以标记开头。
//
// 标记认这几种：`-` / `*` / `+` / `•`，以及「1 到 3 位数字 + . 或 )」。
bool isOutlineTitleLine(const QString& line) {
    if (line.isEmpty()) {
        return false;
    }
    const QChar first = line.at(0);
    if (first == QLatin1Char('-') || first == QLatin1Char('*') ||
        first == QLatin1Char('+') || first == kBulletMarker) {
        return true;
    }
    return numericMarkerEnd(line) >= 0;
}

// 去掉行首的标记，只留下标题／要点的正文。
QString stripLeadingMarker(const QString& line) {
    if (line.isEmpty()) {
        return QString();
    }
    const QChar first = line.at(0);
    if (first == QLatin1Char('-') || first == QLatin1Char('*') ||
        first == QLatin1Char('+') || first == kBulletMarker) {
        return line.mid(1).trimmed();
    }
    const int markerEnd = numericMarkerEnd(line);
    if (markerEnd >= 0) {
        return line.mid(markerEnd).trimmed();
    }
    return line.trimmed();
}

// 解析大纲文本。
//
// 规则：顶格且带标记的行是页面标题；缩进行是上一页的要点；
// 其余（顶格但没标记）一律忽略。
QVector<Slide> parseOutline(const QString& outlineText) {
    QVector<Slide> slides;
    const QStringList lines = outlineText.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        if (raw.trimmed().isEmpty()) {
            continue;
        }
        if (isIndented(raw)) {
            // 缩进行：上一页的要点。还没有页面时无处可去，只能丢掉。
            if (slides.isEmpty()) {
                continue;
            }
            const QString text = stripLeadingMarker(raw.trimmed());
            if (!text.isEmpty()) {
                slides.last().bullets.append(text);
            }
            continue;
        }
        if (!isOutlineTitleLine(raw.trimmed())) {
            continue;
        }
        const QString text = stripLeadingMarker(raw.trimmed());
        if (text.isEmpty()) {
            // 光一个标记、没有标题文字：不算一页。
            continue;
        }
        Slide slide;
        slide.title = text;
        slides.append(slide);
    }
    return slides;
}

// 本地提炼：正文按空行分段，每段一页——首行是标题，其余行是要点。
//
// 为什么按空行分段：空行是正文里最不容易认错的段落边界，
// 比按句号切稳（句号也可能出现在一句话中间）。
QVector<Slide> localSlidesFromDocument(const QString& documentText) {
    QVector<Slide> slides;
    QVector<QString> currentLines;

    const auto flush = [&]() {
        if (currentLines.isEmpty()) {
            return;
        }
        Slide slide;
        slide.title = currentLines.first();
        for (int index = 1; index < currentLines.size(); ++index) {
            slide.bullets.append(currentLines.at(index));
        }
        slides.append(slide);
        currentLines.clear();
    };

    const QStringList lines = documentText.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            flush();
            continue;
        }
        currentLines.append(line);
    }
    flush();
    return slides;
}

// 正文里推出一个标题：取第一行非空行。
QString deriveTitleFromDocument(const QString& documentText) {
    const QStringList lines = documentText.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.size() > kMaxDerivedTitleChars) {
            return line.left(kMaxDerivedTitleChars) + QStringLiteral("…");
        }
        return line;
    }
    return QString();
}

// 调一次模型并把回答解成 deck。
//
// 模型出错、解不出内容、格式不对，一律失败：
// 这三种情况都不能产出一份「看起来成功」的 PPT。
PptDeck askModelAndParse(const DeckModelCall& modelCall,
                          const QString& prompt,
                          const QString& failurePrefix) {
    if (!modelCall) {
        return failedDeck(QStringLiteral("没有可用的模型调用"));
    }
    QString callError;
    const QByteArray raw = modelCall(prompt, &callError);
    if (!callError.isEmpty()) {
        return failedDeck(QStringLiteral("%1：%2").arg(failurePrefix, callError));
    }
    if (raw.trimmed().isEmpty()) {
        return failedDeck(QStringLiteral("%1：模型没有返回内容").arg(failurePrefix));
    }
    return parseDeckResponse(raw);
}

}  // namespace

PptDeck generateDeckFromTopic(const QString& topic,
                               const QString& instruction,
                               int maxSlides,
                               const DeckModelCall& modelCall) {
    const QString trimmedTopic = topic.trimmed();
    if (trimmedTopic.isEmpty()) {
        return failedDeck(QStringLiteral("主题不能为空"));
    }
    if (!modelCall) {
        return failedDeck(QStringLiteral("没有可用的模型调用，无法由主题生成 PPT"));
    }

    const int limit = clampMaxSlides(maxSlides);
    PptDeck deck = askModelAndParse(modelCall,
                                    topicPrompt(trimmedTopic, instruction, limit),
                                    QStringLiteral("模型调用失败"));
    if (!deck.error.isEmpty()) {
        return deck;
    }

    deck.source = PptSource::Topic;
    deck.theme = trimmedTopic;
    deck.slides = truncateAndRenumber(deck.slides, limit);
    if (deck.title.trimmed().isEmpty()) {
        // 模型没给标题就用主题顶上：PPT 总得有个标题。
        deck.title = trimmedTopic;
    }
    return deck;
}

PptDeck generateDeckFromDocument(const QString& documentText,
                                  const QString& title,
                                  int maxSlides,
                                  const DeckModelCall& modelCall) {
    const QString trimmedText = documentText.trimmed();
    if (trimmedText.isEmpty()) {
        return failedDeck(QStringLiteral("文档内容为空"));
    }

    const int limit = clampMaxSlides(maxSlides);
    // 调用方显式给的标题最权威：那是用户自己写的，
    // 没给时才轮到模型拟的，再没有才从正文里推一个。
    const QString explicitTitle = title.trimmed();

    if (modelCall) {
        PptDeck deck = askModelAndParse(modelCall,
                                        documentPrompt(trimmedText, limit),
                                        QStringLiteral("模型调用失败"));
        if (!deck.error.isEmpty()) {
            return deck;
        }
        deck.source = PptSource::Document;
        deck.slides = truncateAndRenumber(deck.slides, limit);
        if (!explicitTitle.isEmpty()) {
            deck.title = explicitTitle;
        } else if (deck.title.trimmed().isEmpty()) {
            deck.title = deriveTitleFromDocument(trimmedText);
        }
        deck.theme = deck.title;
        return deck;
    }

    // 没有模型：本地提炼。
    PptDeck deck;
    deck.source = PptSource::Document;
    deck.title = explicitTitle.isEmpty() ? deriveTitleFromDocument(trimmedText) : explicitTitle;
    deck.theme = deck.title;
    deck.slides = truncateAndRenumber(localSlidesFromDocument(trimmedText), limit);
    if (deck.slides.isEmpty()) {
        return failedDeck(QStringLiteral("文档里没有提炼出任何内容"));
    }
    return deck;
}

PptDeck generateDeckFromOutline(const QString& outlineText,
                                 const QString& title,
                                 int maxSlides) {
    const QString trimmedText = outlineText.trimmed();
    if (trimmedText.isEmpty()) {
        return failedDeck(QStringLiteral("大纲内容为空"));
    }

    const int limit = clampMaxSlides(maxSlides);
    PptDeck deck;
    deck.source = PptSource::Outline;
    deck.slides = truncateAndRenumber(parseOutline(trimmedText), limit);
    if (deck.slides.isEmpty()) {
        return failedDeck(
            QStringLiteral("大纲里没有识别出任何页面（页面标题需以 - 或数字开头）"));
    }
    const QString trimmedTitle = title.trimmed();
    deck.title = trimmedTitle.isEmpty() ? deck.slides.first().title : trimmedTitle;
    deck.theme = deck.title;
    return deck;
}

QString deckToMarkdown(const PptDeck& deck) {
    QString markdown;
    if (!deck.title.trimmed().isEmpty()) {
        markdown += QStringLiteral("# %1\n").arg(deck.title.trimmed());
    }
    for (int position = 0; position < deck.slides.size(); ++position) {
        const Slide& slide = deck.slides.at(position);
        // 页码优先用 deck 里记的；没记（自己拼的 deck）就用位置，
        // 免得导出一份「第 0 页」的 PPT。
        const int pageNumber = slide.index > 0 ? slide.index : position + 1;
        markdown += QStringLiteral("\n## 第 %1 页：%2\n")
                       .arg(QString::number(pageNumber), slide.title);
        if (!slide.subtitle.trimmed().isEmpty()) {
            markdown += QStringLiteral("\n%1\n").arg(slide.subtitle.trimmed());
        }
        for (const QString& bullet : slide.bullets) {
            if (bullet.trimmed().isEmpty()) {
                continue;
            }
            markdown += QStringLiteral("\n- %1").arg(bullet.trimmed());
        }
        if (!slide.notes.trimmed().isEmpty()) {
            markdown += QStringLiteral("\n\n> 备注：%1").arg(slide.notes.trimmed());
        }
    }
    return markdown;
}

PptDeck parseDeckResponse(const QByteArray& jsonBytes) {
    if (jsonBytes.trimmed().isEmpty()) {
        return failedDeck(QStringLiteral("模型返回的内容为空"));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failedDeck(
            QStringLiteral("模型返回的内容不是合法 JSON：%1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return failedDeck(QStringLiteral("模型返回的 JSON 顶层不是对象"));
    }

    const QJsonObject root = document.object();
    const QJsonValue slidesValue = root.value(QString::fromLatin1(kKeySlides));
    if (slidesValue.isUndefined() || slidesValue.isNull()) {
        return failedDeck(QStringLiteral("模型返回的 JSON 缺少 slides 字段"));
    }
    if (!slidesValue.isArray()) {
        return failedDeck(QStringLiteral("模型返回的 slides 不是数组"));
    }
    const QJsonArray slideArray = slidesValue.toArray();
    if (slideArray.isEmpty()) {
        return failedDeck(QStringLiteral("模型返回的 slides 为空"));
    }

    PptDeck deck;
    QString fieldError;
    if (!readOptionalString(root, QString::fromLatin1(kKeyTitle), &deck.title, &fieldError)) {
        return failedDeck(fieldError);
    }

    for (int index = 0; index < slideArray.size(); ++index) {
        const QJsonValue slideValue = slideArray.at(index);
        if (!slideValue.isObject()) {
            return failedDeck(QStringLiteral("slides 第 %1 项不是对象").arg(index + 1));
        }
        const QJsonObject slideObject = slideValue.toObject();

        Slide slide;
        if (!readOptionalString(slideObject, QString::fromLatin1(kKeyTitle), &slide.title,
                                &fieldError)) {
            return failedDeck(fieldError);
        }
        if (!readOptionalString(slideObject, QString::fromLatin1(kKeySubtitle), &slide.subtitle,
                                &fieldError)) {
            return failedDeck(fieldError);
        }
        if (!readOptionalString(slideObject, QString::fromLatin1(kKeyNotes), &slide.notes,
                                &fieldError)) {
            return failedDeck(fieldError);
        }

        const QJsonValue bulletsValue = slideObject.value(QString::fromLatin1(kKeyBullets));
        if (!bulletsValue.isUndefined() && !bulletsValue.isNull()) {
            if (!bulletsValue.isArray()) {
                return failedDeck(
                    QStringLiteral("slides 第 %1 项的 bullets 不是数组").arg(index + 1));
            }
            const QJsonArray bulletArray = bulletsValue.toArray();
            for (int bulletIndex = 0; bulletIndex < bulletArray.size(); ++bulletIndex) {
                const QJsonValue bulletValue = bulletArray.at(bulletIndex);
                if (!bulletValue.isString()) {
                    return failedDeck(QStringLiteral("slides 第 %1 项的第 %2 条要点不是字符串")
                                          .arg(QString::number(index + 1),
                                               QString::number(bulletIndex + 1)));
                }
                slide.bullets.append(bulletValue.toString());
            }
        }

        slide.index = index + 1;
        deck.slides.append(slide);
    }
    return deck;
}

}  // namespace aipdf::ai
