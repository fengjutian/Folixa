#include "ai/LocalHashEmbedding.h"

#include <QHash>
#include <QStringList>

#include <cmath>

namespace aipdf::ai {

namespace {

// 是不是「CJK 汉字」或「假名 / 谚文」这类没有空格的文字。
bool isWideScriptChar(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) ||   // CJK 统一表意文字
           (code >= 0x3400 && code <= 0x4DBF) ||   // CJK 扩展 A
           (code >= 0x3040 && code <= 0x30FF) ||   // 平假名 / 片假名
           (code >= 0xAC00 && code <= 0xD7AF);     // 谚文音节
}

// 是不是「拉丁字母 / 数字」。
bool isLatinWordChar(QChar ch) {
    return ch.isLetterOrNumber() && !isWideScriptChar(ch);
}

}  // namespace

LocalHashEmbedding::LocalHashEmbedding(int dimension)
    : dimension_(dimension > 0 ? dimension : kDefaultDimension) {}

QString LocalHashEmbedding::id() const {
    return QStringLiteral("local-hash");
}

int LocalHashEmbedding::dimension() const {
    return dimension_;
}

QString LocalHashEmbedding::normalize(const QString& text) {
    QString result;
    result.reserve(text.size());
    bool pendingSpace = false;
    for (QChar ch : text) {
        // 全角转半角：NFKC 的常见子集。中文文档里全角标点很常见，
        // 不转的话「，」和「,」会被当成两个不同的东西。
        if (ch.unicode() == 0x3000) {
            ch = QLatin1Char(' ');
        } else if (ch.unicode() >= 0xFF01 && ch.unicode() <= 0xFF5E) {
            ch = QChar(ch.unicode() - 0xFEE0);
        }
        if (ch.isSpace()) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !result.isEmpty()) {
            result.append(QLatin1Char(' '));
        }
        pendingSpace = false;
        result.append(ch.toLower());
    }
    return result;
}

QStringList LocalHashEmbedding::tokenize(const QString& text) {
    const QString normalized = normalize(text);
    QStringList tokens;
    int index = 0;
    const int length = normalized.size();

    while (index < length) {
        const QChar ch = normalized.at(index);
        if (isWideScriptChar(ch)) {
            // 中文：单字 + 与后一个字组成的二字组。
            //
            // 「只取单字」太短，「自动保存」与「自动保存文档」就分不出；
            // 「只取二字组」在只有一个字的地方会丢内容，所以两个都要。
            tokens.append(ch);
            if (index + 1 < length && isWideScriptChar(normalized.at(index + 1))) {
                tokens.append(normalized.mid(index, 2));
            }
            ++index;
            continue;
        }
        if (isLatinWordChar(ch)) {
            const int start = index;
            while (index < length && isLatinWordChar(normalized.at(index))) {
                ++index;
            }
            tokens.append(normalized.mid(start, index - start));
            continue;
        }
        // 标点与符号：跳过。它们对检索没有帮助，却会把向量算歪。
        ++index;
    }
    return tokens;
}

QVector<EmbeddingVector> LocalHashEmbedding::embed(const QStringList& texts) const {
    QVector<EmbeddingVector> result;
    result.reserve(texts.size());

    for (const QString& text : texts) {
        const QStringList tokens = tokenize(text);
        EmbeddingVector vector(dimension_, 0.0f);

        // 先数词频，再按次线性权重累加：
        // 1 + ln(tf) 让高频词压下去一点，罕见词不至于被淹没。
        QHash<QString, int> frequencies;
        for (const QString& token : tokens) {
            frequencies[token] += 1;
        }
        for (auto it = frequencies.constBegin(); it != frequencies.constEnd(); ++it) {
            const int bucket =
                static_cast<int>(qHash(it.key()) % static_cast<uint>(dimension_));
            vector[bucket] += static_cast<float>(1.0 + std::log(static_cast<double>(it.value())));
        }

        // L2 归一化：余弦相似度就退化成点积，比较起来快得多。
        double squaredNorm = 0.0;
        for (float value : vector) {
            squaredNorm += static_cast<double>(value) * static_cast<double>(value);
        }
        if (squaredNorm > 0.0) {
            const float norm = static_cast<float>(std::sqrt(squaredNorm));
            for (float& value : vector) {
                value /= norm;
            }
        }
        result.append(vector);
    }
    return result;
}

}  // namespace aipdf::ai
