#include "pdf/AppearanceStream.h"
#include <QImage>

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
        case AnnotationType::Ellipse:
        case AnnotationType::Ink:
        case AnnotationType::FreeText:
        case AnnotationType::Stamp:
        case AnnotationType::Measure:
            return 1.0;
        case AnnotationType::Line:
        case AnnotationType::Arrow:
        case AnnotationType::Polygon:
        case AnnotationType::Cloud:
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
        case AnnotationType::Squiggly:
            return 1.2;
        case AnnotationType::Square:
        case AnnotationType::Ellipse:
        case AnnotationType::Line:
        case AnnotationType::Arrow:
        case AnnotationType::Polygon:
        case AnnotationType::Cloud:
            return 1.5;
        case AnnotationType::FreeText:
            return 0.0;  // 文本框默认无边框
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
    if (annotation.type == AnnotationType::Stamp) {
        appearance.resources =
            "/Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
            "/Encoding /WinAnsiEncoding >> >>";
    }
    if (annotation.type == AnnotationType::FreeText) {
        appearance.resources =
            "/Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
            "/Encoding /WinAnsiEncoding >> >>";
    }
    const Local local{ bounds.width(), bounds.height(), bounds };

    // 批注自带的值优先：属性面板改过之后，外观流必须跟着变。
    const double opacity = (annotation.opacity > 0.0 && annotation.opacity <= 1.0)
                               ? annotation.opacity
                               : appearanceOpacity(annotation.type);
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

    // 线宽同样以批注自带值为准（0 表示用类型默认）。
    const double effectiveWidth = (annotation.lineWidth > 0.0)
                                      ? annotation.lineWidth
                                      : appearanceLineWidth(annotation.type);
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
        case AnnotationType::Squiggly: {
            // 波浪线：沿着每行下沿画一条锯齿折线。
            // 振幅与周期取"看起来像波浪、又不会糊成色块"的经验值。
            const double width = effectiveWidth;
            const double amplitude = std::max(0.8, width);
            const double period = std::max(3.0, width * 3.0);
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
                const double baseY = mapped.bottom() + amplitude;
                out += num(mapped.left());
                out += ' ';
                out += num(baseY);
                out += " m\n";
                bool up = true;
                for (double x = mapped.left() + period; x <= mapped.right(); x += period) {
                    out += num(x);
                    out += ' ';
                    out += num(baseY + (up ? amplitude : -amplitude));
                    out += " l\n";
                    up = !up;
                }
                out += num(mapped.right());
                out += ' ';
                out += num(baseY);
                out += " l\nS\n";
            }
            break;
        }
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut: {
            const double width = effectiveWidth;
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
            // 这里的 lineWidth 会在下面统一按 annotation.lineWidth 覆盖。
            const double width = effectiveWidth;
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
        case AnnotationType::Ellipse: {
            // 椭圆用四段三次贝塞尔近似（kappa 系数是圆的标准近似）。
            const double width = effectiveWidth;
            const QRectF mapped = local.map(bounds).adjusted(0.75, 0.75, -0.75, -0.75);
            const double kappa = 0.5522847498;
            const double rx = mapped.width() / 2.0;
            const double ry = mapped.height() / 2.0;
            const double cx = mapped.center().x();
            const double cy = mapped.center().y();
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n1 J 0 j\n";
            out += num(cx + rx);
            out += ' ';
            out += num(cy);
            out += " m\n";
            out += num(cx + rx) + ' ' + num(cy + kappa * ry) + ' ' + num(cx + kappa * rx) + ' ' +
                   num(cy + ry) + ' ' + num(cx) + ' ' + num(cy + ry) + " c\n";
            out += num(cx - kappa * rx) + ' ' + num(cy + ry) + ' ' + num(cx - rx) + ' ' +
                   num(cy + kappa * ry) + ' ' + num(cx - rx) + ' ' + num(cy) + " c\n";
            out += num(cx - rx) + ' ' + num(cy - kappa * ry) + ' ' + num(cx - kappa * rx) + ' ' +
                   num(cy - ry) + ' ' + num(cx) + ' ' + num(cy - ry) + " c\n";
            out += num(cx + kappa * rx) + ' ' + num(cy - ry) + ' ' + num(cx + rx) + ' ' +
                   num(cy - kappa * ry) + ' ' + num(cx + rx) + ' ' + num(cy) + " c\n";
            out += "S\n";
            break;
        }
        case AnnotationType::Stamp: {
            if (!annotation.imageData.isEmpty()) {
                // 自定义图片图章：解码成 RGB，再用 Flate 压进一个 Image XObject。
                QImage image;
                if (image.loadFromData(annotation.imageData)) {
                    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
                    const QRectF mapped = local.map(bounds);
                    const QByteArray raw(reinterpret_cast<const char*>(rgb.constBits()),
                                         static_cast<qsizetype>(rgb.sizeInBytes()));
                    const QByteArray compressed = qCompress(raw, 6);
                    QByteArray body = "<< /Type /XObject /Subtype /Image /Width ";
                    body += QByteArray::number(rgb.width());
                    body += " /Height ";
                    body += QByteArray::number(rgb.height());
                    body += " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length ";
                    body += QByteArray::number(compressed.size());
                    body += " >>\nstream\n";
                    body += compressed;
                    body += "\nendstream";
                    appearance.imageObjectBody = body;

                    out += "q\n";
                    out += num(mapped.width()) + " 0 0 " + num(mapped.height()) + ' ' +
                           num(mapped.left()) + ' ' + num(mapped.bottom()) + " cm\n/Im0 Do\nQ\n";
                    appearance.resources = "/XObject << /Im0 @EXTRA0 0 R >>";
                    break;
                }
                // 图片解不开就退回文字图章，不要让用户看到一个空白框。
            }
            // 内置文字图章：一圈边框 + 居中的粗体字。没有位图，任意缩放都清晰。
            const QRectF mapped = local.map(bounds).adjusted(1.0, 1.0, -1.0, -1.0);
            const double width = std::max(1.5, mapped.height() * 0.06);
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n1 J 0 j\n";
            appendRectPath(out, mapped);
            out += "S\n";

            bool latinOnly = true;
            for (const QChar character : annotation.contents) {
                if (character.unicode() > 0xFF) {
                    latinOnly = false;
                    break;
                }
            }
            const QString label = annotation.contents.trimmed().isEmpty()
                                      ? QStringLiteral("APPROVED")
                                      : annotation.contents.trimmed();
            if (!latinOnly) {
                break;  // 中文图章由本程序画布绘制
            }
            // 字号按宽度反推：让文字大约占满 80% 宽。
            const double size =
                std::clamp(mapped.width() * 0.8 / std::max<qsizetype>(1, label.size()) / 0.62, 6.0, 48.0);
            const double textWidth = static_cast<double>(label.size()) * size * 0.62;
            const double x = mapped.center().x() - textWidth / 2.0;
            const double y = mapped.center().y() - size * 0.35;
            out += "BT\n/F1 ";
            out += num(size);
            out += " Tf\n";
            out += rgb(color);
            out += " rg\n";
            out += num(x) + ' ' + num(y) + " Td\n(";
            QByteArray escaped;
            for (const QChar character : label) {
                const char ch = static_cast<char>(character.unicode());
                if (ch == '(' || ch == ')' || ch == '\\') {
                    escaped += '\\';
                }
                escaped += ch;
            }
            out += escaped;
            out += ") Tj\nET\n";
            break;
        }
        case AnnotationType::FreeText: {
            // 只画能安全编码进 WinAnsi 的字符；出现中文等字符时留空外观，
            // 让阅读器按 /DA 与 /Contents 自己排版（我们自己的画布不受影响）。
            bool latinOnly = true;
            for (const QChar character : annotation.contents) {
                if (character.unicode() > 0xFF) {
                    latinOnly = false;
                    break;
                }
            }
            if (!latinOnly || annotation.contents.trimmed().isEmpty()) {
                break;
            }
            const double size = (annotation.fontSize > 0.0) ? annotation.fontSize : 12.0;
            const QRectF mapped = local.map(bounds);
            out += "BT\n";
            out += "/F1 ";
            out += num(size);
            out += " Tf\n";
            out += rgb(color);
            out += " rg\n";
            // 局部空间是 PDF 方向（y 向上）：bottom() 才是文字块的顶边。
            double y = mapped.bottom() - size;
            const double lineHeight = size * 1.25;
            const double maxWidth = mapped.width();
            QStringList lines = annotation.contents.split(QLatin1Char('\n'));
            for (const QString& rawLine : lines) {
                if (y < mapped.top()) {
                    break;  // 超出文本框高度就不再画
                }
                QString line = rawLine;
                // 粗排：Helvetica 平均字宽约 0.5em。超过就压缩到能放下为止。
                if (line.size() * size * 0.5 > maxWidth && maxWidth > 0.0) {
                    const int fit = static_cast<int>(maxWidth / (size * 0.5));
                    line = line.left(std::max(1, fit));
                }
                out += num(mapped.left());
                out += ' ';
                out += num(y);
                out += " Td\n";
                out += '(';
                QByteArray escaped;
                for (const QChar character : line) {
                    const char ch = static_cast<char>(character.unicode());
                    if (ch == '(' || ch == ')' || ch == '\\') {
                        escaped += '\\';
                    }
                    escaped += ch;
                }
                out += escaped;
                out += ") Tj\n";
                out += num(-mapped.left());
                out += ' ';
                out += num(-y);
                out += " Td\n";
                y -= lineHeight;
            }
            out += "ET\n";
            break;
        }
        case AnnotationType::Measure:
        case AnnotationType::Line:
        case AnnotationType::Arrow: {
            if (annotation.vertices.size() < 2) {
                break;
            }
            const double width = effectiveWidth;
            const QPointF a = local.map(annotation.vertices.at(0));
            const QPointF b = local.map(annotation.vertices.at(1));
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n1 J 0 j\n";
            out += num(a.x()) + ' ' + num(a.y()) + " m\n";
            out += num(b.x()) + ' ' + num(b.y()) + " l\nS\n";
            if (annotation.type == AnnotationType::Arrow) {
                // 开口箭头：两段短线，角度固定 25°，长度随线宽放大但不小于 6pt。
                const double dx = b.x() - a.x();
                const double dy = b.y() - a.y();
                const double length = std::hypot(dx, dy);
                if (length > 0.01) {
                    const double head = std::max(6.0, width * 4.0);
                    const double ux = dx / length;
                    const double uy = dy / length;
                    const double angle = 0.4363;  // 25°
                    const double ca = std::cos(angle);
                    const double sa = std::sin(angle);
                    const QPointF left(b.x() - head * (ux * ca - uy * sa),
                                       b.y() - head * (uy * ca + ux * sa));
                    const QPointF right(b.x() - head * (ux * ca + uy * sa),
                                        b.y() - head * (uy * ca - ux * sa));
                    out += num(b.x()) + ' ' + num(b.y()) + " m\n";
                    out += num(left.x()) + ' ' + num(left.y()) + " l\nS\n";
                    out += num(b.x()) + ' ' + num(b.y()) + " m\n";
                    out += num(right.x()) + ' ' + num(right.y()) + " l\nS\n";
                }
            }
            break;
        }
        case AnnotationType::Polygon:
        case AnnotationType::Cloud: {
            if (annotation.vertices.size() < 3) {
                break;
            }
            const double width = effectiveWidth;
            out += rgb(color);
            out += " RG\n";
            out += num(width);
            out += " w\n1 J 0 j\n";
            QVector<QPointF> points;
            points.reserve(annotation.vertices.size());
            for (const QPointF& vertex : annotation.vertices) {
                points.append(local.map(vertex));
            }
            // 云线：边框用一串小圆弧拼出来（PDF 的 /BE 只影响阅读器自绘，
            // 不认 /BE 的阅读器仍然应该看到"云朵"轮廓）。
            const bool cloud = annotation.type == AnnotationType::Cloud;
            out += num(points.first().x()) + ' ' + num(points.first().y()) + " m\n";
            for (int i = 1; i < points.size(); ++i) {
                out += num(points.at(i).x()) + ' ' + num(points.at(i).y()) + " l\n";
            }
            if (cloud) {
                out += "S\n";
                // 沿每条边撒小半圆，形成扇贝边。
                const double bump = 4.0;
                for (int i = 0; i < points.size(); ++i) {
                    const QPointF from = points.at(i);
                    const QPointF to = points.at((i + 1) % points.size());
                    const double dx = to.x() - from.x();
                    const double dy = to.y() - from.y();
                    const double len = std::hypot(dx, dy);
                    if (len < 0.01) {
                        continue;
                    }
                    const int steps = std::max(1, static_cast<int>(len / (bump * 2.0)));
                    for (int s = 0; s < steps; ++s) {
                        const double t0 = static_cast<double>(s) / steps;
                        const double t1 = static_cast<double>(s + 1) / steps;
                        const QPointF p0(from.x() + dx * t0, from.y() + dy * t0);
                        const QPointF p1(from.x() + dx * t1, from.y() + dy * t1);
                        const QPointF mid((p0.x() + p1.x()) / 2.0, (p0.y() + p1.y()) / 2.0);
                        const double nx = -dy / len;
                        const double ny = dx / len;
                        out += num(p0.x()) + ' ' + num(p0.y()) + " m\n";
                        out += num(mid.x() + nx * bump) + ' ' + num(mid.y() + ny * bump) + ' ' +
                               num(mid.x() + nx * bump) + ' ' + num(mid.y() + ny * bump) + ' ' +
                               num(p1.x()) + ' ' + num(p1.y()) + " c\nS\n";
                    }
                }
            } else {
                out += "s\n";
            }
            break;
        }
        case AnnotationType::Ink: {
            const double width = effectiveWidth;
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
