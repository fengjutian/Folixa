#include "pdf/annot/AnnotationToolkit.h"

#include <cmath>

namespace aipdf::pdf {

namespace {

// ===============================================================
// 词法
// ===============================================================

// 只认 ASCII 数字：QChar::isDigit() 对阿拉伯数字等 Unicode 数字也返回 true，
// 但 QString::toDouble() 只认 ASCII，两者对不上会让 "١٢" 变成 0。
bool isAsciiDigit(QChar c) {
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

enum class TokenType { Number, Operator, LParen, RParen, End };

struct Token {
    TokenType type = TokenType::End;
    double value = 0.0;
    QChar op;
    // 规范化后的数字文本（去掉千分位与前导零），用来拼规范化算式。
    QString text;
    int position = 0;
};

// 把算式切成记号。
//
// 为什么自己切而不是用正则：正则处理不了"千分位写错"和"小数点写两遍"
// 这类脏输入，而这类输入恰恰是用户真会敲出来的。
// 脏输入必须给出一句人话，而不是默默算成 0。
class Lexer {
public:
    explicit Lexer(const QString& text) : m_text(text) {}

    bool tokenize(QVector<Token>* tokens, QString* error) {
        while (true) {
            skipSpaces();
            if (m_pos >= m_text.size()) {
                Token end;
                end.type = TokenType::End;
                end.position = m_pos;
                tokens->append(end);
                return true;
            }
            const QChar c = m_text.at(m_pos);
            if (isAsciiDigit(c) ||
                (c == QLatin1Char('.') && m_pos + 1 < m_text.size() && isAsciiDigit(m_text.at(m_pos + 1)))) {
                Token token;
                if (!scanNumber(&token, error)) {
                    return false;
                }
                tokens->append(token);
                continue;
            }
            if (c == QLatin1Char('(') || c == QLatin1Char(')')) {
                Token token;
                token.type = (c == QLatin1Char('(')) ? TokenType::LParen : TokenType::RParen;
                token.position = m_pos;
                ++m_pos;
                tokens->append(token);
                continue;
            }
            if (c == QLatin1Char('+') || c == QLatin1Char('-') || c == QLatin1Char('*') ||
                c == QLatin1Char('/') || c == QLatin1Char('%')) {
                Token token;
                token.type = TokenType::Operator;
                token.op = c;
                token.position = m_pos;
                ++m_pos;
                tokens->append(token);
                continue;
            }
            *error = QStringLiteral("算式里出现无法识别的字符「%1」").arg(c);
            return false;
        }
    }

private:
    void skipSpaces() {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace()) {
            ++m_pos;
        }
    }

    bool scanNumber(Token* token, QString* error) {
        const int start = m_pos;

        // 整数部分：数字与千分位逗号。
        QString raw;
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (isAsciiDigit(c) || c == QLatin1Char(',')) {
                raw += c;
                ++m_pos;
                continue;
            }
            break;
        }

        QString integerPart;
        if (!raw.isEmpty()) {
            const QStringList groups = raw.split(QLatin1Char(','));
            if (groups.size() > 1) {
                // 千分位写法：首组 1..3 位，之后每组必须正好三位。
                // "1,234" 对，"1,23" 与 "1,2345" 都不对。
                if (groups.first().isEmpty() || groups.first().size() > 3) {
                    *error = QStringLiteral("千分位写法不对：逗号前最多只能有三位数字");
                    return false;
                }
                for (int i = 1; i < groups.size(); ++i) {
                    if (groups.at(i).size() != 3) {
                        *error = QStringLiteral("千分位写法不对：逗号后必须三位一组");
                        return false;
                    }
                }
            }
            integerPart = raw;
            integerPart.remove(QLatin1Char(','));
        }

        // 小数部分。
        QString fraction;
        if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char('.')) {
            ++m_pos;
            while (m_pos < m_text.size() && isAsciiDigit(m_text.at(m_pos))) {
                fraction += m_text.at(m_pos);
                ++m_pos;
            }
            if (fraction.isEmpty()) {
                *error = QStringLiteral("小数点后必须有数字");
                return false;
            }
            if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char('.')) {
                *error = QStringLiteral("一个小数里只能有一个小数点");
                return false;
            }
        }

