#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "doc/edit/EditCommand.h"
#include "pdf/ops/CrossMergePlanner.h"

#include <memory>

namespace aipdf::doc {

// 交叉合并时「从外部来源取一页」的入口。
//
// 为什么要有这个接口：命令层不直接碰 PDFium（见 EditContext.h 的分层说明），
// 而「对方文档的第 3 页」在命令层根本拿不到——那份文档归应用层管，
// 它的句柄活在自己的作用域里。命令只负责说清「我要哪份的哪一页」，
// 怎么把那页弄出来由上层实现这个接口。
//
// 实现方要做的事：用自己那份文档的第 sourcePage 页做一份备份
// （app::PdfPageOperations::backupPage 就是现成的），交给命令。
class CrossMergePageSource {
public:
    virtual ~CrossMergePageSource() = default;

    // 备份 sourceId 这份来源的第 sourcePage 页（0 基）。
    // 成功返回 true 并把备份写进 backup；失败返回 false。
    virtual bool backupPage(int sourceId,
                            int sourcePage,
                            std::shared_ptr<PageBackup>* backup) = 0;
};

// 交叉合并：一次交叉 = 一条命令。
//
// 粒度与 ReplacePageCommand 同级——**一次交叉合并是一条记录**，
// 不是每页一条。用户按一次 Ctrl+Z 就该完整回到交叉前，
// 而不是回到「只交叉了一半」的中间态。
//
// 数据形状（三块）：
//   1. **页序描述**（plan_）：由 pdf::planCrossMerge 算出来的最终页序，
//      每个页位说清「这一页来自哪份来源的第几页」。它是纯数据，可序列化。
//   2. **外部来源的取页入口**（source_）：见 CrossMergePageSource。
//      应用层实现它，命令靠它把外面的页搬进来。
//   3. **交叉前的页面备份**（originalPages_）：执行时现拍的一份，
//      撤销全靠它。备份是运行时数据，不参与序列化
//      （与 DeletePageCommand 的备份同一个道理：恢复日志只重放「向前」）。
//
// apply / undo 契约：
//   - apply：先拍下交叉前的每一页 → 清空当前文档的页面 → 按页序计划把页面装回去
//            → 交出「旧页号 → 新页号」的映射（让批注跟着页面走）。
//   - revert：清空当前文档的页面 → 把交叉前拍下的页面按原顺序装回去
//            → 交出「合并页号 → 原页号」的映射。
//   - 装到一半失败时会把交叉前的页面放回去：不能停在半截状态
//     （与 SwapPagesCommand「失败不留半截」同一条原则）。
//   - 两条命令都不碰批注模型本身：批注的页码由 EditContext::pageStructureChanged
//     的映射规则搬（应用层负责重映射），命令不重复搬一次。
//
// 撤销的前提：originalPages_ 非空。它在第一次 apply 时拍下，
// 所以「从恢复日志重放出来的命令」（没有备份）无法撤销——
// 这与 ReplacePageCommand 的处境一样。
class CrossMergeCommand final : public EditCommand {
public:
    CrossMergeCommand() = default;
    CrossMergeCommand(pdf::CrossMergePlan plan,
                      std::shared_ptr<CrossMergePageSource> source);

    QString typeName() const override;
    QString displayName() const override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data, QString* error) override;
    bool apply(EditContext* context) const override;
    bool revert(EditContext* context) const override;

    // 页序计划（最终页序）。
    const pdf::CrossMergePlan& plan() const;
    // 交叉前的页数（执行之后才有值：执行时现拍的）。
    int originalPageCount() const;

private:
    // 拍下交叉前的每一页（撤销要靠它）。
    bool captureOriginalPages(PageOperations* pages) const;
    // 清空当前文档的页面。
    bool clearPages(PageOperations* pages) const;
    // 按计划把页面装回去。
    bool installPlannedPages(PageOperations* pages) const;
    // 把交叉前的页面按原顺序装回去。
    bool restoreOriginalPages(PageOperations* pages) const;
    // 「交叉前的页号 → 交叉后的页号」。
    QVector<int> mappingFromOriginalToPlan() const;
    // 「交叉后的页号 → 交叉前的页号」（mergedCount 是交叉后的页数）。
    QVector<int> mappingFromPlanToOriginal(int mergedCount) const;

    pdf::CrossMergePlan plan_;
    std::shared_ptr<CrossMergePageSource> source_;
    // 交叉前的页面备份。mutable：apply 是 const，但备份要就地生成。
    mutable QVector<std::shared_ptr<PageBackup>> originalPages_;
    // 备份拍过了没有。
    // 为什么不看 originalPages_ 是否为空：交叉前的文档本来就可能一页都没有，
    // 那时备份是「空但已拍」，不能再拍一遍（第二遍拍到的会是合并后的页面）。
    mutable bool captured_ = false;
};

}  // namespace aipdf::doc
