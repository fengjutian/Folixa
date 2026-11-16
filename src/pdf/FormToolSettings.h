#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace aipdf::pdf {

// 新建表单域时用的默认外观。
//
// 为什么要有它：建域对话框只管"这个域叫什么、放在哪"，外观（字体、字号、颜色）
// 每次都写成同一串 /DA。用户想统一改一遍外观，就得逐个域去改——
// 「表单工具设置」就是把这串默认值提到一处。
struct FormToolSettings {
    // 标准 14 字体：Helv（无衬线）/ TiRo（衬线）/ Cour（等宽）。
    // 只用这三种：挂外部字体会破坏 SBOM 与许可证清单。
    QString fontName = QStringLiteral("Helv");
    double fontSize = 10.0;
    QColor textColor = QColor(0, 0, 0);

    // 界面上的字体选项。
    static QStringList fontChoices();

    // 外观串：写进字段字典的 /DA。
    QString appearance() const;

    // 从设置里读；没有记录时用默认值。
    static FormToolSettings load();

    // 写回设置。
    void save() const;
};

}  // namespace aipdf::pdf
