#pragma once

#include <QString>
#include <QVector>

namespace aipdf::speech {

// 一个词：在原文里的位置，用来把高亮画到页面上。
struct SpeechWord {
    QString text;
    int start = 0;   // 在原文里的 UTF-16 起始下标
    int length = 0;  // 长度（UTF-16 码元）
};

// 把文本切成朗读单元。
// 规则：CJK 字符（中日韩表意文字、假名、韩文、标点）逐字成词；
// 拉丁字母/数字连成一个词；空白不朗读但计入位置；其余符号单独成词。
//
// 切分覆盖原文里所有非空白字符且不改变顺序：把词拼回去（去掉空白后）
// 一定等于原文去掉空白，界面才能把高亮画到正确的位置。
QVector<SpeechWord> splitIntoWords(const QString& text);

// 把词序列拼回文本（用于校验「切分不丢字符」）。
// 词里不含空白，所以拼回来的结果没有空白。
QString joinWords(const QVector<SpeechWord>& words);

}  // namespace aipdf::speech
