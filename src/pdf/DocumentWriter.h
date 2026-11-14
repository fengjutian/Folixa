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

// 把文档序列化成可保存的字节，并给本程序写的批注补上 /AP 外观流。
//
// 为什么合在一起：/AP 只能在**序列化之后**补（需要对象级写入），而调用方只关心
// 「给我一份能落盘的字节」。把两步拆开，迟早有人只调 serialize() 而忘了补外观，
// 症状是文件在别人那儿打开时批注集体消失——而且不会有任何报错。
//
// 只处理本程序写的批注（/NM 以 "Folixa:" 开头，id 非 0）：别的软件写进来的批注
// 通常自带外观流，重写它们既没必要也不安全。
SaveBytes serializeForSave(const PdfDocument& document);

}  // namespace aipdf::pdf
