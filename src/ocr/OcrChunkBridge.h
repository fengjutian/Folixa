#pragma once

#include "doc/ChunkBuilder.h"
#include "doc/DocumentChunk.h"
#include "ocr/OcrTypes.h"

#include <QString>
#include <QVector>

namespace aipdf::ocr {

// 把一页 OCR 结果接进索引。
//
// 走的是**同一套分块器**（doc::ChunkBuilder），所以 OCR 页与文本页产出的块
// 形状完全一致：页码、行框、顺序、id 规则都一样，索引与引用不需要为 OCR 开特例。
// 唯一的区别是每个块带 fromOcr = true——引用要能说明「这句是识别来的」。
QVector<doc::DocumentChunk> chunksFromOcrPage(
    const OcrPageResult& page,
    int startOrder,
    const doc::ChunkBuilder::Options& options = doc::ChunkBuilder::Options(),
    const QString& idPrefix = QString());

}  // namespace aipdf::ocr
