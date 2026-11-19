#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 富文本里的一段。颜色无效（QColor()）表示用 /DA 的默认色。
struct FormTextRun {
    QString text;
    bool bold = false;
    bool italic = false;
    QColor color;
    // <= 0 表示用 /DA 的字号。
    double fontSize = 0.0;
};

// 要写回的一个字段值。
//
// runs 非空时按富文本处理：/V 写纯文本（各段拼接），/RV 写 XHTML，
// /AP 按段切换字体与颜色；runs 为空时就是原来的纯文本路径。
struct FormValueUpdate {
    QString fieldName;
    QString value;
    QVector<FormTextRun> runs;
};

struct FormAppearanceReport {
    int fieldsUpdated = 0;
    int appearancesWritten = 0;
    // 其中按富文本写回（/V + /RV + 多段 /AP）的字段数。
    int richTextFields = 0;
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
