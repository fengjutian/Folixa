// PDF 表单域脚本（/AA 的 /C 计算、/F 格式化、/V 校验）用的 JavaScript 子集求值器。
//
// 为什么自己写一个：项目没有 Qt Qml 模块，用不了 QJSEngine；而表单脚本在实践中
// 就是"算出一个值"的短表达式，完整 JS 引擎既重又能碰到宿主对象。
// 所以这里只实现够用的一小撮语法，遇到不认识的东西明确报错，而不是猜。
//
// 分三层：Tokenizer（词法）→ Parser（递归下降语法树）→ Evaluator（求值）。
// 每层都独立，语法错误和运行错误能分开报，测试也好写。

#include "pdf/forms/FormJsEvaluator.h"

#include <QHash>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <memory>

namespace aipdf::pdf {

namespace {

// 递归深度上限：脚本是 PDF 里带来的，作者可以把括号套几百层。
// 解析用递归下降，不设上限就会爆栈，所以在入口处数一层。
constexpr int kMaxDepth = 64;

// 表达式树深度上限：源码里没有嵌套，但 "1+1+1+…" 这样的长链也会让树变深，
// 求值是递归的，所以树深度也要设上限。256 层足够真实表单脚本用。
constexpr int kMaxAstDepth = 256;

// ------------------------------------------------------------------ 值 ----

struct Value {
    enum Kind { Null, Bool, Number, Str };
    Kind kind = Null;
    bool boolean = false;
    double number = 0.0;
    QString text;
};

Value makeNull() { return Value(); }

Value makeBool(bool b) {
    Value v;
    v.kind = Value::Bool;
    v.boolean = b;
    return v;
}

Value makeNumber(double d) {
    Value v;
    v.kind = Value::Number;
    v.number = d;
    return v;
}

Value makeStr(const QString& s) {
    Value v;
    v.kind = Value::Str;
    v.text = s;
    return v;
}

// 数字 → 文本：整数不带小数点，跟 JS 里 Number 的观感一致。
QString numberToText(double d) {
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
        return QString::number(static_cast<qlonglong>(d));
    }
    return QString::number(d, 'g', 15);
}

// 真值规则：空串、0、false、null 都是假，其它为真。
bool truthy(const Value& v) {
    switch (v.kind) {
    case Value::Null: return false;
    case Value::Bool: return v.boolean;
    case Value::Number: return v.number != 0.0 && !std::isnan(v.number);
    case Value::Str: return !v.text.isEmpty();
    }
    return false;
}

// 字符串 → 数字：空串当 0；不是数字就失败，不静默给 0。
bool textToNumber(const QString& text, double* out) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        *out = 0.0;
        return true;
    }
    bool ok = false;
    const double d = trimmed.toDouble(&ok);
    if (!ok || !std::isfinite(d)) {
        return false;
    }
    *out = d;
    return true;
}

bool valueToNumber(const Value& v, double* out) {
    switch (v.kind) {
    case Value::Null:
        *out = 0.0;
        return true;
    case Value::Bool:
        *out = v.boolean ? 1.0 : 0.0;
        return true;
    case Value::Number:
        *out = v.number;
        return true;
    case Value::Str:
        return textToNumber(v.text, out);
    }
    return false;
}

QString valueToText(const Value& v) {
    switch (v.kind) {
    case Value::Null: return QString();
    case Value::Bool: return v.boolean ? QStringLiteral("true") : QStringLiteral("false");
    case Value::Number: return numberToText(v.number);
    case Value::Str: return v.text;
    }
    return QString();
}

// 不支持的语法统一用这个前缀，方便调用方/界面识别。
QString unsupported(const QString& what) {
    return QStringLiteral("不支持的语法：") + what;
}

// 数字格式化（AFNumber_Format 语义），定义在文件后面的格式化一节。
QString applyNumberFormat(const QString& rawValue, int decimals, bool thousandsSeparator, bool minusSign,
                           int grouping, const QString& decimalPoint);

QString numberError(const Value& value) {
    return QStringLiteral("求值错误：无法把 '") + valueToText(value) + QStringLiteral("' 当作数字");
}

// ---------------------------------------------------------------- 词法 ----

enum class TokenType { End, Number, String, Identifier, Punct };

struct Token {
    TokenType type = TokenType::End;
    QString text;
    double number = 0.0;
    int position = 0;
};

