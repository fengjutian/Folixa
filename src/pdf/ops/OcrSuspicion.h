#pragma once

#include <QString>
#include <QVector>

#include "pdf/kernel/TextPage.h"

namespace aipdf::pdf {

// 一个可疑字符：OCR 认出来的字，以及它最可能应该是的那个字。
struct SuspiciousChar {
    // 所在页号（0 基）。
    int pageIndex = 0;
    // 上下文片段：可疑字符所在的那一串（前后各带一点上下文）。
    QString context;
    // OCR 认出来的字。
    QString recognized;
    // 建议替换成的字（形近字）。
    QString suggestion;
    // 中文理由：形近 / 上下文不符 / 低置信度。
    QString reason;
};

// 在 OCR 文字层里找出可疑字符。
//
// 扫描来源的优先级（为什么不反过来）：
//   1. 各页的文本层（TextPage::plainText()）——这样每条结论都能落到具体页；
//   2. 所有页都没有文本时，才用 recognizedText 作为整篇文本（归到第 1 页）。
//
// 为什么只在「数字 / 英文单词语境」里提示：纯中文正文里出现 0 或 O 是完全正常的
// （型号、编号都可能写 0），不分语境地报，误报会多到没人愿意看这份报告。
//
// 判定的具体规则（形近字表见实现）：
//   - 一串 ASCII 字母数字里，如果数字比字母少 → 数字是可疑的（英文单词里混进了数字）；
//   - 如果字母比数字少 → 字母是可疑的（数字串里混进了字母）；
//   - 全字母的一串里，rn 可能是被拆开的 m。
//
// 结果按页码、页内位置排序。
QVector<SuspiciousChar> findSuspiciousCharacters(const QVector<TextPage>& pages,
                                                 const QString& recognizedText);

}  // namespace aipdf::pdf
