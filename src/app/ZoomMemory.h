#pragma once

#include <QString>

namespace aipdf::app {

// 按文档记住的缩放比例。
//
// 为什么按"文档路径的散列"存，而不是按标题或页数：同一份文件换个位置打开是另一份
// 视图，而两份同名文件放在不同目录里又该各记各的。路径散列同时满足这两点，
// 且不会把用户的完整路径写进设置文件（路径里有用户名、项目名这类信息）。
QString zoomSettingsKey(const QString& filePath);

// 记住某份文档的缩放。zoom 超出合理区间或文件路径为空时忽略。
void rememberZoomForFile(const QString& filePath, double zoom);

// 取回记住的缩放。没记过或记录的数值不可用时返回 0（调用方据此保持默认）。
double rememberedZoomForFile(const QString& filePath);

// 合理的缩放区间（与 PdfView 的上下限保持一致）。
inline constexpr double kMinRememberedZoom = 0.1;
inline constexpr double kMaxRememberedZoom = 8.0;

}  // namespace aipdf::app