// 标识符允许 Unicode 字母，PDF 表单里常有中文字段名。
bool isIdentStart(QChar c) {
    return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

bool isIdentPart(QChar c) {
    return isIdentStart(c) || c.isDigit() || c.isMark();
}

class Tokenizer {
public:
    explicit Tokenizer(const QString& source) : m_source(source) {}

    bool tokenize(QVector<Token>* out, QString* error) {
        while (true) {
            skipTrivia();
            if (m_pos >= m_source.size()) {
                break;
            }
            const QChar c = m_source.at(m_pos);
            if (c.isDigit() ||
                (c == QLatin1Char('.') && m_pos + 1 < m_source.size() && m_source.at(m_pos + 1).isDigit())) {
                if (!readNumber(out, error)) {
                    return false;
                }
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                if (!readString(out, error)) {
                    return false;
                }
            } else if (isIdentStart(c)) {
                readIdentifier(out);
            } else if (!readPunct(out, error)) {
                return false;
            }
        }
        Token end;
        end.type = TokenType::End;
        end.position = m_pos;
        out->append(end);
        return true;
    }

private:
    void skipTrivia() {
        while (m_pos < m_source.size()) {
            const QChar c = m_source.at(m_pos);
            if (c.isSpace()) {
                ++m_pos;
                continue;
            }
            if (c == QLatin1Char('/') && m_pos + 1 < m_source.size()) {
                const QChar next = m_source.at(m_pos + 1);
                if (next == QLatin1Char('/')) {  // 行注释
                    m_pos += 2;
                    while (m_pos < m_source.size() && m_source.at(m_pos) != QLatin1Char('\n')) {
                        ++m_pos;
                    }
                    continue;
                }
                if (next == QLatin1Char('*')) {  // 块注释
                    m_pos += 2;
                    while (m_pos + 1 < m_source.size() &&
                           !(m_source.at(m_pos) == QLatin1Char('*') && m_source.at(m_pos + 1) == QLatin1Char('/'))) {
                        ++m_pos;
                    }
                    m_pos = qMin(m_pos + 2, m_source.size());
                    continue;
                }
            }
            break;
        }
    }

    bool readNumber(QVector<Token>* out, QString* error) {
        const int start = m_pos;
        bool hasDigits = false;
        while (m_pos < m_source.size() && m_source.at(m_pos).isDigit()) {
            ++m_pos;
            hasDigits = true;
        }
        if (m_pos < m_source.size() && m_source.at(m_pos) == QLatin1Char('.')) {
            ++m_pos;
            while (m_pos < m_source.size() && m_source.at(m_pos).isDigit()) {
                ++m_pos;
                hasDigits = true;
            }
        }
        if (!hasDigits) {
            *error = unsupported(QStringLiteral("非法数字"));
            return false;
        }
        if (m_pos < m_source.size() &&
            (m_source.at(m_pos) == QLatin1Char('e') || m_source.at(m_pos) == QLatin1Char('E'))) {
            const int save = m_pos;
            ++m_pos;
            if (m_pos < m_source.size() &&
                (m_source.at(m_pos) == QLatin1Char('+') || m_source.at(m_pos) == QLatin1Char('-'))) {
                ++m_pos;
            }
            if (m_pos < m_source.size() && m_source.at(m_pos).isDigit()) {
                while (m_pos < m_source.size() && m_source.at(m_pos).isDigit()) {
                    ++m_pos;
                }
            } else {
                m_pos = save;
            }
        }
        const QString text = m_source.mid(start, m_pos - start);
        bool ok = false;
        const double d = text.toDouble(&ok);
        if (!ok || !std::isfinite(d)) {
            *error = unsupported(QStringLiteral("非法数字 '") + text + QLatin1Char('\''));
            return false;
        }
        Token token;
        token.type = TokenType::Number;
        token.text = text;
        token.number = d;
        token.position = start;
        out->append(token);
        return true;
    }

    bool readString(QVector<Token>* out, QString* error) {
        const QChar quote = m_source.at(m_pos);
        const int start = m_pos;
        ++m_pos;
        QString text;
        while (m_pos < m_source.size()) {
            const QChar c = m_source.at(m_pos);
            if (c == quote) {
                ++m_pos;
                Token token;
                token.type = TokenType::String;
                token.text = text;
                token.position = start;
                out->append(token);
                return true;
            }
            if (c == QLatin1Char('\\')) {
                ++m_pos;
                if (m_pos >= m_source.size()) {
                    break;
                }
                const QChar escaped = m_source.at(m_pos);
                ++m_pos;
                switch (escaped.unicode()) {
                case 'n': text += QLatin1Char('\n'); break;
                case 't': text += QLatin1Char('\t'); break;
                case 'r': text += QLatin1Char('\r'); break;
                case 'b': text += QLatin1Char('\b'); break;
                case 'f': text += QLatin1Char('\f'); break;
                case 'u': {
                    bool ok = false;
                    const uint code = (m_pos + 4 <= m_source.size())
                                          ? m_source.mid(m_pos, 4).toUInt(&ok, 16)
                                          : 0;
                    if (ok) {
                        text += QChar(static_cast<char16_t>(code));
                        m_pos += 4;
                    } else {
                        text += escaped;
                    }
                    break;
                }
                default: text += escaped; break;
                }
                continue;
            }
            text += c;
            ++m_pos;
        }
        *error = unsupported(QStringLiteral("字符串没有结束引号"));
        return false;
    }

    void readIdentifier(QVector<Token>* out) {
        const int start = m_pos;
        while (m_pos < m_source.size() && isIdentPart(m_source.at(m_pos))) {
            ++m_pos;
        }
        Token token;
        token.type = TokenType::Identifier;
        token.text = m_source.mid(start, m_pos - start);
        token.position = start;
        out->append(token);
    }

    bool readPunct(QVector<Token>* out, QString* error) {
        static const char* kTwoCharOps[] = { "==", "!=", "<=", ">=", "&&", "||" };
        for (const char* op : kTwoCharOps) {
            if (m_source.mid(m_pos, 2) == QLatin1String(op)) {
                Token token;
                token.type = TokenType::Punct;
                token.text = QLatin1String(op);
                token.position = m_pos;
                m_pos += 2;
                out->append(token);
                return true;
            }
        }
        const QChar c = m_source.at(m_pos);
        if (c == QLatin1Char('?')) {
            *error = unsupported(QStringLiteral("三元运算符 ?"));
            return false;
        }
        // 括号、大括号、方括号都留给语法层判断用途（代码块 / 数组 / 对象）。
        static const QString kSingleOps = QStringLiteral("+-*/%(),;=<>!.{}[]");
        if (!kSingleOps.contains(c)) {
            *error = unsupported(QStringLiteral("无法识别的字符 '") + c + QLatin1Char('\''));
            return false;
        }
        Token token;
        token.type = TokenType::Punct;
        token.text = QString(c);
        token.position = m_pos;
        ++m_pos;
        out->append(token);
        return true;
    }

    const QString& m_source;
    int m_pos = 0;
};

// ---------------------------------------------------------------- 语法 ----

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;
struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct Expr {
    enum Kind { NumberLit, StringLit, BoolLit, NullLit, Ident, Member, Call, Unary, Binary, Assign };
    Kind kind = NullLit;
    double number = 0.0;
    bool boolean = false;
    QString text;  // 字面量文本 / 标识符名 / 成员名 / 运算符
    int depth = 0; // 表达式树深度，用来防长运算符链把求值递归撑爆
    ExprPtr left;
    ExprPtr right;
    ExprPtr callee;
    QVector<ExprPtr> args;
};

struct Stmt {
    enum Kind { ExprStmt, VarDecl, If, Return, Block };
    Kind kind = ExprStmt;
    QString name;  // VarDecl 的变量名
    ExprPtr expr;
    QVector<StmtPtr> body;
    StmtPtr thenStmt;
    StmtPtr elseStmt;
};

// 明确不支持的关键字：碰到就报错，别当成未知字段蒙混过去。
bool isUnsupportedKeyword(const QString& word) {
    static const QStringList kKeywords = {
        QStringLiteral("var"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("return"),
        QStringLiteral("function"), QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("do"),
        QStringLiteral("switch"), QStringLiteral("case"), QStringLiteral("try"), QStringLiteral("catch"),
        QStringLiteral("finally"), QStringLiteral("throw"), QStringLiteral("new"), QStringLiteral("delete"),
        QStringLiteral("typeof"), QStringLiteral("instanceof"), QStringLiteral("void"), QStringLiteral("class"),
        QStringLiteral("extends"), QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("async"),
        QStringLiteral("await"), QStringLiteral("yield"), QStringLiteral("break"), QStringLiteral("continue"),
        QStringLiteral("with"), QStringLiteral("debugger")
    };
    return kKeywords.contains(word);
}

// 二元运算符优先级：数字越大越紧。
int binaryPrecedence(const QString& op) {
    if (op == QLatin1String("||")) return 1;
    if (op == QLatin1String("&&")) return 2;
    if (op == QLatin1String("==") || op == QLatin1String("!=")) return 3;
    if (op == QLatin1String("<") || op == QLatin1String("<=") || op == QLatin1String(">") ||
        op == QLatin1String(">=")) {
        return 4;
    }
    if (op == QLatin1String("+") || op == QLatin1String("-")) return 5;
    if (op == QLatin1String("*") || op == QLatin1String("/") || op == QLatin1String("%")) return 6;
    return 0;
}

class Parser {
public:
    explicit Parser(const QVector<Token>& tokens) : m_tokens(tokens) {}

    bool parseProgram(QVector<StmtPtr>* program) {
        while (!atEnd()) {
            StmtPtr stmt = parseStatement(0);
            if (!stmt) {
                return false;
            }
            program->append(stmt);
        }
        return true;
    }

    const QString& error() const { return m_error; }

private:
    const Token& peek(int offset = 0) const {
        const int last = m_tokens.isEmpty() ? 0 : m_tokens.size() - 1;
        return m_tokens.at(qMin(m_index + offset, last));
    }

    bool atEnd() const { return peek().type == TokenType::End; }

    void advance() {
        if (m_index + 1 < m_tokens.size()) {
            ++m_index;
        }
    }

    bool isPunct(const QString& text, int offset = 0) const {
        const Token& token = peek(offset);
        return token.type == TokenType::Punct && token.text == text;
    }

    bool matchPunct(const QString& text) {
        if (!isPunct(text)) {
            return false;
        }
        advance();
        return true;
    }

    bool fail(const QString& message) {
        if (m_error.isEmpty()) {
            m_error = message;
        }
        return false;
    }

    // 表达式树深度上限：长运算符链也会撑深求值递归。
    bool checkAstDepth(int depth) {
        if (depth > kMaxAstDepth) {
            fail(QStringLiteral("表达式过于复杂（嵌套层级上限 %1 层）").arg(kMaxAstDepth));
            return false;
        }
        return true;
    }

    StmtPtr parseStatement(int depth) {
        if (depth > kMaxDepth) {
            fail(QStringLiteral("脚本嵌套层级过深（上限 %1 层）").arg(kMaxDepth));
            return nullptr;
        }
        const Token& token = peek();
        if (token.type == TokenType::Identifier) {
            if (token.text == QLatin1String("var")) {
                return parseVarDecl(depth);
            }
            if (token.text == QLatin1String("if")) {
                return parseIf(depth);
            }
            if (token.text == QLatin1String("return")) {
                return parseReturn(depth);
            }
            if (isUnsupportedKeyword(token.text)) {
                fail(unsupported(QStringLiteral("关键字 ") + token.text));
                return nullptr;
            }
        }
        if (isPunct(QStringLiteral("{"))) {
            return parseBlock(depth);
        }
        if (isPunct(QStringLiteral(";"))) {
            advance();
            return std::make_shared<Stmt>();  // 空语句
        }
        StmtPtr stmt = std::make_shared<Stmt>();
        stmt->kind = Stmt::ExprStmt;
        stmt->expr = parseExpression(depth);
        if (!stmt->expr) {
            return nullptr;
        }
        if (!matchPunct(QStringLiteral(";")) && !atEnd()) {
            fail(unsupported(QStringLiteral("语句结尾缺少 ';'")));
            return nullptr;
        }
        return stmt;
    }

    StmtPtr parseBlock(int depth) {
        if (!matchPunct(QStringLiteral("{"))) {
            fail(unsupported(QStringLiteral("'{'")));
            return nullptr;
        }
        StmtPtr block = std::make_shared<Stmt>();
        block->kind = Stmt::Block;
        while (!atEnd() && !isPunct(QStringLiteral("}"))) {
            StmtPtr stmt = parseStatement(depth + 1);
            if (!stmt) {
                return nullptr;
            }
            block->body.append(stmt);
        }
        if (!matchPunct(QStringLiteral("}"))) {
            fail(unsupported(QStringLiteral("代码块缺少 '}'")));
            return nullptr;
        }
        return block;
    }

    StmtPtr parseVarDecl(int depth) {
        advance();  // var
        if (peek().type != TokenType::Identifier) {
            fail(unsupported(QStringLiteral("var 后缺少变量名")));
            return nullptr;
        }
        StmtPtr stmt = std::make_shared<Stmt>();
        stmt->kind = Stmt::VarDecl;
        stmt->name = peek().text;
        advance();
        if (matchPunct(QStringLiteral("="))) {
            stmt->expr = parseExpression(depth);
            if (!stmt->expr) {
                return nullptr;
            }
        }
        if (!matchPunct(QStringLiteral(";")) && !atEnd()) {
            fail(unsupported(QStringLiteral("语句结尾缺少 ';'")));
            return nullptr;
        }
        return stmt;
    }

    StmtPtr parseIf(int depth) {
        advance();  // if
        if (!matchPunct(QStringLiteral("("))) {
            fail(unsupported(QStringLiteral("if 后缺少 '('")));
            return nullptr;
        }
        StmtPtr stmt = std::make_shared<Stmt>();
        stmt->kind = Stmt::If;
        stmt->expr = parseExpression(depth);
        if (!stmt->expr) {
            return nullptr;
        }
        if (!matchPunct(QStringLiteral(")"))) {
            fail(unsupported(QStringLiteral("if 缺少 ')'")));
            return nullptr;
        }
        stmt->thenStmt = parseStatement(depth + 1);
        if (!stmt->thenStmt) {
            return nullptr;
        }
        if (peek().type == TokenType::Identifier && peek().text == QLatin1String("else")) {
            advance();
            stmt->elseStmt = parseStatement(depth + 1);
            if (!stmt->elseStmt) {
                return nullptr;
            }
        }
        return stmt;
    }

    StmtPtr parseReturn(int depth) {
        advance();  // return
        StmtPtr stmt = std::make_shared<Stmt>();
        stmt->kind = Stmt::Return;
        if (!isPunct(QStringLiteral(";")) && !isPunct(QStringLiteral("}")) && !atEnd()) {
            stmt->expr = parseExpression(depth);
            if (!stmt->expr) {
                return nullptr;
            }
        }
        if (!matchPunct(QStringLiteral(";")) && !atEnd()) {
            fail(unsupported(QStringLiteral("语句结尾缺少 ';'")));
            return nullptr;
        }
        return stmt;
    }

    ExprPtr parseExpression(int depth) { return parseAssign(depth); }

    ExprPtr parseAssign(int depth) {
        ExprPtr left = parseBinary(depth, 1);
        if (!left) {
            return nullptr;
        }
        if (isPunct(QStringLiteral("="))) {
            advance();
            if (!isEventValueTarget(left)) {
                fail(unsupported(QStringLiteral("赋值目标只能是 event.value")));
                return nullptr;
            }
            ExprPtr right = parseAssign(depth);
            if (!right) {
                return nullptr;
            }
            ExprPtr assign = std::make_shared<Expr>();
            assign->kind = Expr::Assign;
            assign->left = left;
            assign->right = right;
            assign->depth = qMax(left->depth, right->depth) + 1;
            if (!checkAstDepth(assign->depth)) {
                return nullptr;
            }
            return assign;
        }
        return left;
    }

    static bool isEventValueTarget(const ExprPtr& expr) {
        if (!expr || expr->kind != Expr::Member || !expr->left || expr->left->kind != Expr::Ident) {
            return false;
        }
        const QString& base = expr->left->text;
        return (base == QLatin1String("event") || base == QLatin1String("this")) &&
               expr->text == QLatin1String("value");
    }

    ExprPtr parseBinary(int depth, int minPrecedence) {
        ExprPtr left = parseUnary(depth);
        if (!left) {
            return nullptr;
        }
        while (true) {
            const Token& token = peek();
            if (token.type != TokenType::Punct) {
                break;
            }
            const int precedence = binaryPrecedence(token.text);
            if (precedence == 0 || precedence < minPrecedence) {
                break;
            }
            const QString op = token.text;
            advance();
            ExprPtr right = parseBinary(depth, precedence + 1);
            if (!right) {
                return nullptr;
            }
            ExprPtr node = std::make_shared<Expr>();
            node->kind = Expr::Binary;
            node->text = op;
            node->left = left;
            node->right = right;
            node->depth = qMax(left->depth, right->depth) + 1;
            if (!checkAstDepth(node->depth)) {
                return nullptr;
            }
            left = node;
        }
        return left;
    }

    ExprPtr parseUnary(int depth) {
        if (depth > kMaxDepth) {
            fail(QStringLiteral("脚本嵌套层级过深（上限 %1 层）").arg(kMaxDepth));
            return nullptr;
        }
        const Token& token = peek();
        if (token.type == TokenType::Punct &&
            (token.text == QLatin1String("!") || token.text == QLatin1String("-") ||
             token.text == QLatin1String("+"))) {
            const QString op = token.text;
            // ++ / -- 会改状态，不支持；但要给出明确原因而不是含糊的语法错。
            if ((op == QLatin1String("+") || op == QLatin1String("-")) && isPunct(op, 1) &&
                peek(1).position == peek().position + 1) {
                fail(unsupported(QStringLiteral("自增/自减运算符 ") + op + op));
                return nullptr;
            }
            advance();
            ExprPtr operand = parseUnary(depth + 1);
            if (!operand) {
                return nullptr;
            }
            ExprPtr node = std::make_shared<Expr>();
            node->kind = Expr::Unary;
            node->text = op;
            node->left = operand;
            node->depth = operand->depth + 1;
            if (!checkAstDepth(node->depth)) {
                return nullptr;
            }
            return node;
        }
        return parsePostfix(depth);
    }

    ExprPtr parsePostfix(int depth) {
        ExprPtr base = parsePrimary(depth);
        if (!base) {
            return nullptr;
        }
        while (true) {
            if (isPunct(QStringLiteral("."))) {
                advance();
                if (peek().type != TokenType::Identifier) {
                    fail(unsupported(QStringLiteral("'.' 后缺少属性名")));
                    return nullptr;
                }
                ExprPtr node = std::make_shared<Expr>();
                node->kind = Expr::Member;
                node->text = peek().text;
                node->left = base;
                node->depth = base->depth + 1;
                if (!checkAstDepth(node->depth)) {
                    return nullptr;
                }
                advance();
                base = node;
                continue;
            }
            if (isPunct(QStringLiteral("("))) {
                ExprPtr node = std::make_shared<Expr>();
                node->kind = Expr::Call;
                node->callee = base;
                node->depth = base->depth + 1;
                if (!checkAstDepth(node->depth)) {
                    return nullptr;
                }
                if (!parseArguments(depth, &node->args)) {
                    return nullptr;
                }
                base = node;
                continue;
            }
            break;
        }
        return base;
    }

    bool parseArguments(int depth, QVector<ExprPtr>* out) {
        if (!matchPunct(QStringLiteral("("))) {
            fail(unsupported(QStringLiteral("缺少 '('")));
            return false;
        }
        if (matchPunct(QStringLiteral(")"))) {
            return true;
        }
        while (true) {
            ExprPtr argument = parseExpression(depth + 1);
            if (!argument) {
                return false;
            }
            out->append(argument);
            if (matchPunct(QStringLiteral(","))) {
                continue;
            }
            if (matchPunct(QStringLiteral(")"))) {
                return true;
            }
            fail(unsupported(QStringLiteral("参数列表缺少 ')'")));
            return false;
        }
    }

    ExprPtr parsePrimary(int depth) {
        const Token& token = peek();
        if (token.type == TokenType::Number) {
            advance();
            ExprPtr node = std::make_shared<Expr>();
            node->kind = Expr::NumberLit;
            node->number = token.number;
            return node;
        }
        if (token.type == TokenType::String) {
            advance();
            ExprPtr node = std::make_shared<Expr>();
            node->kind = Expr::StringLit;
            node->text = token.text;
            return node;
        }
        if (token.type == TokenType::Identifier) {
            advance();
            if (token.text == QLatin1String("true") || token.text == QLatin1String("false")) {
                ExprPtr node = std::make_shared<Expr>();
                node->kind = Expr::BoolLit;
                node->boolean = token.text == QLatin1String("true");
                return node;
            }
            if (token.text == QLatin1String("null") || token.text == QLatin1String("undefined")) {
                ExprPtr node = std::make_shared<Expr>();
                node->kind = Expr::NullLit;
                return node;
            }
            if (isUnsupportedKeyword(token.text)) {
                fail(unsupported(QStringLiteral("关键字 ") + token.text));
                return nullptr;
            }
            ExprPtr node = std::make_shared<Expr>();
            node->kind = Expr::Ident;
            node->text = token.text;
            return node;
        }
        if (token.type == TokenType::Punct) {
            if (token.text == QLatin1String("(")) {
                advance();
                ExprPtr inner = parseExpression(depth + 1);
                if (!inner) {
                    return nullptr;
                }
                if (!matchPunct(QStringLiteral(")"))) {
                    fail(unsupported(QStringLiteral("缺少 ')'")));
                    return nullptr;
                }
                return inner;
            }
            if (token.text == QLatin1String("[")) {
                fail(unsupported(QStringLiteral("数组字面量 []")));
                return nullptr;
            }
            if (token.text == QLatin1String("{")) {
                fail(unsupported(QStringLiteral("对象字面量 {}")));
                return nullptr;
            }
            fail(unsupported(QStringLiteral("意外的记号 '") + token.text + QLatin1Char('\'')));
            return nullptr;
        }
        fail(unsupported(QStringLiteral("表达式不完整")));
        return nullptr;
    }

    const QVector<Token>& m_tokens;
    int m_index = 0;
    QString m_error;
};

// ---------------------------------------------------------------- 求值 ----

// 松散相等："1" == 1 为真；null 只跟 null 相等。
bool looseEquals(const Value& a, const Value& b) {
    if (a.kind == Value::Null || b.kind == Value::Null) {
        return a.kind == b.kind;
    }
    if (a.kind == Value::Str && b.kind == Value::Str) {
        return a.text == b.text;
    }
    if (a.kind == Value::Bool && b.kind == Value::Bool) {
        return a.boolean == b.boolean;
    }
    double x = 0.0;
    double y = 0.0;
    if (!valueToNumber(a, &x) || !valueToNumber(b, &y)) {
        return false;
    }
    return x == y;
}

int intArgument(const Value& value, int fallback) {
    double d = 0.0;
    if (!valueToNumber(value, &d)) {
        return fallback;
    }
    return static_cast<int>(d);
}

// parseInt / parseFloat：取字符串开头的数字，"12abc" → 12。
bool parseNumericPrefix(const QString& text, double* out, bool integerOnly) {
    int i = 0;
    while (i < text.size() && text.at(i).isSpace()) {
        ++i;
    }
    if (i < text.size() && (text.at(i) == QLatin1Char('+') || text.at(i) == QLatin1Char('-'))) {
        ++i;
    }
    const int digitsStart = i;
    while (i < text.size() && text.at(i).isDigit()) {
        ++i;
    }
    bool hasDigits = i > digitsStart;
    if (!integerOnly && i < text.size() && text.at(i) == QLatin1Char('.')) {
        const int save = i;
        ++i;
        const int fractionStart = i;
        while (i < text.size() && text.at(i).isDigit()) {
            ++i;
        }
        if (i > fractionStart) {
            hasDigits = true;
        } else {
            i = save;
        }
    }
    if (!hasDigits) {
        return false;
    }
    bool ok = false;
    const double d = text.left(i).toDouble(&ok);
    if (!ok) {
        return false;
    }
    *out = d;
    return true;
}

class Evaluator {
public:
    Evaluator(const QVector<FormFieldValue>& fields, const QString& eventField, const QString& eventValue)
        : m_fields(fields), m_eventField(eventField), m_eventValue(eventValue) {}

    bool run(const QVector<StmtPtr>& program, Value* out) {
        for (const StmtPtr& stmt : program) {
            if (!execStatement(stmt)) {
                return false;
            }
            if (m_returned) {
                break;
            }
        }
        *out = m_returned ? m_returnValue : m_lastValue;
        return true;
    }

    const QString& error() const { return m_error; }
    const QStringList& notices() const { return m_notices; }

private:
    bool fail(const QString& message) {
        if (m_error.isEmpty()) {
            m_error = message;
        }
        return false;
    }

    bool execStatement(const StmtPtr& stmt) {
        if (!stmt) {
            return true;
        }
        switch (stmt->kind) {
        case Stmt::Block: {
            for (const StmtPtr& inner : stmt->body) {
                if (!execStatement(inner)) {
                    return false;
                }
                if (m_returned) {
                    return true;
                }
            }
            return true;
        }
        case Stmt::ExprStmt: {
            if (!stmt->expr) {
                return true;
            }
            Value value;
            if (!evalExpr(stmt->expr, &value)) {
                return false;
            }
            m_lastValue = value;
            return true;
        }
        case Stmt::VarDecl: {
            Value value = makeNull();
            if (stmt->expr && !evalExpr(stmt->expr, &value)) {
                return false;
            }
            m_vars.insert(stmt->name, value);
            return true;
        }
        case Stmt::If: {
            Value condition;
            if (!evalExpr(stmt->expr, &condition)) {
                return false;
            }
            if (truthy(condition)) {
                return execStatement(stmt->thenStmt);
            }
            return execStatement(stmt->elseStmt);
        }
        case Stmt::Return: {
            Value value = makeNull();
            if (stmt->expr && !evalExpr(stmt->expr, &value)) {
                return false;
            }
            m_returnValue = value;
            m_returned = true;
            return true;
        }
        }
        return true;
    }

    bool evalExpr(const ExprPtr& expr, Value* out) {
        if (!expr) {
            *out = makeNull();
            return true;
        }
        switch (expr->kind) {
        case Expr::NumberLit:
            *out = makeNumber(expr->number);
            return true;
        case Expr::StringLit:
            *out = makeStr(expr->text);
            return true;
        case Expr::BoolLit:
            *out = makeBool(expr->boolean);
            return true;
        case Expr::NullLit:
            *out = makeNull();
            return true;
        case Expr::Ident:
            return evalIdent(expr, out);
        case Expr::Member:
            return evalMember(expr, out);
        case Expr::Call:
            return evalCall(expr, out);
        case Expr::Unary:
            return evalUnary(expr, out);
        case Expr::Binary:
            return evalBinary(expr, out);
        case Expr::Assign: {
            Value value;
            if (!evalExpr(expr->right, &value)) {
                return false;
            }
            // event.value 存的是文本，跟 PDF 域值一致。
            m_eventValue = valueToText(value);
            *out = value;
            return true;
        }
        }
        return true;
    }

    bool evalIdent(const ExprPtr& expr, Value* out) {
        const QString& name = expr->text;
        if (name == QLatin1String("this")) {
            *out = makeStr(m_eventField);
            return true;
        }
        if (name == QLatin1String("event")) {
            return fail(unsupported(QStringLiteral("event（只能使用 event.value / event.target）")));
        }
        const auto variable = m_vars.constFind(name);
        if (variable != m_vars.constEnd()) {
            *out = variable.value();
            return true;
        }
        for (const FormFieldValue& field : m_fields) {
            if (field.name == name) {
                *out = makeStr(field.value);
                return true;
            }
        }
        // 找不到不是错误：脚本可能引用了还没填的字段，按空串继续。
        m_notices.append(QStringLiteral("未知字段：") + name);
        *out = makeStr(QString());
        return true;
    }

    bool evalMember(const ExprPtr& expr, Value* out) {
        const ExprPtr& base = expr->left;
        if (base && base->kind == Expr::Ident) {
            const QString& baseName = base->text;
            if (baseName == QLatin1String("event") || baseName == QLatin1String("this")) {
                if (expr->text == QLatin1String("value")) {
                    *out = makeStr(m_eventValue);
                    return true;
                }
                if (expr->text == QLatin1String("target") || expr->text == QLatin1String("name")) {
                    *out = makeStr(m_eventField);
                    return true;
                }
                return fail(unsupported(QStringLiteral("不支持的属性 ") + baseName + QLatin1Char('.') + expr->text));
            }
            if (baseName == QLatin1String("Math")) {
                return fail(unsupported(QStringLiteral("Math 对象（只能调用 Math.round 等函数）")));
            }
        }
        return fail(unsupported(QStringLiteral("不支持的属性访问")));
    }

    bool evalUnary(const ExprPtr& expr, Value* out) {
        Value operand;
        if (!evalExpr(expr->left, &operand)) {
            return false;
        }
        if (expr->text == QLatin1String("!")) {
            *out = makeBool(!truthy(operand));
            return true;
        }
        double d = 0.0;
        if (!valueToNumber(operand, &d)) {
            return fail(numberError(operand));
        }
        if (expr->text == QLatin1String("-")) {
            d = -d;
        }
        *out = makeNumber(d);
        return true;
    }

    bool evalBinary(const ExprPtr& expr, Value* out) {
        const QString& op = expr->text;
        if (op == QLatin1String("&&") || op == QLatin1String("||")) {
            Value left;
            if (!evalExpr(expr->left, &left)) {
                return false;
            }
            if (op == QLatin1String("&&") && !truthy(left)) {
                *out = left;
                return true;
            }
            if (op == QLatin1String("||") && truthy(left)) {
                *out = left;
                return true;
            }
            Value right;
            if (!evalExpr(expr->right, &right)) {
                return false;
            }
            *out = right;
            return true;
        }

        Value left;
        Value right;
        if (!evalExpr(expr->left, &left)) {
            return false;
        }
        if (!evalExpr(expr->right, &right)) {
            return false;
        }

        if (op == QLatin1String("==") || op == QLatin1String("!=")) {
            const bool equal = looseEquals(left, right);
            *out = makeBool(op == QLatin1String("==") ? equal : !equal);
            return true;
        }

        if (op == QLatin1String("<") || op == QLatin1String("<=") || op == QLatin1String(">") ||
            op == QLatin1String(">=")) {
            bool result = false;
            if (left.kind == Value::Str && right.kind == Value::Str) {
                const int comparison = QString::compare(left.text, right.text);
                result = (op == QLatin1String("<")) ? comparison < 0
                       : (op == QLatin1String("<=")) ? comparison <= 0
                       : (op == QLatin1String(">")) ? comparison > 0
                                                    : comparison >= 0;
            } else {
                double a = 0.0;
                double b = 0.0;
                if (!valueToNumber(left, &a)) {
                    return fail(numberError(left));
                }
                if (!valueToNumber(right, &b)) {
                    return fail(numberError(right));
                }
                result = (op == QLatin1String("<")) ? a < b
                       : (op == QLatin1String("<=")) ? a <= b
                       : (op == QLatin1String(">")) ? a > b
                                                    : a >= b;
            }
            *out = makeBool(result);
            return true;
        }

        // + 遇到字符串就是拼接，这是 JS 规则，也是表单脚本最容易踩的坑。
        if (op == QLatin1String("+") && (left.kind == Value::Str || right.kind == Value::Str)) {
            *out = makeStr(valueToText(left) + valueToText(right));
            return true;
        }

        double a = 0.0;
        double b = 0.0;
        if (!valueToNumber(left, &a)) {
            return fail(numberError(left));
        }
        if (!valueToNumber(right, &b)) {
            return fail(numberError(right));
        }
        double result = 0.0;
        if (op == QLatin1String("+")) {
            result = a + b;
        } else if (op == QLatin1String("-")) {
            result = a - b;
        } else if (op == QLatin1String("*")) {
            result = a * b;
        } else if (op == QLatin1String("/")) {
            if (b == 0.0) {
                return fail(QStringLiteral("除零错误：除数不能为 0"));
            }
            result = a / b;
        } else if (op == QLatin1String("%")) {
            if (b == 0.0) {
                return fail(QStringLiteral("除零错误：除数不能为 0"));
            }
            result = std::fmod(a, b);
        } else {
            return fail(unsupported(QStringLiteral("运算符 ") + op));
        }
        if (!std::isfinite(result)) {
            return fail(QStringLiteral("求值错误：数值溢出"));
        }
        *out = makeNumber(result);
        return true;
    }

    bool evalCall(const ExprPtr& expr, Value* out) {
        const ExprPtr& callee = expr->callee;
        if (!callee) {
            return fail(unsupported(QStringLiteral("无法识别的调用")));
        }
        if (callee->kind == Expr::Ident) {
            const QString& name = callee->text;
            if (name == QLatin1String("AFNumber_Format")) {
                return evalFormatCall(expr, out);
            }
            if (name == QLatin1String("AFSimple_Calculate")) {
                return evalCalculateCall(expr, out);
            }
            if (name == QLatin1String("String")) {
                Value argument;
                if (!evalFirstArgument(expr, &argument)) {
                    return false;
                }
                *out = makeStr(valueToText(argument));
                return true;
            }
            if (name == QLatin1String("Number")) {
                Value argument;
                if (!evalFirstArgument(expr, &argument)) {
                    return false;
                }
                double d = 0.0;
                if (!valueToNumber(argument, &d)) {
                    return fail(numberError(argument));
                }
                *out = makeNumber(d);
                return true;
            }
            if (name == QLatin1String("parseInt") || name == QLatin1String("parseFloat")) {
                Value argument;
                if (!evalFirstArgument(expr, &argument)) {
                    return false;
                }
                double d = 0.0;
                const bool integerOnly = name == QLatin1String("parseInt");
                if (!parseNumericPrefix(valueToText(argument), &d, integerOnly)) {
                    return fail(QStringLiteral("求值错误：无法解析数字 '") + valueToText(argument) +
                                QLatin1Char('\''));
                }
                *out = makeNumber(integerOnly ? std::trunc(d) : d);
                return true;
            }
            return fail(unsupported(QStringLiteral("未知函数 ") + name));
        }
        if (callee->kind == Expr::Member) {
            const ExprPtr& base = callee->left;
            if (base && base->kind == Expr::Ident && base->text == QLatin1String("Math")) {
                return evalMathCall(expr, out);
            }
            return fail(unsupported(QStringLiteral("不支持的方法调用")));
        }
        return fail(unsupported(QStringLiteral("不支持的调用")));
    }

    bool evalFirstArgument(const ExprPtr& expr, Value* out) {
        if (expr->args.isEmpty()) {
            *out = makeNull();
            return true;
        }
        return evalExpr(expr->args.first(), out);
    }

    bool evalFormatCall(const ExprPtr& expr, Value* out) {
        int decimals = 0;
        bool thousandsSeparator = false;
        bool minusSign = true;
        int grouping = 0;
        QString decimalPoint = QStringLiteral(".");
        for (int i = 0; i < expr->args.size(); ++i) {
            Value argument;
            if (!evalExpr(expr->args.at(i), &argument)) {
                return false;
            }
            switch (i) {
            case 0: decimals = intArgument(argument, 0); break;
            case 1: thousandsSeparator = intArgument(argument, 0) != 0; break;
            case 2: minusSign = intArgument(argument, 1) != 0; break;
            case 3: grouping = intArgument(argument, 0); break;
            case 4: decimalPoint = valueToText(argument); break;
            default: break;
            }
        }
        if (grouping <= 0) {
            grouping = 3;
        }
        *out = makeStr(applyNumberFormat(m_eventValue, decimals, thousandsSeparator, minusSign, grouping,
                                        decimalPoint));
        return true;
    }

    bool evalCalculateCall(const ExprPtr& expr, Value* out) {
        if (expr->args.isEmpty()) {
            return fail(unsupported(QStringLiteral("AFSimple_Calculate 缺少字段名")));
        }
        Value nameValue;
        if (!evalExpr(expr->args.first(), &nameValue)) {
            return false;
        }
        const QString fieldName = valueToText(nameValue);
        for (const FormFieldValue& field : m_fields) {
            if (field.name != fieldName) {
                continue;
            }
            double d = 0.0;
            if (textToNumber(field.value, &d)) {
                *out = makeNumber(d);
            } else {
                *out = makeStr(field.value);
            }
            return true;
        }
        // 取不到就用 fallback：这是脚本作者预期的行为，不算错误。
        if (expr->args.size() > 1) {
            Value fallback;
            if (!evalExpr(expr->args.at(1), &fallback)) {
                return false;
            }
            *out = fallback;
            return true;
        }
        *out = makeNull();
        return true;
    }

    bool evalMathCall(const ExprPtr& expr, Value* out) {
        const QString& name = expr->callee->text;
        QVector<double> numbers;
        for (const ExprPtr& argument : expr->args) {
            Value value;
            if (!evalExpr(argument, &value)) {
                return false;
            }
            double d = 0.0;
            if (!valueToNumber(value, &d)) {
                return fail(numberError(value));
            }
            numbers.append(d);
        }
        if (numbers.isEmpty()) {
            return fail(unsupported(QStringLiteral("Math.") + name + QStringLiteral(" 缺少参数")));
        }
        if (name == QLatin1String("min") || name == QLatin1String("max")) {
            double result = numbers.first();
            for (double d : numbers) {
                result = (name == QLatin1String("min")) ? qMin(result, d) : qMax(result, d);
            }
            *out = makeNumber(result);
            return true;
        }
        double result = numbers.first();
        if (name == QLatin1String("round")) {
            result = std::round(result);
        } else if (name == QLatin1String("floor")) {
            result = std::floor(result);
        } else if (name == QLatin1String("ceil")) {
            result = std::ceil(result);
        } else if (name == QLatin1String("abs")) {
            result = std::fabs(result);
        } else {
            return fail(unsupported(QStringLiteral("未知函数 Math.") + name));
        }
        *out = makeNumber(result);
        return true;
    }

    const QVector<FormFieldValue>& m_fields;
    QString m_eventField;
    QString m_eventValue;
    QHash<QString, Value> m_vars;
    QStringList m_notices;
    QString m_error;
    bool m_returned = false;
    Value m_returnValue;
    Value m_lastValue;
};

// -------------------------------------------------------------- 格式化 ----

// 按 AFNumber_Format 的语义格式化一个数；不是数字就原样返回。
QString applyNumberFormat(const QString& rawValue, int decimals, bool thousandsSeparator, bool minusSign,
                           int grouping, const QString& decimalPoint) {
    bool ok = false;
    const double d = rawValue.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(d)) {
        return rawValue;
    }
    if (decimals < 0) {
        decimals = 0;
    }
    if (decimals > 20) {
        decimals = 20;
    }
    if (grouping <= 0) {
        grouping = 3;
    }
    QString text = QString::number(d, 'f', decimals);
    if (text.startsWith(QLatin1Char('-')) && !minusSign) {
        text.remove(0, 1);
    }
    QString integerPart = text;
    QString fractionPart;
    const int dot = text.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        integerPart = text.left(dot);
        fractionPart = text.mid(dot + 1);
    }
    if (thousandsSeparator) {
        // 小数点是逗号时，千分位就用点，反之亦然。
        const QChar groupSeparator = (decimalPoint == QLatin1String(",")) ? QLatin1Char('.') : QLatin1Char(',');
        QString grouped;
        const int count = integerPart.size();
        for (int i = 0; i < count; ++i) {
            if (i > 0 && (count - i) % grouping == 0) {
                grouped += groupSeparator;
            }
            grouped += integerPart.at(i);
        }
        integerPart = grouped;
    }
    QString result = integerPart;
    if (!fractionPart.isEmpty() && !decimalPoint.isEmpty()) {
        // decimalPoint 留空表示不要小数部分。
        result += decimalPoint + fractionPart;
    }
    return result;
}

