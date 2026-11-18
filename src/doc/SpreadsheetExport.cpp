#include "doc/SpreadsheetExport.h"

#include "doc/XmlText.h"
#include "doc/ZipWriter.h"

namespace aipdf::doc {

namespace {

// 工作表名：Excel 的硬规矩是 ≤31 字符，且不能含 []:*?/\。
//
// 为什么要在导出前收拾：非法的工作表名会让 Excel 弹出「文件已损坏」，
// 而用户做的事只是导出一份表单 —— 这种错不该由用户来发现。
//
// 这里逐字符过滤而不是用正则：正则的字符类里要转义 `]` 与 `\`，
// 写错一个反斜杠就会把整条规则改掉（少过滤一个字符 = 文件打不开）。
QString sanitizeSheetName(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name) {
        if (ch == QLatin1Char('[') || ch == QLatin1Char(']') || ch == QLatin1Char(':') ||
            ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('/') ||
            ch == QLatin1Char('\\')) {
            continue;
        }
        out.append(ch);
    }
    out = out.trimmed();
    if (out.isEmpty()) {
        out = QStringLiteral("Sheet1");
    }
    if (out.size() > 31) {
        out.truncate(31);
    }
    return out;
}

// 列号 → 列名（0 → A，1 → B，26 → AA）。
QString columnName(int index) {
    QString out;
    int value = index + 1;
    while (value > 0) {
        const int rest = (value - 1) % 26;
        out.prepend(QChar(QLatin1Char('A').unicode() + rest));
        value = (value - 1) / 26;
    }
    return out;
}

QByteArray contentTypesXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd."
        "openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>"));
    return out.toUtf8();
}

QByteArray packageRelsXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

QByteArray workbookXml(const QString& sheetName) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"%1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>").arg(escapeOoxmlText(sheetName)));
    return out.toUtf8();
}

QByteArray workbookRelsXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
        "Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
        "Target=\"styles.xml\"/>"
        "</Relationships>"));
    return out.toUtf8();
}

// 最小样式表：Excel 打开工作簿时要求 styles 部件存在，
// 少一个 fill / border 会被判成损坏。
QByteArray stylesXml() {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"2\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "</fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
        // cellStyles：缺这一段，Excel / openpyxl 会说「工作簿没有默认样式」。
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>"));
    return out.toUtf8();
}

// 一个文本单元格。
QByteArray textCell(const QString& reference, const QString& value) {
    return QStringLiteral("<c r=\"%1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%2</t></is></c>")
        .arg(reference, escapeOoxmlText(value))
        .toUtf8();
}

QByteArray sheetXml(const QVector<SheetRow>& rows) {
    QString out;
    out.append(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"));
    out.append(QStringLiteral(
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<sheetData>"));
    // 表头与 CSV 保持一致（name / value）：同一份数据的两种导出，
    // 表头不一样就没法对着看。
    out.append(QStringLiteral("<row r=\"1\">"));
    out.append(textCell(QStringLiteral("A1"), QStringLiteral("name")));
    out.append(textCell(QStringLiteral("B1"), QStringLiteral("value")));
    out.append(QStringLiteral("</row>"));

    int row = 2;
    for (const SheetRow& field : rows) {
        out.append(QStringLiteral("<row r=\"%1\">").arg(row));
        out.append(textCell(QStringLiteral("%1%2").arg(columnName(0)).arg(row), field.name));
        out.append(textCell(QStringLiteral("%1%2").arg(columnName(1)).arg(row), field.value));
        out.append(QStringLiteral("</row>"));
        ++row;
    }
    out.append(QStringLiteral("</sheetData></worksheet>"));
    return out.toUtf8();
}

}  // namespace

QByteArray rowsToXlsx(const QVector<SheetRow>& rows, const QString& sheetName) {
    zip::Writer writer;
    writer.addFile(QStringLiteral("[Content_Types].xml"), contentTypesXml());
    writer.addFile(QStringLiteral("_rels/.rels"), packageRelsXml());
    writer.addFile(QStringLiteral("xl/workbook.xml"), workbookXml(sanitizeSheetName(sheetName)));
    writer.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRelsXml());
    writer.addFile(QStringLiteral("xl/styles.xml"), stylesXml());
    writer.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), sheetXml(rows));
    return writer.toByteArray();
}

}  // namespace aipdf::doc
