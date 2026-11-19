#pragma once

#include "pdf/forms/XfaSupport.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 脚本能看见的一个字段。
//
// 为什么单独一个结构：域脚本用的 FormFieldValue 只有名字和值，
// 但 XFA 脚本要知道字段是不是数值字段——数值字段参与 + 时是加法，
// 文本字段是字符串拼接。不区分就会把 1 + 2 算成 "12"。
struct XfaScriptField {
    QString name;
    QString value;
    // 是不是数值字段（模板里挂在 numericField 上的字段）。
    bool numeric = false;
};

// 一段 XFA 脚本跑完的结果。
struct XfaScriptRun {
    bool ok = false;
    // 失败原因（中文）。跑通了但有提示时，这里放的是提示。
    QString error;
    // 脚本算出来的字段值：字段名 → 新值。
    QVector<QPair<QString, QString>> assignments;
    // 脚本读到的字段（用来排依赖顺序）。
    QStringList reads;
    // 脚本写入的字段。
    QStringList writes;
    // 非致命提示（脚本引用了不存在的字段之类的）。
    QStringList warnings;
};

// 执行一段 XFA 脚本（白名单子集）。
//
// 做什么：
//   * 把 XFA 的 SOM 写法（xfa.form.form1.Field.value）翻译成求值器认得的写法；
//   * 用仓库既有的 FormJsEvaluator（AcroForm 那边在用的那个白名单子集）求值；
//   * 把「脚本给哪个字段赋值」接住并落到字段值上——
//     FormJsEvaluator 只会写 event.value，XFA 的赋值目标是字段，所以这层要自己接。
//
// 不做什么（边界）：
//   * 不执行事件脚本、校验脚本、初始化脚本：那些要运行时才有意义；
//   * 不支持子集外的语法：会明确报错（错误里带「不支持的语法」），不会静默跳过；
//   * 不做 XFA 的对象模型（SOM）：只认 xfa.form.<子表单>.<字段> 这一种路径形状。
//
// analyzeOnly = true 时只分析（读了哪些字段、写了哪些字段），不执行。
XfaScriptRun evaluateXfaScript(const QString& code,
                               const QVector<XfaScriptField>& fields,
                               const QString& ownerField,
                               bool analyzeOnly = false);

// 执行 XFA 里的计算脚本（白名单子集），把算出来的值写进字段并写回 PDF。
//
// 做什么：
//   * 找出 XFA 里的脚本节点（<calculate> 里的 <script>，代码常放在 <exObject> 里）；
//   * 用仓库既有的 FormJsEvaluator 子集求值器算值；
//   * 脚本之间按依赖排序（读了别人写的字段的脚本排在后面）；
//   * 把算出来的值写进数据集并写回 PDF（走 writeXfaFieldValues 那条路）。
//
// 不做什么（边界）：
//   * **只执行计算脚本**。事件脚本、校验脚本、初始化脚本要运行时才有意义，
//     本程序不执行——遇到它们会在 warnings 里点名，不静默忽略；
//   * 不执行子集外的语法：遇到就整体失败并给出中文原因（「脚本用了不支持的语法：…」），
//     不会算出半份数据；
//   * 不做 XFA 的对象模型（SOM）、布局、事件绑定。
//
// 失败时 outPdf 不动（不做半成品输出）。
XfaWriteReport runXfaCalculateScripts(const QByteArray& pdfBytes, QByteArray* outPdf,
                                      QString* error = nullptr);

}  // namespace aipdf::pdf
