// XFA 脚本（<script> / <calculate> 里的那段代码）的子集求值。
//
// 为什么单独一个文件、而不是直接用 FormJsEvaluator：
// FormJsEvaluator 是给 AcroForm 域脚本写的，它只认 this.value / event.value 一种赋值目标，
// 而且没有「xfa.form.form1.Field.value」这种 SOM 路径的概念。
// XFA 脚本要的是「给某个字段赋值」，所以这层负责翻译路径、接住赋值目标，
// 表达式本身仍然交给 FormJsEvaluator 求值——白名单子集只有一份，不重复实现。
//
// 为什么与 XfaSupport 分成两个文件：XfaSupport.cpp 会被单独的测试目标编译，
// 那个目标没有 FormJsEvaluator.cpp；求值器这一层要用到它，分开才都链接得上。
//
// 子集外的语法必须报错，不许静默忽略：静默忽略等于给用户一份「算过了」的数据，
// 而那个数据根本没算。

#include "pdf/forms/XfaScriptRunner.h"

#include "pdf/forms/FormJsEvaluator.h"

#include <QHash>
#include <QSet>

namespace aipdf::pdf {

// ---------------------------------------------------------------- 脚本 ----

namespace {

// 重算轮数上限。
//
// 为什么需要多轮：一个脚本里可能有 A = B + 1; C = A + 1; 这种前后依赖。
// 求值器只有一个 event.value 槽位，一次只能接住一个字段的值，
// 所以每个目标字段各跑一遍，靠多跑几轮把前一个字段的新值带进后一个字段的计算里。
constexpr int kMaxRounds = 4;

// ------------------------------------------------------------------ 词法 ----

enum class TokType { Ident, Number, String, Punct };

struct Tok {
    TokType type = TokType::Punct;
    QString text;
};

// 标识符允许 Unicode 字母：XFA 的字段名里常有中文。
bool isIdentStart(QChar c) {
    return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

bool isIdentPart(QChar c) {
    return isIdentStart(c) || c.isDigit() || c.isMark();
}

bool isValidIdentifier(const QString& name) {
    if (name.isEmpty()) return false;
    if (!isIdentStart(name.at(0))) return false;
    for (const QChar c : name) {
        if (!isIdentPart(c)) return false;
    }
    return true;
}

class Lexer {
public:
    explicit Lexer(const QString& source) : m_source(source) {}

    bool tokenize(QVector<Tok>* out, QString* error) {
        while (m_pos < m_source.size()) {
            const QChar c = m_source.at(m_pos);
            if (c.isSpace()) {
                ++m_pos;
                continue;
            }
            // 注释：脚本里常见，词法阶段就丢掉，免得后面的重写把它们当代码。
            if (c == QLatin1Char('/') && m_pos + 1 < m_source.size() &&
                m_source.at(m_pos + 1) == QLatin1Char('/')) {
                while (m_pos < m_source.size() && m_source.at(m_pos) != QLatin1Char('\n')) ++m_pos;
                continue;
            }
            if (c == QLatin1Char('/') && m_pos + 1 < m_source.size() &&
                m_source.at(m_pos + 1) == QLatin1Char('*')) {
                m_pos += 2;
                while (m_pos + 1 < m_source.size() &&
                       !(m_source.at(m_pos) == QLatin1Char('*') && m_source.at(m_pos + 1) == QLatin1Char('/'))) {
                    ++m_pos;
                }
                m_pos = qMin(m_pos + 2, m_source.size());
                continue;
            }
            if (isIdentStart(c)) {
                readIdent(out);
                continue;
            }
            if (c.isDigit()) {
                readNumber(out);
                continue;
            }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                if (!readString(out, error)) return false;
                continue;
            }
            readPunct(out);
        }
        return true;
    }

private:
    void readIdent(QVector<Tok>* out) {
        const int start = m_pos;
        while (m_pos < m_source.size() && isIdentPart(m_source.at(m_pos))) ++m_pos;
        Tok tok;
        tok.type = TokType::Ident;
        tok.text = m_source.mid(start, m_pos - start);
        out->append(tok);
    }

    void readNumber(QVector<Tok>* out) {
        const int start = m_pos;
        while (m_pos < m_source.size() && m_source.at(m_pos).isDigit()) ++m_pos;
        if (m_pos + 1 < m_source.size() && m_source.at(m_pos) == QLatin1Char('.') &&
            m_source.at(m_pos + 1).isDigit()) {
            ++m_pos;
            while (m_pos < m_source.size() && m_source.at(m_pos).isDigit()) ++m_pos;
        }
        Tok tok;
        tok.type = TokType::Number;
        tok.text = m_source.mid(start, m_pos - start);
        out->append(tok);
    }

    bool readString(QVector<Tok>* out, QString* error) {
        const QChar quote = m_source.at(m_pos);
        const int start = m_pos;
        ++m_pos;
        bool closed = false;
        while (m_pos < m_source.size()) {
            const QChar c = m_source.at(m_pos);
            if (c == QLatin1Char('\\')) {
                m_pos += 2;
                continue;
            }
            if (c == quote) {
                ++m_pos;
                closed = true;
                break;
            }
            ++m_pos;
        }
        if (!closed) {
            *error = QStringLiteral("脚本用了不支持的语法：字符串没有结束引号");
            return false;
        }
        Tok tok;
        tok.type = TokType::String;
        // 保留原文（含引号与转义）：后面拼回去时不能把转义吃掉。
        tok.text = m_source.mid(start, m_pos - start);
        out->append(tok);
        return true;
    }

    void readPunct(QVector<Tok>* out) {
        static const char* const kTwoCharOps[] = {"==", "!=", "<=", ">=", "&&", "||"};
        Tok tok;
        tok.type = TokType::Punct;
        for (const char* op : kTwoCharOps) {
            if (m_source.mid(m_pos, 2) == QLatin1String(op)) {
                tok.text = QLatin1String(op);
                m_pos += 2;
                out->append(tok);
                return;
            }
        }
        // 不认识的字符照样收成记号：求值器会给出「无法识别的字符」，比这里猜一个强。
        tok.text = QString(m_source.at(m_pos));
        ++m_pos;
        out->append(tok);
    }

    const QString& m_source;
    int m_pos = 0;
};

// ------------------------------------------------------------------ 点链 ----

// 一条点链：标识符后跟若干「. 标识符」。
struct Chain {
    int first = -1;
    int last = -1;
    bool valid = false;
};

Chain chainAt(const QVector<Tok>& toks, int index) {
    Chain chain;
    if (index < 0 || index >= toks.size() || toks.at(index).type != TokType::Ident) return chain;
    int last = index;
    while (last + 2 < toks.size() && toks.at(last + 1).type == TokType::Punct &&
           toks.at(last + 1).text == QLatin1String(".") && toks.at(last + 2).type == TokType::Ident) {
        last += 2;
    }
    chain.first = index;
    chain.last = last;
    chain.valid = true;
    return chain;
}

// 从 index 往回找出的那条点链（以 index 结尾）。
//
// 为什么需要它：赋值左边是 xfa.form.form1.Field.value，
// 从 '=' 往前看第一个记号只是 "value"，整条链要从头找。
Chain chainEndingAt(const QVector<Tok>& toks, int index) {
    Chain chain;
    if (index < 0 || index >= toks.size() || toks.at(index).type != TokType::Ident) return chain;
    int first = index;
    while (first - 2 >= 0 && toks.at(first - 1).type == TokType::Punct &&
           toks.at(first - 1).text == QLatin1String(".") && toks.at(first - 2).type == TokType::Ident) {
        first -= 2;
    }
    chain.first = first;
    chain.last = index;
    chain.valid = true;
    return chain;
}

QString chainText(const QVector<Tok>& toks, const Chain& chain) {
    QStringList parts;
    for (int i = chain.first; i <= chain.last; i += 2) parts.append(toks.at(i).text);
    return parts.join(QLatin1Char('.'));
}

// 从 xfa.form.<子表单>.<字段>[.value] 这样的路径里取出字段名。
//
// 为什么只认这一种形状：XFA 的 SOM 还有一堆写法（xfa.host、xfa.record、深层子表单、
// .ui 之类），本程序只支持「读写某个字段的值」这一种；
// 认不出来的形状留给求值器报错，不在这里猜。
bool fieldFromSomChain(const QVector<Tok>& toks, const Chain& chain, QString* field) {
    QStringList segments;
    for (int i = chain.first; i <= chain.last; i += 2) segments.append(toks.at(i).text);
    if (segments.size() < 4) return false;
    if (segments.at(0) != QLatin1String("xfa") || segments.at(1) != QLatin1String("form")) return false;
    if (segments.size() == 4) {
        *field = segments.at(3);
        return true;
    }
    const QString last = segments.last();
    if (last == QLatin1String("value") || last == QLatin1String("rawValue")) {
        *field = segments.at(segments.size() - 2);
        return true;
    }
    return false;
}

// 把文本变成脚本里的字符串字面量。
QString quoteJs(const QString& text) {
    QString out;
    out += QLatin1Char('"');
    for (const QChar c : text) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('"')) {
            out += QLatin1Char('\\');
            out += c;
        } else if (c == QLatin1Char('\n')) {
            out += QLatin1String("\\n");
        } else if (c == QLatin1Char('\r')) {
            out += QLatin1String("\\r");
        } else if (c == QLatin1Char('\t')) {
            out += QLatin1String("\\t");
        } else {
            out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

// -------------------------------------------------------------- 重写上下文 ----

struct RewriteContext {
    // 字段值表（重算过程中会变）。
    QHash<QString, QString> valueByName;
    // 哪些字段是数值字段。
    QHash<QString, bool> numericByName;
    // 脚本归属的字段（this 指向谁）。
    QString owner;
};

// 求值器只认 event.value / this.value 一种赋值目标。
// XFA 脚本写的是 xfa.form.form1.Field.value，所以要翻译。
bool resolveAssignmentTarget(const QVector<Tok>& toks, const Chain& chain, const RewriteContext& ctx,
                             QString* field, QString* error) {
    const QString base = toks.at(chain.first).text;
    if (base == QLatin1String("this")) {
        if (chain.last == chain.first + 2 && toks.at(chain.last).text == QLatin1String("value")) {
            if (ctx.owner.isEmpty()) {
                *error = QStringLiteral("脚本用了不支持的语法：this.value 赋值需要脚本属于某个字段");
                return false;
            }
            *field = ctx.owner;
            return true;
        }
        *error = QStringLiteral("脚本用了不支持的语法：赋值目标 %1").arg(chainText(toks, chain));
        return false;
    }
    if (base == QLatin1String("xfa")) {
        QString name;
        if (!fieldFromSomChain(toks, chain, &name)) {
            *error = QStringLiteral("脚本用了不支持的语法：赋值目标 %1").arg(chainText(toks, chain));
            return false;
        }
        *field = name;
        return true;
    }
    // 裸字段名（Total.value / Total）：不在标准 XFA 里，但认下来没坏处。
    if (chain.last == chain.first) {
        *field = base;
        return true;
    }
    if (chain.last == chain.first + 2 && toks.at(chain.last).text == QLatin1String("value")) {
        *field = base;
        return true;
    }
    *error = QStringLiteral("脚本用了不支持的语法：赋值目标 %1").arg(chainText(toks, chain));
    return false;
}

// 读路径的翻译结果。
struct ReadResolution {
    bool replaced = false;
    QString text;
};

// 把读到的路径翻译成求值器认识的写法。
//
// 数值字段包一层 Number()：求值器里的字段值是字符串，
// 不包的话 "1" + 2 会拼成 "12"，跟 XFA 的 JavaScript 语义对不上。
bool resolveReadChain(const QVector<Tok>& toks, const Chain& chain, const RewriteContext& ctx,
                      ReadResolution* out, QString* error) {
    out->replaced = false;
    const QString base = toks.at(chain.first).text;
    if (base == QLatin1String("xfa")) {
        QString name;
        if (!fieldFromSomChain(toks, chain, &name)) {
            // 形状不认识就原样留着，求值器会报「不支持的属性访问」——
            // 这比这里自己猜一个字段名可靠。
            return true;
        }
        if (!isValidIdentifier(name)) {
            *error = QStringLiteral("脚本用了不支持的语法：字段名「%1」不能用作脚本标识符").arg(name);
            return false;
        }
        out->text = ctx.numericByName.value(name, false) ? QStringLiteral("Number(%1)").arg(name) : name;
        out->replaced = true;
        return true;
    }
    if (base == QLatin1String("this")) {
        if (chain.last == chain.first) {
            out->text = quoteJs(ctx.owner);
            out->replaced = true;
            return true;
        }
        const QString member = toks.at(chain.last).text;
        if (member == QLatin1String("value")) {
            const QString ownerValue = ctx.valueByName.value(ctx.owner);
            if (ctx.numericByName.value(ctx.owner, false) && isValidIdentifier(ctx.owner)) {
                out->text = QStringLiteral("Number(%1)").arg(ctx.owner);
            } else {
                out->text = quoteJs(ownerValue);
            }
            out->replaced = true;
            return true;
        }
        if (member == QLatin1String("name")) {
            out->text = quoteJs(ctx.owner);
            out->replaced = true;
            return true;
        }
        return true;
    }
    return true;
}

// 把改写后的记号拼回脚本源码。
//
// 为什么用空格拼：记号边界已经确定，空白对求值器没有意义，
// 但能避免两个记号粘成一个（var__x 这种）。
QString tokensToCode(const QVector<Tok>& toks) {
    QStringList parts;
    for (const Tok& tok : toks) parts.append(tok.text);
    return parts.join(QLatin1Char(' '));
}

Tok identTok(const QString& text) {
    Tok tok;
    tok.type = TokType::Ident;
    tok.text = text;
    return tok;
}

Tok punctTok(const QString& text) {
    Tok tok;
    tok.type = TokType::Punct;
    tok.text = text;
    return tok;
}

// 把脚本改写成求值器认识的写法。
//
// 目标字段的赋值 → event.value = …（求值器唯一认的赋值目标）；
// 其它字段的赋值 → 丢进一个临时变量（值照样算，但不落到字段上）。
// 为什么要丢掉：求值器只有一个 event.value 槽位，
// 一个脚本给多个字段赋值时只能一个一个接住，接不住的那个先丢掉。
QVector<Tok> rewriteForTarget(const QVector<Tok>& toks, const QString& target, const RewriteContext& ctx,
                               QSet<QString>* reads, QString* error) {
    QVector<Tok> out;
    int discardIndex = 0;
    int i = 0;
    while (i < toks.size()) {
        const Tok& tok = toks.at(i);
        if (tok.type != TokType::Ident) {
            out.append(tok);
            ++i;
            continue;
        }
        const Chain chain = chainAt(toks, i);
        const bool isAssignmentTarget =
            chain.last + 1 < toks.size() && toks.at(chain.last + 1).type == TokType::Punct &&
            toks.at(chain.last + 1).text == QLatin1String("=");

        if (isAssignmentTarget) {
            QString field;
            if (!resolveAssignmentTarget(toks, chain, ctx, &field, error)) return QVector<Tok>();
            if (field == target) {
                out.append(identTok(QStringLiteral("event")));
                out.append(punctTok(QStringLiteral(".")));
                out.append(identTok(QStringLiteral("value")));
            } else {
                out.append(identTok(QStringLiteral("var")));
                out.append(identTok(QStringLiteral("__aipdfDiscard%1").arg(discardIndex++)));
            }
            i = chain.last + 1;  // 「=」留给下一轮正常处理
            continue;
        }

        ReadResolution read;
        if (!resolveReadChain(toks, chain, ctx, &read, error)) return QVector<Tok>();
        if (read.replaced) {
            if (reads != nullptr && chain.first >= 0) {
                QString name;
                if (fieldFromSomChain(toks, chain, &name)) reads->insert(name);
            }
            out.append(identTok(read.text));
            i = chain.last + 1;
            continue;
        }
        out.append(tok);
        ++i;
    }
    return out;
}

// 求值器的错误分两类：语法不支持（脚本写了子集外的东西）与运行失败（除零之类）。
// 两者要分开说，不然用户会以为「换个写法就能跑」。
QString normalizeScriptError(const QString& message) {
    static const QString kSyntaxPrefix = QStringLiteral("不支持的语法：");
    if (message.startsWith(kSyntaxPrefix)) {
        return QStringLiteral("脚本用了不支持的语法：%1").arg(message.mid(kSyntaxPrefix.size()));
    }
    if (message.isEmpty()) return QStringLiteral("脚本执行失败：原因不明");
    return QStringLiteral("脚本执行失败：%1").arg(message);
}

QVector<FormFieldValue> toFormFieldValues(const QHash<QString, QString>& valueByName) {
    QVector<FormFieldValue> values;
    for (auto it = valueByName.constBegin(); it != valueByName.constEnd(); ++it) {
        FormFieldValue field;
        field.name = it.key();
        field.value = it.value();
        values.append(field);
    }
    return values;
}

}  // namespace

XfaScriptRun evaluateXfaScript(const QString& code, const QVector<XfaScriptField>& fields,
                               const QString& ownerField, bool analyzeOnly) {
    XfaScriptRun run;
    if (code.trimmed().isEmpty()) {
        // 空脚本不是错误：字段本来就可以没有脚本。
        run.ok = true;
        return run;
    }

    RewriteContext ctx;
    ctx.owner = ownerField;
    for (const XfaScriptField& field : fields) {
        ctx.valueByName.insert(field.name, field.value);
        ctx.numericByName.insert(field.name, field.numeric);
    }

    QVector<Tok> toks;
    QString error;
    Lexer lexer(code);
    if (!lexer.tokenize(&toks, &error)) {
        run.error = normalizeScriptError(error);
        return run;
    }

    // 找出所有赋值目标（按出现顺序）。
    QStringList targets;
    for (int i = 0; i < toks.size(); ++i) {
        if (toks.at(i).type != TokType::Punct || toks.at(i).text != QLatin1String("=")) continue;
        const int left = i - 1;
        if (left < 0 || toks.at(left).type != TokType::Ident) continue;
        // 赋值左边要的是整条链（xfa.form.form1.Field.value），不是最后那一段。
        const Chain chain = chainEndingAt(toks, left);
        QString field;
        if (!resolveAssignmentTarget(toks, chain, ctx, &field, &error)) {
            run.error = normalizeScriptError(error);
            return run;
        }
        if (!targets.contains(field)) targets.append(field);
    }

    QSet<QString> reads;
    const QVector<Tok> probe = rewriteForTarget(toks, QString(), ctx, &reads, &error);
    if (probe.isEmpty() && !error.isEmpty()) {
        run.error = normalizeScriptError(error);
        return run;
    }
    for (const QString& field : reads) run.reads.append(field);
    run.writes = targets;

    if (analyzeOnly) {
        run.ok = true;
        return run;
    }

    if (targets.isEmpty()) {
        // 没有赋值也要跑一遍：脚本里可能有语法错误，跑一遍才知道。
        const QString rewritten = tokensToCode(probe);
        const FormJsResult result =
            evaluateFormScript(rewritten, toFormFieldValues(ctx.valueByName), ownerField, QString());
        if (!result.ok) {
            run.error = normalizeScriptError(result.error);
            return run;
        }
        run.ok = true;
        if (!result.error.isEmpty()) run.warnings.append(result.error);
        return run;
    }

    // 每个目标字段各跑一遍：求值器一次只能接住一个字段的值。
    // 多跑几轮，让前一个字段的新值带进后一个字段的计算（脚本内部的先后依赖）。
    for (int round = 0; round < kMaxRounds; ++round) {
        bool changed = false;
        for (const QString& target : targets) {
            QSet<QString> targetReads;
            const QVector<Tok> rewritten = rewriteForTarget(toks, target, ctx, &targetReads, &error);
            if (rewritten.isEmpty()) {
                run.error = normalizeScriptError(error);
                return run;
            }
            QString script = tokensToCode(rewritten);
            if (!script.trimmed().endsWith(QLatin1Char(';'))) script += QLatin1String(" ;");
            // 哨兵：再读一次 event.value。
            // 为什么加它：赋值写在 if 分支里、这次没执行时，
            // 读到的就是目标字段原来的值，写回去等于没改，不会把字段清空。
            script += QLatin1String(" event.value ;");
            const FormJsResult result = evaluateFormScript(script, toFormFieldValues(ctx.valueByName),
                                                            ownerField, ctx.valueByName.value(target));
            if (!result.ok) {
                run.error = normalizeScriptError(result.error);
                return run;
            }
            if (!result.error.isEmpty() && !run.warnings.contains(result.error)) {
                run.warnings.append(result.error);
            }
            if (ctx.valueByName.value(target) != result.value) {
                ctx.valueByName.insert(target, result.value);
                changed = true;
            }
        }
        if (!changed) break;
    }

    for (const QString& target : targets) {
        run.assignments.append(qMakePair(target, ctx.valueByName.value(target)));
    }
    run.ok = true;
    return run;
}

namespace {
// 一个待执行的脚本，连同它读了哪些字段、写了哪些字段。
struct XfaScriptPlan {
    XfaScriptUnit unit;
    QStringList reads;
    QStringList writes;
};

// 脚本在报告里怎么称呼（没有名字就用代码开头几字）。
QString scriptLabel(const XfaScriptUnit& unit) {
    if (!unit.name.isEmpty()) return unit.name;
    const QString code = unit.code.trimmed().left(16);
    return code.isEmpty() ? QStringLiteral("未命名脚本") : code;
}

// 按依赖排序：读了别人写的字段的脚本要排在后面。
//
// 为什么必须排序：算 Total 的脚本读了 Amount，算 Amount 的脚本又读了 Base。
// 顺序错了，总数就是拿旧值算出来的——看着有结果，其实不对。
bool sortScriptsByDependency(const QVector<XfaScriptPlan>& plans, QVector<int>* order,
                             QString* error) {
    QVector<bool> done(plans.size(), false);
    int remaining = plans.size();
    while (remaining > 0) {
        int picked = -1;
        for (int i = 0; i < plans.size(); ++i) {
            if (done.at(i)) continue;
            bool ready = true;
            for (int j = 0; j < plans.size(); ++j) {
                if (done.at(j) || j == i) continue;
                for (const QString& written : plans.at(j).writes) {
                    if (plans.at(i).reads.contains(written)) {
                        ready = false;
                        break;
                    }
                }
                if (!ready) break;
            }
            if (ready) {
                picked = i;
                break;
            }
        }
        if (picked < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("脚本之间存在循环依赖，没法按顺序重算。");
            }
            return false;
        }
        done[picked] = true;
        order->append(picked);
        --remaining;
    }
    return true;
}

// 代码里有没有赋值。
//
// 为什么需要判断：<field> 下面的 <script> 可能是事件脚本（初始化、校验），
// 那些不该执行；只有明显在给字段赋值的脚本才当成计算脚本。
bool containsAssignment(const QString& code) {
    bool inString = false;
    char quote = 0;
    for (int i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (inString) {
            if (c == QChar(QLatin1Char(quote))) inString = false;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inString = true;
            quote = c.toLatin1();
            continue;
        }
        if (c != QLatin1Char('=')) continue;
        if (i + 1 < code.size() && code.at(i + 1) == QLatin1Char('=')) {
            ++i;
            continue;
        }
        if (i > 0) {
            const QChar prev = code.at(i - 1);
            if (prev == QLatin1Char('=') || prev == QLatin1Char('!') || prev == QLatin1Char('<') ||
                prev == QLatin1Char('>')) {
                continue;
            }
        }
        return true;
    }
    return false;
}

}  // namespace

// ------------------------------------------------------------ 脚本执行 ----

XfaWriteReport runXfaCalculateScripts(const QByteArray& pdfBytes, QByteArray* outPdf,
                                      QString* error) {
    XfaWriteReport report;
    if (pdfBytes.isEmpty()) {
        report.error = QStringLiteral("输入是空的，没有可执行的 XFA 脚本。");
        if (error != nullptr) *error = report.error;
        return report;
    }

    // 找脚本。
    const XfaScriptListing listing = listXfaScripts(pdfBytes);
    if (!listing.ok) {
        report.error = listing.error;
        if (error != nullptr) *error = report.error;
        return report;
    }

    // 当前字段值：脚本看到的输入。
    const XfaReport fields = inspectXfa(pdfBytes);
    if (fields.status == XfaStatus::Error) {
        report.error = fields.error.isEmpty()
                           ? QStringLiteral("读不出这份 XFA 表单的字段值。")
                           : fields.error;
        if (error != nullptr) *error = report.error;
        return report;
    }
    QVector<XfaScriptField> working;
    for (const XfaField& field : fields.fields) {
        XfaScriptField item;
        item.name = field.name;
        item.value = field.value;
        // 数值字段参与 + 时是加法，文本字段是字符串拼接（XFA 的 JavaScript 语义）。
        item.numeric = field.type == QLatin1String("numberField");
        working.append(item);
    }

    // 挑出要执行的脚本：只有计算脚本。
    //
    // 为什么不执行事件 / 校验脚本：那些要运行时才有意义（初始化、校验规则），
    // 静态算一遍得出的值是假的。遇到它们点名说明，不静默忽略。
    QVector<XfaScriptPlan> plans;
    for (const XfaScriptUnit& unit : listing.scripts) {
        if (unit.role == QLatin1String("calculate")) {
            plans.append(XfaScriptPlan{unit, QStringList(), QStringList()});
            continue;
        }
        if (unit.role == QLatin1String("script") && containsAssignment(unit.code)) {
            // 字段里的脚本：只有明显在给字段赋值的才算计算脚本。
            plans.append(XfaScriptPlan{unit, QStringList(), QStringList()});
            continue;
        }
        report.warnings.append(QStringLiteral("脚本「%1」没有执行：本程序只执行计算脚本。")
                                   .arg(scriptLabel(unit)));
    }
    if (plans.isEmpty()) {
        report.ok = true;
        report.warnings.append(QStringLiteral("这份 XFA 表单里没有可执行的计算脚本。"));
        if (error != nullptr) error->clear();
        return report;
    }

    // 先分析每个脚本读了什么、写了什么，才能排依赖顺序。
    for (int i = 0; i < plans.size(); ++i) {
        const XfaScriptRun analysis =
            evaluateXfaScript(plans.at(i).unit.code, working, plans.at(i).unit.owner, true);
        if (!analysis.ok) {
            report.error = QStringLiteral("脚本「%1」执行失败：%2")
                               .arg(scriptLabel(plans.at(i).unit), analysis.error);
            if (error != nullptr) *error = report.error;
            return report;
        }
        plans[i].reads = analysis.reads;
        plans[i].writes = analysis.writes;
    }

    QVector<int> order;
    if (!sortScriptsByDependency(plans, &order, &report.error)) {
        if (error != nullptr) *error = report.error;
        return report;
    }

    // 按依赖顺序跑脚本。
    QStringList changedOrder;
    QHash<QString, QString> finalValues;
    for (int index : order) {
        const XfaScriptPlan& plan = plans.at(index);
        const XfaScriptRun run = evaluateXfaScript(plan.unit.code, working, plan.unit.owner);
        if (!run.ok) {
            // 脚本失败就不写回：宁可什么都不改，也不要写进半份数据。
            report.error = QStringLiteral("脚本「%1」执行失败：%2")
                               .arg(scriptLabel(plan.unit), run.error);
            if (error != nullptr) *error = report.error;
            return report;
        }
        for (const QString& warning : run.warnings) {
            if (!report.warnings.contains(warning)) report.warnings.append(warning);
        }
        for (const QPair<QString, QString>& assignment : run.assignments) {
            if (!finalValues.contains(assignment.first)) changedOrder.append(assignment.first);
            finalValues.insert(assignment.first, assignment.second);
            for (XfaScriptField& item : working) {
                if (item.name == assignment.first) item.value = assignment.second;
            }
        }
    }

    if (finalValues.isEmpty()) {
        report.ok = true;
        report.warnings.append(QStringLiteral("计算脚本没有给任何字段赋值。"));
        if (error != nullptr) error->clear();
        return report;
    }

    // 把算出来的值写进数据集（写值那条路：逐项校验、逐项给中文原因）。
    QVector<QPair<QString, QString>> computed;
    for (const QString& name : changedOrder) {
        computed.append(qMakePair(name, finalValues.value(name)));
    }
    const XfaWriteReport written = writeXfaFieldValues(pdfBytes, computed, outPdf, error);
    report.updates = written.updates;
    report.ok = written.ok;
    if (!written.error.isEmpty()) report.error = written.error;
    for (const QString& warning : written.warnings) {
        if (!report.warnings.contains(warning)) report.warnings.append(warning);
    }
    return report;
}

}  // namespace aipdf::pdf

