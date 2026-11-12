#include "pdf/TextPage.h"

namespace aipdf::pdf {

QString TextPage::plainText() const {
    QString text;
    text.reserve(chars.size());
    for (const TextChar& character : chars) {
        text += character.text;
    }
    return text;
}

}  // namespace aipdf::pdf
