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

// 一个词表来源的加载结果。
//
// 为什么单独一个类型：词表加载失败必须能被调用方看到，
// 不能悄悄退回内置词表继续跑——那样用户会以为自己的词表生效了。
struct DictionaryLoadResult {
    QString path;              // 词表路径（内置词表为空）
    bool ok = false;           // 是否加载成功
    int wordCount = 0;         // 读进来的词条数
    QString error;             // 失败原因（中文）；成功时为空
};

// 拼写检查器：内置词表 + 启发式规则 + 可加载的用户词典。
//
// 词典来源是可插拔的：内置词表始终在，用户/外部词表按需追加。
// 外部词表是 UTF-8 文本，一行一个词：
//   - 空行与 # 开头的行是注释；
//   - Hunspell 的 /FLAGS 后缀会被去掉（zzword/ABC → zzword）；
//   - 纯数字的行不是词，会被跳过。
//
// 为什么漏判可以接受、误判不行：
// 词表覆盖不到的词（专有名词、技术术语）会被漏判——这只是少画一条波浪线；
// 而误判会在满篇正常文字里到处画红线，用户很快就不信这条波浪线了。
// 因此下面所有规则都只盯「明显可疑」的词：常见拼错对、连续重复的词、
// 与常用词只差一个编辑的近邻词、句中大小写异常。
class SpellChecker final {
public:
    // 内置检查器：常用英文词表 + 启发式规则（不查中文）。
    // 这是既有行为，调用方不受影响。
    static SpellChecker builtin();

    // 检查器的配置。
    struct Options {
        // 追加的外部词表路径（UTF-8 文本，一行一词）。加载失败的会在
        // results 里如实报出来；strictDictionaries 为 true 时整个检查器作废。
        QStringList dictionaryPaths;
        bool chineseEnabled = true;      // 是否检查中文
        bool englishEnabled = true;      // 是否检查英文
        // true：任一词表加载失败就认为检查器不可用（isUsable() 返回 false，
        // check() 不再给结果），避免「词表没加载上却照常检查」的假象。
        bool strictDictionaries = false;
    };

    // 按配置创建检查器：内置词表 + 外部词表。
    // results（可为空）里是每个词表来源的加载结果，失败原因写在里面。
    static SpellChecker create(const Options& options, QVector<DictionaryLoadResult>* results = nullptr);

    // 外部词表的默认搜索位置（用户把自定义词表放这里就会被自动读到）。
    static QStringList defaultDictionaryPaths();

    // 词表是否可用。strictDictionaries 下加载失败会是 false。
    bool isUsable() const;
    // 各词表来源的加载结果（含失败原因）。
    QVector<DictionaryLoadResult> dictionaryResults() const;

    // 加载用户词典（Hunspell .dic 的一个子集：每行一个词，
    // 可选 /FLAGS 后缀；# 开头是注释）。返回读进来的词数。
    //
    // 读不进来时返回 0，并通过 error 给出中文原因。
    // 纯中文的词条会进中文词表，其余进英文词表。
    int loadUserDictionary(const QString& path, QString* error = nullptr);

    // 追加自定义词（不落盘）。
    void addWord(const QString& word);

    // 检查一段文本：按配置检查英文与中文，结果按位置排序。
    QVector<SpellIssue> check(const QString& text) const;
    // 只检查英文（等价于旧版 check 的行为）。
    QVector<SpellIssue> checkEnglish(const QString& text) const;
    // 只检查中文：形近/音近错字 + 词组级错字。
    QVector<SpellIssue> checkChinese(const QString& text) const;

    // 某个词是否在词典里（大小写不敏感）。
    bool isKnownWord(const QString& word) const;
    // 某个中文词是否在中文词表里。
    bool isKnownChineseWord(const QString& word) const;

    // 给一个拼错的词找建议（编辑距离 1 内的候选，最多 maxCount 个）。
    // 先常见后罕见：按内置词表里的次序排，用户词典里的词排在最后。
    QStringList suggestions(const QString& word, int maxCount = 3) const;

    // 内置词表规模（供测试与报告用）。
    static int builtinEnglishWordCount();
    static int builtinChineseWordCount();
    static int builtinCommonWordCount();
    static int builtinConfusableGroupCount();
    static int builtinHomophoneGroupCount();
    static int builtinPhraseTypoCount();

private:
    SpellChecker() = default;

    void insertWord(const QString& word, int rank);
    void insertChineseWord(const QString& word);
    // 宽松查找：除了原形，还认常见的所有格与复数、过去式、进行时等变体。
    bool isKnownRelaxed(const QString& lower) const;
    bool isKnown(const QString& lower) const;
    bool isUserKnown(const QString& lower) const;
    bool isKnownChinese(const QString& word) const;
    bool isKnownChineseCommon(const QString& word) const;
    QStringList collectNeighbors(const QString& word, int maxCount, bool allowTransposition) const;
    QVector<SpellIssue> checkEnglishImpl(const QString& text) const;
    QVector<SpellIssue> checkChineseImpl(const QString& text) const;

    QSet<QString> m_userWords;                 // 用户词典里的英文词（小写）
    QHash<QString, int> m_userRanks;            // 用户词的次序（排在内置词表之后）
    QHash<int, QVector<QString>> m_userByLength;// 用户词按长度分桶
    QSet<QString> m_userChineseWords;          // 用户词典里的中文词
    QVector<DictionaryLoadResult> m_dictionaryResults;
    int m_nextRank = 0;                        // 用户词的次序（排在内置词表之后）
    bool m_chineseEnabled = false;
    bool m_englishEnabled = true;
    bool m_usable = true;
};

}  // namespace aipdf::pdf
