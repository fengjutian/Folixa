#pragma once

#include "pdf/kernel/Navigation.h"

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

namespace aipdf::pdf {

// ============================================================================
// 交叉引用：正文里指向别处的引用
// ============================================================================
//
// 做什么：
//   1. 从正文文字里认出引用（「见第 12 页」「详见 3.2 节」「see page 12」）；
//   2. 把引用对上目标（页码 / 标题 / 章节号），并给出**置信度**；
//   3. 把认出来的引用变成可点击的跳转。
//
// 不做什么：
//   * **不猜**。页码对不上、标题也找不到，就老老实实列成「未解析」，
//     绝不替用户编一个页码出来——编出来的跳转比没有跳转更糟。
//   * **不改正文**。这一层只读文字、只给候选；要不要改、怎么改是界面层的事。
//
// 边界（必须让用户知道）：
//   * 页码按**物理页序**算：引用里写「第 12 页」对应第 12 个页面对象。
//     文档自己带 /PageLabels（罗马数字前言之类）时这里对不上。
//   * 只认显式的引用词（见 / 详见 / 参见 / see / on …）。纯数字引用
//     （"3.2" 单独出现）不算引用，否则满篇都是。

// 引用提到的是什么。
enum class CrossReferenceKind {
    Page,     // 「见第 12 页」/ "see page 12"
    Section,  // 「详见 3.2 节」/ "see section 3.2"
    Chapter,  // 「详见第 3 章」/ "see chapter 3"
    Unknown,
};

// 目标是怎么对上的。
enum class CrossReferenceMatch {
    PageNumber,  // 按页码直接命中（高置信度）
    Title,       // 按标题 / 书签命中（中置信度）
    Unresolved,  // 没对上
};

// 置信度。
//
// 为什么分三档而不是给个分数：页码命中与标题命中的可信程度差别是**性质上**的
// （一个数对不对 vs 一个名字像不像），分数反而会让人以为它们可比。
enum class CrossReferenceConfidence {
    High,   // 页码直接命中
    Medium, // 标题 / 书签命中
    None,   // 未解析
};

// 一个候选目标。
struct CrossReferenceCandidate {
    // 引用里指向它的那段文字，例如「第 12 页」。
    QString referenceText;
    // 引用里写的页码（1 基）。章节型引用为 -1。
    int referencedPageNumber = -1;
    // 引用里写的章节号，例如「3.2」。页码型引用为空。
    QString sectionNumber;
    // 解析出来的目标页（0 基）。-1 表示没解析出来。
    int pageIndex = -1;
    // 解析出来的目标标题。空表示没有。
    QString title;
    // 命中方式。
    CrossReferenceMatch match = CrossReferenceMatch::Unresolved;
    // 置信度。
    CrossReferenceConfidence confidence = CrossReferenceConfidence::None;
    // 中文说明：为什么是这个判定。
    QString note;
};

// 一条识别出来的交叉引用。
struct CrossReference {
    // 整段引用原文，例如「见第 12 页和第 13 页」。
    QString text;
    // 引用出现在哪一页（0 基）。
    int sourcePageIndex = -1;
    // 在该页文字里的起始位置与长度（QString 字符下标）。
    int startOffset = 0;
    int length = 0;
    // 引用提到的目标。一段引用可能提到多个目标。
    QVector<CrossReferenceCandidate> candidates;
    // 整段引用的置信度：取候选里**最低**的那个（保守口径）。
    CrossReferenceConfidence confidence = CrossReferenceConfidence::None;
    // 至少有一个目标解析出来了。
    bool isResolved() const;
    // 中文说明。
    QString note;
};

// 扫描结果。
struct CrossReferenceScanResult {
    QVector<CrossReference> references;
    // 解析出来的引用数 / 未解析的引用数。
    int resolvedCount = 0;
    int unresolvedCount = 0;
    // 中文说明（整体层面的，例如"这份文档没有书签，章节号引用都只能算未解析"）。
    QStringList notes;
};

// 一页的文字（由调用方给：pdf::PdfPage::extractText() 的结果）。
struct CrossReferencePage {
    int pageIndex = -1;
    QString text;
};

// 扫描正文里的交叉引用。
//
// pages 是各页的文字；outline 是文档现有的大纲（书签），用来做标题匹配，
// 没有书签时传空列表；pageCount 是文档页数（<= 0 表示不知道）。
CrossReferenceScanResult scanCrossReferences(const QVector<CrossReferencePage>& pages,
                                             const std::vector<OutlineItem>& outline,
                                             int pageCount = -1);

// 一条引用变成可点击跳转时需要的参数。
struct CrossReferenceLinkSpec {
    // 引用所在页（0 基）。
    int sourcePageIndex = -1;
    // 引用文字在页面上的矩形（PDF user space，左下原点）。
    QRectF rect;
    // 显示文字。
    QString displayText;
    // 目标页（0 基）。
    int targetPageIndex = -1;
};

// 写回结果。
struct CrossReferenceLinkResult {
    QByteArray bytes;
    bool ok = false;
    QString error;
};

// 用识别出来的引用填一个跳转参数。
//
// rect 由调用方给出：引用文字在页面上的位置（界面拿字符框换算）。
// 挑目标时取第一个解析出来的候选——没有解析出来的候选就返回
// 一个 targetPageIndex 为 -1 的参数，调用方一看就知道不能跳。
CrossReferenceLinkSpec crossReferenceLinkSpec(const CrossReference& reference, const QRectF& rect);

// 把一条引用变成可点击的跳转，写进 PDF 字节。
//
// 复用仓库既有的插入通路（pdf::injectInlineAnnotations）：PDF 里能跨页跳的
// 只有 Link 批注，可见文字由界面层另外补（批注文本框），两者叠在同一块区域上。
CrossReferenceLinkResult insertCrossReferenceLink(const QByteArray& pdfBytes,
                                                  const CrossReferenceLinkSpec& spec);

}  // namespace aipdf::pdf
