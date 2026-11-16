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
    // 按页改写内容流里的文字，改写方式完全交给回调决定。
    //
    // 为什么要有这个重载：rewritePdfText 只会「找 query 换 replacement」，
    // 全字匹配、正则、捕获组引用这些语义它都表达不了；
    // 与其给同一个函数塞一串布尔开关，不如把「每个文本操作数交给调用方处理」
    // 这件事交出去。回调返回什么就写回什么，原样返回表示这一处不改。
    //
    // pageIndexes 为空表示整篇文档（rewritePdfText 的空列表表示「哪页都不改」，
    // 两者语义不同，这里不复用它的行为）。
    // replacements 统计被改动的文本操作数个数——一个操作数里改了几处由回调自己知道，
    // 这里数的是操作数。
    static QByteArray rewritePdfTextWith(const QByteArray& pdfBytes, const QVector<int>& pageIndexes,
                                          const std::function<QString(const QString&)>& rewrite,
                                          int* replacements, QString* error = nullptr);
};

}  // namespace aipdf::pdf
