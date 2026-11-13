#include "pdf/Navigation.h"

namespace aipdf::pdf {

namespace {
// 摊平时的安全上限。大纲树本身在适配层构建时已经限过深度，
// 这里再兜一层，防止将来数据来源变化后无限展开。
constexpr int kMaxFlattenDepth = 64;
constexpr int kMaxFlattenItems = 20000;
}  // namespace

std::vector<OutlineEntry> flattenOutline(const std::vector<OutlineItem>& outline) {
    std::vector<OutlineEntry> result;
    if (outline.empty()) {
        return result;
    }
    result.reserve(outline.size());

    // 显式栈遍历：递归在层数很深时会吃掉不少栈空间，
    // 而文档大纲的深度不可控。
    struct Pending {
        const OutlineItem* item;
        int depth;
    };
    std::vector<Pending> stack;
    for (auto it = outline.rbegin(); it != outline.rend(); ++it) {
        stack.push_back({&(*it), 0});
    }

    while (!stack.empty()) {
        const Pending pending = stack.back();
        stack.pop_back();
        if (pending.depth >= kMaxFlattenDepth) {
            continue;
        }
        if (result.size() >= kMaxFlattenItems) {
            break;
        }
        result.push_back(OutlineEntry{pending.item->title, pending.item->pageIndex, pending.depth});
        // 子条目要按文档顺序排在父条目之后，因此倒序入栈。
        for (auto it = pending.item->children.rbegin(); it != pending.item->children.rend(); ++it) {
            stack.push_back({&(*it), pending.depth + 1});
        }
    }
    return result;
}

}  // namespace aipdf::pdf
