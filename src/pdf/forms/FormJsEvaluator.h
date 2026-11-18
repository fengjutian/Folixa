#pragma once

#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 表单里某个字段的当前值。
struct FormFieldValue {
    QString name;
    QString value;
};

struct FormJsResult {
    bool ok = false;
    QString error;
    // 求值结果：计算脚本是算出来的值，格式化脚本是格式化后的文本。
    QString value;
    // 校验脚本的结果；其它脚本恒为 true。
    bool valid = true;
};

// 求值一段表单域脚本。
// fieldValues：当前所有字段的值（按名字索引；找不到的名字当空串）。
// eventField：event 上下文里的字段名（脚本里写 this.name / event.target 时用）。
// eventValue：event 上下文里的旧值（脚本里写 event.value 时用）。
//
// 说明：
// - ok=false 表示脚本没跑通（语法不支持、运行出错），error 里是中文原因。
// - 脚本引用了不存在的字段不算失败：按空串继续跑，同时在 error 里留一条
//   "未知字段：xxx" 提示，ok 仍然是 true。
// - 校验脚本返回 true/false 时 valid 反映这个布尔值，其它脚本 valid 恒为 true。
FormJsResult evaluateFormScript(const QString& script,
                                const QVector<FormFieldValue>& fieldValues,
                                const QString& eventField = QString(),
                                const QString& eventValue = QString());

// 只做格式化：把 format 套到 value 上，返回格式化结果。
// format 用 AFNumber_Format 的参数语义：decimals 小数位数、thousandsSeparator 千分位开关等。
//
// format 可以写成两种形式：
// - 完整调用："AFNumber_Format(2, 1, 0, 0, \".\", false)"
// - 裸参数表："2, 1, 0, 0, \".\", false"
// 参数顺序与 Acrobat 一致：decimals, thousandsSeparator, minusSign, grouping,
// decimalPoint, negativeRed（负数标红在纯文本里没有意义，忽略）。
// value 不是数字时原样返回；decimalPoint 传空串表示不要小数部分。
QString formatFormNumber(const QString& format, const QString& value);

}  // namespace aipdf::pdf
