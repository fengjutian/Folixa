#include "pdf/AnnotationShapes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aipdf::pdf {

namespace {

// 数字格式：3 位小数并去掉尾随 0。
// 与 AppearanceStream.cpp 的 num() 保持一致：同一份批注在屏幕与外观流里
// 的坐标不能差一个数量级，否则细看会错位。
QByteArray num(double value) {
    if (std::abs(value) < 0.0005) {
        value = 0.0;
    }
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    if (text.isEmpty() || text == QLatin1String("-0")) {
        text = QStringLiteral("0");
    }
    return text.toLatin1();
}

QByteArray rgb(const QColor& color) {
    QByteArray out;
    out += num(color.redF());
    out += ' ';
    out += num(color.greenF());
    out += ' ';
    out += num(color.blueF());
    return out;
}

void setError(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
}

// 坐标是否有限。NaN / 无穷画进外观流会让阅读器直接报错，必须先拒掉。
bool isFinitePoint(const QPointF& point) {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool isFiniteRect(const QRectF& rect) {
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) &&
           std::isfinite(rect.width()) && std::isfinite(rect.height());
}

// 两个点是否几乎重合。
bool nearlySame(const QPointF& a, const QPointF& b, double tolerance = 1e-6) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

// 页面坐标 → 批注自身坐标：原点挪到 origin（批注矩形的左下角）。
QPointF toLocal(const QPointF& page, const QPointF& origin) {
    return QPointF(page.x() - origin.x(), page.y() - origin.y());
}

// 点是否落在矩形内（留一点余量：界面换算过来的坐标常有 0.01 级误差）。
bool insideRect(const QRectF& box, const QPointF& point, double tolerance = 0.5) {
    return point.x() >= box.x() - tolerance &&
           point.x() <= box.x() + box.width() + tolerance &&
           point.y() >= box.y() - tolerance &&
           point.y() <= box.y() + box.height() + tolerance;
}

// 把一个点并进矩形（页面 user space，y 向上）。
//
// 为什么不用 QRectF::united：united 碰到零尺寸矩形会把它当空矩形直接丢掉，
// 而本仓库的 QRectF 是"y 是下边界"的 PDF 约定，Qt 自带的 top()/bottom()
// 与之相反，用它们会把矩形翻面。这里显式取 min/max，不会出错。
void growRect(QRectF* rect, const QPointF& point) {
    const double minX = std::min(rect->x(), point.x());
    const double maxX = std::max(rect->x() + rect->width(), point.x());
    const double minY = std::min(rect->y(), point.y());
    const double maxY = std::max(rect->y() + rect->height(), point.y());
    *rect = QRectF(minX, minY, maxX - minX, maxY - minY);
}

// 从 center 沿 anchor 方向射出，求与矩形边界的交点（箭头的引出点）。
//
// anchor 在框内时，交点落在 anchor 那一侧的边上（近边）；
// anchor 在框外时，交点落在远边 —— 两种情况都正好是
// "箭头从框上引出去"的那个位置。
QPointF borderExitPoint(const QRectF& box, const QPointF& center, const QPointF& anchor) {
    const double dx = anchor.x() - center.x();
    const double dy = anchor.y() - center.y();
    double t = std::numeric_limits<double>::max();
    if (std::abs(dx) > 1e-12) {
        const double edge = (dx > 0.0) ? (box.x() + box.width()) : box.x();
        t = std::min(t, (edge - center.x()) / dx);
    }
    if (std::abs(dy) > 1e-12) {
        const double edge = (dy > 0.0) ? (box.y() + box.height()) : box.y();
        t = std::min(t, (edge - center.y()) / dy);
    }
    if (!std::isfinite(t)) {
        return center;
    }
    return QPointF(center.x() + dx * t, center.y() + dy * t);
}

// 箭头折线（页面坐标）：引出点、折点（可选）、箭头尖。
struct CalloutGeometry {
    QVector<QPointF> pagePoints;
    bool hasKnee = false;
};

// 算箭头折线。失败时写中文错误并返回 false。
bool computeCalloutGeometry(const CalloutSpec& spec, CalloutGeometry* geometry, QString* error) {
    const QRectF box = spec.box.normalized();
    if (box.width() <= 0.0 || box.height() <= 0.0) {
        setError(error, QStringLiteral("批注框没有有效面积：宽或高为 0"));
        return false;
    }
    if (!isFiniteRect(box) || !isFinitePoint(spec.tip) || !isFinitePoint(spec.knee)) {
        setError(error, QStringLiteral("坐标非法：批注框、箭头尖或折点含有 NaN 或无穷"));
        return false;
    }

    const bool hasKnee = !nearlySame(spec.knee, spec.tip);
    const QPointF anchor = hasKnee ? spec.knee : spec.tip;
    const QPointF center =
        QPointF(box.x() + box.width() / 2.0, box.y() + box.height() / 2.0);
    if (nearlySame(anchor, center)) {
        setError(error, QStringLiteral("箭头方向退化：折点与箭头尖都落在批注框中心，引不出箭头"));
        return false;
    }

    geometry->pagePoints.clear();
    geometry->pagePoints.append(borderExitPoint(box, center, anchor));
    if (hasKnee) {
        geometry->pagePoints.append(spec.knee);
    }
    geometry->pagePoints.append(spec.tip);
    geometry->hasKnee = hasKnee;
    return true;
}

// 生成 callout 的内容流。
//
// frame  = 要画出来的注释框（页面坐标）。
// canvas = 批注矩形（页面坐标）：内容流的坐标范围就是它。
// pagePoints = 箭头折线（页面坐标）。
QByteArray buildCalloutContent(const QRectF& frame, const QRectF& canvas,
                               const QVector<QPointF>& pagePoints, double strokeWidth,
                               const QColor& color) {
    const QPointF origin(canvas.x(), canvas.y());
    const QPointF frameOrigin = toLocal(QPointF(frame.x(), frame.y()), origin);

    QByteArray out;
    out += "q\n";
    out += rgb(color);
    out += " RG\n";
    out += num(strokeWidth);
    out += " w\n1 J 0 j\n";

    // 裁剪区先设成批注矩形：出框的部分阅读器也会按 BBox 裁掉，
    // 自己先裁一次，引线才不会越界画到旁边的内容上。
    out += num(0.0) + ' ' + num(0.0) + ' ' + num(canvas.width()) + ' ' + num(canvas.height()) +
           " re\n";
    out += "W n\n";

    // 注释框：线宽的一半往里收，不然描边会被裁掉半条。
    const double inset = strokeWidth / 2.0;
    out += num(frameOrigin.x() + inset) + ' ' + num(frameOrigin.y() + inset) + ' ' +
           num(std::max(0.0, frame.width() - 2.0 * inset)) + ' ' +
           num(std::max(0.0, frame.height() - 2.0 * inset)) + " re\n";
    out += "S\n";

    // 引线。
    for (int i = 0; i < pagePoints.size(); ++i) {
        const QPointF point = toLocal(pagePoints.at(i), origin);
        out += num(point.x()) + ' ' + num(point.y()) + (i == 0 ? " m\n" : " l\n");
    }
    out += "S\n";

    // 箭头尖：两段短线，角度 25°，长度随线宽放大但不小于 6pt。
    if (pagePoints.size() >= 2) {
        const QPointF tip = toLocal(pagePoints.last(), origin);
        const QPointF from = toLocal(pagePoints.at(pagePoints.size() - 2), origin);
        const double dx = tip.x() - from.x();
        const double dy = tip.y() - from.y();
        const double length = std::hypot(dx, dy);
        if (length > 0.01) {
            const double head = std::min(std::max(6.0, strokeWidth * 4.0), length * 0.45);
            const double ux = dx / length;
            const double uy = dy / length;
            const double angle = 0.4363;  // 25°
            const double ca = std::cos(angle);
            const double sa = std::sin(angle);
            const QPointF left(tip.x() - head * (ux * ca - uy * sa),
                               tip.y() - head * (uy * ca + ux * sa));
            const QPointF right(tip.x() - head * (ux * ca + uy * sa),
                                tip.y() - head * (uy * ca - ux * sa));
            out += num(tip.x()) + ' ' + num(tip.y()) + " m\n";
            out += num(left.x()) + ' ' + num(left.y()) + " l\nS\n";
            out += num(tip.x()) + ' ' + num(tip.y()) + " m\n";
            out += num(right.x()) + ' ' + num(right.y()) + " l\nS\n";
        }
    }

    out += "Q\n";
    return out;
}

double crossProduct(const QPointF& o, const QPointF& a, const QPointF& b) {
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

bool onSegment(const QPointF& a, const QPointF& b, const QPointF& p, double tolerance) {
    return p.x() >= std::min(a.x(), b.x()) - tolerance &&
           p.x() <= std::max(a.x(), b.x()) + tolerance &&
           p.y() >= std::min(a.y(), b.y()) - tolerance &&
           p.y() <= std::max(a.y(), b.y()) + tolerance;
}

// 两线段是否相交（含共线重叠与端点相碰）。
bool segmentsIntersect(const QPointF& p1, const QPointF& p2, const QPointF& p3, const QPointF& p4) {
    const double tolerance = 1e-9;
    const double d1 = crossProduct(p3, p4, p1);
    const double d2 = crossProduct(p3, p4, p2);
    const double d3 = crossProduct(p1, p2, p3);
    const double d4 = crossProduct(p1, p2, p4);
    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) &&
        ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
        return true;
    }
    if (std::abs(d1) <= tolerance && onSegment(p3, p4, p1, tolerance)) return true;
    if (std::abs(d2) <= tolerance && onSegment(p3, p4, p2, tolerance)) return true;
    if (std::abs(d3) <= tolerance && onSegment(p1, p2, p3, tolerance)) return true;
    if (std::abs(d4) <= tolerance && onSegment(p1, p2, p4, tolerance)) return true;
    return false;
}

