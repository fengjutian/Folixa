#include "pdf/OutlineEditor.h"

#include <algorithm>
#include <utility>

namespace aipdf::pdf {

namespace {

// 摊平定位用的递归：cursor 是"已经走过几条"的计数器。
// 遍历顺序必须与 flattenOutline 一致（先父后子、深度优先），
// 否则这里数出来的行号和界面上的行号会对不上。
OutlineItem* itemAtCursor(std::vector<OutlineItem>* items, int target, int* cursor) {
    if (items == nullptr || cursor == nullptr) {
        return nullptr;
    }
    for (OutlineItem& item : *items) {
        if ((*cursor)++ == target) {
            return &item;
        }
        if (OutlineItem* found = itemAtCursor(&item.children, target, cursor)) {
            return found;
        }
    }
    return nullptr;
}

bool removeAtCursor(std::vector<OutlineItem>* items, int target, int* cursor) {
    if (items == nullptr || cursor == nullptr) {
        return false;
    }
    for (auto it = items->begin(); it != items->end(); ++it) {
        if ((*cursor)++ == target) {
            items->erase(it);
            return true;
        }
        if (removeAtCursor(&it->children, target, cursor)) {
            return true;
        }
    }
    return false;
}

// 路径就是把"每层里的第几个"依次记下来。
// 递归失败时把自己压进去的那一格弹掉，所以失败时 path 会自动回到空。
bool pathAtCursor(const std::vector<OutlineItem>& items, int target, int* cursor,
                  std::vector<int>* path) {
    if (cursor == nullptr || path == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        path->push_back(static_cast<int>(i));
        if ((*cursor)++ == target) {
            return true;
        }
        if (pathAtCursor(items[i].children, target, cursor, path)) {
            return true;
        }
        path->pop_back();
    }
    return false;
}

}  // namespace

OutlineItem* OutlineEditor::itemAt(std::vector<OutlineItem>* items, int flatIndex) {
    if (items == nullptr || flatIndex < 0) {
        return nullptr;
    }
    int cursor = 0;
    return itemAtCursor(items, flatIndex, &cursor);
}

bool OutlineEditor::removeAt(std::vector<OutlineItem>* items, int flatIndex) {
    if (items == nullptr || flatIndex < 0) {
        return false;
    }
    int cursor = 0;
    return removeAtCursor(items, flatIndex, &cursor);
}

bool OutlineEditor::pathAt(const std::vector<OutlineItem>& items, int flatIndex,
                           std::vector<int>* path) {
    if (path == nullptr || flatIndex < 0) {
        return false;
    }
    path->clear();
    int cursor = 0;
    return pathAtCursor(items, flatIndex, &cursor, path);
}

int OutlineEditor::levelAt(const std::vector<OutlineItem>& items, int flatIndex) {
    std::vector<int> path;
    if (!pathAt(items, flatIndex, &path) || path.empty()) {
        return -1;
    }
    // 路径长度就是它有多深：顶层 1 格 → 0 层。
    return static_cast<int>(path.size()) - 1;
}

std::vector<OutlineItem>* OutlineEditor::containerAt(std::vector<OutlineItem>* items,
                                                     const std::vector<int>& path) {
    if (items == nullptr || path.empty()) {
        return nullptr;
    }
    std::vector<OutlineItem>* level = items;
    // 路径的最后一位是"目标在这一层里的下标"，
    // 它之前的那几格决定的是"目标待在哪个容器里"。
    for (std::size_t depth = 0; depth + 1 < path.size(); ++depth) {
        if (path[depth] < 0 || path[depth] >= static_cast<int>(level->size())) {
            return nullptr;
        }
        level = &(*level)[path[depth]].children;
    }
    return level;
}

int OutlineEditor::subtreeSize(const OutlineItem* item) {
    if (item == nullptr) {
        return 0;
    }
    int size = 0;
    for (const OutlineItem& child : item->children) {
        size += 1 + subtreeSize(&child);
    }
    return size;
}

int OutlineEditor::rowOfPath(const std::vector<OutlineItem>* items, const std::vector<int>& path) {
    if (items == nullptr || path.empty()) {
        return -1;
    }
    const std::vector<OutlineItem>* level = items;
    int row = 0;
    for (std::size_t depth = 0; depth < path.size(); ++depth) {
        const int index = path[depth];
        if (index < 0 || index >= static_cast<int>(level->size())) {
            return -1;
        }
        // 走到这一条之前，前面的兄弟连同它们的子树都要占行。
        for (int i = 0; i < index; ++i) {
            row += 1 + subtreeSize(&(*level)[i]);
        }
        if (depth + 1 == path.size()) {
            return row;
        }
        level = &(*level)[index].children;
    }
    return row;
}

bool OutlineEditor::moveUp(std::vector<OutlineItem>* items, int flatIndex) {
    if (items == nullptr) {
        return false;
    }
    std::vector<int> path;
    if (!pathAt(*items, flatIndex, &path) || path.empty()) {
        return false;
    }
    std::vector<OutlineItem>* level = containerAt(items, path);
    if (level == nullptr) {
        return false;
    }
    const int index = path.back();
    // 本层第一个：上面没有兄弟可换。
    if (index <= 0) {
        return false;
    }
    std::swap((*level)[index - 1], (*level)[index]);
    return true;
}

bool OutlineEditor::moveDown(std::vector<OutlineItem>* items, int flatIndex) {
    if (items == nullptr) {
        return false;
    }
    std::vector<int> path;
    if (!pathAt(*items, flatIndex, &path) || path.empty()) {
        return false;
    }
    std::vector<OutlineItem>* level = containerAt(items, path);
    if (level == nullptr) {
        return false;
    }
    const int index = path.back();
    // 本层最后一个：下面没有兄弟可换。
    if (index < 0 || index + 1 >= static_cast<int>(level->size())) {
        return false;
    }
    std::swap((*level)[index], (*level)[index + 1]);
    return true;
}

bool OutlineEditor::indent(std::vector<OutlineItem>* items, int flatIndex) {
    if (items == nullptr) {
        return false;
    }
    std::vector<int> path;
    if (!pathAt(*items, flatIndex, &path) || path.empty()) {
        return false;
    }
    std::vector<OutlineItem>* level = containerAt(items, path);
    if (level == nullptr) {
        return false;
    }
    const int index = path.back();
    // 本层第一个：前面没有兄弟可以当父级。
    if (index <= 0) {
        return false;
    }
    // 连同它自己的子树一起搬：降级的是整条书签，不只是标题。
    OutlineItem item = (*level)[index];
    (*level)[index - 1].children.push_back(std::move(item));
    level->erase(level->begin() + index);
    return true;
}

bool OutlineEditor::outdent(std::vector<OutlineItem>* items, int flatIndex) {
    if (items == nullptr) {
        return false;
    }
    std::vector<int> path;
    if (!pathAt(*items, flatIndex, &path) || path.empty()) {
        return false;
    }
    // 已经在顶层：没有父节点可以"升"出去。
    if (path.size() < 2) {
        return false;
    }
    const std::vector<int> parentPath(path.begin(), path.end() - 1);
    std::vector<OutlineItem>* grand = containerAt(items, parentPath);
    if (grand == nullptr) {
        return false;
    }
    const int parentIndex = parentPath.back();
    if (parentIndex < 0 || parentIndex >= static_cast<int>(grand->size())) {
        return false;
    }
    OutlineItem& parent = (*grand)[parentIndex];
    const int itemIndex = path.back();
    if (itemIndex < 0 || itemIndex >= static_cast<int>(parent.children.size())) {
        return false;
    }
    // 先摘出来，再插到父节点后面——插在父节点之后而不是之前，
    // 这样"升上去"的书签仍然排在它原来那一章的后面，读起来不倒序。
    OutlineItem item = parent.children[itemIndex];
    parent.children.erase(parent.children.begin() + itemIndex);
    grand->insert(grand->begin() + parentIndex + 1, std::move(item));
    return true;
}

}  // namespace aipdf::pdf
