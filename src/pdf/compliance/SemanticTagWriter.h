#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/compliance/RoleInference.h"
#include "pdf/compliance/StructTreeModel.h"

namespace aipdf::pdf {

// 写语义标签树的选项。
struct SemanticWriteOptions {
    // 文档语言（写进 catalog 的 /Lang）。空表示不写。
    QString documentLanguage;
    // 文档标题（写进 /Info /Title）。空表示不写。
    QString documentTitle;
    // 要不要写 /DisplayDocTitle true。
    bool displayDocTitle = false;
    // 要不要给每页写 /Tabs /S（PDF/UA 要求 Tab 顺序跟结构一致）。
    bool writeTabsStructureOrder = true;
    // XMP 元数据（写进 catalog 的 /Metadata）。空表示不写。
    QByteArray xmpMetadata;
};

// 写语义标签树的结果。
struct SemanticWriteResult {
    // 是否写成功。失败时 error 里有中文原因。
    bool ok = false;
    // 失败原因（中文）。
    QString error;
    // 写出来的 PDF 字节。失败时为空。
    QByteArray bytes;
    // 写进去的那棵树（写完之后回读一遍，便于测试与回显）。
    SemanticTree tree;
    // 归类不可靠、需要人工确认的地方。
    QStringList issues;
};

// 给一份 PDF 写一棵**语义**标签树。
//
// 树的形状：Document → Part(每页) → Sect → H1..H6 / P / L / Table(TR/TH/TD)
// / Figure / Form。
//
// 写了什么：
//   * 内容流里块内的每个文本显示操作符被包进 `/Tag <</MCID n>> BDC … EMC`；
//   * 结构元素带 /S、/T、/Alt、/ActualText、/Lang，表头格带 /Scope；
//   * 页上写 /StructParents，按需要写 /Tabs /S；
//   * catalog 写 /StructTreeRoot 与 /MarkInfo，按选项写 /Lang、/DisplayDocTitle、
//     /Metadata，/Info 写 /Title。
//
// 标记内容、MCID 分配与 /ParentTree 由**共用的写手**完成
// （pdf::writeStructPlan）——行级标签树与语义标签树不写两套。
// 这里只负责「把语义变成写手能懂的计划」。
SemanticWriteResult writeSemanticStructure(const QByteArray& pdfBytes,
                                           const QVector<SemanticBlock>& blocks,
                                           const SemanticTree& tree,
                                           const SemanticWriteOptions& options = SemanticWriteOptions());

}  // namespace aipdf::pdf
