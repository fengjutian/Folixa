#pragma once

namespace aipdf::ocr {

// 一页文本层的统计量，用来判断这一页是不是「扫描页」。
struct PageTextStats {
    // 文本层里的字符数（去掉空白后）。
    int charCount = 0;
    // 文本块包围盒并集的面积 / 页面面积，0..1。
    double textAreaRatio = 0.0;
};

// 少于这么多字符、且覆盖面积低于阈值，就当作扫描页。
//
// 阈值的取法：页眉页脚常常只有一行字（十几个字符、覆盖不到 1%），
// 这种页面按「有文本层」处理的话，正文永远检索不到。宁可多 OCR 一页。
constexpr int kSparseCharThreshold = 40;
constexpr double kSparseAreaThreshold = 0.05;

// 这一页要不要 OCR。
bool needsOcr(const PageTextStats& stats);

}  // namespace aipdf::ocr
