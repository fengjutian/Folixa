#pragma once

#include "pdf/forms/FormJsEvaluator.h"

#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 脚本执行的资源上限。
struct QuickJsLimits {
    // 一次求值最多跑多久；到点由 QuickJS 的中断回调打断。
    int timeoutMs = 50;
    int memoryMb = 64;
};

// 用 QuickJS 执行表单脚本（AcroForm 的 /AA）。
//
// 与 evaluateFormScript 的输入 / 输出保持同一套语义，方便把白名单子集换成真引擎：
//   * event.value / event.rc / event.change / event.selStart / event.selEnd / event.target
//   * this.name / this.value
//   * this.getField("name").value（也支持裸 getField）
//   * Math、String、Array、JSON 等标准库全部可用（QuickJS 自带）
//
// 与子集求值器的差别（也是换引擎的理由）：
//   * 支持变量、循环、if/else、函数、字符串方法；
//   * 不支持 Acrobat 的 app / doc / util / SOAP / ADBC / 网络与文件 API——
//     这些要么没有安全实现，要么根本不该在表单脚本里跑。
class QuickJsFormScript final {
public:
    // 编译时是否带上了 QuickJS。
    static bool available();

    static FormJsResult evaluate(const QString& script, const QVector<FormFieldValue>& fields,
                                 const QString& eventField, const QString& eventValue,
                                 const QString& eventChange = QString(),
                                 int selectionStart = 0, int selectionEnd = 0,
                                 const QuickJsLimits& limits = QuickJsLimits());
};

}  // namespace aipdf::pdf
