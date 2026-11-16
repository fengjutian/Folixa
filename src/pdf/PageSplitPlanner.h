#pragma once

#include "pdf/Navigation.h"

#include <QVector>

#include <vector>

namespace aipdf::pdf {

// 一段页区间：[first, last) 半开区间，页序号从 0 开始。
//
// 为什么用半开区间：PDFium 的页面操作（导入、抽取）用的就是
// "第几页到第几页之前"这种说法，半开区间在拼接时不用再做 -1 的换算，
// 少一个出错的地方。
struct PageRange {
    int first = 0;
    int last = 0;
    int count() const { return last - first; }
};

// 拆分口径的**纯计算**：只回答"从哪里断、每段到哪儿结束"。
//
// 为什么抽到 pdf 层：界面层原本有三个各自写着循环的拆分对话框
//（每 N 页 / 按书签 / 按体积），每个循环都在做同一件事——把页序列切成若干段。
// 切法是纯计算，不该埋在对话框里：既没法单测，
// 三处各写一遍也迟早会写出三种不一样的边界。
// 抽到这里之后，界面层只负责选参数、拼文件名、写文件。
class PageSplitPlanner final {
public:
    // 每 N 页一份。
    //
    // 边界：pageCount <= 0 或 pagesPerFile <= 0 返回空列表（无效输入，
    // 界面上的输入框本来就限死了下限 1）。末段不足 N 页时按实际页数算：
    // 10 页每 3 页 → 4 段，最后一段只有 1 页。
    static QVector<PageRange> byCount(int pageCount, int pagesPerFile);

    // 按书签：书签指向的页作为各段起点，去重升序；最后一段到结尾。
    //
    // 只看**顶层**书签：子书签是章节里的小节，按它拆会切出一堆碎片
    //（顶层书签才是"章"的粒度）。调用方传进来的就是顶层那一层。
    // pageIndex 为 -1（目标解析不出来）或越界的书签不参与分段。
    //
    // 边界：
    //   - 没有任何有效书签 → 空列表（没有可依据的起点）；
    //   - 只有一个有效起点 → 只出 1 段；起点之前的那几页不在任何一段里
    //     （与界面层现有行为一致）；
    //   - 界面层现有"书签少于两个就不拆"这条判断仍然要由调用方做：
    //     段数少于 2 时应该提示用户，而不是默默拆出一份。
    static QVector<PageRange> byOutline(int pageCount, const std::vector<OutlineItem>& outline);

    // 按体积：pageSizes[i] 是第 i 页单独序列化后的字节数，超过 limit 就断开。
    //
    // 单页就超过上限时不会把这一页切两半（PDF 页本来就不可分），
    // 它会自成一个文件——所以"超限就断"的前提是当前段里已经有页了。
    // limitBytes <= 0 时每页自成一档。
    static QVector<PageRange> bySize(const QVector<qint64>& pageSizes, qint64 limitBytes);

    // 提取范围（1 基的起止页，含两端）→ 0 基半开区间。
    //
    // 边界：越界会夹到 [1, pageCount]（用户填 0 或 999 不该让程序崩）；
    // 夹完之后 first > last 返回空列表。
    static QVector<PageRange> extract(int pageCount, int firstPage, int lastPage);
};

}  // namespace aipdf::pdf
