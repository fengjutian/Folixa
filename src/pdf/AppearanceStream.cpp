#include "pdf/AppearanceStream.h"

#include <algorithm>
#include <cmath>

namespace aipdf::pdf {

namespace {

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

// 局部坐标：以 bbox 左下角为原点，并夹在 [0, w] x [0, h] 内。
// 夹的意义是：外观流一旦画出 BBox，阅读器会按 BBox 缩放，越界部分直接消失。
//
// 注意：这些 QRectF 装的是 **PDF user space** 的坐标（y 向上），
// 所以 rect.y() 是下边界、rect.y() + height 是上边界。
// 用 Qt 的 bottom()（= y + height）当"下边界"会整体翻过来。
struct Local {
    double w = 0;
    double h = 0;
    QRectF bbox;

    QRectF map(const QRectF& rect) const {
        const double left = std::clamp(rect.x() - bbox.x(), 0.0, w);
        const double bottom = std::clamp(rect.y() - bbox.y(), 0.0, h);
        const double right = std::clamp(rect.x() + rect.width() - bbox.x(), 0.0, w);
        const double top = std::clamp(rect.y() + rect.height() - bbox.y(), 0.0, h);
        return QRectF(left, bottom, right - left, top - bottom);
    }

    QPointF map(const QPointF& point) const {
        return QPointF(std::clamp(point.x() - bbox.x(), 0.0, w),
                       std::clamp(point.y() - bbox.y(), 0.0, h));
    }
};

void appendRectPath(QByteArray& out, const QRectF& rect) {
    out += num(rect.x());
    out += ' ';
    out += num(rect.y());
    out += ' ';
    out += num(rect.width());
    out += ' ';
    out += num(rect.height());
    out += " re\n";
}

QRectF effectiveBounds(const Annotation& annotation) {
    // 显式累计 min/max：QRectF::united() 遇到空矩形会直接返回另一个，
    // 用它累加点集会把"只有一个点"的笔画吞掉。
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool have = false;
    auto add = [&](double x0, double y0, double x1, double y1) {
        if (!have) {
            minX = x0; minY = y0; maxX = x1; maxY = y1;
            have = true;
            return;
        }
        minX = std::min(minX, x0);
        minY = std::min(minY, y0);
        maxX = std::max(maxX, x1);
        maxY = std::max(maxY, y1);
    };

    const QRectF rect = annotation.rect.normalized();
    if (rect.width() > 0.0 && rect.height() > 0.0) {
        add(rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height());
    }
    for (const QRectF& quad : annotation.quads) {
        const QRectF normalized = quad.normalized();
        if (normalized.width() <= 0.0 && normalized.height() <= 0.0) {
            continue;
        }
        add(normalized.x(), normalized.y(),
            normalized.x() + normalized.width(), normalized.y() + normalized.height());
    }
    for (const QVector<QPointF>& stroke : annotation.strokes) {
        for (const QPointF& point : stroke) {
            add(point.x(), point.y(), point.x(), point.y());
        }
    }
    return have ? QRectF(minX, minY, maxX - minX, maxY - minY) : QRectF();
}

}  // namespace

double appearanceOpacity(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return 0.40;
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut:
        case AnnotationType::Square:
        case AnnotationType::Ink:
            return 1.0;
        case AnnotationType::Note:
            return 0.0;  // 便签没有图形外观
    }
    return 1.0;
}

double appearanceLineWidth(AnnotationType type) {
    switch (type) {
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut:
            return 1.2;
        case AnnotationType::Square:
            return 1.5;
        case AnnotationType::Ink:
            return 1.5;
        default:
            return 1.0;
    }
}

Appearance buildAnnotationAppearance(const Annotation& annotation) {
    Appearance appearance;
    const QRectF bounds = effectiveBounds(annotation).normalized();
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        return appearance;
    }

    appearance.bbox = bounds;
    const Local local{ bounds.width(), bounds.height(), bounds };

    const double opacity = appearanceOpacity(annotation.type);
    if (opacity <= 0.0) {
        return appearance;  // 便签：没有可画的图形
    }

    const QColor color = annotation.color.isValid()
                             ? annotation.color
                             : defaultAnnotationColor(annotation.type);

    appearance.usesTransparency = opacity < 1.0;
    if (appearance.usesTransparency) {
        appearance.resources = "/ExtGState << /GS0 << /Type /ExtGState /ca ";
        appearance.resources += num(opacity);
        appearance.resources += " >> >>";
    }

    QByteArray& out = appearance.content;
    out += "q\n";
    if (appearance.usesTransparency) {
        out += "/GS0 gs\n";
    }

    switch (annotation.type) {
        case AnnotationType::Highlight: {
            out += rgb(color);
            out += " rg\n";
            const QVector<QRectF> areas =
                annotation.quads.isEmpty() ? QVector<QRectF>{ bounds } : annotation.quads;
            for (const QRectF& area : areas) {
                const QRectF mapped = local.map(area);
                if (mapped.width() <= 0.0 || mapped.height() <= 0.0) {
                    continue;
                }
                appendRectPath(out, mapped);
            }
            out += "f\n";
            break;
        }
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut: {
            const double width = appearanceLineWidth(annotation.type);
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n0 J 0 j\n";
            const QVector<QRectF> areas =
                annotation.quads.isEmpty() ? QVector<QRectF>{ bounds } : annotation.quads;
            for (const QRectF& area : areas) {
                const QRectF mapped = local.map(area);
                if (mapped.width() <= 0.0) {
                    continue;
                }
                // 下划线贴底、删除线居中；都内缩半个线宽，免得被 BBox 裁掉一半。
                const double y = annotation.type == AnnotationType::Underline
                                     ? mapped.bottom() + width * 0.75
                                     : mapped.center().y();
                out += num(mapped.left());
                out += ' ';
                out += num(y);
                out += " m\n";
                out += num(mapped.right());
                out += ' ';
                out += num(y);
                out += " l\n";
            }
            out += "S\n";
            break;
        }
        case AnnotationType::Square: {
            const double width = appearanceLineWidth(annotation.type);
            const double inset = width * 0.5;
            const QRectF mapped = local.map(bounds);
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n1 J 0 j\n";
            appendRectPath(out, mapped.adjusted(inset, inset, -inset, -inset));
            out += "S\n";
            break;
        }
        case AnnotationType::Ink: {
            const double width = appearanceLineWidth(annotation.type);
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n1 J 1 j\n";
            for (const QVector<QPointF>& stroke : annotation.strokes) {
                if (stroke.size() < 2) {
                    continue;
                }
                for (int i = 0; i < stroke.size(); ++i) {
                    const QPointF mapped = local.map(stroke.at(i));
                    out += num(mapped.x());
                    out += ' ';
                    out += num(mapped.y());
                    out += (i == 0 ? " m\n" : " l\n");
                }
                out += "S\n";
            }
            break;
        }
        case AnnotationType::Note:
            break;
    }

    out += "Q\n";
    return appearance;
}

}  // namespace aipdf::pdf
