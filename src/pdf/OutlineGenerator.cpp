#include "pdf/OutlineGenerator.h"

#include <algorithm>

namespace aipdf::pdf {

namespace {

// 生成过程中的中间节点。
//
// 为什么不用指针直接往 std::vector<OutlineItem> 里挂子节点：
// vector 扩容会让元素搬家，之前拿到的指针就悬空了。
// 先用下标把树搭出来，最后再一次性展开成 OutlineItem，稳。
struct Node {
    QString title;
    int pageIndex = -1;
    // 父节点在 nodes 里的下标，-1 表示顶层。
    int parent = -1;
    // 子节点在 nodes 里的下标。
    std::vector<int> children;
};

// 截断标题：总长度不超过 maxLength（省略号算在里面）。
//
// 为什么省略号算在长度里：maxTitleLength 是给界面留的显示宽度，
// 如果截完再加一个省略号，实际宽度就比设定的多一个字符。
QString truncateTitle(const QString& title, int maxLength) {
    if (maxLength <= 0) {
        return title;
    }
    if (title.size() <= maxLength) {
        return title;
    }
    if (maxLength == 1) {
        return QStringLiteral("…");
    }
    return title.left(maxLength - 1) + QStringLiteral("…");
}

// 标题文本：默认只取块文本的第一行。
QString titleOf(const doc::DocumentChunk& chunk, bool firstLineOnly) {
    if (!firstLineOnly) {
        return chunk.text;
    }
    return chunk.text.section(QLatin1Char('\n'), 0, 0);
}

// 把中间节点展开成最终的 OutlineItem 树。
void expand(const std::vector<Node>& nodes, int index, OutlineItem* out) {
    const Node& node = nodes.at(static_cast<size_t>(index));
    out->title = node.title;
    out->pageIndex = node.pageIndex;
    out->children.reserve(node.children.size());
    for (int child : node.children) {
        OutlineItem childItem;
        expand(nodes, child, &childItem);
        out->children.push_back(std::move(childItem));
    }
}

}  // namespace

std::vector<OutlineItem> generateOutlineFromChunks(const QVector<doc::DocumentChunk>& chunks,
                                                   const OutlineGeneratorOptions& options) {
    std::vector<Node> nodes;
    std::vector<int> roots;
    // 层级栈：记录一路走下来的标题层级与它们在 nodes 里的下标。
    // 栈顶永远是"最近的一个可以当爹的标题"。
    std::vector<std::pair<int, int>> stack;

    const int minLevel = options.minHeadingLevel > 0 ? options.minHeadingLevel : 1;
    const int maxDepth = options.maxDepth > 0 ? options.maxDepth : 1;

    for (const doc::DocumentChunk& chunk : chunks) {
        // 层级 0 是正文，不是标题。
        if (chunk.headingLevel <= 0) {
            continue;
        }
        if (chunk.headingLevel < minLevel) {
            continue;
        }
        QString title = titleOf(chunk, options.useFirstLineAsTitle).trimmed();
        if (title.isEmpty()) {
            // 没有文字的标题块生成不了书签（生成了也是空条目）。
            continue;
        }
        title = truncateTitle(title, options.maxTitleLength);

        // 超过最大层级的标题压到最深的这一层，而不是丢掉：
        // 丢掉标题等于让用户看不到这一节。
        const int level = std::min(chunk.headingLevel, maxDepth);

        // 找父级：把层级不比当前浅的都弹掉，剩下的栈顶就是最近的可用父级。
        // 这同时处理了两种情况：
        //   同层级连续出现 → 弹到上一个同层级之前，成为兄弟；
        //   层级跳跃（1 之后直接 3）→ 栈顶的 1 还留着，3 挂到 1 下面。
        while (!stack.empty() && stack.back().first >= level) {
            stack.pop_back();
        }

        Node node;
        node.title = title;
        node.pageIndex = chunk.pageIndex;
        const int nodeIndex = static_cast<int>(nodes.size());
        if (stack.empty()) {
            node.parent = -1;
            roots.push_back(nodeIndex);
        } else {
            node.parent = stack.back().second;
            nodes.at(static_cast<size_t>(node.parent)).children.push_back(nodeIndex);
        }
        nodes.push_back(node);
        stack.emplace_back(level, nodeIndex);
    }

    std::vector<OutlineItem> outline;
    outline.reserve(roots.size());
    for (int root : roots) {
        OutlineItem item;
        expand(nodes, root, &item);
        outline.push_back(std::move(item));
    }
    return outline;
}

}  // namespace aipdf::pdf
