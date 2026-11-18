#include "ocr/ScannedPageDetector.h"

namespace aipdf::ocr {

bool needsOcr(const PageTextStats& stats) {
    // 完全没有文本层：一定是扫描件。
    if (stats.charCount <= 0) {
        return true;
    }
    // 有字但少得可怜、而且几乎不占版面：典型是扫描页上的页眉或页码。
    if (stats.charCount < kSparseCharThreshold && stats.textAreaRatio < kSparseAreaThreshold) {
        return true;
    }
    return false;
}

}  // namespace aipdf::ocr
