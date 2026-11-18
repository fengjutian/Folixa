#pragma once

#include <QString>
#include <QVector>

namespace aipdf::pdf {

// ===============================================================
// 注释工具箱（Annotation Toolkit）
// ===============================================================
//
// 五个"往批注里放东西"的小工具的领域层：
//   1. 会计计算器 —— 用户敲一串算式，我们算出结果并整理成能贴进批注的文本；
//   2. 符号面板   —— 一张常用符号表，以及符号变成批注文本的规则；
//   3. 编号图章   —— 数字/字母图章的编号规则与计数器；
//   4. 保持工具选择 —— 逐工具记住"放完别退回选择态"；
//   5. 连续批注   —— 记住"这个工具还开着"，让用户连着标几十个点。
//
// 这一层只负责算东西：不碰文档、不碰界面、不建控件。
// 界面拿到的是算好的字符串与状态，剩下的（怎么弹面板、怎么插进批注框）
// 由上层负责——这样同一套规则在批注、便签、图章上都能复用，
// 也不会因为界面改版而把算式规则改坏。

// ---------------------------------------------------------------
// 会计计算器
// ---------------------------------------------------------------

// 一次算式的求值结果。
//
// 为什么把"文本"和"数值"都放进结果里：
// 数值给界面做二次计算（再乘一个税率之类），文本给批注直接贴。
// 两者由同一个精度设置产生，不会出现"文本说 2.5、数值却是 2.4999"的情况。
struct CalculationResult {
    // 是否求值成功。**失败时 false，绝不用 0 冒充成功。**
    bool ok = false;
    // 规范化后的算式（去掉空白与千分位逗号）。
    // 失败时保存用户原始输入，方便界面回显"你刚才敲的是啥"。
    QString expression;
    // 结果文本（按精度四舍五入并去掉多余的 0，例如 2.50 → "2.5"）。
    QString value;
    // 与 value 文本对应的数值（同样按精度舍入过）。
    double numericValue = 0.0;
    // 失败原因（中文硬编码）。成功时为空。
    QString error;
};

// 求值一个算式。
//
// 支持：+ - * / %、括号、正负号、小数点、千分位逗号。
//
// **百分号规则**（最容易读错的一条，单独写清楚）：
//   * `%` 是后缀，作用于紧挨着它的那个数字（或整个括号）。
//   * 单独使用时相当于除以 100：`50%` = 0.5（没有基数可参照）。
//   * 出现在加法链里时，它是**基数（加法链第一项）的百分比**：
//     `100+10%` = 100 + 10% × 100 = **110**（这就是合同里写的"涨 10%"），
//     `100-10%` = 90。
//   * 出现在乘法链里时，它是**除以 100 的因子**：`100*10%` = 10。
//   * 括号内重新起算：`100+(10%)` = 100.1（括号里的百分号够不着外层的基数）。
//
// 不支持：变量、函数、幂运算。这些留给上层扩展，领域层不猜用户的意思。
// 非法算式（`1+`、`*/2`、`(1+2`、除以 0）一律 ok=false + 中文原因。
//
// decimals 是小数位数（0..12，超出按边界处理），默认 2（金额习惯）。
CalculationResult evaluateExpression(const QString& expression, int decimals = 2);

// 把算式与结果整理成能放进批注的文本，例如 "100+10% = 110"。
//
// 失败时返回空串：失败的原因已经在 result.error 里，
// 把错误塞进批注正文只会污染文档。
QString formatCalculationForAnnotation(const CalculationResult& result);

// ---------------------------------------------------------------
// 符号面板
// ---------------------------------------------------------------

// 符号分类。界面按分类摆按钮，领域层只管给目录。
enum class SymbolCategory {
    Currency,      // 货币
    Punctuation,   // 标点
    Mathematics,   // 数学
    Marks,         // 勾叉记号
    Arrows,        // 箭头
};

// 一个符号条目。
//
// 为什么连"名字"一起存：符号面板要在按钮上显示中文名，
// 而批注里往往只放符号本身。名字跟着符号走，界面就不必再维护第二张表。
struct SymbolEntry {
    QString symbol;   // 实际字符，例如 "¥" "±" "→" "✓"
    QString name;     // 中文名，例如 "人民币符号"
    SymbolCategory category = SymbolCategory::Marks;
};

// 某一类符号的目录。未知类别（越界的枚举值）返回空列表。
QVector<SymbolEntry> symbolCatalog(SymbolCategory category);

// 全部符号的目录（按分类顺序拼接）。
QVector<SymbolEntry> symbolCatalog();

// 分类的中文名，例如 "货币"。给界面分组标题用。
QString symbolCategoryName(SymbolCategory category);

// 把符号整理成能放进文字批注的文本。
//
// includeName=false 时只放符号本身（"✓"）；true 时是 "✓ 通过" 这样的形式，
// 中间一个空格：批注正文是窄栏，符号紧贴中文会挤在一起。
QString formatSymbolForAnnotation(const SymbolEntry& entry, bool includeName = false);

// 把符号插入一段文字的指定位置（文字批注/便签用）。
//
// position 超出范围时按"末尾"处理：界面给的光标位置偶尔会越界，
// 越界就当追加，**不能把用户已经写的内容弄丢**。
QString insertSymbolIntoText(const QString& text, int position, const SymbolEntry& entry);

// ---------------------------------------------------------------
// 编号图章（数字图章 / 字母图章）
// ---------------------------------------------------------------

// 编号图章的编号形式。
//
// 为什么要有三种：审阅时最常见的两种批注是「第几页第几条」——
// 一边用阿拉伯数字（1、2、3），一边用字母（A、B、C），
// 用户自己挑，我们只管把序号写对。
enum class StampNumberStyle {
    Arabic,     // 1, 2, 3
    UpperAlpha, // A, B, C（超过 Z 接着 AA、AB）
    LowerAlpha, // a, b, c
};

// 编号图章的选项。
//
// 为什么前缀后缀放在选项里：图章上常写「第 3 号」或者「No.3」，
// 这些字样每次都一样，属于设置而不是每次输入的内容。
struct StampNumberOptions {
    StampNumberStyle style = StampNumberStyle::Arabic;
    // 起始序号。1 表示从 1 开始。
    int start = 1;
    // 补零位数（0 表示不补）。例如 3 位时 1 → "001"。
    // 字母形式下这个设置没有意义，直接忽略。
    int digits = 0;
    // 前缀与后缀，例如 "第" 与 "号"。
    QString prefix;
    QString suffix;
};

// 把序号写成图章上的文字。
//
// index 是**从 0 开始的偏移量**（第 0 个就是起始序号那个）。
// 起始序号小于 1 时按 1 处理；偏移为负时按 0 处理。
QString formatStampNumber(const StampNumberOptions& options, int index);

// 编号图章的计数器：每放一个图章自动取下一个编号。
//
// 为什么计数器在领域层而不是界面层：编号要连着排，
// 中间弹一次对话框或者切一次工具都不该让编号重来。
class StampNumberSequence {
public:
    StampNumberSequence() = default;
    explicit StampNumberSequence(const StampNumberOptions& options);

