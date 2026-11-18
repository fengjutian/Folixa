#include "ocr/OcrEngineFactory.h"

#include "ocr/TesseractCliEngine.h"
#include "ocr/WindowsOcrEngine.h"

namespace aipdf::ocr {

std::unique_ptr<OcrEngine> createDefaultEngine() {
    auto tesseract = std::make_unique<TesseractCliEngine>();
    if (tesseract->isAvailable()) {
        return tesseract;
    }
    // 没装 tesseract 时退回系统 OCR：Windows 10+ 自带，零安装。
    return std::make_unique<WindowsOcrEngine>();
}

}  // namespace aipdf::ocr
