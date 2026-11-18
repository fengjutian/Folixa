#include "ai/content/PptxExport.h"

#include "doc/export/PptxWriter.h"

namespace aipdf::ai {

// 打包本身在 doc::pptx（PptxWriter）里：PDF → PPT 也要用同一套代码，
// 放在 doc 层才不用让 doc 反过来依赖 ai。
// 这一层只负责把 AI 的 PptDeck 翻译成打包器认得的 Deck。
QByteArray deckToPptx(const PptDeck& deck) {
    doc::pptx::Deck converted;
    converted.title = deck.title;
    for (const Slide& slide : deck.slides) {
        doc::pptx::Slide item;
        item.index = slide.index;
        item.title = slide.title;
        item.subtitle = slide.subtitle;
        for (const QString& bullet : slide.bullets) {
            item.bullets.append(bullet);
        }
        item.notes = slide.notes;
        converted.slides.append(item);
    }
    return doc::pptx::deckToPptx(converted);
}

}  // namespace aipdf::ai
