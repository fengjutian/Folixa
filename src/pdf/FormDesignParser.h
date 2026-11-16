#pragma once

#include <QString>
#include <QVector>

#include "pdf/FormFieldWriter.h"

namespace aipdf::pdf {

// 设计出来的一个字段。
//
// 用比例坐标（相对页面尺寸）而不是绝对点：同一份设计套到 A4 与 Letter 上都能对齐。
struct DesignedFormField {
    QString name;
    QString toolTip;
    FormFieldKind kind = FormFieldKind::Text;
    double relX = 0.1;
    double relY = 0.1;
    double relWidth = 0.3;
    double relHeight = 0.03;
};

// 模型看到的系统提示词。
//
// 为什么要求"只输出 JSON"：我们要的是一份能落盘的字段清单，不是散文。
// 让模型自己套代码块、加解释，解析就得写一堆容错——所以在提示词里就把格式钉死。
QString designSystemPrompt();

// 模型的种类名 → 建域用的类型。认不出来的一律当文本域。
FormFieldKind kindFromName(const QString& name);

// 建域用的类型 → 界面表格里显示的种类名。
QString kindName(FormFieldKind kind);

// 把模型可能加上的代码块围栏去掉。
QString stripCodeFence(const QString& text);

// 解析模型给出的字段清单。
//
// 失败时 *error 写的是可以直接显示给用户的说明：文案与「设计助手」对话框
// 原来显示的完全一致（沿用同一个翻译上下文），所以界面行为一个字都没变。
// out / error 都可以是空指针。
bool parseDesignedFields(const QString& answer, QVector<DesignedFormField>* out, QString* error);

}  // namespace aipdf::pdf
