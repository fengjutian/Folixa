#pragma once

#include <QString>
#include <QStringList>

namespace aipdf::ocr {

// 是不是 CJK 字符（中日韩 + 假名 + 谚文）。
bool isCjkChar(QChar ch);

// 把识别出来的词拼成一行。
//
// 中文按字切开时不能加空格，否则「中华人民共和国」会变成「中 华 人 民 …」，
// 检索与引用都对不上原文；夹在中文之间的英文单词保持原样。
QString joinWords(const QStringList& words);

}  // namespace aipdf::ocr
