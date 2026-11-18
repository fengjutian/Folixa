#include "pdf/forms/FormData.h"

#include <QRegularExpression>
#include <QStringList>

namespace aipdf::pdf {

namespace {

QString csvEscape(const QString& value) {
    if (!value.contains(QLatin1Char(',')) && !value.contains(QLatin1Char('"')) &&
        !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'))) {
        return value;
    }
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// 逐字符解析：CSV 的引号里可以出现逗号和换行，按行 split 会切坏。
QVector<QStringList> parseCsvRows(const QString& text) {
    QVector<QStringList> rows;
    QStringList row;
    QString cell;
    bool inQuotes = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (inQuotes) {
            if (character == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cell += character;
            }
            continue;
        }
        if (character == QLatin1Char('"')) {
            inQuotes = true;
        } else if (character == QLatin1Char(',')) {
            row.append(cell);
            cell.clear();
        } else if (character == QLatin1Char('\n')) {
            row.append(cell);
            cell.clear();
            rows.append(row);
            row.clear();
        } else if (character != QLatin1Char('\r')) {
            cell += character;
        }
    }
    if (!cell.isEmpty() || !row.isEmpty()) {
        row.append(cell);
        rows.append(row);
    }
    return rows;
}

QString fdfEscape(const QString& value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('('), QStringLiteral("\\("));
    escaped.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    return escaped;
}

}  // namespace

QByteArray formValuesToCsv(const QVector<FormField>& fields) {
    QByteArray csv;
    csv += "name,value\n";
    for (const FormField& field : fields) {
        csv += csvEscape(field.name).toUtf8();
        csv += ',';
        csv += csvEscape(field.value).toUtf8();
        csv += '\n';
    }
    return csv;
}

QVector<FormValue> formValuesFromCsv(const QByteArray& csv) {
    QVector<FormValue> values;
    const QVector<QStringList> rows = parseCsvRows(QString::fromUtf8(csv));
    for (int i = 1; i < rows.size(); ++i) {  // 跳过表头
        const QStringList& row = rows.at(i);
        if (row.size() < 2) {
            continue;
        }
        values.append({ row.at(0), row.at(1) });
    }
    return values;
}

QByteArray formValuesToFdf(const QVector<FormField>& fields) {
    QByteArray fdf = "%FDF-1.2\n1 0 obj\n<< /FDF << /Fields [";
    for (const FormField& field : fields) {
        fdf += " << /T (";
        fdf += fdfEscape(field.name).toUtf8();
        fdf += ") /V (";
        fdf += fdfEscape(field.value).toUtf8();
        fdf += ") >>";
    }
    fdf += " ] >> >>\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n";
    return fdf;
}

QVector<FormValue> formValuesFromFdf(const QByteArray& fdf) {
    QVector<FormValue> values;
    const QString text = QString::fromUtf8(fdf);
    // /T (name) /V (value)：FDF 的字符串是 PDF 字符串，这里只认括号形式，
    // 遇到转义字符按 PDF 规则还原。
    static const QRegularExpression pattern(
        QStringLiteral(R"(/T\s*\(((?:\\.|[^\\)])*)\)\s*/V\s*\(((?:\\.|[^\\)])*)\))"));
    auto unescape = [](QString value) {
        value.replace(QStringLiteral("\\("), QStringLiteral("("));
        value.replace(QStringLiteral("\\)"), QStringLiteral(")"));
        value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        return value;
    };
    auto it = pattern.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        values.append({ unescape(match.captured(1)), unescape(match.captured(2)) });
    }
    return values;
}

}  // namespace aipdf::pdf
