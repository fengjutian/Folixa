#include "pdf/content/TableStructure.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aipdf::pdf {
namespace {

double median(QVector<double> values) {
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    return values.size() % 2 ? values.at(middle)
                             : (values.at(middle - 1) + values.at(middle)) * 0.5;
}

struct Row {
    double center = 0.0;
    QVector<TableTextObject> objects;
};

}  // namespace

const TableCell* TableStructure::cell(int row, int column) const {
    const auto found = std::find_if(cells.cbegin(), cells.cend(), [=](const TableCell& value) {
        return value.row == row && value.column == column;
    });
    return found == cells.cend() ? nullptr : &*found;
}

TableStructure TableStructure::detect(const QVector<TableTextObject>& input) {
    TableStructure result;
    QVector<TableTextObject> objects;
    QVector<double> heights;
    for (const TableTextObject& object : input) {
        if (object.objectId < 0 || object.bounds.isEmpty() || object.text.trimmed().isEmpty()) continue;
        objects.append(object);
        heights.append(object.bounds.height());
    }
    if (objects.size() < 4) return result;
    const double rowTolerance = qMax(2.0, median(heights) * 0.65);
    std::sort(objects.begin(), objects.end(), [](const auto& left, const auto& right) {
        if (!qFuzzyCompare(left.bounds.center().y(), right.bounds.center().y()))
            return left.bounds.center().y() > right.bounds.center().y();
        return left.bounds.left() < right.bounds.left();
    });
    QVector<Row> rows;
    for (const TableTextObject& object : objects) {
        const double center = object.bounds.center().y();
        auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& value) {
            return std::abs(value.center - center) <= rowTolerance;
        });
        if (row == rows.end()) rows.append(Row{center, {object}});
        else {
            row->objects.append(object);
            row->center = (row->center * (row->objects.size() - 1) + center) / row->objects.size();
        }
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& row) {
        return row.objects.size() < 2;
    }), rows.end());
    if (rows.size() < 2) return result;
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.center > b.center; });
    int columns = 0;
    for (Row& row : rows) {
        std::sort(row.objects.begin(), row.objects.end(), [](const auto& a, const auto& b) {
            return a.bounds.center().x() < b.bounds.center().x();
        });
        columns = qMax(columns, row.objects.size());
    }
    if (columns < 2) return result;
    QVector<double> columnCenters(columns, 0.0);
    QVector<int> columnSamples(columns, 0);
    for (const Row& row : rows) {
        if (row.objects.size() != columns) continue;
        for (int column = 0; column < columns; ++column) {
            columnCenters[column] += row.objects[column].bounds.center().x();
            ++columnSamples[column];
        }
    }
    for (int column = 0; column < columns; ++column) {
        if (!columnSamples[column]) return {};
        columnCenters[column] /= columnSamples[column];
    }
    int aligned = 0;
    int total = 0;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const Row& row = rows.at(rowIndex);
        for (const TableTextObject& object : row.objects) {
            int bestColumn = 0;
            double bestDistance = std::numeric_limits<double>::max();
            for (int column = 0; column < columns; ++column) {
                const double distance = std::abs(object.bounds.center().x() - columnCenters[column]);
                if (distance < bestDistance) { bestDistance = distance; bestColumn = column; }
            }
            const double tolerance = qMax(4.0, object.bounds.width() * 0.75);
            if (bestDistance <= tolerance) ++aligned;
            ++total;
            TableCell cell;
            cell.row = rowIndex;
            cell.column = bestColumn;
            cell.objectIds = {object.objectId};
            cell.bounds = object.bounds;
            cell.text = object.text;
            result.cells.append(cell);
        }
    }
    result.rowCount = rows.size();
    result.columnCount = columns;
    result.confidence = total ? static_cast<double>(aligned) / total : 0.0;
    if (result.confidence < 0.70) return {};
    return result;
}

}  // namespace aipdf::pdf
