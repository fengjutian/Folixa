// 2026-11-19 wave21：拼写检查专项用例。
//
// 覆盖四件事：
//   1) 英文：常见错词能命中并给出正确建议；一组合法词不误报（带误报率上限断言）。
//   2) 中文：形近/音近错字能命中并给建议；一段正确中文不误报（带误报率上限断言）。
//   3) 词典可插拔：外部词表加载成功 / 加载失败两条路径都要有结果。
//   4) 既有行为不变：builtin() 仍然只查英文，既有 SpellIssue 字段语义不变。

#include "pdf/ops/SpellChecker.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTest>

using aipdf::pdf::DictionaryLoadResult;
using aipdf::pdf::SpellChecker;
using aipdf::pdf::SpellIssue;

namespace {

// 编辑距离（Levenshtein）：用来独立验一遍建议到底差多少。
int editDistance(const QString& a, const QString& b) {
    if (a == b) return 0;
    if (a.isEmpty()) return b.size();
    if (b.isEmpty()) return a.size();
    QVector<int> previous(b.size() + 1);
    QVector<int> current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        previous = current;
    }
    return previous[b.size()];
}

// 常见英文手误：错词 -> 对词。
// 既有的是「常见拼错对表」里的条目，另有几条是「只差一个字母」的近邻词，
// 用来守住近邻规则这条线。
struct EnglishCase {
    const char* wrong;
    const char* right;
};

const EnglishCase kEnglishCases[] = {
    {"recieve", "receive"},
    {"seperate", "separate"},
    {"definately", "definitely"},
    {"occured", "occurred"},
    {"wich", "which"},
    {"teh", "the"},
    {"adn", "and"},
    {"taht", "that"},
    {"thier", "their"},
    {"grammer", "grammar"},
    {"documant", "document"},
    {"goverment", "government"},
    {"independant", "independent"},
    {"neccessary", "necessary"},
    {"publically", "publicly"},
};

// 一组正确的英文词：常见词 + 技术/领域词 + 偏生僻但确实正确的词。
// 生僻词是这一组的关键：它们不在内置词表里，最容易被近邻规则误报。
const char* const kCorrectEnglishWords[] = {
    // 常用词
    "the","be","to","of","and","in","that","have","this","with","from","they","what",
    "when","make","like","time","just","know","take","people","into","year","your",
    "good","some","them","see","other","than","then","now","look","only","come","its",
    "over","think","also","back","after","use","two","how","our","work","first","well",
    "way","even","new","want","because","any","these","give","day","most","been","has",
    // 常用动词/名词的变形
    "walked","walking","runs","running","studies","studied","boxes","carries","carried",
    "happier","happiest","quickly","happiness","national","ability","organization",
    // PDF 编辑器领域词
    "workflow","metadata","bookmark","annotation","signature","hyperlink","checkbox",
    "dropdown","template","templates","widgets","document","documents","paragraph",
    "paragraphs","reader","writer","editor","viewer","printer","folder","filename",
    "database","database","keyboard","clipboard","toolbar","shortcut","thumbnail",
    // 偏生僻但正确的词（很多不在内置词表里）
    "abjure","abnegate","abscond","abstemious","obfuscate","perfunctory","quixotic",
    "susurrus","ineffable","lachrymose","pellucid","recondite","sagacious","tenacious",
    "truculent","ubiquitous","venerable","winsome","xenophobic","yearning","zealous",
    "antidisestablishmentarianism","floccinaucinihilipilification","pneumonoultramicroscopic",
    // 专有名词（首字母大写不应该被判成错字）
    "kubernetes","kubernetes","qpdf","pdfium","minGW","windows","linux","qtCreator",
};

