#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一条替换规则。
//
// 为什么规则是数据而不是回调：界面要把规则收集成一张表给用户看、
// 允许逐条勾选、允许改顺序、能存成预设。规则一旦是数据，这些事都好办；
// 回调就没法存、没法展示顺序。
struct ReplaceRule {
    QString find;                 // 查找内容
    QString replace;              // 替换成什么
    bool caseSensitive = false;   // 大小写敏感
    bool wholeWord = false;       // 全字匹配：命中前后不能是字母/数字/下划线
    bool regex = false;           // 查找内容按正则解释
};

// 替换范围。
struct BatchReplaceOptions {
    QVector<int> pageIndexes;        // 为空表示整篇
    bool stopOnFirstError = false;   // 一条规则出错就停，不再执行后面的规则
};

// 每条规则的结果。
struct ReplaceRuleResult {
    QString find;             // 出错时把规则内容带上，方便界面定位到哪一条
    int replacements = 0;      // 这条规则实际改掉了几处
    bool ok = false;
    QString error;            // 中文错误信息，给开发/日志看，不是界面文案
};

// 整批替换的结果。
struct BatchReplaceResult {
    int rules = 0;                       // 规则条数
    int replacements = 0;                // 各条规则替换数之和
    QVector<ReplaceRuleResult> perRule; // 逐条结果，顺序与规则一致
    bool ok = false;
    QString error;
};

// 按顺序应用多条规则，返回改写后的整份 PDF 字节。
//
// 语义要点：
//   * 规则按给定顺序依次执行，**每条规则在上一条的结果上继续改**。
//     这与编辑器的「查找替换」一致：用户排的顺序就是他要的顺序，
//     「先把 A 换成 B 再把 B 换成 C」必须得到 C，而不是被第一条规则吃掉。
//   * 某条规则非法（查找内容为空、正则不合法）时，只有这一条规则失败：
//     结果里逐条记录错误，其余规则照常执行；只有 stopOnFirstError 为真时才中止。
//   * 一条都没匹配上时，返回的字节与输入等价（内容不变）。
//   * 整批失败（输入为空、没有规则、底层改写失败）时返回空 QByteArray，
//     并通过 error 给出中文原因。
//
// 领域层不放界面文案：这里的错误信息是给开发与日志看的，
// 用户看到的提示由 app 层自己拼。
QByteArray applyBatchReplace(const QByteArray& pdfBytes, const QVector<ReplaceRule>& rules,
                              const BatchReplaceOptions& options, BatchReplaceResult* result = nullptr,
                              QString* error = nullptr);

}  // namespace aipdf::pdf
