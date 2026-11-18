#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace aipdf::pdf {

// PDF 的**标准 14 号字体**：资源名 → BaseFont。
//
// 为什么是两张表而不是一张：
//   * 表单域的 /DA 里写的是**资源名**（例如 "/HeBo 10 Tf"），
//     阅读器按这个名字去 AcroForm 的 /DR /Font 里找；
//   * /DR 里那个条目写的是 **BaseFont**（例如 "/Helvetica-Bold"）。
// 两边必须对得上，所以表以资源名为键。
//
// 资源名沿用 Acrobat 的老约定（Helv / HeBo / TiRo …）：
// 已有文档里的 /DA 写的就是这些名字，换一套会让老文档里的字体设置失效。
//
// 为什么只有这 14 种：它们是 PDF 规范里**阅读器必须自带**的字体，
// 挂外部字体会破坏 SBOM 与许可证清单（见 docs/DEPENDENCY_POLICY.md）。
//
// 为什么放在头文件里：这个表只有数据，谁用都只是查表，
// 不值得为了它给每个用到它的编译单元都加一个 .cpp 依赖。
inline const QHash<QString, QString>& standardFormFonts() {
    static const QHash<QString, QString> fonts{
        {QStringLiteral("Helv"), QStringLiteral("Helvetica")},
        {QStringLiteral("HeBo"), QStringLiteral("Helvetica-Bold")},
        {QStringLiteral("HeOb"), QStringLiteral("Helvetica-Oblique")},
        {QStringLiteral("HeBO"), QStringLiteral("Helvetica-BoldOblique")},
        {QStringLiteral("Cour"), QStringLiteral("Courier")},
        {QStringLiteral("CoBo"), QStringLiteral("Courier-Bold")},
        {QStringLiteral("CoOb"), QStringLiteral("Courier-Oblique")},
        {QStringLiteral("CoBO"), QStringLiteral("Courier-BoldOblique")},
        {QStringLiteral("TiRo"), QStringLiteral("Times-Roman")},
        {QStringLiteral("TiBo"), QStringLiteral("Times-Bold")},
        {QStringLiteral("TiIt"), QStringLiteral("Times-Italic")},
        {QStringLiteral("TiBI"), QStringLiteral("Times-BoldItalic")},
        {QStringLiteral("Symb"), QStringLiteral("Symbol")},
        {QStringLiteral("ZaDb"), QStringLiteral("ZapfDingbats")},
    };
    return fonts;
}

// 14 个资源名，按"无衬线 / 衬线 / 等宽 / 符号"的顺序排。
//
// 顺序固定：界面上的下拉框按这个顺序显示，用户每次看到的排列一样。
inline QStringList standardFormFontNames() {
    return {QStringLiteral("Helv"), QStringLiteral("HeBo"), QStringLiteral("HeOb"),
            QStringLiteral("HeBO"), QStringLiteral("Cour"), QStringLiteral("CoBo"),
            QStringLiteral("CoOb"), QStringLiteral("CoBO"), QStringLiteral("TiRo"),
            QStringLiteral("TiBo"), QStringLiteral("TiIt"), QStringLiteral("TiBI"),
            QStringLiteral("Symb"), QStringLiteral("ZaDb")};
}

// 资源名 → BaseFont；不认识的名字返回空串。
inline QString standardFormBaseFont(const QString& resourceName) {
    return standardFormFonts().value(resourceName);
}

}  // namespace aipdf::pdf
