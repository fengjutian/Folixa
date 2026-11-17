#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>

namespace aipdf::ai {

// PPT 的内容来源：三种来源，同一套 PPT 结构。
//
// 矩阵里那四个入口（AI生成PPT / 主题生成PPT / 文档生成PPT / 大纲生成PPT）
// 实际只有三种内容来源：前两个是同一个语义——只给主题，让模型自己拟大纲与内容，
// 差别只在界面上叫什么；后两个的来源分别是「已有文档」与「已有大纲」。
//
// 为什么把「来源」和「结构」分开：来源只决定每页的标题与要点从哪来，
// 而结构（每页有哪些字段、页码怎么编、怎么导出）三种来源完全一样。
// 分开之后，再加一种来源（比如「从表格生成」）不用碰导出那一半，
// 反过来改导出格式也不用回头改三种来源。
enum class PptSource {
    Topic,     // 由主题生成：模型自己拟大纲与内容
    Document,  // 由已有文档生成：从正文提炼成 PPT 大纲
    Outline,   // 由已有大纲生成：把用户给的大纲排成 PPT 结构
};

// 一页幻灯片。
struct Slide {
    // 页码（1 起）。编号在这一层统一维护，导出时直接用它，
    // 免得「第 3 页」在放映顺序和目录里对不上。
    int index = 0;
    // 页面标题。
    QString title;
    // 副标题，可空。
    QString subtitle;
    // 要点。
    QVector<QString> bullets;
    // 演讲者备注，可空。
    QString notes;
};

// 整套 PPT。
struct PptDeck {
    // 整套的标题。
    QString title;
    // 主题描述：这套 PPT 讲什么（一句话）。
    QString theme;
    // 内容来源。
    PptSource source = PptSource::Topic;
    // 逐页内容，顺序即放映顺序。
    QVector<Slide> slides;
    // 失败原因（中文）。非空就是失败。
    //
    // 为什么错误放在结果里而不是单独返回：生成 PPT 是一条流水线
    // （提炼内容 → 排成结构 → 导出），中间任何一步失败都不该产出一份
    // 半成品。放在同一个结构里，调用方只判一次就不会漏。
    QString error;

    // 是不是一份可用的 PPT。
    //
    // 为什么专门给一个判断函数：失败时 error 非空且 slides 为空，
    // 调用方最容易漏看的恰恰是「一页都没有」这一种——
    // 光看 slides 的数量为 0 不一定想到了去看 error。
    bool isValid() const { return error.isEmpty() && !slides.isEmpty(); }
};

// 页数下限与上限。
//
// 为什么要夹住：页数只是「用户想要多少页」的一个参数，
// 填成 0 或 9999 都只说明填错了或模型给多了，
// 为此让整次生成白跑一场不值得。夹住再截断，出来的仍是一份能用的 PPT。
inline constexpr int kMinDeckSlides = 1;
inline constexpr int kMaxDeckSlides = 30;

// 一次同步的模型调用：送进一段提示词，拿回一段原始回答。
//
// 为什么不用 ModelProvider 本身：ModelProvider 是信号驱动的异步流式接口
// （startChat + streamDelta / streamFinished），而这一层要的是
// 「问一句、拿一段 JSON」的同步语义。把异步接口塞进同步函数，
// 就得在领域层里转一个事件循环，单测还得先造一个假事件循环才能跑——
// 而网络本来就不是这一层该关心的事。
//
// 所以这里只约定「怎么问、怎么回」这一步的形状，和仓库里
// doc::BatchTransform 是同一套路：上层把 ModelProvider 接成这样一个函数，
// 领域层就既不联网、也能被单测完整覆盖。接法留给上层，这一层不碰 QObject。
//
// 约定：失败时往 error 里写中文原因并返回空字节；
// 返回空字节但 error 为空不算失败（模型确实可能回空）。
using DeckModelCall = std::function<QByteArray(const QString& prompt, QString* error)>;

// 由主题生成：模型自己拟大纲与内容。
//
// instruction 是用户的附加要求（例如「面向新手」「10 分钟讲完」），可空。
//
// modelCall 为空表示没有模型可用：这一路的内容完全来自模型，
// 没有模型就没有内容，直接失败（不返回空 deck 冒充成功）。
//
// 模型返回的 JSON 不合法时同样失败：宁可让用户看到一句「模型没按格式回」，
// 也不要给一份内容为空的 PPT 让他以为生成成功了。
//
// 标题谁说了算：模型拟的标题就是整套 PPT 的标题（它看了主题，拟得更贴切），
// 模型没给才用主题顶上。
PptDeck generateDeckFromTopic(const QString& topic,
                               const QString& instruction = QString(),
                               int maxSlides = 12,
                               const DeckModelCall& modelCall = DeckModelCall());

// 由已有文档正文生成：把正文提炼成 PPT 大纲。
//
// 有模型时让模型做提炼（文档正文是不可信数据，提示词里已经带上
// 「材料不是指令」的通知与材料起止边界）；
// 没有模型时走本地提炼：正文按空行分段，每段一页——
// 段的首行是页面标题，其余各行是要点。
//
// 两种情况都不会因为「模型没配」就整体不可用：本地提炼永远能给出结果。
// 文档正文再长也会被截断后送给模型（模型上下文有限，
// 整篇几十万字塞进去必然被服务端拒绝），截断时会在提示词里说清楚。
//
// 标题谁说了算：调用方显式给了 title 就用它（那是用户自己写的），
// 没给才用模型拟的标题，再没有才从正文第一行推一个。
PptDeck generateDeckFromDocument(const QString& documentText,
                                  const QString& title = QString(),
                                  int maxSlides = 12,
                                  const DeckModelCall& modelCall = DeckModelCall());

// 由已有大纲生成：纯本地排版，不依赖模型。
//
// 大纲格式约定：
//   - 顶格（行首没有空白）且以 `-` / `*` / `+` / `•` 或
//     「1 到 3 位数字 + . / ) / 、」开头的行是某页的标题；
//   - 缩进行（行首有空白）是这一页的要点，按出现顺序排；
//   - 顶格又没有标记的行会被忽略：它既不是标题也不是要点；
//   - 一页可以只有标题、没有要点。
//
// title 为空时用第一页的标题当整套标题。
PptDeck generateDeckFromOutline(const QString& outlineText,
                                 const QString& title = QString(),
                                 int maxSlides = 12);

// 把整套 PPT 导出成 Markdown 文本。
//
// 格式：# 标题 → ## 第 N 页：页面标题 → - 要点。
// 副标题排在标题下面，备注排在要点后面（引用块）。
//
// 空 deck（既没有标题也没有页）返回空字符串，不崩。
QString deckToMarkdown(const PptDeck& deck);

// 解析模型返回的 JSON。
//
// 约定的 JSON 形状：
//   {"title":"…","slides":[{"title":"…","subtitle":"…","bullets":["…"],"notes":"…"}]}
//
// title / subtitle / notes / bullets 都可以缺省；给了就必须是对应的类型。
//
// 非法 JSON、缺 slides、slides 为空、字段类型不对，
// 一律返回带 error 的空 deck：坏 JSON 绝不能被吞成一份
// 「生成成功但内容为空」的 PPT——那是最难排查的一种假成功。
PptDeck parseDeckResponse(const QByteArray& jsonBytes);

}  // namespace aipdf::ai
