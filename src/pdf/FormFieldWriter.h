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
};

struct FormFieldWriteResult {
    QByteArray bytes;
    bool ok = false;
    QString error;
    int fieldObject = 0;
};

// 创建真正的 AcroForm Widget：字段对象同时登记到页面 /Annots 与目录
// /AcroForm/Fields。写入采用增量更新，不改动原 PDF 的既有字节。
FormFieldWriteResult appendFormField(const QByteArray& pdf, const FormFieldInsert& field);

}  // namespace aipdf::pdf
