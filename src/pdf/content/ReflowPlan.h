#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/kernel/PageObjectModel.h"

namespace aipdf::pdf {

// ============================================================================
// 重排计划：源对象搬到了哪里
// ============================================================================
//
// 整篇重排之后，用户最想知道的是「原来那段文字／那张图现在在第几页」。
// 这个结构就是那张对照表：每个源对象一条记录，记下它的稳定身份、
// 原来在哪、现在在哪。
//
// 为什么身份用「稳定 id」而不是「第几个对象」：
// 重排会重排对象、删对象、加对象，PDFium 给的下标会漂移；
// 稳定 id 由「种类 + 内容指纹 + 几何 + 序号」派生，跨版本、跨修订都认得出
// （见 pdf::derivePageObjectId）。

// 一个锚点：源文档里的一个对象搬到了新文档的哪个位置。
struct ReflowAnchor {
    // 源对象的稳定身份（pdf::derivePageObjectId 派生）。
    QString objectId;
    // 对象的种类。
    PageObjectKind kind = PageObjectKind::Unknown;
    // 原来在第几页（0 基）。
    int sourcePage = -1;
    // 现在在第几页（0 基）。-1 表示这个对象没有搬过去。
    int targetPage = -1;
    // 原来的位置。
    QRectF sourceBounds;
    // 新的位置。
    QRectF targetBounds;
    // 一句话说明（文字块是开头几个字，图片是替代文字，表格是行列数）。
    QString summary;
    // 这个对象是不是被重复放到了多页（页眉页脚每页一份）。
    bool repeated = false;
};

// 重排计划。
struct ReflowPlan {
    // 所有锚点（按源文档的阅读顺序）。
    QVector<ReflowAnchor> anchors;
    // 源文档的页数。
    int sourcePageCount = 0;
    // 重排结果的页数。
    int targetPageCount = 0;
    // 重排前的对象计数。
    PageObjectCounts sourceCounts;
    // 重排后的对象计数。
    PageObjectCounts targetCounts;

    // 按稳定 id 找锚点。找不到返回 nullptr。
    const ReflowAnchor* anchorById(const QString& objectId) const {
        for (const ReflowAnchor& anchor : anchors) {
            if (anchor.objectId == objectId) return &anchor;
        }
        return nullptr;
    }

    // 某一类对象的锚点。
    QVector<ReflowAnchor> anchorsOfKind(PageObjectKind kind) const {
        QVector<ReflowAnchor> result;
        for (const ReflowAnchor& anchor : anchors) {
            if (anchor.kind == kind) result.append(anchor);
        }
        return result;
    }

    int anchorCount() const { return anchors.size(); }
};

}  // namespace aipdf::pdf