    // 换选项（并把计数归零）：换了样式还接着旧编号会让人看不懂。
    void setOptions(const StampNumberOptions& options);
    const StampNumberOptions& options() const;

    // 取下一个编号的文字（计数 +1）。永不返回空串。
    QString next();

    // 已经发出过几个编号。
    int issued() const;

    // 计数归零，选项保留。
    void reset();

private:
    StampNumberOptions options_;
    int issued_ = 0;
};

// ---------------------------------------------------------------
// 保持工具选择
// ---------------------------------------------------------------

// 记住"哪些工具在放完之后保持激活"。
//
// 与 ContinuousAnnotationMode 的分工：
//   * ContinuousAnnotationMode 是**全局**的连续批注（开一次，所有工具一起连着放）；
//   * ToolRetentionSet 是**逐工具**的（"矩形保持选中，箭头放完就退"）。
//
// 逐工具才是实际干活时想要的粒度：标完一串矩形顺手切走，
// 但批注还得一个个点。
class ToolRetentionSet {
public:
    // 标记或不标记某个工具。toolId 为空串时什么都不做。
    void setRetained(const QString& toolId, bool retained);

    // 某个工具是否被标记为"放完保持"。
    bool isRetained(const QString& toolId) const;

    // 被标记的工具（按标记顺序）。
    QVector<QString> retainedTools() const;

    bool isEmpty() const;

    // 全部清掉。
    void clear();

    // 放完某个工具后调用：它被标记为保持 → true（调用方别回到选择态）。
    bool keepsAfterPlacement(const QString& toolId) const;

private:
    // 保序去重的工具列表：界面上的顺序要跟用户标记的顺序一致。
    QVector<QString> tools_;
};

// ---------------------------------------------------------------
// 连续批注模式
// ---------------------------------------------------------------

// 连续批注：记录"哪些工具在连续模式下保持激活"。
//
// 为什么要有这个：现在每放一个批注工具就回到选择态，
// 连续标注同一类批注要点几十次，每次都要重新点一次工具 ——
// 这类"机械重复"最容易把人逼疯。
//
// 状态只有两个：当前挂着哪个工具、这个工具放完要不要继续保持。
// 领域层只记状态，"放完以后界面怎么表现"由上层决定。
class ContinuousAnnotationMode {
public:
    // 激活某个工具的连续模式。
    //
    // repeatLastPlacement=false 表示这个工具"放完就退出"
    //（有些工具连着放会越用越歪，比如手绘墨迹、随手画的小箭头）。
    // toolId 为空等同于 deactivate()：没有工具就谈不上连续。
    void activate(const QString& toolId, bool repeatLastPlacement = true);

    // 当前是否处于连续模式（也就是：有没有工具挂着）。
    bool isActive() const;

    // 当前连续的是哪个工具；未激活时返回空串。
    QString activeTool() const;

    // 放完一个批注后调用：
    //   * 连续模式下工具保持激活 → 返回 true（调用方别回到选择态）；
    //   * 否则返回 false（调用方应回到选择态）。
    // 放完就退出的工具（repeatLastPlacement=false）会在这里自动退出连续模式。
    bool afterPlacement();

    // 关掉连续模式，回到选择态。
    void deactivate();

    // 连续模式下是否"放完就保持"。
    bool repeatsLastPlacement() const;

private:
    QString m_toolId;
    bool m_repeat = false;
};

}  // namespace aipdf::pdf
