#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace aipdf::pdf {

enum class FormFieldKind {
    Text,
    CheckBox,
    RadioButton,
    ComboBox,
    ListBox,
    PushButton,
    Barcode,
};

struct FormFieldInsert {
    int pageIndex = 0;
    QString name;
    QString toolTip;
    QStringList options;
    QRectF rect;
    FormFieldKind kind = FormFieldKind::Text;
    bool readOnly = false;
    bool required = false;
    bool multiline = false;

    // 单选按钮的分组名：同名的单选按钮共享一个父字段（/Parent），
    // 勾选其中一个时其余自动取消。留空表示不分组，行为与原来完全一致。
    QString groupName;

    // 条形码内容（kind == Barcode 时生效）。留空时回退用字段名当条码内容；
    // 内容必须能被 Code 128 编码（ASCII 32~127），否则建域会直接报错。
    QString barcodeText;

    // 条形码下方的说明文字（kind == Barcode 时生效），留空就不画。
    QString barcodeCaption;

    // 字段的外观串（写进 /DA）。留空时用默认的 "(/Helv 10 Tf 0 g)"。
    // 由「表单工具设置」统一决定，避免每个域都要单独调外观。
    QString appearance;
};

struct FormFieldWriteResult {
    QByteArray bytes;
    bool ok = false;
    QString error;
    int fieldObject = 0;
};

// 创建真正的 AcroForm Widget：字段对象同时登记到页面 /Annots 与目录
// /AcroForm/Fields。写入采用增量更新，不改动原 PDF 的既有字节。
//
// 原文件用经典 xref 表就写 xref 表，用交叉引用流（/Type /XRef）就写交叉引用流：
// PDF 32000-1 §7.5.8.3 规定，用过交叉引用流的文件后续更新必须继续用它，混用是非法的。
FormFieldWriteResult appendFormField(const QByteArray& pdf, const FormFieldInsert& field);

// 修改已有组合框/列表框的 /Opt 数组。采用增量重写字段对象，保持其它字典项不变。
FormFieldWriteResult updateFormFieldOptions(const QByteArray& pdf,
                                            const QString& fieldName,
                                            const QStringList& options);

}  // namespace aipdf::pdf