const char* const kCorrectChineseWords[] = {
    "自己","已经","未来","工作","发展","中国","问题","没有","句子","中文","英文",
    "我们","什么","因为","所以","但是","如果","虽然","然后","这样","那些","可以",
    "应该","需要","认为","觉得","发现","开始","出现","进行","通过","对于","关于",
    "以及","或者","而且","由于","一个","他们","不是","公司","人民","社会","知道",
    "国家","经济","全国","重要","技术","文化","企业","地区","主要","地方","起来",
    "现在","可能","也许","大概","几乎","完全","比较","更加","方式","方法","过程",
    "结果","情况","条件","环境","系统","功能","版本","界面","按钮","菜单","窗口",
    "文件","图片","表格","内容","数据","用户","账号","密码","设置","选项","默认",
    "安装","卸载","保存","打开","关闭","删除","恢复","备份","导出","导入","打印",
    "复制","粘贴","撤销","搜索","替换","插入","注释","签名","加密","解密","权限",
    "配置","更新","升级","下载","上传","网络","服务器","客户端","源代码","编译器",
    "脚本","插件","扩展","组件","模块","算法","接口","协议","文档","教程","示例",
    "项目","开发","测试","发布","缺陷","需求","设计","实现","部署","监控","日志",
    "缓存","队列","线程","进程","并发","性能","优化","重构","单元","集成","用例",
};

// 真实中文文本：《岳阳楼记》节选（宋·范仲淹，公有领域）。
// 用它量误报率：古典汉语里也有不少词不在常用词表里，
// 正是「会不会乱报」的照妖镜。
const char* const kRealChineseText =
    "庆历四年春，滕子京谪守巴陵郡。越明年，政通人和，百废具兴。乃重修岳阳楼，"
    "增其旧制，刻唐贤今人诗赋于其上。属予作文以记之。予观夫巴陵胜状，在洞庭一湖。"
    "衔远山，吞长江，浩浩汤汤，横无际涯；朝晖夕阴，气象万千。此则岳阳楼之大观也。"
    "前人之述备矣。然则北通巫峡，南极潇湘，迁客骚人，多会于此，览物之情，得无异乎。";

// 真实中文文本：白话说明文片段（自撰，句子结构与日常文档一致）。
const char* const kModernChineseText =
    "这份文档介绍了编辑器的使用方法。打开文件以后，可以在左侧的页面列表里选择页面，"
    "也可以直接使用快捷键跳转到指定位置。修改内容以后，记得先保存再关闭，"
    "否则系统会提示是否放弃修改。打印和导出功能都在文件菜单里面，"
    "如果打印机没有连接，可以先导出成图片或者文本文件。遇到问题时，"
    "可以查看帮助文档，里面有常见问题的说明。";

int countIssues(const QVector<SpellIssue>& issues, const QString& needle) {
    int count = 0;
    for (const SpellIssue& issue : issues)
        if (issue.word == needle) ++count;
    return count;
}

bool hasIssueWithSuggestion(const QVector<SpellIssue>& issues, const QString& wrong, const QString& right) {
    for (const SpellIssue& issue : issues) {
        if (issue.word != wrong) continue;
        if (issue.suggestions.contains(right)) return true;
    }
    return false;
}

// 一个错写可能被报成两种形态：整词（「迫不急待」→「迫不及待」），
// 或者只报那个错字（「按」→「安」）。两种都算命中。
bool reportsCorrection(const QVector<SpellIssue>& issues, const QString& text, const QString& wrong,
                       const QString& right) {
    if (hasIssueWithSuggestion(issues, wrong, right)) return true;
    if (wrong.size() != right.size()) return false;
    const int base = text.indexOf(wrong);
    if (base < 0) return false;
    for (int i = 0; i < wrong.size(); ++i) {
        for (const SpellIssue& issue : issues) {
            if (issue.word != wrong.mid(i, 1)) continue;
            if (!issue.suggestions.contains(right.mid(i, 1))) continue;
            if (issue.start < base || issue.start >= base + wrong.size()) continue;  // 位置得在这个错词里
            return true;
        }
    }
    return false;
}

// 控制台是 GBK，中文打出来是乱码；把码点打出来才能定位问题。
QString describeIssue(const SpellIssue& issue) {
    QStringList codes;
    for (const QChar c : issue.word)
        codes << QStringLiteral("U+%1").arg(c.unicode(), 4, 16, QLatin1Char('0'));
    return QStringLiteral("pos=%1 len=%2 %3 -> %4")
        .arg(issue.start)
        .arg(issue.length)
        .arg(codes.join(QLatin1Char(',')))
        .arg(issue.suggestions.join(QLatin1Char(',')));
}

// 汉字个数：中文误报率按「每百字」算才有意义。
int countHanChars(const QString& text) {
    int count = 0;
    for (const QChar c : text) {
        const ushort code = c.unicode();
        if ((code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF))
            ++count;
    }
    return count;
}

}  // namespace

