#pragma once

#include "app/layout/PageLayout.h"

#include <QString>

#include <optional>

namespace aipdf::app {

// 视图设置的按文件键。
//
// 为什么是一族键：缩放、页面布局都是「同一份文档各自的视图状态」，
// 键里带分组名（view/<group>/v1/<hash>），于是同族互不干扰，
// 将来换键算法时整族一起失效，不会读出旧算法留下的乱七八糟的值。
//
// 键里只有路径的散列，不含完整路径：设置文件里不该留下用户的
// 目录结构与项目名。
QString viewSettingsKey(const QString& filePath, const QString& group);

// 记住某份文档的页面布局。
//
// 没记过（或记录的值不是合法布局）时返回空值，调用方据此保持默认（连续长卷）。
std::optional<PageLayout::LayoutMode> rememberedLayoutForFile(const QString& filePath);

// 记住某份文档的页面布局。路径为空时忽略。
void rememberLayoutForFile(const QString& filePath, PageLayout::LayoutMode mode);

}  // namespace aipdf::app
