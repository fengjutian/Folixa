#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 要写回的一个字段值。
struct FormValueUpdate {
    QString fieldName;
    QString value;
};

struct FormAppearanceReport {
    int fieldsUpdated = 0;
    int appearancesWritten = 0;
    QStringList notes;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

struct FormAppearanceResult {
    QByteArray bytes;
    FormAppearanceReport report;

    bool ok() const { return report.ok() && !bytes.isEmpty(); }
};

// 把字段值写回 PDF，并重新生成 /AP 外观流。
//
// 只改该改的那几个对象：字段的 /V、widget 的 /AP /N；不重写整页内容，
// 也不动其它字段。
//
// 边界（MVP，都是明确失败而不是静默降级）：
//   * 只支持文本域（/Tx）与复选 / 单选（/Btn）的 /V 与 /AS；
//     下拉 / 列表的 /Opt 索引、签名域不在这一层；
//   * 文本只支持单字节编码（WinAnsi / PDFDocEncoding 范围）；
//     中文需要嵌入字体，这一层直接报错并指出是哪个字段、哪个字；
//   * 不处理加密文档（qpdf 需要密码才能改）；
//   * 任一更新失败就整体失败，不产出半成品字节。
FormAppearanceResult writeFormValues(const QByteArray& pdfBytes,
                                     const QVector<FormValueUpdate>& updates);

}  // namespace aipdf::pdf