        QString numberText = integerPart;
        if (numberText.isEmpty()) {
            numberText = QStringLiteral("0");
        }
        if (!fraction.isEmpty()) {
            numberText += QLatin1Char('.');
            numberText += fraction;
        }
        bool converted = false;
        const double value = numberText.toDouble(&converted);
        if (!converted || !std::isfinite(value)) {
            *error = QStringLiteral("数字太大，算不出来");
            return false;
        }

        // 规范化数字：去前导零（"007" → "7"），小数部分保持原样。
        QString normalizedInteger = integerPart;
        if (normalizedInteger.isEmpty()) {
            normalizedInteger = QStringLiteral("0");
        }
        int firstNonZero = 0;
        while (firstNonZero + 1 < normalizedInteger.size() &&
               normalizedInteger.at(firstNonZero) == QLatin1Char('0')) {
            ++firstNonZero;
        }
        normalizedInteger = normalizedInteger.mid(firstNonZero);
        QString normalized = normalizedInteger;
        if (!fraction.isEmpty()) {
            normalized += QLatin1Char('.');
            normalized += fraction;
        }

        token->type = TokenType::Number;
        token->value = value;
        token->text = normalized;
        token->position = start;
        return true;
    }

    QString m_text;
    int m_pos = 0;
};

// ===============================================================
// 语法树
// ===============================================================

// 一个因子：一个数字（可带符号、可带百分号）或一个括号整体。
struct FactorNode {
    double value = 0.0;
    bool percent = false;
};

// 乘法链：若干因子用 * / 连起来。
struct TermNode {
    QVector<FactorNode> factors;
    QVector<QChar> ops;  // '*' 或 '/'
};

// 加法链：若干项用 + - 连起来。
struct ExpressionNode {
    QVector<TermNode> terms;
    QVector<QChar> ops;  // '+' 或 '-'
};

bool evaluateExpressionNode(const ExpressionNode& expression, double* value, QString* error);

// ===============================================================
// 语法（递归下降）
// ===============================================================
//
// 文法（标准优先级）：
//   expression := term (('+' | '-') term)*
//   term       := factor (('*' | '/') factor)*
//   factor     := ('+' | '-')? primary '%'*
//   primary    := number | '(' expression ')'
//
// 为什么自己写而不用 QJSEngine：脚本引擎能算，但会把用户输入当成代码执行，
// 而且这个仓库明确禁止引入脚本引擎（安全面太大、依赖也太重）。

class Parser {
public:
    explicit Parser(const QVector<Token>& tokens) : m_tokens(tokens) {}

    bool parse(ExpressionNode* expression, QString* error) {
        if (!parseExpression(expression, error)) {
            return false;
        }
        if (current().type != TokenType::End) {
            if (current().type == TokenType::RParen) {
                *error = QStringLiteral("算式里有没配对的右括号");
            } else {
                *error = QStringLiteral("算式里出现无法识别的符号「%1」").arg(current().op);
            }
            return false;
        }
        return true;
    }

    // 规范化后的算式：去空白、去千分位、去前导零，运算符不带空格。
    const QString& normalizedExpression() const { return m_normalized; }

private:
    const Token& current() const { return m_tokens.at(m_index); }

    void advance() {
        if (m_index + 1 < m_tokens.size()) {
            ++m_index;
        }
    }

