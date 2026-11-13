#include "doc/LocalIndex.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace aipdf::doc {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyDocumentPath = "documentPath";
constexpr auto kKeyPageCount = "pageCount";
constexpr auto kKeyReferenceCharHeight = "referenceCharHeight";
constexpr auto kKeyFingerprints = "fingerprints";
constexpr auto kKeyChunks = "chunks";

constexpr auto kKeyChunkId = "id";
constexpr auto kKeyChunkPage = "page";
constexpr auto kKeyChunkOrder = "order";
constexpr auto kKeyChunkText = "text";
constexpr auto kKeyChunkSection = "section";
constexpr auto kKeyChunkHeading = "heading";
constexpr auto kKeyChunkWords = "words";
constexpr auto kKeyChunkBoxes = "boxes";

constexpr int kFormatVersion = 1;

// 片段长度：命中前后各留这么多字符。
constexpr int kSnippetRadius = 60;

QString snippetAround(const QString& text, int offset, int length) {
    if (text.isEmpty()) {
        return QString();
    }
    const int start = qMax(0, offset - kSnippetRadius);
    const int end = qMin(text.size(), offset + length + kSnippetRadius);
    QString snippet = text.mid(start, end - start);
    snippet.replace(QLatin1Char('\n'), QLatin1Char(' '));
    snippet = snippet.simplified();
    if (start > 0) {
        snippet.prepend(QStringLiteral("…"));
    }
    if (end < text.size()) {
        snippet.append(QStringLiteral("…"));
    }
    return snippet;
}

}  // namespace

QString LocalIndex::directoryPath() {
    const QString override = qEnvironmentVariable("AIPDF_INDEX_DIR");
    if (!override.isEmpty()) {
        return override;
    }
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("index"));
}

QString LocalIndex::filePathFor(const QString& documentPath) {
    if (documentPath.isEmpty()) {
        return QString();
    }
    const QString absolute = QFileInfo(documentPath).absoluteFilePath();
    const QByteArray hash =
        QCryptographicHash::hash(absolute.toUtf8(), QCryptographicHash::Sha1);
    return QDir(directoryPath()).filePath(QString::fromLatin1(hash.toHex()) +
                                          QStringLiteral(".json"));
}

bool LocalIndex::load(const QString& documentPath) {
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

    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QString::fromLatin1(kKeyVersion)).toInt() != kFormatVersion) {
        return false;
    }

    documentPath_ = root.value(QString::fromLatin1(kKeyDocumentPath)).toString();
    if (documentPath_.isEmpty()) {
        documentPath_ = documentPath;
    }
    pageCount_ = root.value(QString::fromLatin1(kKeyPageCount)).toInt();
    referenceCharHeight_ =
        root.value(QString::fromLatin1(kKeyReferenceCharHeight)).toDouble();

    const QJsonObject fingerprints =
        root.value(QString::fromLatin1(kKeyFingerprints)).toObject();
    for (auto it = fingerprints.constBegin(); it != fingerprints.constEnd(); ++it) {
        fingerprints_.insert(it.key().toInt(), it.value().toString());
    }

    const QJsonArray chunks = root.value(QString::fromLatin1(kKeyChunks)).toArray();
    for (const QJsonValue& value : chunks) {
        const QJsonObject object = value.toObject();
        DocumentChunk chunk;
        chunk.id = object.value(QString::fromLatin1(kKeyChunkId)).toString();
        chunk.pageIndex = object.value(QString::fromLatin1(kKeyChunkPage)).toInt(-1);
        chunk.order = object.value(QString::fromLatin1(kKeyChunkOrder)).toInt();
        chunk.text = object.value(QString::fromLatin1(kKeyChunkText)).toString();
        chunk.sectionPath = object.value(QString::fromLatin1(kKeyChunkSection)).toString();
        chunk.headingLevel = object.value(QString::fromLatin1(kKeyChunkHeading)).toInt();
        chunk.wordCount = object.value(QString::fromLatin1(kKeyChunkWords)).toInt();
        const QJsonArray boxes = object.value(QString::fromLatin1(kKeyChunkBoxes)).toArray();
        for (const QJsonValue& boxValue : boxes) {
            const QJsonArray box = boxValue.toArray();
            if (box.size() != 4) {
                continue;
            }
            chunk.lineBoxes.append(QRectF(box.at(0).toDouble(), box.at(1).toDouble(),
                                          box.at(2).toDouble(), box.at(3).toDouble()));
        }
        if (chunk.isValid()) {
            pageChunks_[chunk.pageIndex].append(chunk);
        }
    }
    return true;
}

