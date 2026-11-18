#include "ai/rag/OfflineQa.h"

#include "ai/rag/LocalHashEmbedding.h"
#include "doc/index/CitationVerifier.h"
#include "doc/index/HybridRetriever.h"
#include "doc/index/LocalIndex.h"
#include "doc/index/VectorIndex.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace aipdf::ai {

namespace {

// 一句话，以及它在片段文本里的起始位置。
struct Sentence {
    QString text;
    int offset = 0;
};

// 中文全角句末标点。
//
// 为什么不能写 QLatin1Char('。')：QLatin1Char 的参数是 char（单字节），
// 而 '。' 是多字符字面量（UTF-8 源码里是三个字节），传进去会被截断成
// 0x82，比较结果永远是假 —— 中文文档整段会被当成一句话，引用粒度全错。
// 要按码点比。
bool isFullWidthSentencePunctuation(QChar ch) {
    switch (ch.unicode()) {
    case 0x3002:  // 。
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0xFF1B:  // ；
    case 0x2026:  // …
        return true;
    default:
        return false;
    }
}

// 切句：按中英文句末标点与换行切。
//
// 为什么英文的 "." 要看后面跟不跟空白：「3.14」里的点不是句号，
// 按标点一刀切会把小数点切一半。
QVector<Sentence> splitSentences(const QString& text) {
    QVector<Sentence> sentences;
    int start = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        bool boundary = false;
        if (ch == QLatin1Char('\n')) {
            boundary = true;
        } else if (isFullWidthSentencePunctuation(ch)) {
            boundary = true;
        } else if (ch == QLatin1Char('.') || ch == QLatin1Char('!') ||
                   ch == QLatin1Char('?') || ch == QLatin1Char(';')) {
            boundary = (index + 1 >= text.size()) || text.at(index + 1).isSpace();
        }
        if (!boundary) {
            continue;
        }
        int offset = start;
        while (offset < index && text.at(offset).isSpace()) {
            ++offset;
        }
        const QString piece = text.mid(offset, index - offset + 1).trimmed();
        if (!piece.isEmpty()) {
            Sentence sentence;
            sentence.text = piece;
            sentence.offset = offset;
            sentences.append(sentence);
        }
        start = index + 1;
    }
    int offset = start;
    while (offset < text.size() && text.at(offset).isSpace()) {
        ++offset;
    }
    const QString tail = text.mid(offset).trimmed();
    if (!tail.isEmpty()) {
        Sentence sentence;
        sentence.text = tail;
        sentence.offset = offset;
        sentences.append(sentence);
    }
    return sentences;
}

// 比较用的归一化文本：去空白、忽略大小写与全半角。
QString normalizedForCompare(const QString& text) {
    QString result;
    for (const QChar ch : LocalHashEmbedding::normalize(text)) {
        if (!ch.isSpace()) {
            result.append(ch);
        }
    }
    return result;
}

// 余弦相似度（两边都是 L2 归一化的向量，点积就是余弦）。
double cosineOf(const EmbeddingVector& left, const EmbeddingVector& right) {
    if (left.isEmpty() || right.isEmpty() || left.size() != right.size()) {
        return 0.0;
    }
    double dot = 0.0;
    for (int index = 0; index < left.size(); ++index) {
        dot += static_cast<double>(left.at(index)) * static_cast<double>(right.at(index));
    }
    return dot;
}

// 一条候选句子。
struct Candidate {
    Sentence sentence;
    int chunkIndex = 0;
    int chunkOrder = 0;
    int chunkPage = 0;
    double score = 0.0;
};

// 引用清单的一行（用仓库自带的引用核验器认得的格式）。
//
// 格式定死成 [n] 第 N 页 | 引文：xxx，是因为 doc::CitationVerifier 就认这个格式：
// 本模块产出的答案可以直接拿去过一遍仓库自己的核验流程，
// 不用再写一套「本模块专用」的解析。
QString citationLine(int number, int pageIndex, const QString& quote) {
    return QStringLiteral("[%1] 第 %2 页 | 引文：%3").arg(number).arg(pageIndex + 1).arg(quote);
}

}  // namespace