class Wave21SpellTests : public QObject {
    Q_OBJECT

private slots:
    void englishDictionaryIsLargeEnough();
    void commonMisspellingsAreCaught();
    void suggestionsAreOneEditAway();
    void correctEnglishWordsAreNotFlagged();
    void englishFalsePositiveRateIsReported();

    void chineseShapeSimilarTyposAreCaught();
    void chineseHomophoneTyposAreCaught();
    void chinesePhraseTyposAreCaught();
    void correctChineseTextIsNotFlagged();
    void chineseFalsePositiveRateIsReported();

    void externalDictionaryIsLoaded();
    void missingExternalDictionaryReportsError();
    void strictModeRefusesToCheckWhenDictionaryFails();
    void builtinCheckerKeepsEnglishOnlyBehaviour();

    void userDictionaryStillWorks();
    void dictionaryLoadCountsAreReported();
};

void Wave21SpellTests::englishDictionaryIsLargeEnough() {
    // 需求：内置词表至少两万常用词。
    const int words = SpellChecker::builtinEnglishWordCount();
    QVERIFY2(words >= 20000, qPrintable(QStringLiteral("内置英文词表只有 %1 个词").arg(words)));
    QVERIFY(SpellChecker::builtinCommonWordCount() >= 10000);
    QVERIFY(SpellChecker::builtinChineseWordCount() >= 10000);
}

// 常见手误必须命中，而且建议里要有正确的写法。
void Wave21SpellTests::commonMisspellingsAreCaught() {
    const SpellChecker checker = SpellChecker::builtin();
    for (const EnglishCase& item : kEnglishCases) {
        const QString wrong = QString::fromLatin1(item.wrong);
        const QString right = QString::fromLatin1(item.right);
        const QString text = QStringLiteral("We %1 it every day.").arg(wrong);
        const QVector<SpellIssue> issues = checker.check(text);
        QVERIFY2(countIssues(issues, wrong) == 1,
                 qPrintable(QStringLiteral("%1 没有被报出来").arg(wrong)));
        QVERIFY2(hasIssueWithSuggestion(issues, wrong, right),
                 qPrintable(QStringLiteral("%1 的建议里没有 %2，实际：%3")
                                .arg(wrong, right, [&] {
                                    QStringList all;
                                    for (const SpellIssue& issue : issues)
                                        all << issue.suggestions.join(QLatin1Char(','));
                                    return all.join(QLatin1Char(';'));
                                }())));
        // 位置必须落在原文的那个词上。
        for (const SpellIssue& issue : issues) {
            if (issue.word != wrong) continue;
            QCOMPARE(issue.length, wrong.size());
            QCOMPARE(text.mid(issue.start, issue.length), wrong);
        }
    }
}

// 建议必须真的只差一个字符（增/删/改一个），不能给一堆不相干的词。
void Wave21SpellTests::suggestionsAreOneEditAway() {
    const SpellChecker checker = SpellChecker::builtin();
    const QStringList suggestions = checker.suggestions(QStringLiteral("documant"), 3);
    QVERIFY(!suggestions.isEmpty());
    QVERIFY(suggestions.contains(QStringLiteral("document")));
    for (const QString& suggestion : suggestions) {
        const int distance = editDistance(QStringLiteral("documant"), suggestion);
        QCOMPARE(distance, 1);
    }
}

// 一组正确词不该被报出来：误报率要有上限。
void Wave21SpellTests::correctEnglishWordsAreNotFlagged() {
    const SpellChecker checker = SpellChecker::builtin();
    const int total = static_cast<int>(sizeof(kCorrectEnglishWords) / sizeof(kCorrectEnglishWords[0]));
    int falsePositives = 0;
    for (int i = 0; i < total; ++i) {
        const QString word = QString::fromLatin1(kCorrectEnglishWords[i]);
        const QVector<SpellIssue> issues = checker.check(word);
        if (issues.isEmpty()) continue;
        ++falsePositives;
        for (const SpellIssue& issue : issues)
            qWarning("FALSEPOS(英文): %s -> %s", qPrintable(word), qPrintable(issue.suggestions.join(QLatin1Char(','))));
    }
    const double rate = 100.0 * falsePositives / total;
    qInfo("英文误报：%d / %d = %.2f%%", falsePositives, total, rate);
    QVERIFY2(rate <= 3.0,
             qPrintable(QStringLiteral("英文误报率 %1% 超过上限 3%").arg(rate, 0, 'f', 2)));
}

