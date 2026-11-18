#pragma once

#include <QImage>

namespace aipdf::ocr {

// OCR 前的页面图像预处理。
//
// 目的只有一个：让识别引擎少吃噪声。手机拍照式的扫描件常有阴影、灰底和孤立黑点，
// 二值化之前先把这些压下去，比事后在结果里猜「这个词是不是糊了」便宜得多。
//
// 全部是纯函数，不依赖引擎，也不改输入图。

// 转 8 位灰度（Rec.601 亮度）。
QImage toGrayscale(const QImage& source);

// 3×3 中值滤波：去掉孤立噪点，同时保住笔画边缘（均值滤波会把细线糊掉）。
QImage medianDenoise(const QImage& gray);

// Otsu 法求全局阈值；图里只有一种灰度时返回该灰度。
int otsuThreshold(const QImage& gray);

// 按阈值二值化：输出 0 或 255 的 8 位灰度图。
QImage binarize(const QImage& gray, int threshold);

// 一条完整的预处理链：灰度 → 中值去噪 → Otsu → 二值化。
QImage preprocessForOcr(const QImage& source);

}  // namespace aipdf::ocr
