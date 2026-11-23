#include "app/ui/FluentTokens.h"

namespace aipdf::app::fluent {

QColor color(ColorRole role, bool dark) {
    if (dark) {
        switch (role) {
            case ColorRole::NeutralBackground1: return QColor(QStringLiteral("#1B2432"));
            case ColorRole::NeutralBackground2: return QColor(QStringLiteral("#151D29"));
            case ColorRole::NeutralBackground3: return QColor(QStringLiteral("#222D3D"));
            case ColorRole::NeutralBackgroundHover: return QColor(QStringLiteral("#2A3648"));
            case ColorRole::NeutralBackgroundPressed: return QColor(QStringLiteral("#344156"));
            case ColorRole::NeutralForeground1: return QColor(QStringLiteral("#EDF2F8"));
            case ColorRole::NeutralForeground2: return QColor(QStringLiteral("#B5C0D0"));
            case ColorRole::NeutralForeground3: return QColor(QStringLiteral("#8491A5"));
            case ColorRole::NeutralForegroundDisabled: return QColor(QStringLiteral("#667287"));
            case ColorRole::NeutralStroke1: return QColor(QStringLiteral("#4A5870"));
            case ColorRole::NeutralStroke2: return QColor(QStringLiteral("#344156"));
            case ColorRole::BrandBackground: return QColor(QStringLiteral("#4169C1"));
            case ColorRole::BrandBackgroundHover: return QColor(QStringLiteral("#5277E8"));
            case ColorRole::BrandBackgroundPressed: return QColor(QStringLiteral("#3555B7"));
            case ColorRole::BrandBackgroundSelected: return QColor(QStringLiteral("#263752"));
            case ColorRole::BrandForeground: return QColor(QStringLiteral("#8FB4FF"));
            case ColorRole::BrandStroke: return QColor(QStringLiteral("#8FB4FF"));
            case ColorRole::DangerForeground: return QColor(QStringLiteral("#FF99A4"));
            case ColorRole::DangerBackground: return QColor(QStringLiteral("#44272B"));
        }
    }

    switch (role) {
        case ColorRole::NeutralBackground1: return QColor(QStringLiteral("#FFFFFF"));
        case ColorRole::NeutralBackground2: return QColor(QStringLiteral("#FAFAFA"));
        case ColorRole::NeutralBackground3: return QColor(QStringLiteral("#F5F5F5"));
        case ColorRole::NeutralBackgroundHover: return QColor(QStringLiteral("#F0F0F0"));
        case ColorRole::NeutralBackgroundPressed: return QColor(QStringLiteral("#E0E0E0"));
        case ColorRole::NeutralForeground1: return QColor(QStringLiteral("#242424"));
        case ColorRole::NeutralForeground2: return QColor(QStringLiteral("#424242"));
        case ColorRole::NeutralForeground3: return QColor(QStringLiteral("#616161"));
        case ColorRole::NeutralForegroundDisabled: return QColor(QStringLiteral("#BDBDBD"));
        case ColorRole::NeutralStroke1: return QColor(QStringLiteral("#D1D1D1"));
        case ColorRole::NeutralStroke2: return QColor(QStringLiteral("#E0E0E0"));
        case ColorRole::BrandBackground: return QColor(QStringLiteral("#4169C1"));
        case ColorRole::BrandBackgroundHover: return QColor(QStringLiteral("#365AA8"));
        case ColorRole::BrandBackgroundPressed: return QColor(QStringLiteral("#24499D"));
        case ColorRole::BrandBackgroundSelected: return QColor(QStringLiteral("#E9F0FB"));
        case ColorRole::BrandForeground: return QColor(QStringLiteral("#3159B8"));
        case ColorRole::BrandStroke: return QColor(QStringLiteral("#5277E8"));
        case ColorRole::DangerForeground: return QColor(QStringLiteral("#C50F1F"));
        case ColorRole::DangerBackground: return QColor(QStringLiteral("#FDE7E9"));
    }
    return {};
}

int metric(MetricRole role) {
    switch (role) {
        case MetricRole::SpacingXXS: return 2;
        case MetricRole::SpacingXS: return 4;
        case MetricRole::SpacingSNudge: return 6;
        case MetricRole::SpacingS: return 8;
        case MetricRole::SpacingMNudge: return 10;
        case MetricRole::SpacingM: return 12;
        case MetricRole::SpacingL: return 16;
        case MetricRole::SpacingXL: return 20;
        case MetricRole::SpacingXXL: return 24;
        case MetricRole::SpacingXXXL: return 32;
        case MetricRole::RadiusSmall: return 2;
        case MetricRole::RadiusMedium: return 4;
        case MetricRole::RadiusLarge: return 6;
        case MetricRole::RadiusXLarge: return 8;
        case MetricRole::StrokeThin: return 1;
        case MetricRole::StrokeThick: return 2;
        case MetricRole::ControlSmall: return 24;
        case MetricRole::ControlMedium: return 32;
        case MetricRole::ControlLarge: return 40;
        case MetricRole::ListItemMedium: return 32;
        case MetricRole::NavigationItem: return 40;
    }
    return 0;
}

}  // namespace aipdf::app::fluent
