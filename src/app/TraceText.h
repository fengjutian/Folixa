#pragma once

#include <QString>
#include <QtGlobal>

namespace aipdf::app {

// 诊断轨迹（AIPDF_TRACE_FILE）里，选区文字、复制内容与搜索词属于用户数据。
// 默认只写长度占位，显式设置 AIPDF_TRACE_TEXT=1 才写原文。
//
// 验收脚本需要从轨迹里核对「选中/复制/搜索到了什么」，会统一打开这个开关；
// 日常排查请保持关闭——轨迹文件不该变成一份新的文档内容副本。
inline QString traceText(const QString& text) {
    if (qEnvironmentVariableIntValue("AIPDF_TRACE_TEXT") == 1) {
        return text;
    }
    return QStringLiteral("<redacted:%1>").arg(text.size());
}

}  // namespace aipdf::app
