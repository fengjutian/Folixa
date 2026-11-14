#pragma once

#include "ocr/OcrEngine.h"

#include <memory>

namespace aipdf::ocr {

// 选一个可用的本地 OCR 引擎：
//   1. 装了 tesseract（PATH 或 AIPDF_TESSERACT_PATH）就用它；
//   2. 否则用 Windows 自带 OCR（Windows 10+，无需安装）。
// 两者都不可用时返回 Windows 引擎，让 recognize() 给出可读错误，
// 上层据此提示「本机没有可用的 OCR」，而不是静默什么也不做。
std::unique_ptr<OcrEngine> createDefaultEngine();

}  // namespace aipdf::ocr