    bool parseExpression(ExpressionNode* expression, QString* error) {
        TermNode term;
        if (!parseTerm(&term, error)) {
            return false;
        }
        expression->terms.append(term);
        while (current().type == TokenType::Operator &&
               (current().op == QLatin1Char('+') || current().op == QLatin1Char('-'))) {
            const QChar op = current().op;
            advance();
            m_normalized += op;
            TermNode next;
            if (!parseTerm(&next, error)) {
                return false;
            }
            expression->terms.append(next);
            expression->ops.append(op);
        }
        return true;
    }

    bool parseTerm(TermNode* term, QString* error) {
        FactorNode factor;
        if (!parseFactor(&factor, error)) {
            return false;
        }
        term->factors.append(factor);
        while (current().type == TokenType::Operator &&
               (current().op == QLatin1Char('*') || current().op == QLatin1Char('/'))) {
            const QChar op = current().op;
            advance();
            m_normalized += op;
            FactorNode next;
            if (!parseFactor(&next, error)) {
                return false;
            }
            term->factors.append(next);
            term->ops.append(op);
        }
        return true;
    }

    bool parseFactor(FactorNode* factor, QString* error) {
        bool negative = false;
        if (current().type == TokenType::Operator &&
            (current().op == QLatin1Char('+') || current().op == QLatin1Char('-'))) {
            negative = (current().op == QLatin1Char('-'));
            m_normalized += current().op;
            advance();
        }
        if (!parsePrimary(factor, error)) {
            return false;
        }
        if (negative) {
            factor->value = -factor->value;
        }
        // 百分号可以连着写（"50%%"），多写的那些没有意义，按一次处理。
        while (current().type == TokenType::Operator && current().op == QLatin1Char('%')) {
            factor->percent = true;
            m_normalized += current().op;
            advance();
        }
        return true;
    }

    bool parsePrimary(FactorNode* factor, QString* error) {
        if (current().type == TokenType::LParen) {
            m_normalized += QLatin1Char('(');
            advance();
            ExpressionNode inner;
            if (!parseExpression(&inner, error)) {
                return false;
            }
            if (current().type != TokenType::RParen) {
                *error = QStringLiteral("算式里的括号没有闭合");
                return false;
            }
            m_normalized += QLatin1Char(')');
            advance();
            double value = 0.0;
            if (!evaluateExpressionNode(inner, &value, error)) {
                return false;
            }
            factor->value = value;
            return true;
        }
        if (current().type == TokenType::Number) {
            factor->value = current().value;
            m_normalized += current().text;
            advance();
            return true;
        }
        if (current().type == TokenType::End) {
            *error = QStringLiteral("算式不完整：末尾还缺一个数字");
            return false;
        }
        if (current().type == TokenType::RParen) {
            *error = QStringLiteral("算式里有没配对的右括号");
            return false;
        }
        *error = QStringLiteral("算式里出现无法识别的符号「%1」").arg(current().op);
        return false;
    }

    QVector<Token> m_tokens;
    int m_index = 0;
    QString m_normalized;
};

// ===============================================================
// 求值
// ===============================================================
//
// 百分号规则（头文件里写了一遍，这里是它的落地）：
//   * 单独一个因子带百分号 → 除以 100（没有基数可参照）。
//   * 加法链里、且该项只有一个因子 → 该项是"基数的百分比"，
//     基数 = 加法链第一项的值（`100+10%` = 110）。
//   * 乘法链里的百分号 → 一律除以 100（`100*10%` = 10）。
bool evaluateTermNode(const TermNode& term, double percentBase, double* value, QString* error) {
    if (term.factors.isEmpty()) {
        *error = QStringLiteral("算式为空，请输入要计算的公式");
        return false;
    }
    if (term.factors.size() == 1) {
        const FactorNode& factor = term.factors.first();
        const double result = factor.percent ? (factor.value / 100.0) * percentBase : factor.value;
        if (!std::isfinite(result)) {
            *error = QStringLiteral("算式结果超出可表示范围");
            return false;
        }
        *value = result;
        return true;
    }
    double product = term.factors.first().value;
    for (int i = 1; i < term.factors.size(); ++i) {
        const FactorNode& factor = term.factors.at(i);
        const double operand = factor.percent ? (factor.value / 100.0) : factor.value;
        if (term.ops.at(i - 1) == QLatin1Char('/')) {
            if (operand == 0.0) {
                *error = QStringLiteral("除数不能为零");
                return false;
            }
            product /= operand;
        } else {
            product *= operand;
        }
        if (!std::isfinite(product)) {
            *error = QStringLiteral("算式结果超出可表示范围");
            return false;
        }
    }
    *value = product;
    return true;
}

