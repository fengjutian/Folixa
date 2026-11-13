#pragma once

#include "doc/DocumentChunk.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::doc {

// 一条检索命中。
struct IndexHit {
    // 命中所属的块 id。
    QString chunkId;
    // 命中所在的页。
    int pageIndex = -1;
    // 命中落在块内的第几行（-1 表示整块命中）。
    int lineIndex = -1;
    // 命中在块文本里的字符下标。
    int charOffset = -1;
    // 上下文片段（结果列表显示用）。
    QString snippet;
    // 章节路径。
    QString sectionPath;
};

// 本地索引：一个文档的块 + 检索。
//
// 存哪儿：应用数据目录下的 index 子目录（可用 AIPDF_INDEX_DIR 改位置）。
// 一个文档一个文件，文件里记着每一页的「内容指纹」：
// 指纹变了说明那一页的文本动过，只有这些页需要重建索引。
//
// 检索为什么不用倒排索引：探针实测（build/probe/fts_probe.py），
// 一千段（每段 220 字）做一次子串扫描只要 0.1 ms——这个规模下，
// 倒排索引带来的复杂度换不来速度。
// 子串匹配还有一个好处：中文直接就能搜，不用分词。
class LocalIndex {
public:
    // 索引目录。
    static QString directoryPath();
    // 某个文档的索引文件路径。
    static QString filePathFor(const QString& documentPath);

    // 载入 / 保存。
    bool load(const QString& documentPath);
    bool save() const;
    void clear();

    QString documentPath() const;
    void setDocumentPath(const QString& documentPath);
    int pageCount() const;
    void setPageCount(int count);
    int chunkCount() const;
    bool isEmpty() const;

    // 全部块（按页序、块序）。
    QVector<DocumentChunk> chunks() const;
    // 某一页的块。
    QVector<DocumentChunk> chunksForPage(int pageIndex) const;
    // 按 id 取块。
    bool chunkById(const QString& chunkId, DocumentChunk* chunk) const;
    // 某一页里按 id 取块。
    //
    // 定位一律要用这个：两页内容完全一样时（比如两页空白），
    // 按 id 全局取会拿到另一页的块，坐标就错位了。
    bool chunkByIdOnPage(int pageIndex, const QString& chunkId, DocumentChunk* chunk) const;

    // 页内容指纹。
    QString pageFingerprint(int pageIndex) const;
    void setPageFingerprint(int pageIndex, const QString& fingerprint);
    // 指纹对不上的页（= 需要重建的页）。currentFingerprints 的下标是页序号。
    QVector<int> pagesNeedingRebuild(const QStringList& currentFingerprints) const;
    // 按内容找页：索引里有没有哪一页的指纹和它一样。
    //
    // 这一条是「增量」的关键：文档前面插了一页，后面的页码整体后移，
    // 但内容没变——按内容找回来就能直接复用那页的块，只改页序号。
    bool findPageByFingerprint(const QString& fingerprint, int* pageIndex) const;
    // 页内容指纹：文本 + 页面盒子 + 文本整体包围盒。
    //
    // 三样都得带上，缺一个就会把「坐标不同」的页当成同一页：
    //   - 文本变了 obviously；
    //   - 页面盒子变了（尺寸或位置），字符坐标就变了；
    //   - 文本在页上的位置变了（同样的字排在不同地方），坐标也不一样。
    // 只有三者全同，才能安全地复用旧块。
    //
    // 两个矩形都按 PDF user space 的 (left, bottom, width, height) 传，
    // 也就是 QRectF 的 x()/y()/width()/height()——不要用 bottom()。
    static QString fingerprintOf(const QString& text,
                                 const QRectF& pageBox,
                                 const QRectF& textBounds);

    // 用某一页的新块替换旧块（增量更新）。
    void replacePage(int pageIndex,
                     const QVector<DocumentChunk>& pageChunks,
                     const QString& fingerprint);
    // 删掉一页的块（页被删了）。
    void removePage(int pageIndex);

    // 整篇文档的正文字高（点）。
    //
    // 标题层级靠它定，所以要跨页一致：存进索引里，
    // 下次重建直接复用，不必再探一遍。
    double referenceCharHeight() const;
    void setReferenceCharHeight(double height);

    // 检索：子串匹配，忽略大小写。
    QVector<IndexHit> search(const QString& query, int limit = 200) const;

private:
    QString documentPath_;
    int pageCount_ = 0;
    // 页 → 该页的块。
    QHash<int, QVector<DocumentChunk>> pageChunks_;
    // 页 → 内容指纹。
    QHash<int, QString> fingerprints_;
    // 整篇文档的正文字高（点）。
    double referenceCharHeight_ = 0.0;
};

}  // namespace aipdf::doc