// 误报率的实测值单独记一条，方便回归时对比。
void Wave21SpellTests::englishFalsePositiveRateIsReported() {
    const SpellChecker checker = SpellChecker::builtin();
    const int total = static_cast<int>(sizeof(kCorrectEnglishWords) / sizeof(kCorrectEnglishWords[0]));
    int falsePositives = 0;
    for (int i = 0; i < total; ++i) {
        const QString word = QString::fromLatin1(kCorrectEnglishWords[i]);
        if (!checker.check(word).isEmpty()) ++falsePositives;
    }
    qInfo("英文样本 %d 个词，误报 %d 个，误报率 %.2f%%", total, falsePositives,
          100.0 * falsePositives / total);
    QVERIFY(falsePositives * 100 <= total * 3);
}

// 形近错字：己/已/巳、末/未。
void Wave21SpellTests::chineseShapeSimilarTyposAreCaught() {
    SpellChecker::Options options;
    options.chineseEnabled = true;
    const SpellChecker checker = SpellChecker::create(options);

    // 「自已」应该是「自己」。
    {
        const QString text = QStringLiteral("这件事我自已处理好了。");
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        QVERIFY2(countIssues(issues, QStringLiteral("已")) == 1,
                 "「自已」里的「已」没有被报出来");
        QVERIFY(hasIssueWithSuggestion(issues, QStringLiteral("已"), QStringLiteral("己")));
        // 位置必须落在「已」上。
        for (const SpellIssue& issue : issues) {
            if (issue.word != QStringLiteral("已")) continue;
            QCOMPARE(issue.length, 1);
            QCOMPARE(text.mid(issue.start, issue.length), QStringLiteral("已"));
        }
    }
    // 「末来」应该是「未来」。
    {
        const QString text = QStringLiteral("这是末来的计划。");
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        QVERIFY2(countIssues(issues, QStringLiteral("末")) == 1,
                 "「末来」里的「末」没有被报出来");
        QVERIFY(hasIssueWithSuggestion(issues, QStringLiteral("末"), QStringLiteral("未")));
    }
    // 「已经」「未来」「自己」本身是正确的，不能报。
    {
        const QString text = QStringLiteral("他已经完成了未来的计划，这本身就是好事。");
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        for (const SpellIssue& issue : issues)
            qWarning("FALSEPOS(中文-形近): %s @%d", qPrintable(issue.word), issue.start);
        QCOMPARE(issues.size(), 0);
    }
}

// 音近错字：及/急、既/继、厉/励。
//
// 口径说明：音近字在中文里太容易撞词（「于其」「存再」本来就是对的），
// 字级判定按音近组跑误报率高，所以音近错字由词组级错字表兜：
// 报出来的是整个错词（如「迫不急待」），建议是正确写法。
void Wave21SpellTests::chineseHomophoneTyposAreCaught() {
    SpellChecker::Options options;
    options.chineseEnabled = true;
    const SpellChecker checker = SpellChecker::create(options);

    struct Case {
        const char* text;
        const char* wrong;
        const char* right;
    };
    const Case cases[] = {
        {"我们迫不急待地等着结果。", "迫不急待", "迫不及待"},
        {"一如继往地努力工作。", "一如继往", "一如既往"},
        {"他再接再励，终于做完了。", "再接再励", "再接再厉"},
        {"这项工作已经以经完成了。", "以经", "已经"},
        {"请尽快把软件按装好。", "按装", "安装"},
    };
    for (const Case& item : cases) {
        const QString text = QString::fromUtf8(item.text);
        const QString wrong = QString::fromUtf8(item.wrong);
        const QString right = QString::fromUtf8(item.right);
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        QVERIFY2(reportsCorrection(issues, text, wrong, right),
                 qPrintable(QStringLiteral("%1 里的「%2 → %3」没有被报出来")
                                .arg(text, wrong, right)));
    }
}

