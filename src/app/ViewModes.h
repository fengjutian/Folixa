#pragma once

#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

#include "pdf/kernel/TextPage.h"

namespace aipdf::app::viewmodes {

// ========================================================== 标尺 ==========
//
// 标尺的坐标一律用**点**（1/72 英寸）：页面尺寸、渲染结果、滚动速度都是点，
// 只有刻度文字才换算成毫米/英寸，免得来回换算时丢精度。
//
// 领域层不做翻译：刻度文字是纯 ASCII（"12.5 mm" / "0.5 in"），
// 因为标尺是画在画布上的小字，翻译后还要按语言重排刻度宽度，得不偿失。

enum class RulerUnit {
    Points,
    Millimeters,
    Inches,
};

// 一根标尺上的一个刻度。
struct RulerTick {
    // 沿标尺的偏移（点）：0 = 标尺起点，length = 标尺末端。
    double position = 0.0;
    // 刻度文字（ASCII）。次刻度没有文字，是空串。
    QString label;
    // 主刻度（长刻线 + 文字）；其余是次刻度。
    bool major = false;
};

// 算出从 origin 开始、长度 length 的一段标尺上的刻度。
//
// origin 与 length 的单位都是点。刻度网格**锚在绝对坐标上**
// （页面上 10mm 的位置永远显示 10mm），不是从标尺起点重新数一遍——
// 否则滚动标尺时数字会跟着窗口跑，用户没法把刻度和页面上的位置对上。
//
// 刻度文字用的是绝对坐标（origin + position）换算后的值，
// position 则是相对标尺起点的偏移：画的时候用 position，
// 标数字的时候用 label。
//
// majorStepPoints <= 0 时按单位自动取：点 = 10pt、毫米 = 10mm、英寸 = 0.5in。
// 非法输入（长度非正、步长非有限等）返回空 vector，不抛异常也不给假数据。
QVector<RulerTick> rulerTicks(double origin, double length, RulerUnit unit, double majorStepPoints = 0.0);

// 单位换算：1 英寸 = 72 点，1 毫米 = 72/25.4 点。
double pointsToUnit(double points, RulerUnit unit);
double unitToPoints(double value, RulerUnit unit);

// 刻度文字。点不带单位后缀，毫米用 " mm"，英寸用 " in"
// （用 ASCII 的 in 而不是排版上的 ″，免得在不同字体里宽度不一致）。
QString formatRulerLabel(double value, RulerUnit unit);

// ======================================================== 自动滚动 ========
//
// 拖动选区/拖拽对象时把指针拖到视口边缘，视口就该跟着滚。
// 这里只算「该滚多快」，不碰滚动条：滚动条归视图层，模型层给速度就够了。

struct AutoScrollState {
    bool active = false;
    // 点/秒：正 = 向下滚，负 = 向上滚。
    double velocity = 0.0;
};

// 依据指针在视口中的位置算滚动速度。
//
// pointerY 是指针在视口里的 y（显示空间，原点在左上，向下为正），
// viewportHeight 是视口高度，edgeMargin 是「离边缘多近算贴边」，
// maxVelocity 是贴边时的最大速度（点/秒）。
//
// 贴边越紧速度越大：指针正好在边缘上时是满速，指针在边缘区里侧时线性衰减到 0。
// 视口中央不滚动（active = false，velocity = 0）。
// 参数非法（视口高度/边缘宽度/最大速度非正或非有限）时返回不滚动状态。
AutoScrollState autoScrollState(double pointerY, double viewportHeight, double edgeMargin, double maxVelocity);

// ======================================================== 页面过渡 ========

enum class PageTransition {
    None,
    Fade,
    SlideLeft,   // 旧页向左让位，新页从右边进来
    SlideRight,  // 旧页向右让位，新页从左边进来
};

// 过渡参数：t 是进度（0 = 旧页完全可见，1 = 过渡结束）。
struct PageTransitionFrame {
    // 旧页的透明度：1 = 完全不透明。
    double opacity = 1.0;
    // 旧页的水平偏移，单位是**页宽**（1.0 = 一个页宽）。
    // 归一化而不是点，是为了跟 opacity 一样与页面尺寸无关；
    // 画的时候乘以页宽的像素值即可。
    double offsetX = 0.0;
};

// 按进度算一帧过渡。
//
// 进度会被夹到 [0, 1]：动画计时器偶尔会给出略大于 1 或略小于 0 的值，
// 不夹住的话会画出花屏的偏移。非有限的进度按「过渡还没开始」处理。
PageTransitionFrame pageTransitionFrame(PageTransition transition, double t);

// ====================================================== 纯文本查看器 ======

struct PlainTextOptions {
    // 按行左边界的差补出缩进空格（pdftotext -layout 的做法）。
    // 关掉就只保留文本层里真实存在的空格。
    bool preserveIndent = true;
};

// 把一页的文本层整理成纯文本：按阅读顺序逐行，行与行之间换行。
//
// 行的划分复用 app::userSpaceTextLines：它用的就是 app::TextLayout 那套
// 「按竖直中心聚类、容差半个行高」的规则，所以同一页在选区里和在纯文本
// 视图里是同样的行，不会出现「选区高亮跨了两行、纯文本却是三行」。
//
// 空格必须保留：PDF 的文本层里词与词之间本来就有空格字符，
// 丢掉的话「Full text search」会粘成「Fulltextsearch」。
//
// 空页（没有字符，或字符全是空的）返回空串。
QString pageAsPlainText(const pdf::TextPage& text, const PlainTextOptions& options = PlainTextOptions{});

// ====================================================== 逆序阅读 ==========
//
// 日漫式「从右往左」阅读：页面本身不翻，**显示空间**的内容左右镜像，
// 于是画面上的文字是反的，读者按自己的习惯从右边开始看。
//
// 之所以放在显示层而不是写进 PDF：改的是「怎么显示」，不是「文档是什么」。

// 把显示空间的 x 坐标镜像到页面宽度内：mirrorX(x) = pageWidth - x。
// 页面宽度非法（非正/非有限）时返回原值（恒等变换），不猜。
double mirrorX(double x, double pageWidth);

// 镜像后的矩形：左右两边互换，宽度与高度不变。
// 显示空间的 y 轴朝下，镜像只影响 x，所以 top/bottom 原样保留。
QRectF mirrorRect(const QRectF& rect, double pageWidth);

// ====================================================== 沉浸阅读 ==========

// 沉浸阅读时界面要变成什么样。
//
// 为什么要有这个"计划"：沉浸阅读要做的事有好几件（藏 ribbon、藏标签栏、
// 藏状态栏、藏面板、全屏、光标自动隐藏），它们必须**一起**开关。
// 少做一件就成了"半沉浸"，用户看着别扭。
struct ImmersiveReadingPlan {
    // 藏掉功能区（Ribbon）。
    bool hideRibbon = true;
    // 藏掉文档标签栏。
    bool hideTabs = true;
    // 藏掉状态栏。
    bool hideStatusBar = true;
    // 藏掉左右面板（导航 / AI）。
    bool hideDocks = true;
    // 窗口全屏。
    bool fullScreen = true;
    // 鼠标静止一会儿就隐藏光标。
    bool autoHideCursor = true;
};

// 算沉浸阅读的界面计划。
//
// focusMode 为真表示用户已经在专注模式里（面板本来就藏着了）：
// 此时面板不用再藏一次，但全屏与光标自动隐藏照旧。
// 反过来看，专注模式开着时进入沉浸阅读，退出沉浸阅读后仍留在专注模式 ——
// 两个开关各自独立，谁也不吃掉谁。
ImmersiveReadingPlan immersiveReadingPlan(bool focusMode);

// ====================================================== 手型平移 ==========

// 拖拽平移的状态。
//
// 为什么把状态放在领域层：平移的规则是"内容跟着手走"，
// 这条规则可以用纯函数算出来，不必真的拖一次鼠标才知道对不对。
struct PanDragState {
    bool active = false;
    // 按下位置（视口坐标）。
    QPoint anchor;
    // 按下时的水平 / 垂直滚动位置。
    int startScrollX = 0;
    int startScrollY = 0;
};

// 按下时建立平移状态。
PanDragState beginPanDrag(const QPoint& pressPos, int scrollX, int scrollY);

// 拖到 currentPointer 时的新滚动位置（夹到 [0, range]）。
//
// 方向：抓住内容往右拖 → 内容跟着往右走 → 滚动位置**变小**。
// range 是滚动条的最大值（内容减去视口）；传 0 或负数时按 0 处理。
int panScrollValue(int startValue, int startPointer, int currentPointer, int range);

}  // namespace aipdf::app::viewmodes
