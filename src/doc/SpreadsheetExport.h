#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::doc {

// 工作表里的一行：字段名 → 字段值。
//
// 为什么在这里定义一份自己的行，而不是直接收 pdf::FormField：
// 这一层只负责「把两列数据写进工作簿」，不该知道这些数据是从 PDF 表单来的。
// 收自己的行结构，就不必把 PDFium 的头（QImage、页面句柄）拖进来。
struct SheetRow {
    QString name;
    QString value;
};

// 把表单字段导成 Excel 工作簿（.xlsx，Office Open XML，zip 打包）。
//
// 为什么要有真 xlsx：命令叫「表单到工作表」，产出的是 CSV ——
// CSV 能被 Excel 打开，但它**不是工作表**：没有工作表名、没有单元格、
// 不能被 WPS / 福昕 / Google Sheets 当成同一个东西读回来。
// 「导出 Excel 可打开的 CSV」这句话在矩阵里一直记着降级。
//
// 产物结构（最小可用集，Excel / WPS / LibreOffice 都能打开）：
//   [Content_Types].xml
//   _rels/.rels
//   xl/workbook.xml + xl/_rels/workbook.xml.rels
//   xl/worksheets/sheet1.xml
//   xl/styles.xml
//
// 单元格用 inlineStr（不建 sharedStrings）：字段表通常只有几十上百行，
// 多一个部件就多一处能出错的连接关系。
//
// sheetName 由调用方给：工作表名是**界面上给人看的文字**，
// 领域层不该写死中文；Excel 也要求它 ≤31 字符且不含 []:*?/\。
QByteArray rowsToXlsx(const QVector<SheetRow>& rows, const QString& sheetName);

}  // namespace aipdf::doc
