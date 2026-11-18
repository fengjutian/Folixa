#pragma once

#include <QString>
#include <QVector>

namespace aipdf::ai {

// 朗读分段里的一个「块」：一段能一口气念完的内容。
struct NarrationSegment {
    // 段序号（0 起）。
    int index = 0;
    // 这一段要念的原文（列表符号、引用符号这类不该念出来的标记已经去掉）。
    QString text;
    // 这一段念完停多久（毫秒）。
    int pauseAfterMs = 0;
    // 是否需要重音（标题行、要点行）。
    bool emphasize = false;
    // 段的类型：「标题」/「正文」/「列表项」/「引用」。
    QString kind;
};

// 一篇文本的朗读计划。
struct NarrationPlan {
    // 是否排出来了。false 时 error 里写中文原因。
    bool ok = false;
    // 排好的分段。
    QVector<NarrationSegment> segments;
    // 真正会念出来的字符数（空白不算）。
    int totalChars = 0;
    // 按每秒 4.5 个字估算的朗读时长（秒，向上取整，不含停顿）。
    int estimatedSeconds = 0;
    // 失败时的中文原因。
    QString error;
};

// 朗读前的文本清洗。
//
// 做三件事：把连续空白压掉；去掉页码行（"第 3 页" / "Page 3" / "- 3 -"）；
// 去掉明显的页眉页脚残留（整行网址、整行邮箱、短版权行）。
//
// 为什么必须洗：PDF 里抽出来的文本常常带着这些噪声，直接念出去就是
// 「第三页、第三页……」这种莫名其妙的重复。
QString cleanTextForNarration(const QString& text);

// 把整篇文本排成朗读分段。
//
// 按空行、标题行、列表项、引用行切段；文本为空（或洗完什么都不剩）时
// 返回 ok=false，并在 error 里写中文原因。
NarrationPlan planNarration(const QString& text);

}  // namespace aipdf::ai