// 多边形面积（鞋带公式）。用绝对值，顺时针/逆时针都一样。
double polygonArea(const QVector<QPointF>& polygon) {
    double sum = 0.0;
    const int count = polygon.size();
    for (int i = 0; i < count; ++i) {
        const QPointF& a = polygon.at(i);
        const QPointF& b = polygon.at((i + 1) % count);
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(sum) / 2.0;
}

}  // namespace

QVector<QPointF> calloutArrowPoints(const CalloutSpec& spec) {
    CalloutGeometry geometry;
    if (!computeCalloutGeometry(spec, &geometry, nullptr)) {
        return QVector<QPointF>();
    }
    const QRectF box = spec.box.normalized();
    const QPointF origin(box.x(), box.y());
    QVector<QPointF> local;
    local.reserve(geometry.pagePoints.size());
    for (const QPointF& point : geometry.pagePoints) {
        local.append(toLocal(point, origin));
    }
    return local;
}

QRectF calloutBounds(const CalloutSpec& spec) {
    const QRectF box = spec.box.normalized();
    CalloutGeometry geometry;
    if (!computeCalloutGeometry(spec, &geometry, nullptr)) {
        return box;
    }
    QRectF bounds = box;
    for (const QPointF& point : geometry.pagePoints) {
        growRect(&bounds, point);
    }
    return bounds;
}

