#pragma once

#include "doc/model/ChunkBuilder.h"

#include <QVector>

namespace aipdf::pdf {
class TextPage;
}

namespace aipdf::app {

// 一页文本 → 行（PDF user space）。
//
// 为什么单独成文件：这份逻辑原先是 IndexController.cpp 里的文件内静态函数，
// 只有建索引用得上。现在「自动创建书签」「字数统计」也要按同一套排版特征
// 把页面切成行（标题识别靠字高、段落识别靠行距与缩进），
// 同一份规则必须只有一处实现——否则两个功能的判断会打架。
QVector<doc::ChunkLine> buildPageLines(const pdf::TextPage& text);

}  // namespace aipdf::app
