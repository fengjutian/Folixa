#include "pdf/Annotation.h"

#include <QJsonArray>

namespace aipdf::pdf {

QString annotationTypeName(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return QStringLiteral("高亮");
        case AnnotationType::Underline:
            return QStringLiteral("下划线");
        case AnnotationType::StrikeOut:
            return QStringLiteral("删除线");
        case AnnotationType::Note:
            return QStringLiteral("便签");
        case AnnotationType::Square:
            return QStringLiteral("矩形");
        case AnnotationType::Ink:
            return QStringLiteral("墨迹");
    }
    return QStringLiteral("批注");
}

QColor defaultAnnotationColor(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return QColor(255, 214, 0);
        case AnnotationType::Underline:
            return QColor(38, 132, 255);
        case AnnotationType::StrikeOut:
            return QColor(226, 62, 62);
        case AnnotationType::Note:
            return QColor(255, 196, 0);
        case AnnotationType::Square:
            return QColor(52, 168, 120);
        case AnnotationType::Ink:
            return QColor(226, 62, 62);
    }
    return QColor(255, 214, 0);
}

QJsonObject annotationToJson(const Annotation& annotation) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<double>(annotation.id));
    object.insert(QStringLiteral("page"), annotation.pageIndex);
    object.insert(QStringLiteral("type"), static_cast<int>(annotation.type));
    object.insert(QStringLiteral("color"), annotation.color.name(QColor::HexArgb));

    QJsonArray quads;
    for (const QRectF& quad : annotation.quads) {
        quads.append(QJsonArray{quad.left(), quad.top(), quad.width(), quad.height()});
    }
    object.insert(QStringLiteral("quads"), quads);

    QJsonArray strokes;
    for (const QVector<QPointF>& stroke : annotation.strokes) {
        QJsonArray points;
        for (const QPointF& point : stroke) {
            points.append(QJsonArray{point.x(), point.y()});
        }
        strokes.append(points);
    }
    object.insert(QStringLiteral("strokes"), strokes);

    QJsonArray rect{annotation.rect.left(), annotation.rect.top(),
                    annotation.rect.width(), annotation.rect.height()};
    object.insert(QStringLiteral("rect"), rect);
    object.insert(QStringLiteral("contents"), annotation.contents);
    object.insert(QStringLiteral("author"), annotation.author);
    return object;
}

bool annotationFromJson(const QJsonObject& object, Annotation* annotation) {
    if (annotation == nullptr) {
        return false;
    }
    const QJsonArray rect = object.value(QStringLiteral("rect")).toArray();
    if (rect.size() != 4) {
        return false;
    }
    const QJsonArray quads = object.value(QStringLiteral("quads")).toArray();
    const QJsonArray strokes = object.value(QStringLiteral("strokes")).toArray();

    Annotation result;
    result.id = static_cast<quint64>(object.value(QStringLiteral("id")).toDouble());
    result.pageIndex = object.value(QStringLiteral("page")).toInt();
    result.type = static_cast<AnnotationType>(object.value(QStringLiteral("type")).toInt());
    result.rect = QRectF(rect.at(0).toDouble(),
                         rect.at(1).toDouble(),
                         rect.at(2).toDouble(),
                         rect.at(3).toDouble());
    result.color = QColor(object.value(QStringLiteral("color")).toString());
    if (!result.color.isValid()) {
        result.color = defaultAnnotationColor(result.type);
    }
    result.contents = object.value(QStringLiteral("contents")).toString();
    result.author = object.value(QStringLiteral("author")).toString();

    for (const QJsonValue& value : quads) {
        const QJsonArray quad = value.toArray();
        if (quad.size() != 4) {
            return false;
        }
        result.quads.append(QRectF(quad.at(0).toDouble(),
                                   quad.at(1).toDouble(),
                                   quad.at(2).toDouble(),
                                   quad.at(3).toDouble()));
    }
    for (const QJsonValue& value : strokes) {
        const QJsonArray points = value.toArray();
        if (points.isEmpty()) {
            return false;
        }
        QVector<QPointF> stroke;
        stroke.reserve(points.size());
        for (const QJsonValue& pointValue : points) {
            const QJsonArray point = pointValue.toArray();
            if (point.size() != 2) {
                return false;
            }
            stroke.append(QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
        }
        result.strokes.append(stroke);
    }

    *annotation = result;
    return true;
}

}  // namespace aipdf::pdf