QByteArray buildCalloutAppearanceStream(const CalloutSpec& spec, QString* error) {
    setError(error, QString());
    CalloutGeometry geometry;
    if (!computeCalloutGeometry(spec, &geometry, error)) {
        return QByteArray();
    }

    const QRectF box = spec.box.normalized();
    // 箭头必须完全落在批注框内：外观流的 BBox 就是批注矩形，
    // 出框的箭头会被阅读器裁掉半截 —— 那是半成品，宁可判失败。
    if (!insideRect(box, spec.tip)) {
        setError(error, QStringLiteral("箭头完全在批注框外：出框的箭头会被外观流裁成半截，已拒绝生成"));
        return QByteArray();
    }
    if (geometry.hasKnee && !insideRect(box, spec.knee)) {
        setError(error, QStringLiteral("折点完全在批注框外：出框的引线会被外观流裁成半截，已拒绝生成"));
        return QByteArray();
    }
    if (nearlySame(geometry.pagePoints.first(), spec.tip, 1e-3)) {
        setError(error, QStringLiteral("箭头长度为 0：箭头尖与引出点重合"));
        return QByteArray();
    }

    const double strokeWidth = (spec.strokeWidth > 0.0) ? spec.strokeWidth : 1.0;
    const QColor color = spec.color.isValid() ? spec.color : QColor(226, 62, 62);
    return buildCalloutContent(box, box, geometry.pagePoints, strokeWidth, color);
}

