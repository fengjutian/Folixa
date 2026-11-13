#include "app/PdfPageOperations.h"

#include <utility>

namespace aipdf::app {

namespace {

// 一页的备份：一份只含这一页的临时 PDFium 文档。
class PdfPageBackup final : public doc::PageBackup {
public:
    explicit PdfPageBackup(pdf::PdfDocument document) : document_(std::move(document)) {}

    pdf::PdfDocument document() const { return document_; }

private:
    pdf::PdfDocument document_;
};

}  // namespace

PdfPageOperations::PdfPageOperations(pdf::PdfDocument document)
    : document_(std::move(document)) {}

bool PdfPageOperations::insertBlankPage(int index,
                                        double width,
                                        double height,
                                        QVector<int>* pageMapping) {
    return document_.insertBlankPage(index, width, height, pageMapping);
}

bool PdfPageOperations::deletePage(int index, QVector<int>* pageMapping) {
    return document_.deletePage(index, pageMapping);
}

bool PdfPageOperations::setPageRotation(int index, int quarterTurns) {
    return document_.setPageRotation(index, quarterTurns);
}

bool PdfPageOperations::movePage(int from, int to, QVector<int>* pageMapping) {
    return document_.movePage(from, to, pageMapping);
}

bool PdfPageOperations::backupPage(int index, std::shared_ptr<doc::PageBackup>* backup) {
    if (backup == nullptr) {
        return false;
    }
    pdf::PdfDocument copy = document_.backupPage(index);
    if (!copy.isValid()) {
        return false;
    }
    *backup = std::make_shared<PdfPageBackup>(copy);
    return true;
}

bool PdfPageOperations::restorePage(int index, const doc::PageBackup& backup) {
    const auto* pdfBackup = dynamic_cast<const PdfPageBackup*>(&backup);
    if (pdfBackup == nullptr) {
        return false;
    }
    return document_.restorePageFrom(pdfBackup->document(), index);
}

int PdfPageOperations::pageCount() {
    return document_.pageCount();
}

}  // namespace aipdf::app