// ============================================================ 公开接口 ======

OfflineQa::OfflineQa(const doc::LocalIndex* index, const doc::VectorIndex* vectors)
    : index_(index), vectors_(vectors) {}

OfflineAnswer OfflineQa::answer(const QString& question, const OfflineQaOptions& options) const {
    QVector<doc::DocumentChunk> candidates;

    if (index_ != nullptr && !index_->isEmpty()) {
        // 混合检索：关键词那半边按词元命中数排名，向量那半边按余弦排名，
        // 两路用 RRF 融合（见 doc::HybridRetriever）。没有向量索引时只走关键词。
        const LocalHashEmbedding embedder;
        const doc::HybridRetriever retriever(index_, vectors_, &embedder);
        doc::HybridOptions hybridOptions;
        hybridOptions.limit = options.candidateChunks;
        const QVector<doc::HybridHit> hits = retriever.search(question, hybridOptions);
        for (const doc::HybridHit& hit : hits) {
            doc::DocumentChunk chunk;
            // 按「页 + 块 id」取块：块 id 里没有页号，只按 id 全局取会拿错页。
            if (index_->chunkByIdOnPage(hit.pageIndex, hit.chunkId, &chunk)) {
                candidates.append(chunk);
            }
        }
        if (candidates.isEmpty()) {
            // 检索一个都没命中：仍然要给用户「最接近的几个片段」，
            // 否则「没找到」就变成了「什么线索都没有」，用户无从判断。
            candidates = index_->chunks();
        }
    }
    return answerFromChunks(candidates, question, options);
}