bool evaluateExpressionNode(const ExpressionNode& expression, double* value, QString* error) {
    if (expression.terms.isEmpty()) {
        *error = QStringLiteral("算式为空，请输入要计算的公式");
        return false;
    }
    // 基数 = 加法链的第一项。加法链里的百分号要按基数的百分比算，
    // 所以必须先把第一项算出来。
    double base = 0.0;
    if (!evaluateTermNode(expression.terms.first(), 1.0, &base, error)) {
        return false;
    }
    double total = base;
    for (int i = 1; i < expression.terms.size(); ++i) {
        double termValue = 0.0;
        if (!evaluateTermNode(expression.terms.at(i), base, &termValue, error)) {
            return false;
        }
        total += (expression.ops.at(i - 1) == QLatin1Char('-')) ? -termValue : termValue;
    }
    if (!std::isfinite(total)) {
        *error = QStringLiteral("算式结果超出可表示范围");
        return false;
    }
    *value = total;
    return true;
}

// 数字格式化：按精度四舍五入，再去掉多余的 0（"2.50" → "2.5"，"2.00" → "2"）。
//
// 为什么要去 0：金额批注写成 "1234.50" 和 "1234.5" 意思一样，
// 但后者更短，在窄栏里能少占一行。
QString formatNumber(double value, int decimals, double* numericOut) {
    const int digits = qBound(0, decimals, 12);
    QString text = QString::number(value, 'f', digits);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    if (text.isEmpty() || text == QLatin1String("-0")) {
        text = QStringLiteral("0");
    }
    if (numericOut) {
        // 用格式化后的文本反算，保证数值与文本永远一致。
        *numericOut = text.toDouble();
    }
    return text;
}

// ===============================================================
// 符号目录
// ===============================================================
//
// 每类至少 8 个：符号面板一屏放不下太少的话，用户就得翻分类，
// 那就失去"面板"的意义了。

QVector<SymbolEntry> currencyCatalog() {
    return {
        {QStringLiteral("¥"), QStringLiteral("人民币符号"), SymbolCategory::Currency},
        {QStringLiteral("$"), QStringLiteral("美元符号"), SymbolCategory::Currency},
        {QStringLiteral("€"), QStringLiteral("欧元符号"), SymbolCategory::Currency},
        {QStringLiteral("£"), QStringLiteral("英镑符号"), SymbolCategory::Currency},
        {QStringLiteral("¢"), QStringLiteral("美分符号"), SymbolCategory::Currency},
        {QStringLiteral("₹"), QStringLiteral("印度卢比符号"), SymbolCategory::Currency},
        {QStringLiteral("₩"), QStringLiteral("韩元符号"), SymbolCategory::Currency},
        {QStringLiteral("฿"), QStringLiteral("泰铢符号"), SymbolCategory::Currency},
        {QStringLiteral("₽"), QStringLiteral("卢布符号"), SymbolCategory::Currency},
        {QStringLiteral("₪"), QStringLiteral("新谢克尔符号"), SymbolCategory::Currency},
    };
}

