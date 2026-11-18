#pragma once

#include "pdf/kernel/Navigation.h"

#include <vector>

namespace aipdf::pdf {

// 大纲（书签）树的结构编辑：排序与层级调整。
//
// 为什么抽到 pdf 层：这些操作本来就是**纯数据操作**——
// 输入一棵 std::vector<OutlineItem>，输出同一棵树的新形态，
// 不碰控件、不碰文档句柄、不碰文件。
// 之前它们整段埋在 app/shell/MainWindow.cpp 的文件内静态 helper 里，代价有两个：
// 一是没法单测（改一行树逻辑就得把整个界面拉起来），
// 二是 app 层替 pdf 层背了树算法，pdf 层自己反而没有这层能力。
// 抽到这里之后，pdf 层能自己证明"层级编辑真的能写进 PDF"，
// app 层只剩"选中哪一行、怎么提示用户"。
//
// 术语：这里的"行号"指**摊平后**的行号，顺序与 flattenOutline 一致
//（先父后子、深度优先）。界面里的书签面板就是按这个顺序一行一条列的，
// 所以行号能直接和界面上的那一行对上。
class OutlineEditor final {
public:
    // 按摊平行号定位节点。行号越界或 items 为空时返回 nullptr。
    // 返回的是树里的真实指针，树一旦被改动它就可能失效——用完即弃。
    static OutlineItem* itemAt(std::vector<OutlineItem>* items, int flatIndex);

    // 删除摊平后的第 flatIndex 个节点（连同它的整棵子树）。
    // 行号越界返回 false，树保持原样。
    static bool removeAt(std::vector<OutlineItem>* items, int flatIndex);

    // 同级换位：上移 / 下移。
    // 只能在**同一层**的兄弟之间换位——把子节点提到父节点前面会破坏树，
    // 所以跨层级一律不动。
    // 边界：本层第一个节点上移返回 false；本层最后一个节点下移返回 false。
    static bool moveUp(std::vector<OutlineItem>* items, int flatIndex);
    static bool moveDown(std::vector<OutlineItem>* items, int flatIndex);

    // 降级：变成**前一个同级节点**的子节点（追加到它的子节点末尾）。
    // 边界：本层第一个节点没有"前一个兄弟"，返回 false。
    static bool indent(std::vector<OutlineItem>* items, int flatIndex);

    // 升级：变成父节点的同级，插在父节点**后面**。
    // 边界：本身就在顶层（没有父节点）时返回 false。
    static bool outdent(std::vector<OutlineItem>* items, int flatIndex);

    // 路径：从根到目标的下标路径（例如 {1,0} 表示"第 2 个顶层节点的第 1 个子节点"）。
    // 进入时 path 会被清空重填；行号越界返回 false，path 保持为空。
    static bool pathAt(const std::vector<OutlineItem>& items, int flatIndex, std::vector<int>* path);

    // 换父节点：把摊平后的第 flatIndex 个节点搬到 newParentPath 节点下面。
    //
    // newParentPath 是**新父节点**的路径；空路径表示搬到顶层（追加到顶层列表末尾）。
    // insertIndex 指定在新父节点（或顶层）这一层里的插入位置；-1 表示追加到末尾。
    //
    // 边界：
    //   - 不能搬进自己的子树（那会成环）：新父节点是自己或自己的后代时返回 false；
    //   - 路径不存在时返回 false，树保持原样。
    //   - 搬的是**整条书签**：连同它自己的子树一起搬。
    static bool reparent(std::vector<OutlineItem>* items,
                         int flatIndex,
                         const std::vector<int>& newParentPath,
                         int insertIndex = -1);

    // 层级：顶层 0、子节点 1、孙节点 2……行号越界返回 -1。
    static int levelAt(const std::vector<OutlineItem>& items, int flatIndex);

    // 路径所在的**容器**（顶层容器就是 items 本身）。
    // 路径越界或为空返回 nullptr。
    static std::vector<OutlineItem>* containerAt(std::vector<OutlineItem>* items,
                                                 const std::vector<int>& path);

    // 路径 → 摊平后的行号。
    // 结构一改行号就变，界面想保持选中项就得重新算一次。
    // 路径无效时返回 -1。
    static int rowOfPath(const std::vector<OutlineItem>* items, const std::vector<int>& path);

    // 一个节点连同它所有后代，一共多少条。
    static int subtreeSize(const OutlineItem* item);
};

}  // namespace aipdf::pdf
