#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 自动打标签时的一条问题记录。
//
// 为什么单独列出来，而不是把话都塞进错误字符串：
// 一份文档里可能只有个别域缺替代文本，其余域都打好了。
// 把它们混成一条错误，调用方只能在「整体成功」与「整体失败」之间二选一，
// 而事实是「整体成功，但有几处要说清楚」。
struct FormTagIssue {
    // 字段名（/T）。域本身没有名字时，这里放的是一句可读的说明（例如「第 2 页的表单域」）。
    QString fieldName;
    // 中文原因：这个问题到底是怎么来的。
    QString reason;
};

// 自动给表单域写标签树的结果。
struct FormAutoTagResult {
    // 是否写成功。失败时 error 里有中文原因。
    bool ok = false;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 写出来的 PDF 字节。失败时为空。
    QByteArray bytes;
    // 打了标签的域数。
    int taggedFieldCount = 0;
    // 文档页数。
    int pageCount = 0;
    // 逐条问题（字段名 + 中文原因）。
    QVector<FormTagIssue> issues;
};

// 给 PDF 里的表单域（AcroForm 的 Widget 注解）写结构标签树 + 替代文本，
// 让读屏软件能把这些域念出来。
//
// 做了什么：
//   * 读出文档里的表单域：字段名（/T）、提示文字（/TU）、字段类型（/FT）、
//     所在页，以及 Widget 注解对象本身；
//   * 为**每一个域**在结构树里写一个结构元素：/S 用 /Form，/T 放字段名，
//     /Alt 放替代文本；
//   * 给 Widget 注解挂上 /StructParent（结构元素在页 ParentTree 里的下标），
//     并把该下标写进页的 ParentTree 数组——没有这一步，读屏软件找不到对应的结构元素。
//
// 替代文本（/Alt）的取值规则（**如实取，不编**）：
//   * 优先用域的提示文字（/TU）；
//   * 没有提示文字时用字段名；
//   * 两者都没有 → **不写 /Alt**，并把这个域记进 issues（字段名 + 中文原因）。
//     宁可少说一句，也不替用户编一句替代文本出来。
//
// 边界与口径：
//   * 文档里一个表单域都没有 → **失败**并给出中文原因，不生成空标签树；
//   * 标签树必须防环：文档里已有的坏 /Parent（指回祖先）不能让遍历绕死；
//   * 重跑不叠加：上一轮写下的 /Form 元素会被摘掉换成新的，
//     结构树里 /Form 元素的数量与域数始终一致；
//   * 写完会重新打开校验一次（结构有问题、有警告、/Form 数量对不上都算失败）。
//
// 全部走 qpdf：读域与写标签树都是对象级操作，
// 不新增 PDFium 调用点（PDFium 在本仓库是进程级互斥的）。
FormAutoTagResult autoTagFormFields(const QByteArray& pdfBytes);

}  // namespace aipdf::pdf