QVector<SymbolEntry> punctuationCatalog() {
    return {
        {QStringLiteral("，"), QStringLiteral("全角逗号"), SymbolCategory::Punctuation},
        {QStringLiteral("。"), QStringLiteral("句号"), SymbolCategory::Punctuation},
        {QStringLiteral("、"), QStringLiteral("顿号"), SymbolCategory::Punctuation},
        {QStringLiteral("；"), QStringLiteral("分号"), SymbolCategory::Punctuation},
        {QStringLiteral("："), QStringLiteral("冒号"), SymbolCategory::Punctuation},
        {QStringLiteral("？"), QStringLiteral("全角问号"), SymbolCategory::Punctuation},
        {QStringLiteral("！"), QStringLiteral("全角感叹号"), SymbolCategory::Punctuation},
        {QStringLiteral("“"), QStringLiteral("左双引号"), SymbolCategory::Punctuation},
        {QStringLiteral("”"), QStringLiteral("右双引号"), SymbolCategory::Punctuation},
        {QStringLiteral("‘"), QStringLiteral("左单引号"), SymbolCategory::Punctuation},
        {QStringLiteral("’"), QStringLiteral("右单引号"), SymbolCategory::Punctuation},
        {QStringLiteral("（"), QStringLiteral("全角左括号"), SymbolCategory::Punctuation},
        {QStringLiteral("）"), QStringLiteral("全角右括号"), SymbolCategory::Punctuation},
        {QStringLiteral("…"), QStringLiteral("省略号"), SymbolCategory::Punctuation},
        {QStringLiteral("—"), QStringLiteral("破折号"), SymbolCategory::Punctuation},
        {QStringLiteral("·"), QStringLiteral("间隔号"), SymbolCategory::Punctuation},
    };
}

QVector<SymbolEntry> mathematicsCatalog() {
    return {
        {QStringLiteral("+"), QStringLiteral("加号"), SymbolCategory::Mathematics},
        {QStringLiteral("−"), QStringLiteral("减号"), SymbolCategory::Mathematics},
        {QStringLiteral("×"), QStringLiteral("乘号"), SymbolCategory::Mathematics},
        {QStringLiteral("÷"), QStringLiteral("除号"), SymbolCategory::Mathematics},
        {QStringLiteral("±"), QStringLiteral("正负号"), SymbolCategory::Mathematics},
        {QStringLiteral("∓"), QStringLiteral("负正号"), SymbolCategory::Mathematics},
        {QStringLiteral("≈"), QStringLiteral("约等于号"), SymbolCategory::Mathematics},
        {QStringLiteral("≠"), QStringLiteral("不等号"), SymbolCategory::Mathematics},
        {QStringLiteral("≤"), QStringLiteral("小于等于号"), SymbolCategory::Mathematics},
        {QStringLiteral("≥"), QStringLiteral("大于等于号"), SymbolCategory::Mathematics},
        {QStringLiteral("∑"), QStringLiteral("求和号"), SymbolCategory::Mathematics},
        {QStringLiteral("∏"), QStringLiteral("求积号"), SymbolCategory::Mathematics},
        {QStringLiteral("√"), QStringLiteral("根号"), SymbolCategory::Mathematics},
        {QStringLiteral("∞"), QStringLiteral("无穷大号"), SymbolCategory::Mathematics},
        {QStringLiteral("∠"), QStringLiteral("角号"), SymbolCategory::Mathematics},
        {QStringLiteral("%"), QStringLiteral("百分号"), SymbolCategory::Mathematics},
    };
}

