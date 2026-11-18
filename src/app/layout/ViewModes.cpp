#include "app/layout/ViewModes.h"

#include "app/layout/PageTextLines.h"

#include <algorithm>
#include <cmath>

namespace aipdf::app::viewmodes {
namespace {

// 1 英寸 = 72 点；1 英寸 = 25.4 毫米。
constexpr double kPointsPerInch = 72.0;
constexpr double kMillimetersPerInch = 25.4;

// 刻度位置的比较容差（点）：浮点误差不该让边界上的刻度凭空消失。
constexpr double kPositionEpsilon = 1e-7;

// 一根标尺最多产出多少个刻度：调用方传入荒唐长度时不至于把内存吃光。
constexpr int kMaxTicks = 20000;

// 把一个数按固定小数位格式化，并去掉多余的 0（10.00 → "10"）。
QString formatNumber(double value, int decimals) {
    const double factor = std::pow(10.0, decimals);
    double rounded = std::round(value * factor) / factor;
    if (rounded == 0.0) {
        rounded = 0.0;  // 抹掉 -0.0，免得出现 "-0"
    }
    QString text = QString::number(rounded, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text;
}

// 没有显式给步长时，各单位的主刻度间隔。
double defaultMajorStep(RulerUnit unit) {
    switch (unit) {
        case RulerUnit::Points:
            return 10.0;
        case RulerUnit::Millimeters:
            return 10.0 * kPointsPerInch / kMillimetersPerInch;
        case RulerUnit::Inches:
            return 0.5 * kPointsPerInch;
    }
    return 10.0;
}

// 一个主刻度切成几段：毫米与点切 5 段，英寸切 16 段（英寸的细分更细，
// 半英寸的主刻度下每段是 1/32 英寸）。
int minorDivisions(RulerUnit unit) {
    switch (unit) {
        case RulerUnit::Points:
            return 5;
        case RulerUnit::Millimeters:
            return 5;
        case RulerUnit::Inches:
            return 16;
    }
    return 5;
}

}  // namespace

// ========================================================== 标尺 ==========

double pointsToUnit(double points, RulerUnit unit) {
    switch (unit) {
        case RulerUnit::Points:
            return points;
        case RulerUnit::Millimeters:
            return points * kMillimetersPerInch / kPointsPerInch;
        case RulerUnit::Inches:
            return points / kPointsPerInch;
    }
    return points;
}

double unitToPoints(double value, RulerUnit unit) {
    switch (unit) {
        case RulerUnit::Points:
            return value;
        case RulerUnit::Millimeters:
            return value * kPointsPerInch / kMillimetersPerInch;
        case RulerUnit::Inches:
            return value * kPointsPerInch;
    }
    return value;
}

QString formatRulerLabel(double value, RulerUnit unit) {
    if (!std::isfinite(value)) {
        return {};
    }
    switch (unit) {
        case RulerUnit::Points:
            return formatNumber(value, 2);
        case RulerUnit::Millimeters:
            return formatNumber(value, 2) + QStringLiteral(" mm");
        case RulerUnit::Inches:
            return formatNumber(value, 3) + QStringLiteral(" in");
    }
    return {};
}

QVector<RulerTick> rulerTicks(double origin, double length, RulerUnit unit, double majorStepPoints) {
    QVector<RulerTick> ticks;
    if (!std::isfinite(origin) || !std::isfinite(length) || length <= 0.0) {
        return ticks;
    }

    double majorStep = majorStepPoints;
    if (!std::isfinite(majorStep) || majorStep <= 0.0) {
        majorStep = defaultMajorStep(unit);
    }
    const int divisions = minorDivisions(unit);
    const double minorStep = majorStep / static_cast<double>(divisions);
    if (!std::isfinite(minorStep) || minorStep <= 0.0) {
        return ticks;
    }

    const double end = origin + length;
    // 网格锚在绝对坐标 0 上：第一个落在 [origin, end] 里的网格点开始画。
    const long long firstIndex = static_cast<long long>(std::floor(origin / minorStep));
    const long long lastIndex = static_cast<long long>(std::ceil(end / minorStep));
    if (lastIndex - firstIndex > static_cast<long long>(kMaxTicks)) {
        // 长度荒唐到画不完：明确返回空，不给半根标尺。
        return ticks;
    }

    for (long long index = firstIndex; index <= lastIndex; ++index) {
        const double absolute = static_cast<double>(index) * minorStep;
        if (absolute < origin - kPositionEpsilon) {
            continue;
        }
        if (absolute > end + kPositionEpsilon) {
            continue;
        }
        RulerTick tick;
        tick.position = absolute - origin;
        // 主刻度落在「主步长」的整数倍上；次刻度网格是主刻度网格的细分，
        // 所以用下标取模判断就行，不用再算一遍浮点余数。
        tick.major = (index % divisions) == 0;
        if (tick.major) {
            // 文字用绝对坐标：页面上的 10mm 永远显示 10mm，
            // 标尺滚到哪儿都不影响数字本身。
            tick.label = formatRulerLabel(pointsToUnit(absolute, unit), unit);
        }
        ticks.append(tick);
    }
    return ticks;
}

// ======================================================== 自动滚动 ========

AutoScrollState autoScrollState(double pointerY, double viewportHeight, double edgeMargin, double maxVelocity) {
    AutoScrollState state;
    if (!std::isfinite(pointerY) || !std::isfinite(viewportHeight) ||
        !std::isfinite(edgeMargin) || !std::isfinite(maxVelocity)) {
        return state;
    }
    if (viewportHeight <= 0.0 || edgeMargin <= 0.0 || maxVelocity <= 0.0) {
        return state;
    }

    const double lowerEdge = edgeMargin;                 // 上边缘区结束的位置
    const double upperEdge = viewportHeight - edgeMargin;  // 下边缘区开始的位置

    double factor = 0.0;
    double sign = 0.0;
    if (upperEdge > lowerEdge) {
        if (pointerY < lowerEdge) {
            // 贴着上边缘：向上滚（速度为负）。
            factor = 1.0 - (pointerY / lowerEdge);
            sign = -1.0;
        } else if (pointerY > upperEdge) {
            // 贴着下边缘：向下滚（速度为正）。
            factor = 1.0 - ((viewportHeight - pointerY) / edgeMargin);
            sign = 1.0;
        } else {
            return state;  // 视口中间：不滚
        }
    } else {
        // 视口比两倍的边缘区还小：整个视口都是边缘区，按离哪边近算。
        const double toTop = pointerY;
        const double toBottom = viewportHeight - pointerY;
        if (toTop <= toBottom) {
            factor = 1.0 - (toTop / edgeMargin);
            sign = -1.0;
        } else {
            factor = 1.0 - (toBottom / edgeMargin);
            sign = 1.0;
        }
    }

    factor = qBound(0.0, factor, 1.0);
    const double velocity = sign * factor * maxVelocity;
    state.velocity = velocity;
    state.active = velocity != 0.0;
    return state;
}

// ======================================================== 页面过渡 ========

PageTransitionFrame pageTransitionFrame(PageTransition transition, double t) {
    PageTransitionFrame frame;
    if (!std::isfinite(t)) {
        return frame;  // 进度算不出来时按「还没开始」处理
    }
    // 动画计时器偶尔会给出略大于 1 或略小于 0 的值，不夹住会画出花屏的偏移。
    const double progress = qBound(0.0, t, 1.0);
    switch (transition) {
        case PageTransition::None:
            break;
        case PageTransition::Fade:
            frame.opacity = 1.0 - progress;
            break;
        case PageTransition::SlideLeft:
            // 旧页向左让位，新页从右边进来。
            frame.offsetX = -progress;
            break;
        case PageTransition::SlideRight:
            frame.offsetX = progress;
            break;
    }
    return frame;
}

// ====================================================== 纯文本查看器 ======

QString pageAsPlainText(const pdf::TextPage& text, const PlainTextOptions& options) {
    // 行划分复用 app::userSpaceTextLines：它用的就是 app::TextLayout 那套
    // 「按竖直中心聚类、容差半个行高」的规则，所以选区里的行和纯文本里的行
    // 一定是同一批，不会出现「高亮跨两行、纯文本却是三行」的矛盾。
    const QVector<UserSpaceLine> lines = userSpaceTextLines(text);
    if (lines.isEmpty()) {
        return {};
    }

    // 补缩进用的空格宽度：优先用页面上真实的空格字符宽度；
    // 页面里没有空格字符时退化成「平均字宽的一半」（经验值，够用即可）。
    double spaceWidth = 0.0;
    double spaceSum = 0.0;
    int spaceCount = 0;
    double glyphSum = 0.0;
    int glyphCount = 0;
    for (const pdf::TextChar& character : text.chars) {
        if (character.text.isEmpty() || character.box.isEmpty()) {
            continue;
        }
        glyphSum += character.box.width();
        ++glyphCount;
        if (character.text == QLatin1String(" ")) {
            spaceSum += character.box.width();
            ++spaceCount;
        }
    }
    if (spaceCount > 0) {
        spaceWidth = spaceSum / static_cast<double>(spaceCount);
    } else if (glyphCount > 0) {
        spaceWidth = 0.5 * (glyphSum / static_cast<double>(glyphCount));
    }

    // 页面内容的左边界：所有行里最靠左的那个。
    double contentLeft = lines.first().box.left();
    for (const UserSpaceLine& line : lines) {
        contentLeft = qMin(contentLeft, line.box.left());
    }

    QString result;
    for (int index = 0; index < lines.size(); ++index) {
        const UserSpaceLine& line = lines.at(index);
        if (index > 0) {
            result += QLatin1Char('\n');
        }
        if (options.preserveIndent && spaceWidth > 0.0) {
            // 行首已经有空格字符的，缩进本来就写在文本层里了，别再补一遍。
            const bool startsWithSpace = !line.text.isEmpty() && line.text.at(0).isSpace();
            if (!startsWithSpace) {
                const double gap = line.box.left() - contentLeft;
                int indent = gap > 0.0 ? static_cast<int>(std::lround(gap / spaceWidth)) : 0;
                indent = qBound(0, indent, 40);
                if (indent > 0) {
                    result += QString(indent, QLatin1Char(' '));
                }
            }
        }
        // 词内的空格是文本层里真实的空格字符，原样保留：
        // 丢掉的话「Full text search」会粘成「Fulltextsearch」。
        result += line.text;
    }
    return result;
}

// ====================================================== 逆序阅读 ==========

double mirrorX(double x, double pageWidth) {
    if (!std::isfinite(x) || !std::isfinite(pageWidth) || pageWidth <= 0.0) {
        return x;  // 页面宽度不合法：恒等变换，不猜
    }
    return pageWidth - x;
}

QRectF mirrorRect(const QRectF& rect, double pageWidth) {
    if (!std::isfinite(pageWidth) || pageWidth <= 0.0) {
        return rect;
    }
    // 手工算左右两端，不依赖 QRectF::left()/right()：
    // 传入的矩形可能带负宽度（未归一化），那样 right() 会比 left() 小。
    const double x1 = rect.left();
    const double x2 = rect.x() + rect.width();
    const double mirroredLeft = pageWidth - qMax(x1, x2);
    const double mirroredRight = pageWidth - qMin(x1, x2);
    // 显示空间的 y 轴朝下，镜像只影响 x，top/bottom 原样保留。
    return QRectF(mirroredLeft, rect.y(), mirroredRight - mirroredLeft, rect.height());
}

// ====================================================== 沉浸阅读 ==========

ImmersiveReadingPlan immersiveReadingPlan(bool focusMode) {
    ImmersiveReadingPlan plan;
    // 专注模式里面板已经藏着了：不用再藏一次，但也别把它放出来。
    plan.hideDocks = !focusMode;
    return plan;
}

// ====================================================== 手型平移 ==========

PanDragState beginPanDrag(const QPoint& pressPos, int scrollX, int scrollY) {
    PanDragState state;
    state.active = true;
    state.anchor = pressPos;
    state.startScrollX = scrollX;
    state.startScrollY = scrollY;
    return state;
}

int panScrollValue(int startValue, int startPointer, int currentPointer, int range) {
    const int maxValue = range > 0 ? range : 0;
    // 内容跟着手走：指针往右拖，滚动位置往回走。
    const int value = startValue - (currentPointer - startPointer);
    if (value < 0) {
        return 0;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

}  // namespace aipdf::app::viewmodes
