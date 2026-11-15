#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "pdf/PdfiumAdapter.h"

namespace aipdf::pdf {

// 表单数据的交换格式：CSV（给 Excel 看）与 FDF（Acrobat 认）。
//
// 两者都只承载"字段名 → 值"，不带样式：数据是数据，外观属于文档本身。
struct FormValue {
    QString name;
    QString value;
};

// CSV：第一行表头 name,value；含逗号/引号/换行的值按 RFC 4180 加引号转义。
QByteArray formValuesToCsv(const QVector<FormField>& fields);
QVector<FormValue> formValuesFromCsv(const QByteArray& csv);

// FDF：最小可用结构，Acrobat / 福昕导入时能回填同名域。
QByteArray formValuesToFdf(const QVector<FormField>& fields);
QVector<FormValue> formValuesFromFdf(const QByteArray& fdf);

}  // namespace aipdf::pdf
