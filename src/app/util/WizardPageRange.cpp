#include "app/util/WizardPageRange.h"

#include "i18n/Translate.h"

#include <QStringList>

namespace aipdf::app {

bool parseWizardPageRange(const QString& text, int pageCount, OutputRange* out, QString* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::WizardPageRange", "没有地方可以写页范围结果。");
        }
        return false;
    }
    if (pageCount <= 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::WizardPageRange", "这份文档没有页面。");
        }
        return false;
    }
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::WizardPageRange", "页范围是空的。");
        }
        return false;
    }
    int first = -1;
    int last = -1;
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const int dash = trimmed.indexOf(QLatin1Char('-'));
        int from = 0;
        int to = 0;
        if (dash > 0) {
            bool fromOk = false;
            from = trimmed.left(dash).trimmed().toInt(&fromOk);
            if (!fromOk) {
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::WizardPageRange",
                                      "页范围 %1 里的页码不是数字。")
                                 .arg(trimmed);
                }
                return false;
            }
            const QString rest = trimmed.mid(dash + 1).trimmed();
            if (rest.isEmpty()) {
                to = from;
            } else {
                bool toOk = false;
                to = rest.toInt(&toOk);
                if (!toOk) {
                    if (error != nullptr) {
                        *error = i18n::tr("aipdf::app::WizardPageRange",
                                          "页范围 %1 里的页码不是数字。")
                                     .arg(trimmed);
                    }
                    return false;
                }
            }
        } else {
            bool fromOk = false;
            from = trimmed.toInt(&fromOk);
            if (!fromOk) {
                if (error != nullptr) {
                    *error = i18n::tr("aipdf::app::WizardPageRange",
                                      "页范围 %1 里的页码不是数字。")
                                 .arg(trimmed);
                }
                return false;
            }
            to = from;
        }
        if (to < from) {
            if (error != nullptr) {
                *error = i18n::tr("aipdf::app::WizardPageRange",
                                  "页范围 %1 的起止页反了。")
                             .arg(trimmed);
            }
            return false;
        }
        // 负例：越界直接判错，不再夹取
        if (from < 1 || from > pageCount || to < 1 || to > pageCount) {
            if (error != nullptr) {
                *error = QStringLiteral("页码越界");
            }
            return false;
        }
        if (first < 0) {
            first = from;
            last = to;
        } else {
            first = qMin(first, from);
            last = qMax(last, to);
        }
    }
    if (first < 0) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::WizardPageRange",
                              "页范围 %1 里没有能解析的页码。")
                         .arg(text);
        }
        return false;
    }
    // 参数是 1 基，OutputRange 是 0 基。
    out->firstPage = first - 1;
    out->lastPage = last - 1;
    return true;
}

}  // namespace aipdf::app
