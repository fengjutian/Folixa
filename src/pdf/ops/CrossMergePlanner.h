#pragma once

#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 当前文档在交叉页序里的固定标识。
//
// 为什么用 -1：交叉合并的每一轮里，当前文档都排**第一位**
// （与旧实现一致），外部来源用调用方自己的编号（0、1、2……）。
// -1 不会与它们撞号，命令层也能靠它一眼分清「这一页是我的」还是
// 「这一页是从外面搬进来的」。
inline constexpr int kCrossMergeCurrentSource = -1;

// 一份交叉来源。
struct CrossMergeSource {
    // 来源标识：外部来源用它区分彼此，命令层靠它向应用层要页。
    // 同一次交叉合并里的各个 id 必须互不相同。
    int id = 0;
    // 这份来源有多少页。
    int pageCount = 0;
    // 人看的名字（一般是文件名）。只用于日志与界面文案，不参与计算。
    QString label;
};

// 页序里的一个页位：最终的第 N 页来自谁。
struct CrossMergeSlot {
    // 来源标识：kCrossMergeCurrentSource 表示当前文档，其余是来源自己的 id。
    int sourceId = kCrossMergeCurrentSource;
    // 该来源里的页号（0 基）。
    int sourcePage = 0;
};

// 一次交叉合并的页序计划（纯计算的结果）。
//
// 为什么单独一个类型：页序是「算出来的东西」，与「谁来搬页」无关。
// 界面层只负责把选中的文件变成来源列表、把计划变成搬运动作；
// 页序本身可以单独单测，不用开 PDF、不用碰界面。
struct CrossMergePlan {
    // 最终页序：pageSlots[i] 就是合并结果的第 i 页。
    //
    // 为什么不叫 slots：Qt 把 slots 定成了宏（qobjectdefs.h 里展开成空），
    // 成员名撞上它会连编译都过不去。
    QVector<CrossMergeSlot> pageSlots;
    // 每个来源实际被取了几页。
    // 长度 = 外部来源数 + 1：下标 0 是当前文档，下标 i+1 是第 i 个外部来源
    //（下标按**传入顺序**，不是按来源自己的 id）。
    QVector<int> takenPerSource;
    // 总轮数：所有来源页数的最大值。
    int rounds = 0;
    // 合并后的总页数。
    int totalPages() const { return pageSlots.size(); }
};

// 交叉合并的页序计算（纯函数：不碰 UI、不碰 PDFium、不读文件）。
//
// 规则：**以页号为主序、来源为次序**。
// 第 1 轮是「当前文档第 1 页、来源 1 第 1 页、来源 2 第 1 页……」，
// 第 2 轮是「当前文档第 2 页、来源 1 第 2 页……」，
// 某个来源的页数不够就在那一轮跳过它，总轮数取所有来源页数的最大值。
//
// 例：当前文档 3 页，来源 A 有 2 页，来源 B 有 3 页
//   → 当前#0, A#0, B#0, 当前#1, A#1, B#1, 当前#2, B#2（共 8 页）
//
// 这条规则是旧实现（「当前文档 ↔ 一份文件」交叉）原样推广过来的：
// 只有一个来源时，本函数算出的页序与旧实现逐项相同
// （当前 3 页 + 来源 2 页 → 当前#0, 来源#0, 当前#1, 来源#1, 当前#2）。
//
// 边界：
//   - 外部来源列表为空 → 只有当前文档的页，顺序不变（等于不做交叉）；
//   - 来源页数为 0 → 这一份不参与，页数不够的轮次直接跳过它；
//   - 当前文档页数为 0 → 页序从第一个来源开始；
//   - 两边都是 0 → 空页序（不会崩、不会越界）。
CrossMergePlan planCrossMerge(int currentPageCount, const QVector<CrossMergeSource>& sources);

// 把页序计划写成人能读的一行（「当前#0, A#0, B#0, …」）。
//
// 用途：状态栏提示与测试断言都用它——
// 「最终第几页来自哪一份的第几页」这件事，写成人话比对着数组读容易得多。
QString describeCrossMergePlan(const CrossMergePlan& plan,
                                const QVector<CrossMergeSource>& sources);

}  // namespace aipdf::pdf
