#include "pdf/ContentStream.h"

#include <QStringList>

namespace aipdf::pdf {
namespace {

bool white(char c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
bool delimiter(char c) {
    return white(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%';
}
bool numberStart(char c) { return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'; }

QString decodeLiteral(const QByteArray& raw) {
    QByteArray out;
    for (int i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\') { out += c; continue; }
        if (++i + 1 >= raw.size()) break;
        c = raw[i];
        if (c == 'n') out += '\n'; else if (c == 'r') out += '\r';
        else if (c == 't') out += '\t'; else if (c == 'b') out += '\b';
        else if (c == 'f') out += '\f';
        else if (c == '\n') continue;
        else if (c == '\r') { if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i; }
        else if (c >= '0' && c <= '7') {
            int value = c - '0';
            for (int n = 0; n < 2 && i + 1 < raw.size() - 1 && raw[i + 1] >= '0' && raw[i + 1] <= '7'; ++n)
                value = value * 8 + (raw[++i] - '0');
            out += static_cast<char>(value);
        } else out += c;
    }
    return QString::fromLatin1(out);
}

QByteArray encodeLiteral(const QString& text) {
    QByteArray out("(");
    for (char c : text.toLatin1()) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t"; else out += c;
    }
    out += ')';
    return out;
}

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i)
        if (tokens[i].type != ContentTokenType::Whitespace && tokens[i].type != ContentTokenType::Comment) return i;
    return -1;
}

}  // namespace

ContentParseResult ContentStream::tokenize(const QByteArray& bytes) {
    ContentParseResult result;
    int i = 0;
    auto add = [&](ContentTokenType type, int start, int end) {
        result.tokens.append({type, bytes.mid(start, end - start), start});
    };
    while (i < bytes.size()) {
        const int start = i;
        const char c = bytes[i];
        if (white(c)) { while (i < bytes.size() && white(bytes[i])) ++i; add(ContentTokenType::Whitespace, start, i); continue; }
        if (c == '%') { while (i < bytes.size() && bytes[i] != '\r' && bytes[i] != '\n') ++i; add(ContentTokenType::Comment, start, i); continue; }
        if (c == '(') {
            int depth = 1; ++i;
            while (i < bytes.size() && depth > 0) {
                if (bytes[i] == '\\') { ++i; if (i < bytes.size() && bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i; ++i; continue; }
                if (bytes[i] == '(') ++depth; else if (bytes[i] == ')') --depth;
                ++i;
            }
            if (depth != 0) { result.error = QStringLiteral("Unterminated literal string at %1").arg(start); return result; }
            add(ContentTokenType::LiteralString, start, i); continue;
        }
        if (c == '<') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '<') { i += 2; add(ContentTokenType::DictionaryStart, start, i); }
            else { ++i; while (i < bytes.size() && bytes[i] != '>') ++i; if (i == bytes.size()) { result.error = QStringLiteral("Unterminated hex string at %1").arg(start); return result; } ++i; add(ContentTokenType::HexString, start, i); }
            continue;
        }
        if (c == '>' && i + 1 < bytes.size() && bytes[i + 1] == '>') { i += 2; add(ContentTokenType::DictionaryEnd, start, i); continue; }
        if (c == '[') { ++i; add(ContentTokenType::ArrayStart, start, i); continue; }
        if (c == ']') { ++i; add(ContentTokenType::ArrayEnd, start, i); continue; }
        if (c == '/') { ++i; while (i < bytes.size() && !delimiter(bytes[i])) ++i; add(ContentTokenType::Name, start, i); continue; }
        while (i < bytes.size() && !delimiter(bytes[i])) ++i;
        if (i == start) { ++i; result.error = QStringLiteral("Unexpected delimiter at %1").arg(start); return result; }
        const QByteArray word = bytes.mid(start, i - start);
        if (word == "BI") {
            // Inline-image data is arbitrary binary and may contain parentheses, brackets or
            // operator-looking bytes. Keep the entire BI ... ID ... EI construct opaque.
            int id = i;
            while (id + 2 < bytes.size()) {
                if (delimiter(bytes[id]) && bytes.mid(id + 1, 2) == "ID" &&
                    id + 3 < bytes.size() && white(bytes[id + 3])) break;
                ++id;
            }
            if (id + 3 >= bytes.size()) { result.error = QStringLiteral("Inline image has no ID at %1").arg(start); return result; }
            int end = id + 4;
            bool found = false;
            while (end + 2 < bytes.size()) {
                if (white(bytes[end]) && bytes.mid(end + 1, 2) == "EI" &&
                    (end + 3 == bytes.size() || delimiter(bytes[end + 3]))) {
                    end += 3;
                    found = true;
                    break;
                }
                ++end;
            }
            if (!found) { result.error = QStringLiteral("Inline image has no EI at %1").arg(start); return result; }
            add(ContentTokenType::InlineImage, start, end);
            i = end;
            continue;
        }
        bool numeric = false;
        if (numberStart(c)) word.toDouble(&numeric);
        add(numeric ? ContentTokenType::Number : ContentTokenType::Operator, start, i);
    }
    validate(result.tokens, &result.error);
    return result;
}

