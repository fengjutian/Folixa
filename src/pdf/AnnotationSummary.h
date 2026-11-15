#pragma once

#include <QString>
#include <QVector>

#include "pdf/Annotation.h"

namespace aipdf::pdf {

// 批注摘要：给"把意见贴进邮件或打印出来"用的。
//
// 生成的是自包含的 HTML（内联样式，不引外部 CSS），因为它的用途就是被复制、
// 被邮件客户端渲染、被打印——任何外部依赖都会在这些场景里失效。
QString annotationsToHtmlSummary(const QVector<Annotation>& annotations, int pageCount,
                                 const QString& title);

}  // namespace aipdf::pdf
