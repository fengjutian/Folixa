#pragma once

#include <QColor>
#include <QMap>
#include <QPalette>
#include <QString>

class QApplication;

namespace aipdf::app::theme {

// 界面主题（设置 → 外观 → 主题）。
//
// 为什么只有三档：样式表只有一份（lightStyleSheet 是唯一事实来源），
// 暗色不是「再写一份样式表」，而是按 darkColorMap() 把浅色表里的色值
// 逐个替换掉。这样加一个控件只需要写一次样式，
// 不会出现「改了浅色忘了改暗色、暗色下留一条亮边」这种事。
enum class ThemeMode {
    Light,   // 浅色
    Dark,    // 暗色
    System,  // 跟随系统
};

// 浅色样式表。唯一事实来源。
QString lightStyleSheet();

// 暗色样式表：浅色样式表按暗色对照表逐个色值替换得到。
QString darkStyleSheet();

// 暗色对照表（两张：文字色与表面色分开）。
//
// 为什么分开：同一个色值常常一身两职，两种角色要变成不同的色值。
// - #FFFFFF：当面板底色时要变暗；当主色按钮上的白字时必须还是白的。
// - #365AA8：当蓝底时要压深（白字才压得住）；当蓝字时要提亮（深底上才看得见）。
//
// 文字色表只管 `color:` 后面的色值，表面色表管背景、描边等其余色值。
// 两张表都必须覆盖浅色样式表里的每一个色值：
// 漏掉一个，那一处在暗色主题下就会留一条亮边（ThemeTests 会拦下来）。
QMap<QString, QString> darkTextColors();
QMap<QString, QString> darkSurfaceColors();

// 浅色 / 暗色下的应用调色板。
//
// 样式表管不到的部分靠它：下拉列表的弹出层、原生提示框、滚动条轨道等。
QPalette lightPalette();
QPalette darkPalette();

// 界面正文色：浅色主题 #202A3A，暗色主题 #E8EAEE。
// 自绘控件（画布空状态、蒙层）要用它，不能直接读调色板。
QColor interfaceTextColor();

// 面板底色：比窗口底亮一档的那层表面（Ribbon、菜单、卡片）。
QColor surfaceColor();

// 当前界面是不是暗色（跟随系统时看系统设置）。
bool isDarkInterface();

// 模式 ↔ 设置里的键（light / dark / system）。
QString modeKey(ThemeMode mode);
ThemeMode modeFromKey(const QString& key);

// 读 / 写设置（appearance/theme）。
ThemeMode currentMode();
void setCurrentMode(ThemeMode mode);

// 应用主题：样式表 + 调色板 + 系统配色方案（Windows 深色标题栏）。
void apply(QApplication& application);

// 按指定模式应用（界面里切换主题时用）。
void applyMode(QApplication& application, ThemeMode mode);

}  // namespace aipdf::app::theme