// 取出 "AFNumber_Format(2, 1, 0, 0, ".", false)" 或 "2,1,0" 里的参数。
QVector<QString> parseFormatArguments(const QString& format) {
    QString body = format.trimmed();
    const int call = body.indexOf(QLatin1String("AFNumber_Format"));
    if (call >= 0) {
        const int open = body.indexOf(QLatin1Char('('), call);
        if (open >= 0) {
            int depth = 0;
            int close = -1;
            for (int i = open; i < body.size(); ++i) {
                const QChar c = body.at(i);
                if (c == QLatin1Char('(')) {
                    ++depth;
                } else if (c == QLatin1Char(')')) {
                    --depth;
                    if (depth == 0) {
                        close = i;
                        break;
                    }
                }
            }
            body = (close > open) ? body.mid(open + 1, close - open - 1) : body.mid(open + 1);
        }
    }
    QVector<QString> args;
    QString current;
    int depth = 0;
    QChar quote;
    for (int i = 0; i < body.size(); ++i) {
        const QChar c = body.at(i);
        if (!quote.isNull()) {
            current += c;
            if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            current += c;
            continue;
        }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) {
            ++depth;
            current += c;
            continue;
        }
        if (c == QLatin1Char(')') || c == QLatin1Char(']')) {
            --depth;
            current += c;
            continue;
        }
        if (c == QLatin1Char(',') && depth == 0) {
            args.append(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty() || !args.isEmpty()) {
        args.append(current);
    }
    return args;
}

QString unquote(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.size() >= 2) {
        const QChar first = trimmed.at(0);
        if ((first == QLatin1Char('"') || first == QLatin1Char('\'')) &&
            trimmed.at(trimmed.size() - 1) == first) {
            return trimmed.mid(1, trimmed.size() - 2);
        }
    }
    return trimmed;
}

}  // namespace

