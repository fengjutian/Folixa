#pragma once

#include "pdf/forms/FormJsEvaluator.h"
#include "pdf/forms/FormScriptStore.h"

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 表单域的字段类型（AcroForm 的 /FT 与 /Ff）。
enum class FormFieldType { Text, Button, Choice, Signature, Unknown };

// 表单事件。顺序是运行时保证的，不靠界面各写各的。
enum class FormEventType {
    Focus,
    Blur,
    Keystroke,
    Validate,
    Calculate,
    Format,
    MouseDown,
    MouseUp,
    PageOpen,
    PageClose,
    DocOpen,
    DocClose,
    WillSave,
    DidSave,
};

QString formEventName(FormEventType type);

// 一个域在运行时的状态。
struct FormFieldState {
    // 全名（层级用 "." 连接，例如 "form1.total"）。
    QString name;
    FormFieldType type = FormFieldType::Unknown;
    // /Ff 原始标志位。
    int flags = 0;
    // /V：当前值。复选 / 单选是导出值（"Yes" / "Off" / 自定义名字）。
    QString value;
    // /DV：默认值（重置表单用）。
    QString defaultValue;
    // Format 脚本算出来的显示值；没有格式脚本时等于 value。
    QString formattedValue;
    // 最近一次通过校验的值：校验失败时界面可以回滚到它。
    QString committedValue;
    // /Opt：下拉 / 列表的选项。
    QStringList options;
    // /TU：提示文字。
    QString tooltip;
    // /DA：外观串。
    QString appearance;
    int pageIndex = -1;
    // 页坐标（PDF user space，左下原点）。
    QRectF rect;
    // /AA 与 /A 里的脚本。
    FormFieldScript script;

    bool readOnly = false;
    bool required = false;
    bool multiline = false;

    bool isCheckBox() const;
    bool isRadio() const;
    bool isPushButton() const;
    bool isCombo() const;
};

// 事件轨迹里的一条。测试断言的是它，不是内部实现。
struct FormEventTrace {
    FormEventType type = FormEventType::Focus;
    QString field;
    bool ok = true;
    // 失败原因、格式化结果或计算后的值。
    QString detail;
};

struct FormEventResult {
    bool ok = true;
    // event.rc：击键 / 校验脚本是否接受这次输入。
    bool accepted = true;
    QString error;
    QString value;
    QString formatted;
    QVector<FormEventTrace> trace;
};

// 表单运行时：字段树 + 固定的事件顺序 + 计算依赖。
//
// 事件顺序（与 Acrobat 对齐）：
//   typeText: 每个字符 Keystroke；被拒绝就停在那里。
//   blur:     Validate -> Calculate（按 /CO）-> Format。
//   setValue: Validate -> Calculate -> Format（导入 / 粘贴走这条）。
//
// 边界：
//   * 脚本仍由 FormJsEvaluator 的白名单子集执行；子集外的语法会明确报错，
//     不会静默放行（这一步不换 QuickJS 引擎）；
//   * 只改运行时状态，不写回 PDF；写回与 /AP 外观流是后面的事；
//   * 计算脚本按 /CO 顺序跑，最多 8 轮；不收敛时如实报「没有收敛」。
class FormRuntime {
public:
    // 从 PDF 字节读 AcroForm 字段树。失败时 error 非空。
    // 没有 AcroForm 不是失败：返回空字段树。
    static FormRuntime load(const QByteArray& pdfBytes, QString* error = nullptr);

    bool isValid() const { return valid_; }
    QString error() const { return error_; }

    const QVector<FormFieldState>& fields() const { return fields_; }
    const FormFieldState* field(const QString& name) const;
    QString value(const QString& name) const;
    QString formattedValue(const QString& name) const;
    // /CO 里的计算次序；为空表示文档没写，按字段顺序兜底。
    QStringList calculationOrder() const { return calculationOrder_; }

    FormEventResult focus(const QString& fieldName);
    FormEventResult blur(const QString& fieldName);
    FormEventResult typeText(const QString& fieldName, const QString& text);
    FormEventResult setValue(const QString& fieldName, const QString& value);

    // 按 /CO 顺序重算所有计算域。返回是否有值发生变化。
    bool recalculate();

    // 文档 / 页面级事件：记录轨迹并执行 /AA 里对应脚本（本层暂无文档脚本，
    // 只保证顺序与可观测性）。
    FormEventResult dispatchDocumentEvent(FormEventType type);

    const QVector<FormEventTrace>& trace() const { return trace_; }
    void clearTrace() { trace_.clear(); }
    // 有没有域值被改过（界面据此决定要不要提示保存）。
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    FormFieldState* mutableField(const QString& name);
    QVector<FormFieldValue> fieldValues() const;
    void record(FormEventType type, const QString& field, bool ok, const QString& detail);
    FormEventResult applyFormat(const QString& fieldName);
    bool runCalculatePass(bool* changed);

    QVector<FormFieldState> fields_;
    QStringList calculationOrder_;
    QString error_;
    bool valid_ = false;
    QVector<FormEventTrace> trace_;
    // 最近一次有域值变化时的文档级脏标记（给界面用，不在本层写盘）。
    bool dirty_ = false;
};

}  // namespace aipdf::pdf
