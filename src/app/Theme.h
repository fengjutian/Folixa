#pragma once

#include <QString>

class QApplication;

namespace aipdf::app::theme {

// Prism Document 的浅色主题。所有颜色和尺寸集中在这里，避免控件各自硬编码。
QString lightStyleSheet();
void apply(QApplication& application);

}  // namespace aipdf::app::theme