QVector<SymbolEntry> marksCatalog() {
    return {
        {QStringLiteral("✓"), QStringLiteral("对勾"), SymbolCategory::Marks},
        {QStringLiteral("✗"), QStringLiteral("叉号"), SymbolCategory::Marks},
        {QStringLiteral("✕"), QStringLiteral("叉号（粗）"), SymbolCategory::Marks},
        {QStringLiteral("★"), QStringLiteral("实心星号"), SymbolCategory::Marks},
        {QStringLiteral("☆"), QStringLiteral("空心星号"), SymbolCategory::Marks},
        {QStringLiteral("●"), QStringLiteral("实心圆点"), SymbolCategory::Marks},
        {QStringLiteral("○"), QStringLiteral("空心圆圈"), SymbolCategory::Marks},
        {QStringLiteral("■"), QStringLiteral("实心方块"), SymbolCategory::Marks},
        {QStringLiteral("□"), QStringLiteral("空心方框"), SymbolCategory::Marks},
        {QStringLiteral("▲"), QStringLiteral("实心上三角"), SymbolCategory::Marks},
        {QStringLiteral("△"), QStringLiteral("空心上三角"), SymbolCategory::Marks},
        {QStringLiteral("☑"), QStringLiteral("已勾选框"), SymbolCategory::Marks},
        {QStringLiteral("☐"), QStringLiteral("未勾选框"), SymbolCategory::Marks},
        {QStringLiteral("⚑"), QStringLiteral("小旗子"), SymbolCategory::Marks},
        {QStringLiteral("✎"), QStringLiteral("铅笔"), SymbolCategory::Marks},
        {QStringLiteral("✦"), QStringLiteral("四角星"), SymbolCategory::Marks},
    };
}

QVector<SymbolEntry> arrowsCatalog() {
    return {
        {QStringLiteral("→"), QStringLiteral("向右箭头"), SymbolCategory::Arrows},
        {QStringLiteral("←"), QStringLiteral("向左箭头"), SymbolCategory::Arrows},
        {QStringLiteral("↑"), QStringLiteral("向上箭头"), SymbolCategory::Arrows},
        {QStringLiteral("↓"), QStringLiteral("向下箭头"), SymbolCategory::Arrows},
        {QStringLiteral("↔"), QStringLiteral("双向箭头"), SymbolCategory::Arrows},
        {QStringLiteral("⇐"), QStringLiteral("双线左箭头"), SymbolCategory::Arrows},
        {QStringLiteral("⇒"), QStringLiteral("双线右箭头"), SymbolCategory::Arrows},
        {QStringLiteral("⇔"), QStringLiteral("双线双向箭头"), SymbolCategory::Arrows},
        {QStringLiteral("↗"), QStringLiteral("右上箭头"), SymbolCategory::Arrows},
        {QStringLiteral("↙"), QStringLiteral("左下箭头"), SymbolCategory::Arrows},
        {QStringLiteral("⟶"), QStringLiteral("长右箭头"), SymbolCategory::Arrows},
        {QStringLiteral("➜"), QStringLiteral("粗右箭头"), SymbolCategory::Arrows},
    };
}

}  // namespace

// ===============================================================
// 公开 API
// ===============================================================

CalculationResult evaluateExpression(const QString& expression, int decimals) {
    CalculationResult result;
    const QString trimmed = expression.trimmed();
    // 失败时保留用户原文，方便界面回显"你刚才敲的是啥"。
    result.expression = trimmed;
    if (trimmed.isEmpty()) {
        result.error = QStringLiteral("算式为空，请输入要计算的公式");
        return result;
    }

    QVector<Token> tokens;
    QString error;
    Lexer lexer(trimmed);
    if (!lexer.tokenize(&tokens, &error)) {
        result.error = error;
        return result;
    }

    Parser parser(tokens);
    ExpressionNode node;
    if (!parser.parse(&node, &error)) {
        result.error = error;
        return result;
    }

    double value = 0.0;
    if (!evaluateExpressionNode(node, &value, &error)) {
        result.error = error;
        return result;
    }

    result.ok = true;
    result.expression = parser.normalizedExpression();
    result.value = formatNumber(value, decimals, &result.numericValue);
    return result;
}

QString formatCalculationForAnnotation(const CalculationResult& result) {
    if (!result.ok) {
        // 失败原因已经在 result.error 里；把错误塞进批注正文只会污染文档。
        return QString();
    }
    if (result.expression.isEmpty()) {
        return result.value;
    }
    return result.expression + QStringLiteral(" = ") + result.value;
}

