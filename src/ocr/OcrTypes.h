#pragma once

#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::ocr {

// 一个识别出来的词：文字、页面用户空间里的框（PDF 点，原点左下）、置信度 0..1。
struct OcrWord {
    QString text;
    QRectF rect;
    double confidence = 0.0;
};

// 一行识别结果。行框是这一行所有词框的并集，供高亮与引用定位使用。
struct OcrLine {
    QString text;
    QRectF rect;
    QVector<OcrWord> words;
};

// 一页的识别结果。
struct OcrPageResult {
    int pageIndex = 0;
    QString engineId;
    QVector<OcrLine> lines;
    double meanConfidence = 0.0;
    // 识别过程中被取消：上层丢弃整页结果，不要半页文字进索引。
    bool cancelled = false;
    // 引擎自己给的失败原因（空串表示成功）。
    QString error;

    QString plainText() const {
        QStringList parts;
        parts.reserve(lines.size());
        for (const OcrLine& line : lines) {
            parts.append(line.text);
        }
        return parts.join(QLatin1Char('\n'));
    }
};

}  // namespace aipdf::ocr

Q_DECLARE_METATYPE(aipdf::ocr::OcrPageResult)