bool LocalIndex::save() const {
    if (documentPath_.isEmpty()) {
        return false;
    }
    const QString path = filePathFor(documentPath_);
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        return false;
    }

    QJsonArray chunks;
    for (int pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
        const auto it = pageChunks_.constFind(pageIndex);
        if (it == pageChunks_.constEnd()) {
            continue;
        }
        for (const DocumentChunk& chunk : it.value()) {
            QJsonObject object;
            object.insert(QString::fromLatin1(kKeyChunkId), chunk.id);
            object.insert(QString::fromLatin1(kKeyChunkPage), chunk.pageIndex);
            object.insert(QString::fromLatin1(kKeyChunkOrder), chunk.order);
            object.insert(QString::fromLatin1(kKeyChunkText), chunk.text);
            object.insert(QString::fromLatin1(kKeyChunkSection), chunk.sectionPath);
            object.insert(QString::fromLatin1(kKeyChunkHeading), chunk.headingLevel);
            object.insert(QString::fromLatin1(kKeyChunkWords), chunk.wordCount);
            QJsonArray boxes;
            for (const QRectF& box : chunk.lineBoxes) {
                QJsonArray values;
                values.append(box.x());
                values.append(box.y());
                values.append(box.width());
                values.append(box.height());
                boxes.append(values);
            }
            object.insert(QString::fromLatin1(kKeyChunkBoxes), boxes);
            chunks.append(object);
        }
    }

    QJsonObject fingerprints;
    for (auto it = fingerprints_.constBegin(); it != fingerprints_.constEnd(); ++it) {
        fingerprints.insert(QString::number(it.key()), it.value());
    }

    QJsonObject root;
    root.insert(QString::fromLatin1(kKeyVersion), kFormatVersion);
    root.insert(QString::fromLatin1(kKeyDocumentPath), documentPath_);
    root.insert(QString::fromLatin1(kKeyPageCount), pageCount_);
    root.insert(QString::fromLatin1(kKeyReferenceCharHeight), referenceCharHeight_);
    root.insert(QString::fromLatin1(kKeyFingerprints), fingerprints);
    root.insert(QString::fromLatin1(kKeyChunks), chunks);

    // 先写临时文件再替换：索引写到一半崩了，原来的索引还在。
    const QString temporaryPath = path + QStringLiteral(".tmp");
    QFile file(temporaryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
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
    if (!QFile::rename(temporaryPath, path)) {
        QFile::remove(temporaryPath);
        return false;
    }
    return true;
}

void LocalIndex::clear() {
    documentPath_.clear();
    pageCount_ = 0;
    pageChunks_.clear();
    fingerprints_.clear();
    referenceCharHeight_ = 0.0;
}

QString LocalIndex::documentPath() const {
    return documentPath_;
}

void LocalIndex::setDocumentPath(const QString& documentPath) {
    documentPath_ = documentPath;
}

int LocalIndex::pageCount() const {
    return pageCount_;
}

void LocalIndex::setPageCount(int count) {
    pageCount_ = qMax(0, count);
}

int LocalIndex::chunkCount() const {
    int total = 0;
    for (auto it = pageChunks_.constBegin(); it != pageChunks_.constEnd(); ++it) {
        total += it.value().size();
    }
    return total;
}

bool LocalIndex::isEmpty() const {
    return chunkCount() == 0;
}

QVector<DocumentChunk> LocalIndex::chunks() const {
    QVector<DocumentChunk> result;
    for (int pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
        const auto it = pageChunks_.constFind(pageIndex);
        if (it == pageChunks_.constEnd()) {
            continue;
        }
        result += it.value();
    }
    return result;
}

QVector<DocumentChunk> LocalIndex::chunksForPage(int pageIndex) const {
    return pageChunks_.value(pageIndex);
}

bool LocalIndex::chunkById(const QString& chunkId, DocumentChunk* chunk) const {
    if (chunkId.isEmpty()) {
        return false;
    }
    for (auto it = pageChunks_.constBegin(); it != pageChunks_.constEnd(); ++it) {
        for (const DocumentChunk& candidate : it.value()) {
            if (candidate.id == chunkId) {
                if (chunk != nullptr) {
                    *chunk = candidate;
                }
                return true;
            }
        }
    }
    return false;
}

bool LocalIndex::chunkByIdOnPage(int pageIndex,
                                 const QString& chunkId,
                                 DocumentChunk* chunk) const {
    if (chunkId.isEmpty()) {
        return false;
    }
    const auto it = pageChunks_.constFind(pageIndex);
    if (it == pageChunks_.constEnd()) {
        return false;
    }
    for (const DocumentChunk& candidate : it.value()) {
        if (candidate.id == chunkId) {
            if (chunk != nullptr) {
                *chunk = candidate;
            }
            return true;
        }
    }
    return false;
}

QString LocalIndex::pageFingerprint(int pageIndex) const {
    return fingerprints_.value(pageIndex);
}

void LocalIndex::setPageFingerprint(int pageIndex, const QString& fingerprint) {
    if (fingerprint.isEmpty()) {
        fingerprints_.remove(pageIndex);
        return;
    }
    fingerprints_.insert(pageIndex, fingerprint);
}

