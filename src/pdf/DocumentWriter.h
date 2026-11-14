#pragma once

#include <QByteArray>
#include <QString>

namespace aipdf::pdf {

class PdfDocument;

// 保存用的字节。
struct SaveBytes {
    // 要落盘的字节。失败时为空。
    QByteArray bytes;
    // 序列化是否成功。外观流补写失败**不算**失败（文件本身是好的）。
    bool ok = false;
    QString error;
    // 外观流没补上的原因。非空时 bytes 是「没补外观」的版本：批注在本程序里正常，
    // 但在福昕、Acrobat 里看不见。调用方应当提示用户，而不是当成成功。
    QString warning;
    // 补上了多少条外观流（0 表示这份文档没有本程序写的批注）。
    int appearanceCount = 0;
};

// 保存结果。
struct SaveOutcome {
    bool ok = false;
    // 失败原因（成功时为空）。
    QString error;
    // 外观流没补上的原因。非空但 ok 为真时：文件是好的，批注在别的阅读器里看不见。
    QString warning;
    // 原文件备份路径（没有备份时为空）。
    QString backupPath;
    // 补上的外观流条数。
    int appearanceCount = 0;
};

// 保存的唯一入口。
//
// 两件事必须绑在一起：
//   1) 序列化之后补 /AP 外观流——不补，批注在福昕、Acrobat 里是隐形的；
//   2) 写临时文件 → 重开校验 → 备份 → 原子替换——不做，保存失败会毁掉原文件。
// 只要还能分步调用，迟早有人只调一半，而两种失败都是静默的。
class DocumentWriter {
public:
    // 把文档序列化成可保存的字节，并给本程序写的批注补上 /AP 外观流。
    //
    // 只处理本程序写的批注（/NM 以 "Folixa:" 开头、id 非 0）：别的软件写进来的批注
    // 通常自带外观流，重写它们既没必要也不安全。
    static SaveBytes serializeForSave(const PdfDocument& document);

    // 保存到磁盘：serializeForSave + 写临时文件 + 重开校验 + 备份 + 原子替换。
    //
    // 校验不通过时原文件与备份都不动，返回的 ok 为 false。
    // 外观流没补上不算失败（文件是好的），但 warning 会非空。
    static SaveOutcome save(const PdfDocument& document,
                            const QString& targetPath,
                            bool createBackup = true);
};

}  // namespace aipdf::pdf