// 词组级错字：成语、常用词整体写错。
void Wave21SpellTests::chinesePhraseTyposAreCaught() {
    SpellChecker::Options options;
    options.chineseEnabled = true;
    const SpellChecker checker = SpellChecker::create(options);

    {
        const QString text = QStringLiteral("他这次甘败下风，输得很惨。");
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        QVERIFY2(hasIssueWithSuggestion(issues, QStringLiteral("甘败下风"), QStringLiteral("甘拜下风")),
                 "「甘败下风」没有被报出来");
    }
    {
        const QString text = QStringLiteral("请尽快把软件按装好。");
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        QVERIFY2(reportsCorrection(issues, text, QStringLiteral("按装"), QStringLiteral("安装")),
                 "「按装」没有被报出来");
    }
    {
        const QString text = QStringLiteral("这项工作已经以经完成了。");
        const QVector<SpellIssue> issues = checker.checkChinese(text);
        // 「以经」既可能被整词报出来，也可能只报那个错字「以」——两种都算对。
        QVERIFY2(reportsCorrection(issues, text, QStringLiteral("以经"), QStringLiteral("已经")),
                 "「以经」没有被报出来");
    }
}

// 正确的中文不该被报：拿真实中文文本跑，误报率要有上限。
void Wave21SpellTests::correctChineseTextIsNotFlagged() {
    SpellChecker::Options options;
    options.chineseEnabled = true;
    const SpellChecker checker = SpellChecker::create(options);

    const QString text = QString::fromUtf8(kRealChineseText);
    const QVector<SpellIssue> issues = checker.checkChinese(text);
    for (const SpellIssue& issue : issues)
        qWarning("FALSEPOS(中文-文言): %s", qPrintable(describeIssue(issue)));
    const int hanChars = countHanChars(text);
    const double rate = 100.0 * issues.size() / hanChars;  // 每百字
    qInfo("文言中文：%d 个汉字，报出 %d 处，误报率 %.2f 处/百字", hanChars, issues.size(), rate);
    QVERIFY2(rate <= 1.0,
             qPrintable(QStringLiteral("文言中文误报率 %1 处/百字 超过上限 1").arg(rate, 0, 'f', 2)));
}

// 白话说明文同样要压住误报。
void Wave21SpellTests::chineseFalsePositiveRateIsReported() {
    SpellChecker::Options options;
    options.chineseEnabled = true;
    const SpellChecker checker = SpellChecker::create(options);

    const QString text = QString::fromUtf8(kModernChineseText);
    const QVector<SpellIssue> issues = checker.checkChinese(text);
    for (const SpellIssue& issue : issues)
        qWarning("FALSEPOS(中文-白话): %s", qPrintable(describeIssue(issue)));

    // 逐词核对：正文里的每个词都不该被报。
    const int total = static_cast<int>(sizeof(kCorrectChineseWords) / sizeof(kCorrectChineseWords[0]));
    int falsePositives = 0;
    for (int i = 0; i < total; ++i) {
        const QString word = QString::fromUtf8(kCorrectChineseWords[i]);
        if (!checker.checkChinese(word).isEmpty()) ++falsePositives;
    }
    qInfo("中文样本 %d 个词，误报 %d 个，误报率 %.2f%%；白话段落报出 %d 处",
          total, falsePositives, 100.0 * falsePositives / total, issues.size());
    QVERIFY2(100.0 * falsePositives / total <= 3.0,
             qPrintable(QStringLiteral("中文误报率 %1% 超过上限 3%")
                            .arg(100.0 * falsePositives / total, 0, 'f', 2)));
    QVERIFY2(issues.size() <= 3,
             qPrintable(QStringLiteral("白话段落报出 %1 处，误报太多").arg(issues.size())));
}

// 外部词表：加载成功。
void Wave21SpellTests::externalDictionaryIsLoaded() {
    const QString path = QDir::tempPath() + QStringLiteral("/aipdf-wave21-external.dic");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("# 外部词表\n2\nzzexternalword/ABC\n外部词条\n");
    file.close();

    SpellChecker::Options options;
    options.dictionaryPaths = {path};
    QVector<DictionaryLoadResult> results;
    const SpellChecker checker = SpellChecker::create(options, &results);

    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().ok);
    QVERIFY(results.first().error.isEmpty());
    QCOMPARE(results.first().wordCount, 2);
    QVERIFY(checker.isUsable());

    QVERIFY(checker.isKnownWord(QStringLiteral("zzexternalword")));
    QVERIFY(checker.isKnownChineseWord(QStringLiteral("外部词条")));
    QCOMPARE(checker.check(QStringLiteral("The zzexternalword is here.")).size(), 0);

    QFile::remove(path);
}