QVector<int> LocalIndex::pagesNeedingRebuild(const QStringList& currentFingerprints) const {
    QVector<int> pages;
    for (int pageIndex = 0; pageIndex < currentFingerprints.size(); ++pageIndex) {
        const QString current = currentFingerprints.at(pageIndex);
        if (current.isEmpty()) {
            continue;
        }
        if (fingerprints_.value(pageIndex) != current) {
            pages.append(pageIndex);
        }
    }
    return pages;
}

bool LocalIndex::findPageByFingerprint(const QString& fingerprint, int* pageIndex) const {
    if (fingerprint.isEmpty()) {
        return false;
    }
    for (auto it = fingerprints_.constBegin(); it != fingerprints_.constEnd(); ++it) {
        if (it.value() == fingerprint) {
            if (pageIndex != nullptr) {
                *pageIndex = it.key();
            }
            return true;
        }
    }
    return false;
}

QString LocalIndex::fingerprintOf(const QString& text,
                                  const QRectF& pageBox,
                                  const QRectF& textBounds) {
    // 注意：PDF user space 里 QRectF 的 y() 是**下沿**，bottom() 是 y()+height()，
    // 那个「下沿」在这里是上沿。所以一律用 x()/y()/width()/height()，
    // 不用 left()/bottom()。
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(text.toUtf8());
    hash.addData(QByteArrayLiteral("|"));
    // 页面盒子：位置与尺寸都算。
    hash.addData(QByteArray::number(qRound(pageBox.x())));
    hash.addData(QByteArrayLiteral(","));
    hash.addData(QByteArray::number(qRound(pageBox.y())));
    hash.addData(QByteArrayLiteral(","));
    hash.addData(QByteArray::number(qRound(pageBox.width())));
    hash.addData(QByteArrayLiteral("x"));
    hash.addData(QByteArray::number(qRound(pageBox.height())));
    hash.addData(QByteArrayLiteral("|"));
    // 文本整体包围盒：文字在页上的位置。
    hash.addData(QByteArray::number(qRound(textBounds.x())));
    hash.addData(QByteArrayLiteral(","));
    hash.addData(QByteArray::number(qRound(textBounds.y())));
    hash.addData(QByteArrayLiteral(","));
    hash.addData(QByteArray::number(qRound(textBounds.width())));
    hash.addData(QByteArrayLiteral("x"));
    hash.addData(QByteArray::number(qRound(textBounds.height())));
    return QString::fromLatin1(hash.result().toHex());
}

void LocalIndex::replacePage(int pageIndex,
                              const QVector<DocumentChunk>& pageChunks,
                              const QString& fingerprint) {
    if (pageChunks.isEmpty()) {
        pageChunks_.remove(pageIndex);
    } else {
        pageChunks_.insert(pageIndex, pageChunks);
    }
    setPageFingerprint(pageIndex, fingerprint);
    pageCount_ = qMax(pageCount_, pageIndex + 1);
}

void LocalIndex::removePage(int pageIndex) {
    pageChunks_.remove(pageIndex);
    fingerprints_.remove(pageIndex);
}

double LocalIndex::referenceCharHeight() const {
    return referenceCharHeight_;
}

void LocalIndex::setReferenceCharHeight(double height) {
    referenceCharHeight_ = (height > 0.0) ? height : 0.0;
}

QVector<IndexHit> LocalIndex::search(const QString& query, int limit) const {
    QVector<IndexHit> hits;
    const QString needle = query.trimmed();
    if (needle.isEmpty() || limit <= 0) {
        return hits;
    }
    for (int pageIndex = 0; pageIndex < pageCount_ && hits.size() < limit; ++pageIndex) {
        const auto it = pageChunks_.constFind(pageIndex);
        if (it == pageChunks_.constEnd()) {
            continue;
        }
        for (const DocumentChunk& chunk : it.value()) {
            int from = 0;
            while (hits.size() < limit) {
                const int offset = chunk.text.indexOf(needle, from, Qt::CaseInsensitive);
                if (offset < 0) {
                    break;
                }
                IndexHit hit;
                hit.chunkId = chunk.id;
                hit.pageIndex = chunk.pageIndex;
                hit.charOffset = offset;
                hit.sectionPath = chunk.sectionPath;
                // 命中落在块内的第几行：数一下前面有几个换行。
                int lineIndex = 0;
                for (int position = 0; position < offset; ++position) {
                    if (chunk.text.at(position) == QLatin1Char('\n')) {
                        ++lineIndex;
                    }
                }
                hit.lineIndex = lineIndex;
                hit.snippet = snippetAround(chunk.text, offset, needle.size());
                hits.append(hit);
                from = offset + needle.size();
            }
            if (hits.size() >= limit) {
                break;
            }
        }
    }
    return hits;
}

}  // namespace aipdf::doc
