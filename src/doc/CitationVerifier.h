#pragma once

#include <QString>
#include <QVector>

namespace aipdf::doc {

// 交给模型的那一块上下文（带引用编号）。
struct CitationBlock {
    // 引用编号（1 起）。模型回答里的 [n] 指的就是它。
    int number = 0;
    // 页序号（0 起）。
    int pageIndex = -1;
    // 块文本。
    QString text;
};

// 模型给出的一条引用。
struct ParsedCitation {
    // 引用编号。
    int number = 0;
    // 模型写的页码（1 起）；模型没写时是 0。
    int pageNumber = 0;
    // 模型引用的原文片段。
    QString quote;
    // 核验是否通过。
    bool valid = false;
    // 没通过的原因（中文，直接显示给用户）。
    QString reason;
};

// 引用核验报告。
struct CitationReport {
    // 解析出来的引用（含无效的）。
    QVector<ParsedCitation> citations;
    // 通过核验的引用数。
    int validCount = 0;
    // 没通过的引用数。
    int invalidCount = 0;
    // 正文里的 [n] 标记（去重、升序）。
    QVector<int> markers;
    // 正文里有没有指向无效引用的标记。
    bool hasUnsupportedMarker = false;
    // 有没有可核实的证据：至少一条引用通过核验。
    bool hasEvidence() const { return validCount > 0; }
};

// 引用核验器：把模型的回答与原文对一遍。
//
// 模型说的引用**不能直接信**（v0.9.0 的设计里就写了这句）。
// 这里做三项检查，全部是确定性的字符串核对，不需要再调一次模型：
//
//   1. **存在性**：引用的编号必须在这次给模型的上下文块里。
//      模型凭空造一个编号，这条引用直接作废。
//   2. **引文核对**：模型引的那段原文，必须真的出现在被引用的那一块里。
//      编号对、但引文是编的——这是最常见的一种伪造，必须挡住。
//   3. **页码一致**：模型写的页码必须与那一块的实际页码一致。
//      不一致的话，用户点引用会跳到错误的页。
//
// 为什么不用「第二次模型调用做蕴含判断」（业界说的 entailment check）：
// 那会让每次提问的成本与延迟翻倍，而且在桌面端不可靠——
// 小模型做蕴含判断本身就可能出错，等于用一个不可靠的检查去守另一个不可靠的输出。
// 逐字核对虽然更严（模型改写一个字就判无效），但它**确定**：
// 判无效只是把引用标成「无法核实」，答案正文照样给用户看，不会把好内容一起丢掉。
class CitationVerifier {
public:
    // 解析并核验。
    static CitationReport verify(const QString& answer,
                                 const QVector<CitationBlock>& blocks);

    // 只解析回答末尾的引用段（公开出来是为了能直接单测格式解析）。
    static QVector<ParsedCitation> parseCitations(const QString& answer);

    // 抽出正文里的 [n] 标记（不含引用段里的）。
    static QVector<int> extractMarkers(const QString& answer);

    // 引文是否出现在块文本里（忽略空白与全半角差异）。
    static bool quoteAppearsIn(const QString& quote, const QString& blockText);

    // 回答里引用段之前的正文（不含引用段）。
    static QString answerBody(const QString& answer);

private:
    // 按编号找上下文块（找不到返回 nullptr）。
    static const CitationBlock* blockByNumber(const QVector<CitationBlock>& blocks,
                                              int number);
};

}  // namespace aipdf::doc
