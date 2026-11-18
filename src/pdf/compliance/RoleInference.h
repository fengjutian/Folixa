#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/compliance/StructTreeModel.h"
#include "pdf/compliance/TagTreeWriter.h"

namespace aipdf::pdf {

// 一块待归类的内容。
//
// 为什么是纯数据而不是「带 qpdf 句柄的东西」：
//   * 归类这件事要能被测试直接构造出来（不用先造一份 PDF）；
//   * 界面层也可以用它自己的文本行（app::userSpaceTextLines）喂进来。
struct SemanticBlock {
    // 所属页（0 基）。
    int pageIndex = -1;
    // 内容在页面上的位置（PDF user space，左下原点）。
    QRectF rect;
    // 这一块的内容（行文字 / 表格单元文字 / 域名）。
    QString text;
    // 这一块里最大的字号（判断标题用）。
    double fontSize = 0.0;
    // 是不是粗体。
    bool bold = false;
    // 列表层级：0 表示不是列表项；1..N 表示第几级。
    int listLevel = 0;
    // 是不是表格单元（下面几个字段只在表格单元上有意义）。
    bool tableCell = false;
    // 这一格在第几行、第几列（0 基）；行数、列数。
    int tableRow = -1;
    int tableColumn = -1;
    int tableRowCount = 0;
    int tableColumnCount = 0;
    // 是不是表头格（TH）。
    bool tableHeader = false;
    // 属于第几张表（同一页里可能有几张表）。
    int tableId = -1;
    // 是不是图像。
    bool image = false;
    // 图像 XObject 在文件里的对象号（写 /OBJR 时用）。
    int imageObjectId = -1;
    int imageGeneration = 0;
    // 是不是表单域。
    bool formField = false;
    // 表单域的字段名与提示文字。
    QString fieldName;
    QString fieldTooltip;
    // 表单域的 widget 注解（对象号 + 世代号）。
    //
    // 写标签树时靠它把 widget 挂进结构元素（/K），并给 widget 写 /StructParent
    // ——没有这一步，读屏软件找不到这个域。
    QVector<ObjectRef> widgetRefs;
    // 这一行里的「列边界」（各段文字左边的 x 坐标）。
    //
    // 为什么放在块里：判断「几行是不是一张表」靠的是**列位置在多行之间对得上**，
    // 光看文字是看不出来的。空表示整行是一块（不是表格行）。
    QVector<double> columnStarts;
    // 每一列的文字（与 columnStarts 一一对应）。
    QStringList columnTexts;
    // 每一列包含的文本运行下标（与 columnStarts 一一对应）。
    QVector<QVector<int>> columnRunIndices;
    // 这一块包含的文本运行下标（页内下标；图像与表单域没有文本运行）。
    QVector<int> runIndices;

    // ---- 推断结果（由 inferSemanticRoles 填） ----
    // 归出来的结构类型。
    StructRole role = StructRole::Unknown;
    // 归类是否可靠。false 时 unreliableReason 说明为什么。
    bool reliable = true;
    // 不可靠的原因（中文）。
    QString unreliableReason;
};

// 归类的一条问题。
struct RoleInferenceIssue {
    // 出问题的是哪一块（页序号 + 文字摘要）。
    QString where;
    // 中文原因。
    QString reason;
};

// 归类的结果。
struct RoleInferenceResult {
    // 每一块的归类结果（与传入的块一一对应）。
    QVector<SemanticBlock> blocks;
    // 语义树：Document → Part(每页) → Sect → 各类内容。
    SemanticTree tree;
    // 归类过程中的问题（哪些地方不可靠、为什么）。
    QVector<RoleInferenceIssue> issues;
};

// 从行 / 表格 / 图 / 域推断角色，并搭出语义树。
//
// 启发式（都是**看得出**的证据，不是猜）：
//   * 表格：连续几行，每行都有若干「列」，而且这些列的横向位置在几行之间
//     对得上 → 一张表；第一行当表头（TH），其余当数据格（TD）；
//   * 标题：字号明显大于正文、或者加粗、或者行首是「第 N 章」「1.2.3」这类
//     编号；层级由编号的层数决定（「1.」→ H1、「1.2」→ H2），
//     没有编号时按字号的相对大小排；
//   * 列表：行首是「•」「-」「*」「1)」这类标记；连续的编号（1、2、3…）
//     也算列表；
//   * 其余连续的正文行合成一个段落（P）。
//
// 置信度不足时**如实标出来**：块上 reliable=false，并写清原因，
// 同时在 issues 里记一条。绝不静默给出一棵看着整齐、其实归类错了的树——
// 用户会以为机器已经替他判断过了。
RoleInferenceResult inferSemanticRoles(const QVector<SemanticBlock>& blocks);

}  // namespace aipdf::pdf
