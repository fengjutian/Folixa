#include "pdf/compliance/StructTreeModel.h"

namespace aipdf::pdf {

int appendSemanticNode(SemanticTree* tree, const SemanticNode& node) {
    if (tree == nullptr) return -1;
    const int index = tree->nodes.size();
    tree->nodes.append(node);
    return index;
}

void appendSemanticChild(SemanticTree* tree, int parentIndex, int childIndex) {
    if (tree == nullptr) return;
    if (parentIndex < 0 || parentIndex >= tree->nodes.size()) return;
    if (childIndex < 0 || childIndex >= tree->nodes.size()) return;
    if (parentIndex == childIndex) return;
    tree->nodes[parentIndex].childIndices.append(childIndex);
    tree->nodes[childIndex].parentIndex = parentIndex;
}

QVector<int> semanticReadingOrder(const SemanticTree& tree) {
    QVector<int> order;
    if (tree.rootIndex < 0 || tree.rootIndex >= tree.nodes.size()) return order;

    // 显式栈的先序遍历：深度可能很大（每页一个 Part），递归有爆栈风险。
    QVector<QPair<int, int>> stack;  // (节点下标, 下一个要处理的子节点下标)
    stack.append({tree.rootIndex, 0});
    order.append(tree.rootIndex);

    // 环与超深都要挡住：语义树是自己拼的，但外部也可能塞进来一棵坏树。
    constexpr int kMaxDepth = 256;
    while (!stack.isEmpty()) {
        QPair<int, int>& top = stack.last();
        const int nodeIndex = top.first;
        const int childIndex = top.second;
        if (childIndex >= tree.nodes.at(nodeIndex).childIndices.size()) {
            stack.removeLast();
            continue;
        }
        ++top.second;
        const int child = tree.nodes.at(nodeIndex).childIndices.at(childIndex);
        if (child < 0 || child >= tree.nodes.size()) continue;
        if (stack.size() > kMaxDepth) continue;
        order.append(child);
        stack.append({child, 0});
    }
    return order;
}

QString semanticReadingText(const SemanticTree& tree, const QString& separator) {
    QStringList pieces;
    const QVector<int> order = semanticReadingOrder(tree);
    for (int index : order) {
        if (index < 0 || index >= tree.nodes.size()) continue;
        const QString text = tree.nodes.at(index).text.trimmed();
        if (!text.isEmpty()) pieces.append(text);
    }
    return pieces.join(separator);
}

QVector<int> semanticNodesWithRole(const SemanticTree& tree, StructRole role) {
    QVector<int> result;
    for (int i = 0; i < tree.nodes.size(); ++i) {
        if (tree.nodes.at(i).role == role) result.append(i);
    }
    return result;
}

}  // namespace aipdf::pdf
