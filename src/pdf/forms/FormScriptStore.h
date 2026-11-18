#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 字段上挂着的脚本与默认值（PDF 对象层的 /AA、/A、/DV）。
//
// 为什么单独解析：PDFium 的公开 API 只暴露 /AA /JS（动作脚本），
// 读不到计算脚本（/AA /C）、格式化脚本（/AA /F）、校验脚本（/AA /V）
// 与默认值（/DV）——而这几个才是「计算次序」与「重置表单」的依据。
//
// 说明：这里的"字段"是 AcroForm 的字段对象（含 /FT 的那一个），
// 不含纯外观用的无 /FT 注释。
struct FormFieldScript {
    QString fieldName;
    // /AA /C：计算脚本。字段值变化时按它算出别的字段的值。
    QString calculate;
    // /AA /F：格式化脚本，决定值怎么显示。
    QString format;
    // /AA /V：校验脚本。
    QString validate;
    // /A /JS：动作脚本。
    QString action;
    // /DV：默认值。「重置表单」用它恢复。
    QString defaultValue;
};

struct FormScriptWriteResult {
    QByteArray bytes;
    bool ok = false;
    QString error;
};

// 读出所有字段的脚本与默认值。
//
// 读取用手写扫描（不依赖 qpdf），因此对「字段字典是普通对象」的 PDF 都有效；
// 藏在对象流（/Type /ObjStm）里的字段读不到（结果里就没有它们），
// 那种文件请走 qpdf 路径。
QVector<FormFieldScript> readFormFieldScripts(const QByteArray& pdf);

// 读出 AcroForm /CO 里的计算次序（字段名列表）。
//
// 没有 /CO 时返回空列表：调用方应当按文档顺序兜底，而不是当成"顺序为空"。
QStringList readCalculationOrder(const QByteArray& pdf);

// 把脚本与计算次序写回 PDF。
//
// 写入走 qpdf 的对象级接口，所以交叉引用流、对象流都能处理；
// 本次构建没启用 qpdf（AIPDF_WITH_QPDF 默认关）时返回明确错误，不会静默失败。
//
// scripts 里只写非空的脚本字段；空串表示"这一项不写"。
// order 里出现的字段名会按顺序写进 AcroForm /CO。
FormScriptWriteResult writeFormScripts(const QByteArray& pdf,
                                        const QVector<FormFieldScript>& scripts,
                                        const QStringList& order);

}  // namespace aipdf::pdf
