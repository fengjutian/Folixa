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

// 一张工作表：行 → 单元格。
//
// 为什么用二维字符串而不是 SheetRow：SheetRow 是「字段名 → 字段值」的两列形态，
// 那是表单导出的形状。PDF → Excel 导出的是**表格**，行列数不固定，
// 硬塞进两列结构只会把表格拆散。
struct SheetGrid {
    // 工作表名。Excel 要求 ≤31 字符且不含 []:*?/\，写入前会收拾一遍。
    QString name;
    // 逐行；每行里的单元格按列序排列。行数、列数可以不一样（缺格就是短行）。
    QVector<QVector<QString>> rows;
};

// 把多张工作表写成一个 Excel 工作簿（.xlsx）。
//
// 为什么要有多表版本：PDF → Excel 导出的是**整篇文档**，
// 一页一张表（或一页一张文字表）才对得上「页数 / 表格数一致」这个验收口径。
// 单表版本（rowsToXlsx）只能塞下一张表。
QByteArray workbookToXlsx(const QVector<SheetGrid>& sheets);

}  // namespace aipdf::doc
