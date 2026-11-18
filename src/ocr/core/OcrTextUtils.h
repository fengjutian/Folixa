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
//
// 输出前还会做两件事（见下），保证识别文本能按原文被搜到：
//   1. 全角标点/数字/字母归一化成半角（`．` → `.`、`０` → `0`）；
//   2. 数字之间被识别引擎拆出来的空格合上（`1 234 ． 56` → `1234.56`）。
QString joinWords(const QStringList& words);

// 全角 ASCII 区（U+FF01–U+FF5E）与全角空格（U+3000）归一化成半角。
//
// 只动"全角形式的 ASCII"，不动中文标点（。、；："" 之类）——
// 那些是中文自己的标点，改成半角反而变形。
QString normalizePunctuation(const QString& text);

// 合上数字之间被拆出来的空格，让票据金额这类数字能被按原文搜到：
//   `1 234` → `1234`（只认千分位分组：左边 1–3 位、右边正好 3 位）
//   `8 , 640 . 00` → `8,640.00`
// 不合并「2024 年 5 月」这类中间夹汉字的，也不合并「1 234 5」这种可疑分组。
QString compactNumericSpacing(const QString& text);

}  // namespace aipdf::ocr