OfflineAnswer OfflineQa::answerFromChunks(const QVector<doc::DocumentChunk>& chunks,
                                           const QString& question,
                                           const OfflineQaOptions& options) {
    OfflineAnswer answer;
    answer.question = question;

    const QString trimmed = question.trimmed();
    if (trimmed.isEmpty()) {
        answer.notice = QStringLiteral("问题为空：没有给出要查的内容。");
        return answer;
    }
    if (chunks.isEmpty()) {
        answer.notice = QStringLiteral("本地索引里没有找到相关内容：本地索引是空的。");
        return answer;
    }

    // 范围限制：只在这一页里找。
    QVector<doc::DocumentChunk> scoped;
    scoped.reserve(chunks.size());
    for (const doc::DocumentChunk& chunk : chunks) {
        if (!options.pageScope.isEmpty() && !options.pageScope.contains(chunk.pageIndex)) {
            continue;
        }
        scoped.append(chunk);
    }
    if (scoped.isEmpty()) {
        answer.notice = QStringLiteral("本地索引里没有找到相关内容：指定的页范围内没有任何片段。");
        return answer;
    }

    // 切句。
    QVector<Candidate> candidates;
    QStringList sentenceTexts;
    for (int chunkIndex = 0; chunkIndex < scoped.size(); ++chunkIndex) {
        const doc::DocumentChunk& chunk = scoped.at(chunkIndex);
        const QVector<Sentence> sentences = splitSentences(chunk.text);
        for (const Sentence& sentence : sentences) {
            Candidate candidate;
            candidate.sentence = sentence;
            candidate.chunkIndex = chunkIndex;
            candidate.chunkOrder = chunk.order;
            candidate.chunkPage = chunk.pageIndex;
            candidates.append(candidate);
            sentenceTexts.append(sentence.text);
        }
    }
    if (candidates.isEmpty()) {
        answer.notice = QStringLiteral("本地索引里没有找到相关内容：这些片段里没有可引用的句子。");
        return answer;
    }

    // 打分：本地哈希嵌入的余弦相似度（不联网、不调模型）。
    const LocalHashEmbedding embedder;
    const EmbeddingVector questionVector = embedder.embedOne(trimmed);
    const QVector<EmbeddingVector> sentenceVectors = embedder.embed(sentenceTexts);
    for (int index = 0; index < candidates.size() && index < sentenceVectors.size(); ++index) {
        candidates[index].score = cosineOf(questionVector, sentenceVectors.at(index));
    }

    // 排序：相似度降序；同分按片段顺序与句在片段里的位置。
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& left, const Candidate& right) {
                         if (left.score != right.score) {
                             return left.score > right.score;
                         }
                         if (left.chunkOrder != right.chunkOrder) {
                             return left.chunkOrder < right.chunkOrder;
                         }
                         if (left.chunkPage != right.chunkPage) {
                             return left.chunkPage < right.chunkPage;
                         }
                         return left.sentence.offset < right.sentence.offset;
                     });

    // 门槛：相似度够 **且** 与问题共享一个词元（长度 ≥ 2）。
    //
    // 为什么要「共享词元」这一条：哈希嵌入的维度不高（默认 256），
    // 两段毫不相干的文本也可能因为哈希撞桶而算出一点余弦。
    // 词元重合是「确实在讲同一件事」的独立证据。
    const QSet<QString> questionTokens = [&trimmed]() {
        QSet<QString> tokens;
        const QStringList list = LocalHashEmbedding::tokenize(trimmed);
        for (const QString& token : list) {
            if (token.size() >= 2) {
                tokens.insert(token);
            }
        }
        return tokens;
    }();

    QVector<Candidate> selected;
    QSet<QString> seenSentences;
    for (const Candidate& candidate : candidates) {
        if (selected.size() >= options.maxSentences) {
            break;
        }
        if (candidate.score < options.minSentenceScore ||
            candidate.score < options.minChunkScore) {
            // 已经按分数降序排过了，后面的只会更低。
            break;
        }
        bool shared = false;
        const QStringList tokens = LocalHashEmbedding::tokenize(candidate.sentence.text);
        for (const QString& token : tokens) {
            if (token.size() >= 2 && questionTokens.contains(token)) {
                shared = true;
                break;
            }
        }
        if (!shared) {
            continue;
        }
        const QString key = normalizedForCompare(candidate.sentence.text);
        if (key.isEmpty() || seenSentences.contains(key)) {
            continue;
        }
        seenSentences.insert(key);
        selected.append(candidate);
    }

    // 片段级最高分（用于「最接近的片段」清单）。
    QVector<int> bestCandidate(scoped.size(), -1);
    for (int index = 0; index < candidates.size(); ++index) {
        const int chunkIndex = candidates.at(index).chunkIndex;
        if (bestCandidate.at(chunkIndex) < 0) {
            bestCandidate[chunkIndex] = index;
        }
    }

    // 「最接近的片段」清单。
    //
    // applyThreshold=false 时不设门槛：没找到的时候这几条就是给用户判断的线索，
    // 哪怕相似度接近 0 也得列出来——「什么线索都没有」比「没找到」还没用。
    const auto buildClosest = [&](int limit, bool skipCited, bool applyThreshold) {
        QVector<OfflineCitation> closest;
        QVector<int> chunkOrder;
        for (int chunkIndex = 0; chunkIndex < scoped.size(); ++chunkIndex) {
            const int candidateIndex = bestCandidate.at(chunkIndex);
            if (candidateIndex < 0) {
                continue;
            }
            const Candidate& candidate = candidates.at(candidateIndex);
            if (applyThreshold && candidate.score < options.minChunkScore) {
                continue;
            }
            if (skipCited) {
                bool cited = false;
                for (const Candidate& chosen : selected) {
                    if (chosen.chunkIndex == chunkIndex) {
                        cited = true;
                        break;
                    }
                }
                if (cited) {
                    continue;
                }
            }
            chunkOrder.append(chunkIndex);
        }
        // 按片段级分数从高到低。
        std::stable_sort(chunkOrder.begin(), chunkOrder.end(),
                         [&candidates, &bestCandidate](int left, int right) {
                             const double leftScore =
                                 candidates.at(bestCandidate.at(left)).score;
                             const double rightScore =
                                 candidates.at(bestCandidate.at(right)).score;
                             return leftScore > rightScore;
                         });
        for (int index = 0; index < chunkOrder.size() && index < limit; ++index) {
            const int chunkIndex = chunkOrder.at(index);
            const Candidate& candidate = candidates.at(bestCandidate.at(chunkIndex));
            const doc::DocumentChunk& chunk = scoped.at(chunkIndex);
            OfflineCitation citation;
            citation.number = 0;  // 「最接近」不是答案里的引用，不编号。
            citation.chunkId = chunk.id;
            citation.pageIndex = chunk.pageIndex;
            citation.quote = candidate.sentence.text;
            citation.charOffset = candidate.sentence.offset;
            citation.score = candidate.score;
            citation.sectionPath = chunk.sectionPath;
            closest.append(citation);
        }
        return closest;
    };

    if (selected.isEmpty()) {
        // 没找到：**不返回任何答案正文**，只说没找到，并列出最接近的片段。
        answer.status = OfflineAnswerStatus::NoMatch;
        answer.text.clear();
        answer.notice = QStringLiteral("本地索引里没有找到相关内容。");
        answer.closest = buildClosest(options.maxClosest, false, false);
        return answer;
    }

    // 组装答案：正文（句子 + [n]）与引用清单。
    QStringList bodyLines;
    QStringList citationLines;
    QVector<doc::CitationBlock> blocks;
    for (const Candidate& candidate : selected) {
        const doc::DocumentChunk& chunk = scoped.at(candidate.chunkIndex);
        // 引用必须核得过：引文要在原文里逐字找得到（用仓库自带的核验规则）。
        if (!doc::CitationVerifier::quoteAppearsIn(candidate.sentence.text, chunk.text)) {
            continue;
        }
        const int number = answer.citations.size() + 1;
        bodyLines.append(candidate.sentence.text + QStringLiteral(" [%1]").arg(number));
        citationLines.append(citationLine(number, chunk.pageIndex, candidate.sentence.text));

        doc::CitationBlock block;
        block.number = number;
        block.pageIndex = chunk.pageIndex;
        block.text = chunk.text;
        blocks.append(block);

        OfflineCitation citation;
        citation.number = number;
        citation.chunkId = chunk.id;
        citation.pageIndex = chunk.pageIndex;
        citation.quote = candidate.sentence.text;
        citation.charOffset = candidate.sentence.offset;
        citation.score = candidate.score;
        citation.sectionPath = chunk.sectionPath;
        answer.citations.append(citation);
    }

    if (answer.citations.isEmpty()) {
        // 句子选出来了却一条都核不过：宁可说没找到，也不给一条没法核对的引用。
        answer.status = OfflineAnswerStatus::NoMatch;
        answer.text.clear();
        answer.notice =
            QStringLiteral("本地索引里没有找到相关内容：候选句子的原文没能核对上。");
        answer.closest = buildClosest(options.maxClosest, false, false);
        return answer;
    }

    answer.status = OfflineAnswerStatus::Found;
    answer.text = bodyLines.join(QLatin1Char('\n')) + QStringLiteral("\n\n引用：\n") +
                  citationLines.join(QLatin1Char('\n'));

    // 自检：拿仓库自带的核验器再核一遍，核不过的条数如实报出去。
    const doc::CitationReport report = doc::CitationVerifier::verify(answer.text, blocks);
    answer.verifiedCitationCount = report.validCount;
    if (report.invalidCount > 0) {
        answer.notice = QStringLiteral("有 %1 条引用没能核对上，请自行核对原文。")
                            .arg(report.invalidCount);
    }
    if (options.includeClosestWhenFound) {
        answer.closest = buildClosest(options.maxClosest, true, true);
    }
    return answer;
}

}  // namespace aipdf::ai
