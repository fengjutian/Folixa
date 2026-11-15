#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 往页面 /Annots 数组里插一条新注释。
//
// 为什么需要单独一层：增量写入器只会「重写已存在的批注对象」，而 PDFium 建不出
// 某些注释类型（/Sound 就是），这些注释在内核里根本不存在，只能由我们自己
// 把字典写进页面对象。
struct InlineAnnotationInsert {
    int pageIndex = 0;
    // 注释字典，可以引用额外对象：@EXTRA0、@EXTRA1…（写入时替换成对象号）。
    QByteArray dictionary;
    // 需要新建的对象体（例如 /Sound 的音频流）。
    QVector<QByteArray> extraObjects;
};

struct InlineWriteResult {
    QByteArray bytes;
    bool ok = false;
    QString error;
    int inserted = 0;
};

// 在已有 PDF 上追加一次增量更新：重写目标页对象（/Annots 里多一条），
// 再追加新对象与新的 xref。原有字节一个都不动。
InlineWriteResult injectInlineAnnotations(const QByteArray& pdf,
                                          const QVector<InlineAnnotationInsert>& inserts);

}  // namespace aipdf::pdf
