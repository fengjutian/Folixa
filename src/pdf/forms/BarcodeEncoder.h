#pragma once

#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一根竖条的描述：width 是模块数（1 模块 = 最细条宽），bar=true 表示黑条、false 表示白空隙。
struct BarcodeModule {
    int width = 1;
    bool bar = true;
};

struct BarcodeCode128 {
    // 从头到尾的模块序列（含起始符、数据、校验位、终止符）。
    QVector<BarcodeModule> modules;
    bool ok = false;
    QString error;
    // 编码后使用的符号集，仅用于诊断与测试。
    QString codeSet;
};

// 把文本编码成 Code 128。
// 要求：
//  1. 纯 ASCII 且落在 32..127（Code 128 B 可编码范围）才允许；含非 ASCII 字符时
//     返回 ok=false 与中文 error（不要用 latin1 静默替换）。
//  2. 自动选码集：文本中出现**偶数长度、长度≥4 的纯数字串**时，该段用 Code 128 C
//     （数字密度更高），其余用 Code 128 B；其余字符集 B 的单字符编码。
//  3. 计算 Mod-103 校验位，插在数据与终止符之间。
//  4. 起始符与终止符都要编码进去（Code 128 B 的 Start B = 104；Code C 的 Start C = 105；
//     切换码集用 Code B=100 / Code C=99；终止符 Stop = 106）。
//  5. modules 里相邻的同色条要合并成一条（width 累加），白空隙同样合并。
BarcodeCode128 encodeCode128(const QString& text);

}  // namespace aipdf::pdf
