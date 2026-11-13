#pragma once

#include <QRectF>
#include <QString>

#include <vector>

namespace aipdf::pdf {

// PDF 大纲（PDF 规范里也叫书签）的一个条目。
//
// 大纲是一棵树：顶层条目可以带子条目，子条目还可以再带子条目。
// pageIndex 是目标页序号（从 0 开始）；解析不出目标页时为 -1
//（条目存在，但目标损坏或指向文档之外）。
struct OutlineItem {
    QString title;
    int pageIndex = -1;
    std::vector<OutlineItem> children;
};

// 大纲摊平后的一条（书签页签用）：depth 是层级，0 为顶层。
struct OutlineEntry {
    QString title;
    int pageIndex = -1;
    int depth = 0;
};

// 把大纲树摊平成列表，保持文档顺序，depth 记录层级。
std::vector<OutlineEntry> flattenOutline(const std::vector<OutlineItem>& outline);

// 页面上的一个链接注释。
//
// rect 用 **PDF user space**（左下原点），与 FPDFLink_GetRect 的定义一致；
// 页面自带的 /Rotate 没有被应用，要和渲染图像对齐得由上层做旋转变换
//（和字符框同一套规则）。
//
// 内部链接给 pageIndex（目标页序号），外部链接给 uri；
// 两者都没有时这个链接不可用（目标损坏或指向文档之外）。
struct PageLink {
    QRectF rect;
    int pageIndex = -1;
    QString uri;

    bool isInternal() const { return pageIndex >= 0; }
    bool isExternal() const { return !uri.isEmpty(); }
    bool isUsable() const { return isInternal() || isExternal(); }
};

}  // namespace aipdf::pdf
