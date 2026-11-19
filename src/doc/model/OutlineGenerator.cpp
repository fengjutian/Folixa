#include "doc/model/OutlineGenerator.h"

#include <QtGlobal>

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

// 逐级挂树：把「标题 + 层级 + 页码」拼成大纲树。
//
// 为什么单独抽出来：判标题有两条路（读标签树 / 按字高估算），
// 两条路产出的是同一种东西，挂树的规则必须完全一样——
// 否则「同一份文档换条路生成的树层级不一样」这种问题会很难查。
class OutlineTreeBuilder {
public:
    explicit OutlineTreeBuilder(const OutlineGeneratorOptions& options)
        : minLevel_(options.minHeadingLevel > 0 ? options.minHeadingLevel : 1),
          maxDepth_(options.maxDepth > 0 ? options.maxDepth : 1),
          maxTitleLength_(options.maxTitleLength) {}

    // 往树上加一个标题。层级不够、标题为空的会被忽略（不会生成空条目）。
    void add(const QString& title, int level, int pageIndex) {
        if (level <= 0 || level < minLevel_) {
            return;
        }
        QString trimmed = title.trimmed();
        if (trimmed.isEmpty()) {
            // 没有文字的标题生成不了书签（生成了也是空条目）。
            return;
        }
        trimmed = truncateTitle(trimmed, maxTitleLength_);

        // 超过最大层级的标题压到最深的这一层，而不是丢掉：
        // 丢掉标题等于让用户看不到这一节。
        const int effective = qMin(level, maxDepth_);

        // 找父级：把层级不比当前浅的都弹掉，剩下的栈顶就是最近的可用父级。
        // 这同时处理了两种情况：
        //   同层级连续出现 → 弹到上一个同层级之前，成为兄弟；
        //   层级跳跃（1 之后直接 3）→ 栈顶的 1 还留着，3 挂到 1 下面。
        while (!stack_.empty() && stack_.back().first >= effective) {
            stack_.pop_back();
        }

        Node node;
        node.title = trimmed;
        node.pageIndex = pageIndex;
        const int index = static_cast<int>(nodes_.size());
        if (stack_.empty()) {
            node.parent = -1;
            roots_.push_back(index);
        } else {
            node.parent = stack_.back().second;
            nodes_.at(static_cast<size_t>(node.parent)).children.push_back(index);
        }
        nodes_.push_back(node);
        stack_.emplace_back(effective, index);
    }

    std::vector<OutlineItem> finish() {
        std::vector<OutlineItem> outline;
        outline.reserve(roots_.size());
        for (int root : roots_) {
            OutlineItem item;
            expand(nodes_, root, &item);
            outline.push_back(std::move(item));
        }
        return outline;
    }

private:
    std::vector<Node> nodes_;
    std::vector<int> roots_;
    // 层级栈：记录一路走下来的标题层级与它们在 nodes 里的下标。
    // 栈顶永远是"最近的一个可以当爹的标题"。
    std::vector<std::pair<int, int>> stack_;
    int minLevel_ = 1;
    int maxDepth_ = 1;
    int maxTitleLength_ = 120;
};

// 树里一共多少条书签。
int countOutlineItems(const std::vector<OutlineItem>& items) {
    int total = 0;
    for (const OutlineItem& item : items) {
        total += 1 + countOutlineItems(item.children);
    }
    return total;
}

// 这个结构类型是不是标题。
//
// 为什么只认 H1–H6 和 H：PDF 32000-1 的标准结构类型里，标题就是这六个，
// 另有通用标签 /H（层级写在 /A 属性字典里）。
bool isHeadingTag(const QString& tag) {
    if (tag == QLatin1String("H")) {
        return true;
    }
    return tag.size() == 2 && tag.at(0) == QLatin1Char('H') &&
           tag.at(1) >= QLatin1Char('1') && tag.at(1) <= QLatin1Char('6');
}

// 标题层级：标签名说了算。
//
// 返回 0 表示不是标题。
int headingLevelOf(const StructElement& element, int headingAncestors) {
    const QString tag = element.tag;
    if (tag.size() == 2 && tag.at(0) == QLatin1Char('H') &&
        tag.at(1) >= QLatin1Char('1') && tag.at(1) <= QLatin1Char('6')) {
        return tag.at(1).unicode() - '0';
    }
    if (tag == QLatin1String("H")) {
        // 通用 /H：层级写在 /A 属性字典的 /H 里，本仓库的标签树读取器
        // 没有暴露属性字典，拿不到那个值——只能按"在标签树里嵌了几层标题"推。
        return qMin(headingAncestors + 1, 6);
    }
    return 0;
}

// 每个元素有几个标题祖先。
//
// 为什么单独算：通用 /H 元素没有层级属性，只能靠"嵌了几层"推。
// 报告里的元素是先序排列的（父在子前），一趟就够。
QVector<int> headingAncestorsOf(const QVector<StructElement>& elements) {
    QVector<int> ancestors(elements.size(), 0);
    for (int index = 0; index < elements.size(); ++index) {
        const int parent = elements.at(index).parentIndex;
        if (parent < 0 || parent >= index) {
            continue;
        }
        ancestors[index] = ancestors.at(parent) +
                           (isHeadingTag(elements.at(parent).tag) ? 1 : 0);
    }
    return ancestors;
}