QVector<SymbolEntry> symbolCatalog(SymbolCategory category) {
    switch (category) {
        case SymbolCategory::Currency:
            return currencyCatalog();
        case SymbolCategory::Punctuation:
            return punctuationCatalog();
        case SymbolCategory::Mathematics:
            return mathematicsCatalog();
        case SymbolCategory::Marks:
            return marksCatalog();
        case SymbolCategory::Arrows:
            return arrowsCatalog();
    }
    // 未知类别（越界的枚举值）：返回空列表，不猜用户想要哪一类。
    return QVector<SymbolEntry>();
}

QVector<SymbolEntry> symbolCatalog() {
    QVector<SymbolEntry> all;
    const SymbolCategory categories[] = {
        SymbolCategory::Currency, SymbolCategory::Punctuation, SymbolCategory::Mathematics,
        SymbolCategory::Marks, SymbolCategory::Arrows,
    };
    for (SymbolCategory category : categories) {
        all += symbolCatalog(category);
    }
    return all;
}

QString symbolCategoryName(SymbolCategory category) {
    switch (category) {
        case SymbolCategory::Currency:
            return QStringLiteral("货币");
        case SymbolCategory::Punctuation:
            return QStringLiteral("标点");
        case SymbolCategory::Mathematics:
            return QStringLiteral("数学");
        case SymbolCategory::Marks:
            return QStringLiteral("勾叉记号");
        case SymbolCategory::Arrows:
            return QStringLiteral("箭头");
    }
    return QString();
}

QString formatSymbolForAnnotation(const SymbolEntry& entry, bool includeName) {
    if (entry.symbol.isEmpty()) {
        return QString();
    }
    if (!includeName || entry.name.isEmpty()) {
        return entry.symbol;
    }
    // 中间留一个空格：批注正文是窄栏，符号紧贴中文会挤在一起。
    return entry.symbol + QStringLiteral(" ") + entry.name;
}

QString insertSymbolIntoText(const QString& text, int position, const SymbolEntry& entry) {
    const QString symbol = formatSymbolForAnnotation(entry, false);
    if (symbol.isEmpty()) {
        return text;
    }
    int index = position;
    if (index < 0) {
        index = 0;
    }
    // 越界就当追加：不能把用户已经写的内容弄丢。
    if (index > text.size()) {
        index = text.size();
    }
    // 位置落在代理对（emoji 之类）中间时往前挪一位：
    // 插在半个字符中间会把那个字劈成两半，批注里就会出现乱码。
    if (index > 0 && index < text.size() && text.at(index - 1).isHighSurrogate() &&
        text.at(index).isLowSurrogate()) {
        --index;
    }
    return text.left(index) + symbol + text.mid(index);
}

// ---------------------------------------------------------------
// 编号图章
// ---------------------------------------------------------------

namespace {

// 序号转字母（1 → A，26 → Z，27 → AA）。
//
// 用的是"双基 26"（bijective base-26）：每一位都从 1 起算，
// 所以没有"第 0 个字母"这回事，26 之后自然进位成 AA。
QString alphaLabel(int value, bool upperCase) {
    if (value <= 0) {
        return QString();
    }
    QString result;
    while (value > 0) {
        const int digit = (value - 1) % 26;
        result.prepend(QChar(upperCase ? ('A' + digit) : ('a' + digit)));
        value = (value - 1) / 26;
    }
    return result;
}

}  // namespace

