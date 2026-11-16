#pragma once

#include <QByteArray>
#include <QVector>

#include <functional>

namespace aipdf::pdf {

enum class ContentTokenType {
    Whitespace, Comment, Number, Name, LiteralString, HexString,
    ArrayStart, ArrayEnd, DictionaryStart, DictionaryEnd, Operator, InlineImage
};

struct ContentToken {
    ContentTokenType type = ContentTokenType::Operator;
    QByteArray raw;
    int offset = 0;
};

struct ContentParseResult {
    QVector<ContentToken> tokens;
    QString error;
    bool isValid() const { return error.isEmpty(); }
};

class ContentStream final {
public:
    static ContentParseResult tokenize(const QByteArray& bytes);
    static QByteArray serialize(const QVector<ContentToken>& tokens);
    static bool validate(const QVector<ContentToken>& tokens, QString* error = nullptr);
    // Rewrites literal-string operands of Tj/'/" and strings inside TJ arrays.
    // The original stream is returned unchanged when parsing or validation fails.
    static QByteArray rewriteText(
        const QByteArray& bytes,
        const std::function<QString(const QString&)>& rewrite,
        QString* error = nullptr);
    static QByteArray rewritePdfText(const QByteArray& pdfBytes, const QVector<int>& pageIndexes,
                                     const QString& query, const QString& replacement,
                                     Qt::CaseSensitivity sensitivity, int* replacements,
                                     QString* error = nullptr);
};

}  // namespace aipdf::pdf
