#include "ocr/OcrChunkBridge.h"

namespace aipdf::ocr {

QVector<doc::DocumentChunk> chunksFromOcrPage(const OcrPageResult& page,
                                              int startOrder,
                                              const doc::ChunkBuilder::Options& options,
                                              const QString& idPrefix) {
    QVector<doc::ChunkLine> lines;
    lines.reserve(page.lines.size());
    for (const OcrLine& line : page.lines) {
        const QString text = line.text.trimmed();
        if (text.isEmpty()) {
            continue;
        }
        doc::ChunkLine chunkLine;
        chunkLine.text = text;
        chunkLine.box = line.rect;
        // 行高用识别框的高度：分块器靠字高认标题，OCR 页没有字体信息，
        // 框高是唯一能把标题与正文分开的证据。
        chunkLine.charHeight = line.rect.height();
        lines.append(chunkLine);
    }

    if (lines.isEmpty()) {
        return {};
    }

    QVector<doc::DocumentChunk> chunks =
        doc::ChunkBuilder::build(page.pageIndex, lines, QString(), startOrder, options, idPrefix);
    for (doc::DocumentChunk& chunk : chunks) {
        chunk.fromOcr = true;
    }
    return chunks;
}

}  // namespace aipdf::ocr