QString formatStampNumber(const StampNumberOptions& options, int index) {
    const int offset = qMax(0, index);
    const int start = options.start < 1 ? 1 : options.start;
    // 序号用 long long 算：超过两亿个图章的场景不存在，
    // 但越界了也不能让整串文字变成乱码，所以夹一下上限。
    const long long value = static_cast<long long>(start) + offset;

    QString body;
    switch (options.style) {
    case StampNumberStyle::UpperAlpha:
        body = alphaLabel(static_cast<int>(qMin<long long>(value, 26LL * 26 * 26)), true);
        break;
    case StampNumberStyle::LowerAlpha:
        body = alphaLabel(static_cast<int>(qMin<long long>(value, 26LL * 26 * 26)), false);
        break;
    case StampNumberStyle::Arabic:
    default: {
        body = QString::number(value);
        // 补零只对数字有意义：字母形式下 "007" 那种写法没人看得懂。
        if (options.digits > 0) {
            body = body.rightJustified(options.digits, QLatin1Char('0'));
        }
        break;
    }
    }
    if (body.isEmpty()) {
        body = QStringLiteral("1");
    }
    return options.prefix + body + options.suffix;
}

StampNumberSequence::StampNumberSequence(const StampNumberOptions& options) : options_(options) {}

void StampNumberSequence::setOptions(const StampNumberOptions& options) {
    options_ = options;
    issued_ = 0;
}

const StampNumberOptions& StampNumberSequence::options() const {
    return options_;
}

QString StampNumberSequence::next() {
    const QString label = formatStampNumber(options_, issued_);
    ++issued_;
    return label;
}

int StampNumberSequence::issued() const {
    return issued_;
}

void StampNumberSequence::reset() {
    issued_ = 0;
}

// ---------------------------------------------------------------
// 保持工具选择
// ---------------------------------------------------------------

void ToolRetentionSet::setRetained(const QString& toolId, bool retained) {
    if (toolId.isEmpty()) {
        return;
    }
    const int index = tools_.indexOf(toolId);
    if (retained) {
        if (index < 0) {
            tools_.append(toolId);
        }
        return;
    }
    if (index >= 0) {
        tools_.remove(index);
    }
}

bool ToolRetentionSet::isRetained(const QString& toolId) const {
    return tools_.contains(toolId);
}

QVector<QString> ToolRetentionSet::retainedTools() const {
    return tools_;
}

bool ToolRetentionSet::isEmpty() const {
    return tools_.isEmpty();
}

void ToolRetentionSet::clear() {
    tools_.clear();
}

bool ToolRetentionSet::keepsAfterPlacement(const QString& toolId) const {
    return isRetained(toolId);
}

bool keepsToolAfterPlacement(const QString& toolId, const ToolRetentionSet& retention,
                             const ContinuousAnnotationMode& continuous) {
    // 逐工具标记过的优先：用户明确说了要保持，就不看全局开关。
    if (!toolId.isEmpty() && retention.isRetained(toolId)) {
        return true;
    }
    // 连续批注模式：开着、挂的就是**这个**工具、且放完不退出
    //（墨迹、橡皮那类在连续模式里会自动退出）。
    if (toolId.isEmpty() || continuous.activeTool() != toolId) {
        return false;
    }
    return continuous.repeatsLastPlacement();
}

void ContinuousAnnotationMode::activate(const QString& toolId, bool repeatLastPlacement) {
    if (toolId.isEmpty()) {
        // 没有工具就谈不上连续。
        deactivate();
        return;
    }
    m_toolId = toolId;
    m_repeat = repeatLastPlacement;
}

bool ContinuousAnnotationMode::isActive() const {
    return !m_toolId.isEmpty();
}

QString ContinuousAnnotationMode::activeTool() const {
    return m_toolId;
}

bool ContinuousAnnotationMode::afterPlacement() {
    if (!isActive()) {
        return false;
    }
    if (!m_repeat) {
        // 放完就退出的工具：退出连续模式，调用方回到选择态。
        deactivate();
        return false;
    }
    return true;
}

void ContinuousAnnotationMode::deactivate() {
    m_toolId.clear();
    m_repeat = false;
}

bool ContinuousAnnotationMode::repeatsLastPlacement() const {
    return m_repeat;
}

}  // namespace aipdf::pdf
