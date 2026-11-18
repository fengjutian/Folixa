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

// 路径 → 该路径上的节点所在的"孩子列表"。
// 路径为空 → 顶层列表本身（相当于虚拟根节点的孩子列表）。
// 路径上任何一格越界返回 nullptr。
std::vector<OutlineItem>* childListAt(std::vector<OutlineItem>* items,
                                       const std::vector<int>& path) {
    if (items == nullptr) {
        return nullptr;
    }
    if (path.empty()) {
        return items;
    }
    std::vector<OutlineItem>* level = items;
    for (std::size_t depth = 0; depth < path.size(); ++depth) {
        if (path[depth] < 0 || path[depth] >= static_cast<int>(level->size())) {
            return nullptr;
        }
        level = &(*level)[path[depth]].children;
    }
    return level;
}

// 路径 a 是不是路径 b 的前缀（b 以 a 开头）。
bool isPrefixOf(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() > b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
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

bool OutlineEditor::reparent(std::vector<OutlineItem>* items, int flatIndex,
                             const std::vector<int>& newParentPath, int insertIndex) {
    if (items == nullptr || flatIndex < 0) {
        return false;
    }

    std::vector<int> movedPath;
    if (!pathAt(*items, flatIndex, &movedPath) || movedPath.empty()) {
        return false;
    }

    // 成环检查：新父节点是自己或自己的后代时搬过去会让树变成环。
    // 后代路径一定以自己的路径开头（多出来的几格是往下的层数），
    // 所以只需比对前缀。
    if (isPrefixOf(movedPath, newParentPath)) {
        return false;
    }

    // 拆成"它住在哪个列表里"和"它在那个列表里的第几格"。
    const std::vector<int> sourcePath(movedPath.begin(), movedPath.end() - 1);
    const int sourceIndex = movedPath.back();

    // 目标父节点的路径要按"摘除"调整一次：被摘掉的书签会让同一层里
    // 排在它后面的兄弟整体前移一格。
    //
    // 只有当目标路径与摘除发生在**同一条链**上时，目标路径上对应
    // 那一层的下标才会跟着动；否则两棵树各在各的层，互不影响。
    std::vector<int> targetPath = newParentPath;
    const int sourceDepth = static_cast<int>(sourcePath.size());
    if (sourceDepth < static_cast<int>(targetPath.size()) &&
        isPrefixOf(sourcePath, targetPath)) {
        if (targetPath[sourceDepth] > sourceIndex) {
            targetPath[sourceDepth] -= 1;
        }
    }

    // 插入位置同理：只有当"要插入的那个列表"正是"被摘除的那个列表"
    // （也就是搬到自己原来的父节点底下）时，下标才会前移。
    int targetIndex = insertIndex;
    if (sourceDepth == static_cast<int>(targetPath.size()) && sourcePath == targetPath &&
        targetIndex > sourceIndex) {
        targetIndex -= 1;
    }

    // 摘除：搬的是**整条书签**，连同它自己的子树一起搬。
    std::vector<OutlineItem>* source = childListAt(items, sourcePath);
    if (source == nullptr || sourceIndex < 0 || sourceIndex >= static_cast<int>(source->size())) {
        return false;
    }
    OutlineItem moved = std::move((*source)[sourceIndex]);
    source->erase(source->begin() + sourceIndex);

    // 目标列表要在摘除**之后**再解析：摘除会把同层后面的兄弟往前挪，
    // 提前拿到的指针会指向已经搬走的地方。
    std::vector<OutlineItem>* target = childListAt(items, targetPath);
    if (target == nullptr) {
        // 目标解析不出来：放回原位，树保持原样。
        source->insert(source->begin() + sourceIndex, std::move(moved));
        return false;
    }
    if (targetIndex < 0 || targetIndex > static_cast<int>(target->size())) {
        target->push_back(std::move(moved));
    } else {
        target->insert(target->begin() + targetIndex, std::move(moved));
    }
    return true;
}

}  // namespace aipdf::pdf
