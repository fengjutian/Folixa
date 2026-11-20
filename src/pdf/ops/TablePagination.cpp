#include "pdf/ops/TablePagination.h"

#include <QtGlobal>

#include <cmath>

namespace aipdf::pdf {

QVector<TableRowChunk> paginateTableRows(int rows, double rowHeight, double contentBottom,
                                         double contentTop, double cursor, double spacing) {
    QVector<TableRowChunk> chunks;
    if (rows <= 0 || rowHeight <= 0.0 || contentTop <= contentBottom) return chunks;

    const double capacity = contentTop - contentBottom;
    const int rowsPerPage = static_cast<int>(std::floor(capacity / rowHeight));
    if (rowsPerPage <= 0) return chunks;

    int row = 0;
    double position = cursor;
    while (row < rows) {
        const double available = contentTop - position;
        int fit = static_cast<int>(std::floor(available / rowHeight));
        bool newPage = false;
        if (fit <= 0) {
            position = contentBottom;
            newPage = true;
            fit = rowsPerPage;
        } else if (fit > rowsPerPage) {
            fit = rowsPerPage;
        }
        const int take = qMin(fit, rows - row);
        if (take <= 0) break;
        TableRowChunk chunk;
        chunk.firstRow = row;
        chunk.rowCount = take;
        chunk.height = take * rowHeight;
        chunk.newPage = newPage;
        chunks.append(chunk);
        position += chunk.height + spacing;
        row += take;
    }
    return chunks;
}

}  // namespace aipdf::pdf
