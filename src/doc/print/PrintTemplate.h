#pragma once

#include <QString>
#include <QVector>

#include "doc/model/BlankDocument.h"

namespace aipdf::doc {

// 一份打印模板：把打印参数（份数、纸张、方向、缩放、页范围）存下来复用。
struct PrintTemplate {
    // 模板 id。保存时留空会自动生成一个。
    QString id;
    // 模板名。不能为空。
    QString name;
    // 打印几份。1..99。
    int copies = 1;
    // 纸张名（例如 "A4"）。
    QString paperSize = QStringLiteral("A4");
    // 方向。
    PageOrientation orientation = PageOrientation::Portrait;
    // 缩放方式："None" / "FitToPage" / "ShrinkToFit"。
    QString scaling = QStringLiteral("FitToPage");
    // 缩放比例。10..399。
    int scalePercent = 100;
    // 是否双面打印。
    bool duplex = false;
    // 是否彩色打印。
    bool color = true;
    // 页码范围（例如 "1-3,5"）。空表示全部页。
    QString pageRange;
};

// 校验一份模板。
//
// 规则：名字不能为空；份数 1..99；缩放比例 10..399；缩放方式只能是
// None / FitToPage / ShrinkToFit 三种之一。
// 不合法时返回 false，并把原因（中文）写进 error。
bool validatePrintTemplate(const PrintTemplate& t, QString* error = nullptr);

// 存一份模板。
//
// 模板 id 为空时自动生成一个（QUuid）。存的位置由 settingsKey 决定：
// 它是设置文件里的分组名，不同的 settingsKey 存到不同的分组，互不干扰。
// 存之前先校验模板，不合法直接返回 false。
bool savePrintTemplate(const PrintTemplate& t, const QString& settingsKey, QString* error = nullptr);

// 读一份模板。读不到（id 为空或不存在）时返回 false 并写明原因。
bool loadPrintTemplate(const QString& id,
                        const QString& settingsKey,
                        PrintTemplate* out,
                        QString* error = nullptr);

// 列出某个分组下的全部模板。
QVector<PrintTemplate> listPrintTemplates(const QString& settingsKey);

// 删一份模板。模板不存在时返回 false（不崩）。
bool deletePrintTemplate(const QString& id, const QString& settingsKey);

}  // namespace aipdf::doc
