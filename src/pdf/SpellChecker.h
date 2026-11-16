#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 一个拼写问题。
struct SpellIssue {
    int start = 0;             // 在原文里的 UTF-16 起始下标
    int length = 0;            // 长度（UTF-16 码元数）
    QString word;              // 拼错的词（原样，保留大小写）
    QStringList suggestions;   // 建议（可能为空）
};

// 拼写检查器：内置常用词表 + 启发式规则 + 可加载的用户词典。
//
// 为什么不是完整词典：仓库里没有可用的词典资源，也不允许为此引入新依赖、
// 不允许联网下载。完整英文词典是十万词量级的数据，凭空手写既不现实也不可靠。
// 所以这里的口径是「一千个左右的高频词 + 若干启发式规则 + 用户自己的词典」。
//
// 为什么这样取舍：词表覆盖不到的词会被漏判，这是刻意的。
// 漏判只是少画一条波浪线；误判却会在满篇正常文字里到处画红线，
// 编辑器的可用性会被这一件事毁掉。所以规则只针对「明显可疑」的词。
class SpellChecker final {
public:
    // 内置检查器：常用英文词表 + 启发式规则。
    static SpellChecker builtin();

    // 加载用户词典（Hunspell .dic 的一个子集：每行一个词，
    // 可选 /FLAGS 后缀；# 开头是注释）。返回读进来的词数。
    //
    // 读不进来时返回 0，并通过 error 给出中文原因。
    int loadUserDictionary(const QString& path, QString* error = nullptr);

    // 追加自定义词（不落盘）。
    void addWord(const QString& word);

    // 检查一段文本。中文按「连续汉字」跳过（中文不做拼写检查）。
    QVector<SpellIssue> check(const QString& text) const;

    // 某个词是否在词典里（大小写不敏感）。
    bool isKnownWord(const QString& word) const;

    // 给一个拼错的词找建议（编辑距离 1 内的候选，最多 maxCount 个）。
    // 先常见后罕见：按内置词表里的次序排，用户词典里的词排在最后。
    QStringList suggestions(const QString& word, int maxCount = 3) const;

private:
    SpellChecker() = default;

    void insertWord(const QString& word, int rank);
    // 宽松查找：除了原形，还认常见的所有格与复数、过去式、进行时尾巴。
    // 内置词表只有一千个词，不做这一步的话 "cars"、"running" 会被当成生词。
    bool isKnownRelaxed(const QString& lower) const;
    QStringList collectNeighbors(const QString& word, int maxCount, bool allowTransposition) const;

    QSet<QString> m_words;                  // 已知词（小写）
    QHash<QString, int> m_ranks;             // 词在词表里的次序，用来给建议排序
    QHash<int, QVector<QString>> m_byLength; // 按长度分桶：找近邻词时只看相邻长度
    QHash<QString, QString> m_misspellings;  // 常见拼错对 → 正确写法
    int m_nextRank = 0;                      // 用户词典里的词排在内置词表之后
};

}  // namespace aipdf::pdf
