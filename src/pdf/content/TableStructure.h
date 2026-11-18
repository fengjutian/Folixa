#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

struct TableTextObject {
    int objectId = -1;
    QRectF bounds;
    QString text;
};

struct TableCell {
    int row = -1;
    int column = -1;
    QVector<int> objectIds;
    QRectF bounds;
    QString text;
};

struct TableStructure {
    int rowCount = 0;
    int columnCount = 0;
    QVector<TableCell> cells;
    double confidence = 0.0;

    bool isValid() const { return rowCount >= 2 && columnCount >= 2 && !cells.isEmpty(); }
    const TableCell* cell(int row, int column) const;

    static TableStructure detect(const QVector<TableTextObject>& objects);
};

}  // namespace aipdf::pdf
