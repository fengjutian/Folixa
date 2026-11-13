#include "doc/VectorIndex.h"

#include "doc/LocalIndex.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aipdf::doc {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyDimension = "dimension";
constexpr auto kKeyProvider = "provider";
constexpr auto kKeyDocument = "document";
constexpr auto kKeyVectors = "vectors";
constexpr auto kKeyPages = "pages";
constexpr int kFormatVersion = 1;

// 向量按**小端 float32**存，不看机器的字节序。
//
// 索引文件是要长期保存的：今天写的文件，半年后换台机器读，
// 字节序不一致就会读出一堆垃圾向量，而且症状是「检索结果莫名其妙」，
// 极难查。所以这里手工拆字节，不直接 memcpy 原始内存。
QByteArray packFloats(const ai::EmbeddingVector& vector) {
    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(vector.size()) * 4);
    auto* out = reinterpret_cast<uchar*>(bytes.data());
    for (qsizetype index = 0; index < vector.size(); ++index) {
        const float value = vector.at(index);
        quint32 bits = 0;
        std::memcpy(&bits, &value, 4);
        out[index * 4 + 0] = static_cast<uchar>(bits & 0xFFu);
        out[index * 4 + 1] = static_cast<uchar>((bits >> 8) & 0xFFu);
        out[index * 4 + 2] = static_cast<uchar>((bits >> 16) & 0xFFu);
        out[index * 4 + 3] = static_cast<uchar>((bits >> 24) & 0xFFu);
    }
    return bytes;
}

ai::EmbeddingVector unpackFloats(const QByteArray& bytes) {
    ai::EmbeddingVector vector;
    const qsizetype count = bytes.size() / 4;
    vector.reserve(count);
    const auto* in = reinterpret_cast<const uchar*>(bytes.constData());
    for (qsizetype index = 0; index < count; ++index) {
        const quint32 bits = static_cast<quint32>(in[index * 4 + 0]) |
                             (static_cast<quint32>(in[index * 4 + 1]) << 8) |
                             (static_cast<quint32>(in[index * 4 + 2]) << 16) |
                             (static_cast<quint32>(in[index * 4 + 3]) << 24);
        float value = 0.0f;
        std::memcpy(&value, &bits, 4);
        vector.append(value);
    }
    return vector;
}

}  // namespace

QString VectorIndex::filePathFor(const QString& documentPath) {
    if (documentPath.isEmpty()) {
        return QString();
    }
    const QString absolute = QFileInfo(documentPath).absoluteFilePath();
    const QByteArray hash =
        QCryptographicHash::hash(absolute.toUtf8(), QCryptographicHash::Sha1);
    // 与 LocalIndex 的目录分开：两个文件的重建节奏不一样。
    const QString directory =
        QDir(LocalIndex::directoryPath()).filePath(QStringLiteral("vectors"));
    return QDir(directory).filePath(QString::fromLatin1(hash.toHex()) +
                                    QStringLiteral(".json"));
}

bool VectorIndex::load(const QString& documentPath) {
    clear();
    const QString path = filePathFor(documentPath);
    if (path.isEmpty()) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QString::fromLatin1(kKeyVersion)).toInt() != kFormatVersion) {
        return false;
    }
    documentPath_ = root.value(QString::fromLatin1(kKeyDocument)).toString();
    providerId_ = root.value(QString::fromLatin1(kKeyProvider)).toString();
    dimension_ = root.value(QString::fromLatin1(kKeyDimension)).toInt();
    if (dimension_ <= 0) {
        return false;
    }

    const QJsonObject entries = root.value(QString::fromLatin1(kKeyVectors)).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QByteArray decoded =
            QByteArray::fromBase64(it.value().toString().toLatin1());
        ai::EmbeddingVector vector = unpackFloats(decoded);
        if (vector.size() != dimension_) {
            // 维度对不上（换了嵌入模型）：这一条不能用。
            continue;
        }
        vectors_.insert(it.key(), vector);
    }
    const QJsonObject pages = root.value(QString::fromLatin1(kKeyPages)).toObject();
    for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
        chunkPages_.insert(it.key(), it.value().toInt());
    }
    return true;
}

