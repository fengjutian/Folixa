#pragma once

#include <QHash>
#include <QImage>
#include <QList>

namespace aipdf::app {

// 页面瓦片缓存：按「页号 + 量化后的缩放」缓存已渲染的页面图像。
//
// 上限按像素字节计（宽 × 高 × 4），不是按条目数：
// 一张页面在 4 倍缩放下就能吃掉几十兆，按条目数限不住。
// 淘汰用 LRU：命中就把条目挪到队首，内存超限时从队尾丢。
class TileCache {
public:
    struct Key {
        int pageIndex = -1;
        qint64 zoomKey = 0;
        // 用户旋转的四分之一圈数：内容变了，键必须跟着变。
        int rotation = 0;

        bool operator==(const Key& other) const {
            return pageIndex == other.pageIndex && zoomKey == other.zoomKey &&
                   rotation == other.rotation;
        }
    };

    explicit TileCache(qint64 maxBytes = 64LL * 1024 * 1024);

    // 把缩放倍率折成整数键，避免浮点抖动让同一页反复失效。
    static qint64 zoomToKey(double zoom);

    void setMaxBytes(qint64 maxBytes);
    qint64 maxBytes() const;

    // 插入瓦片；超限时按 LRU 淘汰。
    void insert(const Key& key, const QImage& image);

    // 取瓦片；命中会刷新它的 LRU 位置。
    QImage lookup(const Key& key);

    bool contains(const Key& key) const;

    void remove(const Key& key);
    void clear();

    int count() const;
    qint64 bytes() const;

private:
    struct Entry {
        QImage image;
        qint64 bytes = 0;
    };

    QHash<Key, Entry> entries_;
    QList<Key> order_;
    qint64 maxBytes_ = 0;
    qint64 bytes_ = 0;

    void touch(const Key& key);
    void evictUntilWithinLimit();
};

inline size_t qHash(const TileCache::Key& key, size_t seed = 0) noexcept {
    return ::qHash(key.zoomKey, seed) ^ ::qHash(key.pageIndex, seed + 1) ^
           ::qHash(key.rotation, seed + 2);
}

}  // namespace aipdf::app