FormJsResult evaluateFormScript(const QString& script, const QVector<FormFieldValue>& fieldValues,
                                const QString& eventField, const QString& eventValue) {
    FormJsResult result;
    if (script.trimmed().isEmpty()) {
        // 空脚本不是错误：域本来就可以没有脚本。
        result.ok = true;
        result.valid = true;
        result.value.clear();
        return result;
    }

    QVector<Token> tokens;
    QString error;
    Tokenizer tokenizer(script);
    if (!tokenizer.tokenize(&tokens, &error)) {
        result.ok = false;
        result.valid = false;
        result.error = error;
        return result;
    }

    QVector<StmtPtr> program;
    Parser parser(tokens);
    if (!parser.parseProgram(&program)) {
        result.ok = false;
        result.valid = false;
        result.error = parser.error();
        return result;
    }

    Evaluator evaluator(fieldValues, eventField, eventValue);
    Value outcome;
    if (!evaluator.run(program, &outcome)) {
        result.ok = false;
        result.valid = false;
        result.error = evaluator.error();
        return result;
    }

    result.ok = true;
    result.value = valueToText(outcome);
    // 校验脚本返回布尔值时，valid 跟着它走；其它脚本恒为 true。
    result.valid = (outcome.kind == Value::Bool) ? outcome.boolean : true;
    if (!evaluator.notices().isEmpty()) {
        result.error = evaluator.notices().join(QStringLiteral("；"));
    }
    return result;
}

QString formatFormNumber(const QString& format, const QString& value) {
    const QVector<QString> args = parseFormatArguments(format);
    const auto argumentNumber = [&args](int index, double fallback) -> double {
        if (index >= args.size()) {
            return fallback;
        }
        bool ok = false;
        const double d = unquote(args.at(index)).trimmed().toDouble(&ok);
        return ok ? d : fallback;
    };
    int decimals = static_cast<int>(argumentNumber(0, 0.0));
    const bool thousandsSeparator = argumentNumber(1, 0.0) != 0.0;
    const bool minusSign = argumentNumber(2, 1.0) != 0.0;
    int grouping = static_cast<int>(argumentNumber(3, 0.0));
    const QString decimalPoint = args.size() > 4 ? unquote(args.at(4)) : QStringLiteral(".");
    if (grouping <= 0) {
        grouping = 3;
    }
    return applyNumberFormat(value, decimals, thousandsSeparator, minusSign, grouping, decimalPoint);
}

}  // namespace aipdf::pdf