QByteArray buildCalloutAppearanceStreamExtended(const CalloutSpec& spec, QString* error) {
    setError(error, QString());
    CalloutGeometry geometry;
    if (!computeCalloutGeometry(spec, &geometry, error)) {
        return QByteArray();
    }

    // 批注矩形 = 注释框 ∪ 箭头：箭头伸出框外也不会被裁掉。
    QRectF canvas = spec.box.normalized();
    for (const QPointF& point : geometry.pagePoints) {
        growRect(&canvas, point);
    }
    if (nearlySame(geometry.pagePoints.first(), spec.tip, 1e-3)) {
        setError(error, QStringLiteral("箭头长度为 0：箭头尖与引出点重合"));
        return QByteArray();
    }

    const double strokeWidth = (spec.strokeWidth > 0.0) ? spec.strokeWidth : 1.0;
    const QColor color = spec.color.isValid() ? spec.color : QColor(226, 62, 62);
    return buildCalloutContent(spec.box.normalized(), canvas, geometry.pagePoints, strokeWidth, color);
}

QRectF polygonBounds(const QVector<QPointF>& polygon) {
    if (polygon.isEmpty()) {
        return QRectF();
    }
    double minX = polygon.first().x();
    double maxX = minX;
    double minY = polygon.first().y();
    double maxY = minY;
    for (const QPointF& point : polygon) {
        minX = std::min(minX, point.x());
        maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y());
        maxY = std::max(maxY, point.y());
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

bool polygonIsSelfIntersecting(const QVector<QPointF>& polygon) {
    const int count = polygon.size();
    if (count < 4) {
        return false;  // 少于 4 个顶点不可能出现交叉
    }
    for (int i = 0; i < count; ++i) {
        const QPointF a1 = polygon.at(i);
        const QPointF a2 = polygon.at((i + 1) % count);
        for (int j = i + 1; j < count; ++j) {
            // 相邻边共享端点，不算自交。
            const bool adjacent = (j == i + 1) || (i == 0 && j == count - 1);
            if (adjacent) {
                continue;
            }
            const QPointF b1 = polygon.at(j);
            const QPointF b2 = polygon.at((j + 1) % count);
            if (segmentsIntersect(a1, a2, b1, b2)) {
                return true;
            }
        }
    }
    return false;
}

QByteArray buildRegionHighlightAppearanceStream(const RegionHighlightSpec& spec, QString* error) {
    setError(error, QString());
    if (spec.polygon.size() < 3) {
        setError(error, QStringLiteral("多边形至少需要 3 个顶点"));
        return QByteArray();
    }
    for (const QPointF& point : spec.polygon) {
        if (!isFinitePoint(point)) {
            setError(error, QStringLiteral("多边形坐标非法：含有 NaN 或无穷"));
            return QByteArray();
        }
    }
    if (polygonIsSelfIntersecting(spec.polygon)) {
        setError(error, QStringLiteral("多边形自交：区域高亮的轮廓会翻车，已拒绝生成"));
        return QByteArray();
    }
    if (polygonArea(spec.polygon) <= 1e-6) {
        setError(error, QStringLiteral("多边形面积为 0：顶点共线，画不出区域"));
        return QByteArray();
    }

    const QRectF canvas = polygonBounds(spec.polygon);
    const QPointF origin(canvas.x(), canvas.y());
    const QColor color = spec.color.isValid() ? spec.color : QColor(255, 214, 0);
    const double opacity = (spec.opacity > 0.0 && spec.opacity <= 1.0) ? spec.opacity : 0.35;

    QByteArray out;
    out += "q\n";
    // 不透明度写在注释里：半透明靠 /GS0 gs 生效，但注释能让读流的人
    // 一眼看出这块高亮该有多透明（实际数值在 /Resources 的 ExtGState 里）。
    out += "% alpha ";
    out += num(opacity);
    out += "\n";
    out += "/GS0 gs\n";
    out += rgb(color);
    out += " rg\n";
    for (int i = 0; i < spec.polygon.size(); ++i) {
        const QPointF point = toLocal(spec.polygon.at(i), origin);
        out += num(point.x()) + ' ' + num(point.y()) + (i == 0 ? " m\n" : " l\n");
    }
    out += "h\nf\n";
    out += "Q\n";
    return out;
}

QByteArray regionHighlightResources(double opacity) {
    const double alpha = (opacity > 0.0 && opacity <= 1.0) ? opacity : 0.35;
    QByteArray out;
    out += "/ExtGState << /GS0 << /Type /ExtGState /ca ";
    out += num(alpha);
    out += " >> >>";
    return out;
}

}  // namespace aipdf::pdf