// 外部词表：加载失败必须如实报错，不能假装加载成功。
void Wave21SpellTests::missingExternalDictionaryReportsError() {
    const QString path = QDir::tempPath() + QStringLiteral("/aipdf-wave21-not-exist.dic");
    QFile::remove(path);

    SpellChecker::Options options;
    options.dictionaryPaths = {path};
    QVector<DictionaryLoadResult> results;
    const SpellChecker checker = SpellChecker::create(options, &results);

    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().ok);
    QVERIFY2(!results.first().error.isEmpty(), "加载失败必须给出原因");
    QCOMPARE(results.first().wordCount, 0);
    QCOMPARE(checker.dictionaryResults().size(), 1);
    QVERIFY(!checker.dictionaryResults().first().error.isEmpty());
}

// strict 模式：词表加载失败就不给检查结果，避免「假装用了这个词表」。
void Wave21SpellTests::strictModeRefusesToCheckWhenDictionaryFails() {
    const QString path = QDir::tempPath() + QStringLiteral("/aipdf-wave21-not-exist.dic");
    QFile::remove(path);

    SpellChecker::Options options;
    options.dictionaryPaths = {path};
    options.strictDictionaries = true;
    QVector<DictionaryLoadResult> results;
    const SpellChecker checker = SpellChecker::create(options, &results);

    QVERIFY(!checker.isUsable());
    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().ok);
    // 不可用时不给结果：宁可没有，也不要基于没加载上的词表得出的结论。
    QCOMPARE(checker.check(QStringLiteral("This is a recieve.")).size(), 0);
}

// builtin() 的既有行为不变：只查英文，中文整段跳过。
void Wave21SpellTests::builtinCheckerKeepsEnglishOnlyBehaviour() {
    const SpellChecker checker = SpellChecker::builtin();
    QCOMPARE(checker.check(QStringLiteral("这是一段中文句子，没有问题。")).size(), 0);
    QCOMPARE(checker.check(QStringLiteral("I will recieve the package.")).size(), 1);
    // builtin() 不查中文，但中文词表本身是有的（供 create() 复用）。
    QVERIFY(checker.isKnownChineseWord(QStringLiteral("自己")));
}

// 既有的用户词典接口不能被破坏。
void Wave21SpellTests::userDictionaryStillWorks() {
    const QString path = QDir::tempPath() + QStringLiteral("/aipdf-wave21-user.dic");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("# 用户词典\n2\nrecieve\nzzword/ABC\n");
    file.close();

    SpellChecker checker = SpellChecker::builtin();
    const QString text = QStringLiteral("I will recieve the package.");
    QCOMPARE(checker.check(text).size(), 1);

    QString error;
    QCOMPARE(checker.loadUserDictionary(path, &error), 2);
    QVERIFY(error.isEmpty());
    QVERIFY(checker.isKnownWord(QStringLiteral("recieve")));
    QCOMPARE(checker.check(text).size(), 0);

    QFile::remove(path);
}

// 词表规模要能被外部读到（界面与报告都要用）。
void Wave21SpellTests::dictionaryLoadCountsAreReported() {
    QVERIFY(SpellChecker::builtinEnglishWordCount() > 0);
    QVERIFY(SpellChecker::builtinCommonWordCount() > 0);
    QVERIFY(SpellChecker::builtinChineseWordCount() > 0);
    QVERIFY(SpellChecker::builtinConfusableGroupCount() > 0);
    QVERIFY(SpellChecker::builtinHomophoneGroupCount() > 0);
    QVERIFY(SpellChecker::builtinPhraseTypoCount() > 0);
    qInfo("内置词表：英文 %d 词（常用 %d）、中文 %d 词、形近 %d 组、音近 %d 组、词组级错字 %d 条",
          SpellChecker::builtinEnglishWordCount(), SpellChecker::builtinCommonWordCount(),
          SpellChecker::builtinChineseWordCount(), SpellChecker::builtinConfusableGroupCount(),
          SpellChecker::builtinHomophoneGroupCount(), SpellChecker::builtinPhraseTypoCount());
}

QTEST_MAIN(Wave21SpellTests)
#include "Wave21SpellTests.moc"
