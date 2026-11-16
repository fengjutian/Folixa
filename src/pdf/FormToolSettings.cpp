#include "pdf/FormToolSettings.h"

#include <QSettings>

namespace aipdf::pdf {

QStringList FormToolSettings::fontChoices() {
    return {QStringLiteral("Helv"), QStringLiteral("TiRo"), QStringLiteral("Cour")};
}

QString FormToolSettings::appearance() const {
    // 颜色算子：黑色写 "0 g"（与 PDF 里最常见的写法一致），其余写 "r g b rg"。
    QString colorOperator;
    if (textColor == QColor(0, 0, 0)) {
        colorOperator = QStringLiteral("0 g");
    } else {
        colorOperator = QStringLiteral("%1 %2 %3 rg")
                            .arg(textColor.redF(), 0, 'f', 3)
                            .arg(textColor.greenF(), 0, 'f', 3)
                            .arg(textColor.blueF(), 0, 'f', 3);
    }
    return QStringLiteral("(/%1 %2 Tf %3)")
        .arg(fontName)
        .arg(fontSize, 0, 'f', 1)
        .arg(colorOperator);
}

FormToolSettings FormToolSettings::load() {
    QSettings values;
    return loadFrom(values);
}

void FormToolSettings::save() const {
    QSettings values;
    saveTo(values);
}

FormToolSettings FormToolSettings::loadFrom(const QSettings& values) {
    FormToolSettings settings;
    const QString font = values.value(QStringLiteral("formTools/fontName")).toString();
    if (!font.isEmpty() && fontChoices().contains(font)) settings.fontName = font;
    const double size = values.value(QStringLiteral("formTools/fontSize"),
                                     settings.fontSize).toDouble();
    // 字号必须落在能画得出来的范围：0 号字画不出东西，超过 72 也没有意义。
    if (size >= 4.0 && size <= 72.0) settings.fontSize = size;
    const QString color = values.value(QStringLiteral("formTools/textColor")).toString();
    if (!color.isEmpty()) {
        const QColor parsed(color);
        if (parsed.isValid()) settings.textColor = parsed;
    }
    return settings;
}

void FormToolSettings::saveTo(QSettings& values) const {
    values.setValue(QStringLiteral("formTools/fontName"), fontName);
    values.setValue(QStringLiteral("formTools/fontSize"), fontSize);
    values.setValue(QStringLiteral("formTools/textColor"), textColor.name());
}

}  // namespace aipdf::pdf
