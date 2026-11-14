#pragma once

#include "ocr/OcrTypes.h"

#include <QImage>
#include <QString>

namespace aipdf::ocr {

// OCR 引擎抽象。
//
// 这一层只负责「图像进、文字与框出」。扫描页判定、坐标换算、索引集成都在上层，
// 所以换引擎（Tesseract / Windows OCR / 云端）不会牵动检索与问答。
//
// 约束：
// - recognize() 是同步的，调用方负责放到后台线程；
// - cancel() 可以从别的线程调用，实现要在下一个检查点返回；
// - 被取消时返回的 OcrPageResult 里 cancelled = true，上层整页丢弃。
class OcrEngine {
public:
    virtual ~OcrEngine() = default;

    virtual QString id() const = 0;

    // 引擎是否可用（语言包/模型是否就位）。不可用时上层退回「无 OCR」。
    virtual bool isAvailable() const = 0;

    // pageImage：页面渲染图；pageIndex：写进结果里，用于回填页码。
    virtual OcrPageResult recognize(const QImage& pageImage, int pageIndex) = 0;

    virtual void cancel() = 0;
};

}  // namespace aipdf::ocr
