#include "pdf/ops/CrossMergePlanner.h"

#include <QStringList>

#include <algorithm>

namespace aipdf::pdf {

namespace {

// 某个来源在页序里的名字：优先用调用方给的 label，没有就用「源<id>」。
QString sourceLabel(int sourceId, const QVector<CrossMergeSource>& sources) {
    if (sourceId == kCrossMergeCurrentSource) {
        return QStringLiteral("当前");
    }
    for (const CrossMergeSource& source : sources) {
        if (source.id == sourceId) {
            return source.label.isEmpty() ? QStringLiteral("源%1").arg(source.id) : source.label;
        }
    }
    return QStringLiteral("源%1").arg(sourceId);
}

}  // namespace

CrossMergePlan planCrossMerge(int currentPageCount, const QVector<CrossMergeSource>& sources) {
    CrossMergePlan plan;
    // 页数不允许为负：负数当成 0（界面上的页数来自 PDFium，不会是负的，
    // 但这是纯函数，边界要自己兜住）。
    const int current = std::max(0, currentPageCount);

    int rounds = current;
    for (const CrossMergeSource& source : sources) {
        rounds = std::max(rounds, std::max(0, source.pageCount));
    }
    plan.rounds = rounds;
    // 下标 0 是当前文档，下标 i+1 是第 i 个外部来源（按传入顺序）。
    plan.takenPerSource.assign(sources.size() + 1, 0);

    // 以页号为主序、来源为次序：外层是页号（轮次），内层是来源。
    for (int round = 0; round < rounds; ++round) {
        // 当前文档永远排这一轮的第一位（与旧实现一致）。
        if (round < current) {
            plan.pageSlots.append(CrossMergeSlot{kCrossMergeCurrentSource, round});
            plan.takenPerSource[0] += 1;
        }
        for (int index = 0; index < sources.size(); ++index) {
            const int count = std::max(0, sources[index].pageCount);
            // 这一份的页数不够了：这一轮跳过它（不截断整轮）。
            if (round >= count) {
                continue;
            }
            plan.pageSlots.append(CrossMergeSlot{sources[index].id, round});
            plan.takenPerSource[index + 1] += 1;
        }
    }
    return plan;
}

QString describeCrossMergePlan(const CrossMergePlan& plan,
                                const QVector<CrossMergeSource>& sources) {
    QStringList parts;
    parts.reserve(plan.pageSlots.size());
    for (const CrossMergeSlot& slot : plan.pageSlots) {
        parts.append(QStringLiteral("%1#%2").arg(sourceLabel(slot.sourceId, sources),
                                                 QString::number(slot.sourcePage)));
    }
    return parts.join(QStringLiteral(", "));
}

}  // namespace aipdf::pdf
