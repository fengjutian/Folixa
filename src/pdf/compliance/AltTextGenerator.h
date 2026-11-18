#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/compliance/RoleInference.h"

namespace aipdf::pdf {

// 替代文本的一条问题。
struct AltTextIssue {
    // 出问题的是哪一块（页序号 + 名字）。
    QString where;
    // 中文原因：为什么没有替代文本。
    QString reason;
};

// 替代文本生成的结果。
struct AltTextResult {
    // 每一块对应的替代文本，与传入的块一一对应。
    // 空串表示**没有依据**，不给它写 /Alt。
    QStringList alternatives;
    // 表格的替代文本：表格里第一个块的序号 → 替代文本（取自表头文字）。
    QHash<int, QString> tableAlternatives;
    // 没有依据的那些块，逐条说明原因。
    QVector<AltTextIssue> issues;
};

// 给图 / 表格 / 表单域生成替代文本（/Alt）。
//
// **纪律：没有依据就返回空串并记一条 issue，绝不编一句。**
// 编出来的替代文本比没有替代文本更坏——读屏软件会把它当成作者写的说明念出来，
// 而那句话可能完全是错的。
//
// 依据从哪来（都取自文档本身，不生成新句子）：
//   * 表单域：域的提示文字（/TU）；没有就用字段名（/T）；
//   * 图：紧挨着图的那一行说明文字（以「图」「Figure」「图片」开头的那种）；
//     认不出来就**不用**，记一条 issue；
//   * 表格：第一行（表头）各格的文字，按顺序用「、」连起来；
//     没有表头就**不用**，记一条 issue。
AltTextResult generateAltTexts(const QVector<SemanticBlock>& blocks);

}  // namespace aipdf::pdf