QByteArray ContentStream::serialize(const QVector<ContentToken>& tokens) {
    QByteArray result;
    for (const ContentToken& token : tokens) result += token.raw;
    return result;
}

bool ContentStream::validate(const QVector<ContentToken>& tokens, QString* error) {
    QVector<ContentTokenType> stack;
    int textDepth = 0, graphicsDepth = 0;
    for (const ContentToken& token : tokens) {
        if (token.type == ContentTokenType::ArrayStart || token.type == ContentTokenType::DictionaryStart) stack.append(token.type);
        else if (token.type == ContentTokenType::ArrayEnd) {
            if (stack.isEmpty() || stack.takeLast() != ContentTokenType::ArrayStart) { if (error) *error = QStringLiteral("Unbalanced array at %1").arg(token.offset); return false; }
        } else if (token.type == ContentTokenType::DictionaryEnd) {
            if (stack.isEmpty() || stack.takeLast() != ContentTokenType::DictionaryStart) { if (error) *error = QStringLiteral("Unbalanced dictionary at %1").arg(token.offset); return false; }
        } else if (token.type == ContentTokenType::Operator) {
            if (token.raw == "BT") ++textDepth; else if (token.raw == "ET") --textDepth;
            else if (token.raw == "q") ++graphicsDepth; else if (token.raw == "Q") --graphicsDepth;
            if (textDepth < 0 || graphicsDepth < 0) { if (error) *error = QStringLiteral("Unbalanced graphics/text state at %1").arg(token.offset); return false; }
        }
    }
    if (!stack.isEmpty() || textDepth != 0 || graphicsDepth != 0) { if (error) *error = QStringLiteral("Unbalanced content stream"); return false; }
    if (error) error->clear();
    return true;
}

QByteArray ContentStream::rewriteText(const QByteArray& bytes, const std::function<QString(const QString&)>& rewrite, QString* error) {
    ContentParseResult parsed = tokenize(bytes);
    if (!parsed.isValid()) { if (error) *error = parsed.error; return bytes; }
    for (int i = 0; i < parsed.tokens.size(); ++i) {
        if (parsed.tokens[i].type != ContentTokenType::Operator) continue;
        const QByteArray op = parsed.tokens[i].raw;
        int previous = previousSignificant(parsed.tokens, i);
        if ((op == "Tj" || op == "'" || op == "\"") && previous >= 0 && parsed.tokens[previous].type == ContentTokenType::LiteralString) {
            parsed.tokens[previous].raw = encodeLiteral(rewrite(decodeLiteral(parsed.tokens[previous].raw)));
        } else if (op == "TJ" && previous >= 0 && parsed.tokens[previous].type == ContentTokenType::ArrayEnd) {
            int depth = 0;
            for (int j = previous; j >= 0; --j) {
                if (parsed.tokens[j].type == ContentTokenType::ArrayEnd) ++depth;
                else if (parsed.tokens[j].type == ContentTokenType::ArrayStart && --depth == 0) {
                    for (int k = j + 1; k < previous; ++k)
                        if (parsed.tokens[k].type == ContentTokenType::LiteralString)
                            parsed.tokens[k].raw = encodeLiteral(rewrite(decodeLiteral(parsed.tokens[k].raw)));
                    break;
                }
            }
        }
    }
    if (!validate(parsed.tokens, error)) return bytes;
    return serialize(parsed.tokens);
}

}  // namespace aipdf::pdf
