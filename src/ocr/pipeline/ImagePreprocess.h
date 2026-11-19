#pragma once

#include <QImage>

namespace aipdf::ocr {

// OCR 前的页面图像预处理。
//
// 目的只有一个：让识别引擎少吃噪声。手机拍照式的扫描件常有阴影、灰底和孤立黑点，
// 二值化之前先把这些压下去，比事后在结果里猜「这个词是不是糊了」便宜得多。
//
// 全部是纯函数，不依赖引擎，也不改输入图。

// 转 8 位灰度（Rec.601 亮度）。
QImage toGrayscale(const QImage& source);

// 3×3 中值滤波：去掉孤立噪点，同时保住笔画边缘（均值滤波会把细线糊掉）。
QImage medianDenoise(const QImage& gray);

// Otsu 法求全局阈值；图里只有一种灰度时返回该灰度。
int otsuThreshold(const QImage& gray);

// 按阈值二值化：输出 0 或 255 的 8 位灰度图。
QImage binarize(const QImage& gray, int threshold);

// 一条完整的预处理链：灰度 → 中值去噪 → Otsu → 二值化。
QImage preprocessForOcr(const QImage& source);

// ── 连通域去噪 ────────────────────────────────────────────────────────────
//
// 中值滤波只能干掉「单个像素级」的飞点：两个像素挨在一起的噪点、笔画交叉处
// 掉下来的墨点，中值滤波都留着。可 OCR 引擎最怕的就是这些——它们会被当成一个
// 字符喂给识别器，识别出一堆乱码。
//
// 所以再加一层按连通域的处理：把二值图的黑像素按 8 邻域分块，凡是面积小到
// 不可能是任何笔画的东西整块抹掉。

// 连通域去噪参数。
struct ConnectedDenoiseOptions {
    // 面积小于该值（像素）的连通域按噪点抹掉。默认 8：300 DPI 下 8 像素约
    // 0.7×0.7 mm，任何一个汉字笔画的落墨量都远大于它，只有扫描飞点会落在
    // 1~4 像素这一档。
    int minArea = 8;

    // 细长笔画保护阈值（像素）。连通域里出现这么长的水平或垂直连续黑段，
    // 说明它是下划线、表格线或细笔画，整块保留。默认 16 ≈ 300 DPI 下 1.35 mm：
    // 短于此的横线在扫描件里基本是脏点，长于此的多半是表格框线。
    int minStrokeLength = 16;

    // 关掉细长笔画保护，只按面积删噪点。默认开启；留这个开关是为了对照测试
    // 和「宁可全删也不要保留细线」的极端场景。
    bool protectStrokes = true;
};

// 二值图连通域去噪：抹掉面积过小的孤立黑点，保留细长笔画。
//
// 输入若不是二值图，内部会先按 Otsu 二值化；输入图本身不会被修改。
QImage connectedDenoise(const QImage& binary, const ConnectedDenoiseOptions& options = ConnectedDenoiseOptions{});

// ── 纠偏 ──────────────────────────────────────────────────────────────────
//
// 扫描歪了 3°，在 OCR 引擎眼里就是「每一行都被切在字腰上」，识别率断崖。
// 纠偏就是先把页面摆正，再交给识别。

// 纠偏参数。
struct DeskewOptions {
    // 搜索范围 ±maxAngleDeg。默认 5°：手扶扫描/手机拍的真实歪斜很少超过 5°，
    // 超过这个量级多半是页面内容本身倾斜（比如表格斜放），掰直反而更糟。
    double maxAngleDeg = 5.0;

    // 粗搜步长。默认 0.5°：投影方差在 ±5° 内是单峰且光滑的，0.5° 粗搜足以
    // 锁定峰值所在的区间，比 0.1° 一步到底省 2/3 的评估次数。
    double coarseStepDeg = 0.5;

    // 精修步长。默认 0.1°：0.1° 在 300 DPI 下不到一个字高的百分之一，
    // 再细纯属自欺欺人；误差上限因此被钉在 0.05°。
    double fineStepDeg = 0.1;

    // 旋转插值方式。默认双线性：黑边会被插值成灰阶，二值化时能找回笔画；
    // 关掉则用最近邻，像素保持硬边但会有锯齿。
    bool smoothInterpolation = true;
};

// 纠偏结果。
struct DeskewResult {
    // 校正后的图（二值，白底黑字）。失败时是原图。
    QImage image;

    // 实际施加的旋转角（度）。页面右倾 3° 时为 -3.0：正值表示往顺时针掰，
    // 负值表示往逆时针掰，界面照这个符号显示「已纠偏 -3.0°」即可。
    double angleDeg = 0.0;

    // 水平投影方差：校正前 / 校正后。方差变大说明行结构真的被摆正了。
    double varianceBefore = 0.0;
    double varianceAfter = 0.0;

    // 输入为空图时为 false。
    bool valid = false;
};

// 水平投影方差法求纠偏角：把二值图按候选角度旋转，统计每行黑像素数的方差，
// 方差最大者即最佳角度。返回需要施加的旋转角（未做任何旋转）。
double estimateSkewAngle(const QImage& binary, const DeskewOptions& options = DeskewOptions{});

// 纠偏：返回校正后的图与纠偏角度。输入图不会被修改。
DeskewResult deskew(const QImage& binary, const DeskewOptions& options = DeskewOptions{});

// ── 一体化出口 ────────────────────────────────────────────────────────────

// 扫描件图像优化参数。
struct ImagePrepOptions {
    // 是否做连通域去噪。
    bool denoise = true;

    // 是否做纠偏。
    bool deskew = true;

    ConnectedDenoiseOptions denoiseOptions;
    DeskewOptions deskewOptions;
};

// 扫描件图像优化结果。
struct ImagePrepResult {
    // 优化后的二值图。
    QImage image;

    // 纠偏角度（未做纠偏时为 0）。
    double deskewAngleDeg = 0.0;

    // 输入为空图时为 false。
    bool valid = false;
};

// 扫描件图像优化全链：preprocessForOcr（灰度→中值→Otsu）→ 连通域去噪 → 纠偏。
//
// preprocessForOcr 本身的行为一个字都没改，这里只是把它当成第一段接上后两步。
// 先去噪再纠偏：纠偏要靠水平投影找角度，原始噪点会污染行墨量统计。
ImagePrepResult preprocessScannedPage(const QImage& source, const ImagePrepOptions& options = ImagePrepOptions{});

}  // namespace aipdf::ocr
