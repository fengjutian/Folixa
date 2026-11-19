#include "pdf/content/ObjectInventory.h"

#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QStringList>

#include <optional>

namespace aipdf::pdf {

QVector<PageObjectInventory> pageObjectInventory(const QByteArray& pdfBytes, QString* error) {
    QVector<PageObjectInventory> inventory;
    if (error) error->clear();
    if (pdfBytes.isEmpty()) {
        if (error) *error = QStringLiteral("PDF 字节为空");
        return inventory;
    }

    const PdfOpenResult opened = openDocumentFromBytes(pdfBytes);
    if (!opened.document.isValid()) {
        if (error) *error = QStringLiteral("PDF 打不开：%1").arg(opened.errorMessage);
        return inventory;
    }

    QString graphicsError;
    const QVector<PageGraphics> graphics = extractDocumentGraphics(pdfBytes, &graphicsError);
    if (!graphicsError.isEmpty()) {
        if (error) *error = graphicsError;
        return inventory;
    }
    if (graphics.size() != opened.document.pageCount()) {
        if (error) {
            *error = QStringLiteral("页面图形清单页数对不上（图形 %1 页，文档 %2 页）")
                         .arg(graphics.size())
                         .arg(opened.document.pageCount());
        }
        return inventory;
    }

    inventory.reserve(opened.document.pageCount());
    for (int pageIndex = 0; pageIndex < opened.document.pageCount(); ++pageIndex) {
        const std::optional<PdfPage> page = opened.document.page(pageIndex);
        if (!page.has_value()) {
            if (error) *error = QStringLiteral("第 %1 页打不开").arg(pageIndex + 1);
            inventory.clear();
            return inventory;
        }
        const std::optional<TextPage> text = page->extractText();
        PageObjectInventory item;
        item.pageIndex = pageIndex;
        item.textChars = text.has_value() ? text->plainText().size() : 0;
        item.images = graphics.at(pageIndex).images.size();
        item.paths = graphics.at(pageIndex).paths.size();
        inventory.append(item);
    }
    return inventory;
}

QString objectInventoryLoss(const QVector<PageObjectInventory>& before,
                            const QVector<PageObjectInventory>& after) {
    if (before.size() != after.size()) {
        return QStringLiteral("页数变了：重排前 %1 页，重排后 %2 页")
            .arg(before.size())
            .arg(after.size());
    }
    QStringList losses;
    for (int i = 0; i < before.size(); ++i) {
        const PageObjectInventory& source = before.at(i);
        const PageObjectInventory& output = after.at(i);
        QStringList pageLosses;
        if (output.textChars < source.textChars) {
            pageLosses.append(QStringLiteral("文字 %1→%2 字")
                                  .arg(source.textChars)
                                  .arg(output.textChars));
        }
        if (output.images < source.images) {
            pageLosses.append(
                QStringLiteral("图片 %1→%2 张").arg(source.images).arg(output.images));
        }
        if (output.paths < source.paths) {
            pageLosses.append(
                QStringLiteral("矢量 %1→%2 条").arg(source.paths).arg(output.paths));
        }
        if (!pageLosses.isEmpty()) {
            losses.append(QStringLiteral("第 %1 页 %2")
                              .arg(source.pageIndex + 1)
                              .arg(pageLosses.join(QStringLiteral("、"))));
        }
    }
    if (losses.isEmpty()) return QString();
    return QStringLiteral("重排会丢对象，已拒绝写回：%1").arg(losses.join(QStringLiteral("；")));
}

}  // namespace aipdf::pdf
