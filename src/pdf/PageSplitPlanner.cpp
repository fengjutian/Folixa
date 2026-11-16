#include "pdf/PageSplitPlanner.h"

#include <algorithm>

namespace aipdf::pdf {

QVector<PageRange> PageSplitPlanner::byCount(int pageCount, int pagesPerFile) {
    QVector<PageRange> ranges;
    if (pageCount <= 0 || pagesPerFile <= 0) {
        return ranges;
    }
    // 末段不足 pagesPerFile 时按实际页数算，不补空段。
    for (int start = 0; start < pageCount; start += pagesPerFile) {
        ranges.append(PageRange{start, std::min(pageCount, start + pagesPerFile)});
    }
    return ranges;
}

QVector<PageRange> PageSplitPlanner::byOutline(int pageCount, const std::vector<OutlineItem>& outline) {
    QVector<PageRange> ranges;
    if (pageCount <= 0) {
        return ranges;
    }
    QVector<int> starts;
    for (const OutlineItem& item : outline) {
        // pageIndex 为 -1 表示目标解析不出来；越界的目标同样不能当起点。
        if (item.pageIndex >= 0 && item.pageIndex < pageCount) {
            starts.append(item.pageIndex);
        }
    }
    // 去重并升序：多个书签可能指向同一页，那一页只能归到第一段里。
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    for (int i = 0; i < starts.size(); ++i) {
        const int start = starts.at(i);
        // 中间段到下一个书签之前；最后一段一直到文档末尾。
        const int end = (i + 1 < starts.size()) ? starts.at(i + 1) : pageCount;
        if (end <= start) {
            // 空段没有意义（去重升序之后不会出现，留着是为了不依赖那个前提）。
            continue;
        }
        ranges.append(PageRange{start, end});
    }
    return ranges;
}

QVector<PageRange> PageSplitPlanner::bySize(const QVector<qint64>& pageSizes, qint64 limitBytes) {
    QVector<PageRange> ranges;
    const int count = pageSizes.size();
    if (count <= 0) {
        return ranges;
    }
    int start = 0;
    qint64 accumulated = 0;
    for (int page = 0; page < count; ++page) {
        const qint64 pageSize = pageSizes.at(page);
        // 当前段里已经有页，而且再加一页就超限 → 先把当前段落盘，再开新段。
        // 单页就超限时不能把它切两半（页本来就不可分），所以它自成一档。
        if (page > start && accumulated + pageSize > limitBytes) {
            ranges.append(PageRange{start, page});
            start = page;
            accumulated = 0;
        }
        accumulated += pageSize;
    }
    if (start < count) {
        ranges.append(PageRange{start, count});
    }
    return ranges;
}

QVector<PageRange> PageSplitPlanner::extract(int pageCount, int firstPage, int lastPage) {
    QVector<PageRange> ranges;
    if (pageCount <= 0) {
        return ranges;
    }
    // 1 基的页号夹到 [1, pageCount]：用户填 0 或 999 都不该让程序崩。
    const int first = std::clamp(firstPage, 1, pageCount);
    const int last = std::clamp(lastPage, 1, pageCount);
    if (first > last) {
        return ranges;
    }
    // 1 基含两端 → 0 基半开区间。
    ranges.append(PageRange{first - 1, last});
    return ranges;
}

}  // namespace aipdf::pdf
