#include "app/TileCache.h"

#include <QtGlobal>

namespace aipdf::app {

namespace {
qint64 imageBytes(const QImage& image) {
    if (image.isNull()) {
        return 0;
    }
    return static_cast<qint64>(image.width()) * static_cast<qint64>(image.height()) * 4;
}
}  // namespace

TileCache::TileCache(qint64 maxBytes) : maxBytes_(qMax<qint64>(0, maxBytes)) {}

qint64 TileCache::zoomToKey(double zoom) {
    // 1/1000 的精度足够区分 25% / 125% / 400% 这类实际用到的倍率，
    // 又不会被浮点误差来回抖动。
    return qRound64(zoom * 1000.0);
}

void TileCache::setMaxBytes(qint64 maxBytes) {
    maxBytes_ = qMax<qint64>(0, maxBytes);
    evictUntilWithinLimit();
}

qint64 TileCache::maxBytes() const {
    return maxBytes_;
}

void TileCache::insert(const Key& key, const QImage& image) {
    if (image.isNull() || key.pageIndex < 0) {
        return;
    }

    remove(key);

    Entry entry;
    entry.image = image;
    entry.bytes = imageBytes(image);

    // 单张图就超过上限时不缓存，但仍要保证已有内容不超限。
    if (entry.bytes > maxBytes_) {
        evictUntilWithinLimit();
        return;
    }

    entries_.insert(key, entry);
    order_.prepend(key);
    bytes_ += entry.bytes;

    evictUntilWithinLimit();
}

QImage TileCache::lookup(const Key& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return {};
    }
    touch(key);
    return it->image;
}

bool TileCache::contains(const Key& key) const {
    return entries_.contains(key);
}

void TileCache::remove(const Key& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return;
    }
    bytes_ -= it->bytes;
    entries_.erase(it);
    order_.removeAll(key);
}

void TileCache::clear() {
    entries_.clear();
    order_.clear();
    bytes_ = 0;
}

int TileCache::count() const {
    return static_cast<int>(entries_.size());
}

qint64 TileCache::bytes() const {
    return bytes_;
}

void TileCache::touch(const Key& key) {
    order_.removeAll(key);
    order_.prepend(key);
}

void TileCache::evictUntilWithinLimit() {
    while (bytes_ > maxBytes_ && !order_.isEmpty()) {
        const Key oldest = order_.takeLast();
        auto it = entries_.find(oldest);
        if (it != entries_.end()) {
            bytes_ -= it->bytes;
            entries_.erase(it);
        }
    }
    if (bytes_ < 0) {
        bytes_ = 0;
    }
}

}  // namespace aipdf::app