bool VectorIndex::save() const {
    const QString path = filePathFor(documentPath_);
    if (path.isEmpty()) {
        return false;
    }
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        return false;
    }

    QJsonObject entries;
    for (auto it = vectors_.constBegin(); it != vectors_.constEnd(); ++it) {
        entries.insert(it.key(),
                       QString::fromLatin1(packFloats(it.value()).toBase64()));
    }
    QJsonObject pages;
    for (auto it = chunkPages_.constBegin(); it != chunkPages_.constEnd(); ++it) {
        pages.insert(it.key(), it.value());
    }
    QJsonObject root;
    root.insert(QString::fromLatin1(kKeyVersion), kFormatVersion);
    root.insert(QString::fromLatin1(kKeyDimension), dimension_);
    root.insert(QString::fromLatin1(kKeyProvider), providerId_);
    root.insert(QString::fromLatin1(kKeyDocument), documentPath_);
    root.insert(QString::fromLatin1(kKeyVectors), entries);
    root.insert(QString::fromLatin1(kKeyPages), pages);

    // 先写临时文件再替换：写到一半崩了，旧的索引还在。
    const QString temporaryPath = path + QStringLiteral(".tmp");
    QFile file(temporaryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        file.close();
        QFile::remove(temporaryPath);
        return false;
    }
    file.close();
    if (!QFile::remove(path) && QFile::exists(path)) {
        QFile::remove(temporaryPath);
        return false;
    }
    return QFile::rename(temporaryPath, path);
}

void VectorIndex::clear() {
    documentPath_.clear();
    providerId_.clear();
    dimension_ = 0;
    vectors_.clear();
    chunkPages_.clear();
}

QString VectorIndex::documentPath() const {
    return documentPath_;
}

void VectorIndex::setDocumentPath(const QString& documentPath) {
    documentPath_ = documentPath;
}

int VectorIndex::dimension() const {
    return dimension_;
}

QString VectorIndex::providerId() const {
    return providerId_;
}

void VectorIndex::setProviderId(const QString& id) {
    providerId_ = id;
}

int VectorIndex::vectorCount() const {
    return static_cast<int>(vectors_.size());
}

bool VectorIndex::isEmpty() const {
    return vectors_.isEmpty();
}

void VectorIndex::put(const QString& chunkId, const ai::EmbeddingVector& vector, int pageIndex) {
    if (chunkId.isEmpty() || vector.isEmpty()) {
        return;
    }
    if (dimension_ == 0) {
        dimension_ = static_cast<int>(vector.size());
    } else if (vector.size() != dimension_) {
        // 维度不一致：说明换了嵌入模型，这份索引整体作废。
        // 不能混着存——两个模型的向量放在同一个空间里比较毫无意义。
        return;
    }
    vectors_.insert(chunkId, vector);
    if (pageIndex >= 0) {
        chunkPages_.insert(chunkId, pageIndex);
    }
}

bool VectorIndex::vectorFor(const QString& chunkId, ai::EmbeddingVector* vector) const {
    const auto it = vectors_.constFind(chunkId);
    if (it == vectors_.constEnd()) {
        return false;
    }
    if (vector != nullptr) {
        *vector = it.value();
    }
    return true;
}

int VectorIndex::pageFor(const QString& chunkId) const {
    return chunkPages_.value(chunkId, -1);
}

void VectorIndex::remove(const QString& chunkId) {
    vectors_.remove(chunkId);
    chunkPages_.remove(chunkId);
}

QStringList VectorIndex::chunkIds() const {
    QStringList ids = vectors_.keys();
    ids.sort();
    return ids;
}

double VectorIndex::cosine(const ai::EmbeddingVector& left, const ai::EmbeddingVector& right) {
    const qsizetype size = qMin(left.size(), right.size());
    if (size <= 0) {
        return 0.0;
    }
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (qsizetype index = 0; index < size; ++index) {
        const double a = left.at(index);
        const double b = right.at(index);
        dot += a * b;
        leftNorm += a * a;
        rightNorm += b * b;
    }
    if (leftNorm <= 0.0 || rightNorm <= 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(leftNorm) * std::sqrt(rightNorm));
}

QVector<VectorHit> VectorIndex::search(const ai::EmbeddingVector& query, int limit) const {
    QVector<VectorHit> hits;
    if (query.isEmpty() || limit <= 0) {
        return hits;
    }
    for (auto it = vectors_.constBegin(); it != vectors_.constEnd(); ++it) {
        const double score = cosine(query, it.value());
        if (score <= 0.0) {
            // 负相似度 = 意思相反。留着只会误导模型。
            continue;
        }
        hits.append(VectorHit{it.key(), score});
    }
    // 只留前 limit 条：分数从高到低。
    std::sort(hits.begin(), hits.end(),
              [](const VectorHit& left, const VectorHit& right) {
                  if (left.score != right.score) {
                      return left.score > right.score;
                  }
                  // 分数相同时按 id 排，保证结果稳定可复现。
                  return left.chunkId < right.chunkId;
              });
    if (hits.size() > limit) {
        hits.resize(limit);
    }
    return hits;
}

}  // namespace aipdf::doc
