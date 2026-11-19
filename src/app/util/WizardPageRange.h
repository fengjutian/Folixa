#pragma once

#include "app/controllers/DocumentOutput.h"

#include <QString>

namespace aipdf::app {

// 解析动作向导的页范围参数（1 基、含两端），转成打印用的 OutputRange。
//
// 写法：「1-3,5,8-10」，逗号分隔，每段是「起-止」或单个页号；空白忽略。
// 段内起止都闭区间（1-3 就是第 1、2、3 页）。
//
// 多段时合成一个覆盖范围：从最早的那页到最晚的那页
// （打印是「这些页打成一份」，不是拆成多份）。
//
// 越界怎么处理：**夹到 [1, pageCount]**，不报错。
// 理由：用户填「1-999」想表达的是「打印到最后一页」，报错会让向导卡住；
// 而填「0」这种明显不合法的是「起页小于 1」，夹到 1 也不会打错页。
// 完全解析不出来（空串、全是非数字）、起止反了、文档没有页面，才报错。
//
// 为什么单独一个文件：这一步是纯解析（不碰文档、不碰界面、不碰打印机），
// 塞进 PdfView 就只能靠构造整个主窗口来测 —— 那样测不到真正的逻辑。
// 放成自由函数后，专项单测直接调它。
bool parseWizardPageRange(const QString& text, int pageCount, OutputRange* out,
                           QString* error = nullptr);

}  // namespace aipdf::app
