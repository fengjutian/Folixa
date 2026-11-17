#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace aipdf::app::theme {

// 阅读配色方案。
//
// 为什么只改「阅读区」而不是整套界面换肤：界面的颜色是硬编码在样式表里的
// （几百处），整套换肤要重写那份样式表，风险远大于收益；
// 而用户真正在意的「护眼 / 夜间 / 高对比」是读页面时眼睛盯着的那一块——
// 页面周围的底色。所以这里只改阅读区底色与页面描边，界面保持浅色。
//
// 页面本身的像素一个都不动：不重新渲染、不加滤镜，
// 这样「换个配色看文档」不会改变文档内容，也不会影响打印与导出。
enum class ReadingScheme {
    Light,         // 浅色（默认）
    Night,         // 夜间：深色底
    EyeCare,       // 护眼：米黄偏绿的底
    HighContrast,  // 高对比：纯黑底 + 纯白页面描边
};

// 阅读区（页面周围的底色）。
QColor backdropColor(ReadingScheme scheme);

// 页面描边色：高对比方案下用纯白，让页面边界一眼可辨。
QColor pageOutlineColor(ReadingScheme scheme);

// 深色方案（夜间）下界面文字需要反色。
bool isDarkScheme(ReadingScheme scheme);

// 应用方案：写入调色板（画布底色走 QPalette::Mid）并持久化到设置。
void applyScheme(QApplication& application, ReadingScheme scheme);

// 读回当前方案（设置里没存过就是浅色）。
ReadingScheme currentScheme();

// 只改设置并刷新调色板；供界面切换时调用。
void setCurrentScheme(ReadingScheme scheme);

}  // namespace aipdf::app::theme
