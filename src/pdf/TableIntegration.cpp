#include "pdf/PdfiumAdapter.h"

namespace aipdf::pdf {

TableStructure PdfPage::detectTableInRect(const QRectF& rectPoints) const {
    QVector<TableTextObject> candidates;
    for (const PageObjectInfo& object : objectsInRect(rectPoints, 0.0)) {
        if (object.type == PageObjectType::Text)
            candidates.append({object.index, object.bounds, object.text});
    }
    return TableStructure::detect(candidates);
}

bool PdfPage::setTableCellText(const TableStructure& table, int row, int column,
                               const QString& text) {
    const TableCell* target = table.cell(row, column);
    if (!target || target->objectIds.size() != 1) return false;
    return setTextObjectText(target->objectIds.constFirst(), text);
}

}  // namespace aipdf::pdf
