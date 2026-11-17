#include "pdf/DocumentOpen.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>

namespace aipdf::pdf {

namespace {

// 扫描标记时的分块大小：足够大以便顺序读，又小到不会把内存撑起来。
constexpr qint64 kMarkerScanChunkBytes = 1024 * 1024;

}  // namespace

DocumentReader::~DocumentReader() = default;

std::shared_ptr<DocumentReader> DocumentReader::openPath(const QString& filePath,
                                                        QString* error) {
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("file not found");
        }
        return nullptr;
    }
    auto reader = std::shared_ptr<DocumentReader>(new DocumentReader());
    reader->kind_ = DocumentSourceKind::FilePath;
    reader->filePath_ = filePath;
    reader->size_ = info.size();
    reader->file_ = std::make_unique<QFile>(filePath);
    if (!reader->file_->open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = reader->file_->errorString();
        }
        return nullptr;
    }
    return reader;
}

std::shared_ptr<DocumentReader> DocumentReader::fromBytes(const QByteArray& bytes) {
    auto reader = std::shared_ptr<DocumentReader>(new DocumentReader());
    reader->kind_ = DocumentSourceKind::MemoryBytes;
    // QByteArray 是隐式共享的：这里只是复制引用，不复制数据。
    reader->bytes_ = bytes;
    reader->size_ = bytes.size();
    return reader;
}

bool DocumentReader::read(qint64 position, char* buffer, qint64 length) {
    if (buffer == nullptr || length < 0 || position < 0) {
        return false;
    }
    if (position + length > size_) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (kind_ == DocumentSourceKind::MemoryBytes) {
        memcpy(buffer, bytes_.constData() + position, static_cast<size_t>(length));
        return true;
    }
    if (!file_) {
        return false;
    }
    if (!file_->seek(position)) {
        return false;
    }
    const qint64 got = file_->read(buffer, length);
    if (got != length) {
        return false;
    }
    bytesReadFromDisk_ += length;
    return true;
}

bool documentContainsMarker(const DocumentReader& reader, const QByteArray& marker) {
    if (marker.isEmpty()) {
        return false;
    }
    if (reader.kind() == DocumentSourceKind::MemoryBytes) {
        return reader.bytes().contains(marker);
    }
    const QString path = reader.filePath();
    if (path.isEmpty()) {
        return false;
    }
    // 另开一个句柄扫：不让扫描和 PDFium 的取数共用一个 QFile，
    // 免得两边同时 seek 同一个句柄把数据搅乱。
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    // 块与块之间留 marker.size() - 1 个字节的重叠，
    // 否则标记正好压在块边界上就会漏掉。
    const qint64 overlap = marker.size() - 1;
    QByteArray buffer;
    buffer.resize(static_cast<int>(kMarkerScanChunkBytes + overlap));
    qint64 position = 0;
    const qint64 total = reader.size();
    while (position < total) {
        if (!file.seek(position)) {
            return false;
        }
        const qint64 got = file.read(buffer.data(), buffer.size());
        if (got <= 0) {
            break;
        }
        const int window = static_cast<int>(got);
        if (QByteArray::fromRawData(buffer.constData(), window).contains(marker)) {
            return true;
        }
        if (got < buffer.size()) {
            break;  // 已经到文件尾了
        }
        position += got - overlap;
    }
    return false;
}

}  // namespace aipdf::pdf
