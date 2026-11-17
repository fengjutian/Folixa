#include "doc/PrintBatch.h"

#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

namespace aipdf::doc {

namespace {

// 单项份数上限。
//
// 为什么有上限：份数是用户随手填的字段，一份 100000 份的队列会先把
// 界面卡死，而真正打印时打印机也不会照这个数打。夹到上限并记一条
// warning，比让上层自己去挡这个数字可靠。
constexpr int kMaxCopies = 999;

// 生成一个还没用过的 id。
//
// 为什么还要去重：调用方给的 id 未必唯一（同一份文件加了两次就是同一个 id），
// 而报告里靠 id 指认任务，撞了就指认不出来了。
QString uniqueId(const QString& candidate, QSet<QString>& used) {
    QString id = candidate;
    int suffix = 2;
    while (used.contains(id)) {
        id = candidate + QStringLiteral("#%1").arg(suffix);
        ++suffix;
    }
    used.insert(id);
    return id;
}

// 展开后的任务 id。
//
// 原始 id 为空时按份生成新的 UUID；有 id 且只有一份时原样保留
// （调用方要能拿它对回自己的数据），多份时加 "#份号" 后缀。
QString expandedId(const QString& baseId, int copies, int copyIndex) {
    if (baseId.isEmpty()) {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (copies <= 1) {
        return baseId;
    }
    return baseId + QStringLiteral("#%1").arg(copyIndex + 1);
}

}  // namespace

PrintBatchPlan planPrintBatch(const QVector<PrintBatchItem>& items,
                              const PrintBatchOptions& options,
                              QString* error) {
    PrintBatchPlan plan;

    const auto fail = [&plan, error](const QString& reason) {
        plan.ok = false;
        plan.error = reason;
        if (error != nullptr) {
            *error = reason;
        }
        return plan;
    };

    if (items.isEmpty()) {
        return fail(QStringLiteral("打印队列为空，没有可打印的文件"));
    }
    if (options.combineIntoOne && options.outputPath.trimmed().isEmpty()) {
        // 合并打印的产物要有地方落：没有输出路径就没法合并，
        // 与其打出一堆没有下文的页，不如当场说清楚。
        return fail(QStringLiteral("合并打印必须指定输出路径"));
    }

    QSet<QString> usedIds;
    for (int index = 0; index < items.size(); ++index) {
        const PrintBatchItem& item = items.at(index);

        if (item.filePath.trimmed().isEmpty()) {
            // 这一项跳过，但整批继续：一项填错不该把整批打印废掉。
            plan.warnings.append(QStringLiteral("第 %1 项没有文件路径，已跳过").arg(index + 1));
            continue;
        }

        int copies = qMax(1, item.copies);
        if (copies != item.copies) {
            plan.warnings.append(QStringLiteral("第 %1 项的份数 %2 无效，按 1 份处理")
                                     .arg(index + 1)
                                     .arg(item.copies));
        }
        if (copies > kMaxCopies) {
            plan.warnings.append(QStringLiteral("第 %1 项的份数 %2 超过上限 %3，已按上限处理")
                                     .arg(index + 1)
                                     .arg(item.copies)
                                     .arg(kMaxCopies));
            copies = kMaxCopies;
        }

        const QString baseId = item.id.trimmed();
        for (int copy = 0; copy < copies; ++copy) {
            PrintBatchItem expandedItem = item;
            // 展开后的每一项就是一份，份数归一，界面读起来才不会看到 3 份的项。
            expandedItem.copies = 1;
            expandedItem.id = uniqueId(expandedId(baseId, copies, copy), usedIds);
            plan.expanded.append(expandedItem);
        }
    }

    // 这一层不碰磁盘：文件存不存在由真正执行打印的那一层去查，
    // 那里才能给出「打不开」这种带文件名的错误。
    plan.totalJobs = plan.expanded.size();
    plan.totalPages = -1;
    plan.ok = true;
    if (error != nullptr) {
        error->clear();
    }
    return plan;
}

}  // namespace aipdf::doc