// 标题文字：该元素下的文字。
//
// 优先用自己的文字；自己没有就取第一个有文字的后代——
// 很多文档是 H1 → Span 这样，标题文字挂在后代上，
// 只看自己的话整份文档会一个标题都取不到。
QString headingTextOf(const QVector<StructElement>& elements, int index, bool firstLineOnly) {
    const StructElement& element = elements.at(index);
    QString text = element.text;
    if (text.trimmed().isEmpty()) {
        // 后代在先序排列里是连续的一段：深度更深，直到深度回到本层为止。
        for (int next = index + 1; next < elements.size(); ++next) {
            if (elements.at(next).depth <= element.depth) {
                break;
            }
            text = elements.at(next).text;
            if (!text.trimmed().isEmpty()) {
                break;
            }
        }
    }
    text = text.trimmed();
    if (firstLineOnly) {
        text = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    }
    return text;
}

}  // namespace

std::vector<OutlineItem> generateOutlineFromChunks(const QVector<doc::DocumentChunk>& chunks,
                                                   const OutlineGeneratorOptions& options) {
    OutlineTreeBuilder builder(options);
    for (const doc::DocumentChunk& chunk : chunks) {
        // 层级 0 是正文，不是标题。
        if (chunk.headingLevel <= 0) {
            continue;
        }
        builder.add(titleOf(chunk, options.useFirstLineAsTitle), chunk.headingLevel, chunk.pageIndex);
    }
    return builder.finish();
}

OutlineGenerationResult generateOutlineFromStructTree(const StructTreeReport& structTree,
                                                      const OutlineGeneratorOptions& options) {
    OutlineGenerationResult result;
    // 这个入口的约定就是"只读标签树"：它**不会**回退到字高估算，
    // 所以 source 恒为 StructTree，note 只说明读到了什么。
    result.source = OutlineTitleSource::StructTree;
    if (!structTree.hasStructTree) {
        result.note = QStringLiteral("这份文档没有结构标签（/StructTreeRoot）");
        return result;
    }

    const QVector<int> ancestors = headingAncestorsOf(structTree.elements);
    OutlineTreeBuilder builder(options);
    for (int index = 0; index < structTree.elements.size(); ++index) {
        const StructElement& element = structTree.elements.at(index);
        const int level = headingLevelOf(element, ancestors.at(index));
        if (level <= 0) {
            continue;
        }
        const QString title = headingTextOf(structTree.elements, index, options.useFirstLineAsTitle);
        if (title.isEmpty()) {
            // 标题在图里（没有可提取的文字）或者标题是空的：
            // 生成不了书签，也不该拿别的文字顶替。
            continue;
        }
        builder.add(title, level, element.pageIndex);
    }

    result.outline = builder.finish();
    result.headingCount = countOutlineItems(result.outline);
    if (result.headingCount == 0) {
        result.note = QStringLiteral("这份文档有结构标签，但标签树里没有可用的标题元素（H1–H6）");
    }
    return result;
}

OutlineGenerationResult generateOutline(const StructTreeReport& structTree,
                                        const QVector<doc::DocumentChunk>& chunks,
                                        const OutlineGeneratorOptions& options) {
    OutlineGenerationResult result = generateOutlineFromStructTree(structTree, options);
    if (!result.outline.empty()) {
        return result;
    }

    // 标签树里没有可用标题 → 退回字高估算，并把"为什么回退"写清楚。
    // 估算结果**不能**被说成读标签树读出来的：source 与 note 就是给界面用的。
    result.outline = generateOutlineFromChunks(chunks, options);
    result.source = OutlineTitleSource::FontHeuristic;
    result.headingCount = countOutlineItems(result.outline);
    if (!structTree.hasStructTree) {
        result.note = QStringLiteral("本文档没有结构标签，标题是按字高估算的。");
        if (!structTree.error.isEmpty()) {
            result.note += QStringLiteral("（读标签树时出错：%1）").arg(structTree.error);
        }
    } else {
        result.note = QStringLiteral("这份文档有结构标签，但标签树里没有可用的标题元素（H1–H6），"
                                     "标题是按字高估算的。");
    }
    return result;
}

OutlineGenerationResult generateOutline(const QByteArray& pdfBytes,
                                        const QVector<doc::DocumentChunk>& chunks,
                                        const OutlineGeneratorOptions& options) {
#ifdef AIPDF_WITH_QPDF
    // 读标签树要 qpdf；读不到时 readStructTree 会把原因写进 error，
    // 上面的回退逻辑会把它一并带进 note。
    const StructTreeReport report = readStructTree(pdfBytes);
    return generateOutline(report, chunks, options);
#else
    // 当前构建没有启用 qpdf，就**读不了**文档的结构标签：
    // 只能估算，并且如实说明估算的原因（缺依赖，不是文档没标签）。
    OutlineGenerationResult result;
    result.outline = generateOutlineFromChunks(chunks, options);
    result.source = OutlineTitleSource::FontHeuristic;
    result.headingCount = countOutlineItems(result.outline);
    result.note = QStringLiteral("当前构建没有启用 qpdf，读不了文档的结构标签，标题是按字高估算的。");
    return result;
#endif
}

}  // namespace aipdf::pdf
