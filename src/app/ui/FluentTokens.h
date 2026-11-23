#pragma once

#include <QColor>

namespace aipdf::app::fluent {

// Fluent 2 的语义角色。业务组件只依赖角色，不保存具体色值。
enum class ColorRole {
    NeutralBackground1,
    NeutralBackground2,
    NeutralBackground3,
    NeutralBackgroundHover,
    NeutralBackgroundPressed,
    NeutralForeground1,
    NeutralForeground2,
    NeutralForeground3,
    NeutralForegroundDisabled,
    NeutralStroke1,
    NeutralStroke2,
    BrandBackground,
    BrandBackgroundHover,
    BrandBackgroundPressed,
    BrandBackgroundSelected,
    BrandForeground,
    BrandStroke,
    DangerForeground,
    DangerBackground,
};

enum class MetricRole {
    SpacingXXS,
    SpacingXS,
    SpacingSNudge,
    SpacingS,
    SpacingMNudge,
    SpacingM,
    SpacingL,
    SpacingXL,
    SpacingXXL,
    SpacingXXXL,
    RadiusSmall,
    RadiusMedium,
    RadiusLarge,
    RadiusXLarge,
    StrokeThin,
    StrokeThick,
    ControlSmall,
    ControlMedium,
    ControlLarge,
    ListItemMedium,
    NavigationItem,
};

QColor color(ColorRole role, bool dark = false);
int metric(MetricRole role);

}  // namespace aipdf::app::fluent
