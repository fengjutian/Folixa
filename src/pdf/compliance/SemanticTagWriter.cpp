#include "pdf/compliance/SemanticTagWriter.h"

#include "i18n/Translate.h"

#include "pdf/compliance/TagTreeWriter.h"

#include <QHash>

namespace aipdf::pdf {

SemanticWriteResult writeSemanticStructure(const QByteArray& pdfBytes,
                                           const QVector<SemanticBlock>& blocks,
                                           const SemanticTree& tree,
                                           const SemanticWriteOptions& options) {
    SemanticWriteResult result;
    result.tree = tree;
    result.issues = tree.issues;

    if (pdfBytes.isEmpty()) {
        result.error = i18n::tr("aipdf::pdf::SemanticTagWriter", "输入的 PDF 内容为空");
        return result;
    }
    if (tree.isEmpty()) {
        result.error = i18n::tr("aipdf::pdf::SemanticTagWriter", "语义树是空的，没有可写的结构元素");
        return result;
    }

    // ---- 1. 块 → 每页的「页内块序号 → 文本运行」 ----
    //
    // 为什么要有「页内块序号」：写手按页处理，一页里的块从 0 重新编号。
    // 结构元素上的块下标也用这套编号，两边才对得上。
    QVector<QVector<QVector<int>>> pageBlockRuns;
    QHash<int, int> localIndexOfBlock;  // 整份 blocks 的下标 → 页内块序号
    QHash<int, int> pageOfBlock;        // 整份 blocks 的下标 → 页序号
    for (int i = 0; i < blocks.size(); ++i) {
        const SemanticBlock& block = blocks.at(i);
        if (block.pageIndex < 0) continue;
        while (pageBlockRuns.size() <= block.pageIndex) {
            pageBlockRuns.append(QVector<QVector<int>>());
        }
        QVector<QVector<int>>& blocksOfPage = pageBlockRuns[block.pageIndex];
        const int local = blocksOfPage.size();
        blocksOfPage.append(block.runIndices);
        localIndexOfBlock.insert(i, local);
        pageOfBlock.insert(i, block.pageIndex);
    }

    // ---- 2. 语义树 → 写手能懂的计划 ----
    StructWritePlan plan;
    plan.rootIndex = tree.rootIndex;
    plan.elements.resize(tree.nodes.size());
    for (int i = 0; i < tree.nodes.size(); ++i) {
        const SemanticNode& node = tree.nodes.at(i);
        StructElementPlan& element = plan.elements[i];
        element.type = roleToName(node.role);
        element.title = node.title;
        element.alt = node.alt;
        element.actualText = node.actualText;
        element.language = node.language;
        element.pageIndex = node.pageIndex;
        element.childIndices = node.childIndices;
        element.parentIndex = node.parentIndex;
        if (node.role == StructRole::TH) {
            // 表头格：默认按列覆盖（PDF/UA-1 §7.5 要求表头声明适用范围）。
            element.scope = QStringLiteral("Column");
        }
        for (int blockIndex : node.blockIndices) {
            const auto local = localIndexOfBlock.constFind(blockIndex);
            if (local == localIndexOfBlock.constEnd()) continue;
            element.blockIndices.append(local.value());
            if (blockIndex < 0 || blockIndex >= blocks.size()) continue;
            const SemanticBlock& block = blocks.at(blockIndex);
            // 表单域：内容是 widget 注解，不是 MCID。
            element.annotations.append(block.widgetRefs);
            // 图像：内容是页面上的图像 XObject（/OBJR）。
            if (block.image && block.imageObjectId >= 0) {
                ObjectRef reference;
                reference.pageIndex = block.pageIndex;
                reference.objectId = block.imageObjectId;
                reference.generation = block.imageGeneration;
                element.objectRefs.append(reference);
            }
        }
        if (element.annotations.isEmpty()) element.annotations.clear();
    }
    plan.pageBlockRuns = pageBlockRuns;
    plan.documentLanguage = options.documentLanguage;
    plan.documentTitle = options.documentTitle;
    plan.displayDocTitle = options.displayDocTitle;
    plan.writeTabsStructureOrder = options.writeTabsStructureOrder;
    plan.xmpMetadata = options.xmpMetadata;

    // ---- 3. 写 ----
    QString error;
    const QByteArray written = writeStructPlan(pdfBytes, plan, &error);
    if (written.isEmpty()) {
        result.error = error.isEmpty()
                           ? i18n::tr("aipdf::pdf::SemanticTagWriter", "写语义标签树失败")
                           : error;
        return result;
    }
    result.bytes = written;
    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
