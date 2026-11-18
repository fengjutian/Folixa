#pragma once

#include <QImage>

namespace aipdf::app {

// 判断一张页面渲染图是不是「空白页」。
//
// 判据是墨迹比例（明显比纸面暗的像素占比），而不是「有没有文本层」：
// 扫描件本来就没有文本层，但那是内容，不是空白。
// 反过来，只有页眉一行字的页面墨迹也很少，需要阈值配合业务判断。
//
// 纯函数，不依赖 PDFium，可以在测试里用现画的小图验证。
double inkRatio(const QImage& pageImage);

// 墨迹比例低于这个值就当空白页。
//
// 0.2% 的取法：一页 A4 上只有一个页码时，墨迹大约 0.3%~0.5%；
// 真空白页（含轻微扫描噪声）通常在 0.1% 以下。
constexpr double kBlankInkRatio = 0.002;

bool looksBlank(const QImage& pageImage, double maxInkRatio = kBlankInkRatio);

}  // namespace aipdf::app
