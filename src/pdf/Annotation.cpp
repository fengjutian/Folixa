#include "pdf/Annotation.h"

#include "i18n/Translate.h"

#include <QJsonArray>

namespace aipdf::pdf {

QString annotationTypeKey(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return QStringLiteral("Highlight");
        case AnnotationType::Underline:
            return QStringLiteral("Underline");
        case AnnotationType::StrikeOut:
            return QStringLiteral("StrikeOut");
        case AnnotationType::Squiggly:
            return QStringLiteral("Squiggly");
        case AnnotationType::Note:
            return QStringLiteral("Note");
        case AnnotationType::Square:
            return QStringLiteral("Square");
        case AnnotationType::Ellipse:
            return QStringLiteral("Ellipse");
        case AnnotationType::Line:
            return QStringLiteral("Line");
        case AnnotationType::Arrow:
            return QStringLiteral("Arrow");
        case AnnotationType::Polygon:
            return QStringLiteral("Polygon");
        case AnnotationType::Cloud:
            return QStringLiteral("Cloud");
        case AnnotationType::FreeText:
            return QStringLiteral("FreeText");
        case AnnotationType::Stamp:
            return QStringLiteral("Stamp");
        case AnnotationType::Measure:
            return QStringLiteral("Measure");
        case AnnotationType::FileAttachment:
            return QStringLiteral("FileAttachment");
        case AnnotationType::Sound:
            return QStringLiteral("Sound");
        case AnnotationType::Ink:
            return QStringLiteral("Ink");
    }
    return QStringLiteral("Unknown");
}

QString annotationTypeName(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return i18n::tr("aipdf::pdf::Annotation", "高亮");
        case AnnotationType::Underline:
            return i18n::tr("aipdf::pdf::Annotation", "下划线");
        case AnnotationType::StrikeOut:
            return i18n::tr("aipdf::pdf::Annotation", "删除线");
        case AnnotationType::Squiggly:
            return i18n::tr("aipdf::pdf::Annotation", "波浪线");
        case AnnotationType::Note:
            return i18n::tr("aipdf::pdf::Annotation", "便签");
        case AnnotationType::Square:
            return i18n::tr("aipdf::pdf::Annotation", "矩形");
        case AnnotationType::Ellipse:
            return i18n::tr("aipdf::pdf::Annotation", "椭圆");
        case AnnotationType::Line:
            return i18n::tr("aipdf::pdf::Annotation", "直线");
        case AnnotationType::Arrow:
            return i18n::tr("aipdf::pdf::Annotation", "箭头");
        case AnnotationType::Polygon:
            return i18n::tr("aipdf::pdf::Annotation", "多边形");
        case AnnotationType::Cloud:
            return i18n::tr("aipdf::pdf::Annotation", "云线");
        case AnnotationType::FreeText:
            return i18n::tr("aipdf::pdf::Annotation", "文本框");
        case AnnotationType::Stamp:
            return i18n::tr("aipdf::pdf::Annotation", "图章");
        case AnnotationType::Measure:
            return i18n::tr("aipdf::pdf::Annotation", "测量");
        case AnnotationType::FileAttachment:
            return i18n::tr("aipdf::pdf::Annotation", "附件");
        case AnnotationType::Sound:
            return i18n::tr("aipdf::pdf::Annotation", "声音");

        case AnnotationType::Ink:
            return i18n::tr("aipdf::pdf::Annotation", "墨迹");
    }
    return i18n::tr("aipdf::pdf::Annotation", "批注");
}

QColor defaultAnnotationColor(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return QColor(255, 214, 0);
        case AnnotationType::Underline:
            return QColor(38, 132, 255);
        case AnnotationType::StrikeOut:
            return QColor(226, 62, 62);
        case AnnotationType::Squiggly:
            return QColor(226, 62, 62);
        case AnnotationType::Note:
            return QColor(255, 196, 0);
        case AnnotationType::Square:
            return QColor(52, 168, 120);
        case AnnotationType::Ellipse:
            return QColor(52, 168, 120);
        case AnnotationType::Line:
        case AnnotationType::Arrow:
        case AnnotationType::Polygon:
        case AnnotationType::Cloud:
            return QColor(226, 62, 62);
        case AnnotationType::FreeText:
            return QColor(32, 42, 58);
        case AnnotationType::Stamp:
            return QColor(226, 62, 62);
        case AnnotationType::Measure:
            return QColor(38, 132, 255);
        case AnnotationType::FileAttachment:
            return QColor(120, 120, 120);
        case AnnotationType::Sound:
            return QColor(38, 132, 255);

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
    object.insert(QStringLiteral("alt"), annotation.altText);
    object.insert(QStringLiteral("author"), annotation.author);
    // 自定义外观流是二进制内容流，JSON 里必须用 base64：
    // 直接塞原始字节会经过 UTF-8 转换，非 ASCII 字节会被改写，往返就丢了。
    if (!annotation.customAppearance.isEmpty()) {
        object.insert(QStringLiteral("customAppearance"),
                      QString::fromLatin1(annotation.customAppearance.toBase64()));
    }
    if (!annotation.customAppearanceResources.isEmpty()) {
        object.insert(QStringLiteral("customAppearanceResources"),
                      QString::fromLatin1(annotation.customAppearanceResources.toBase64()));
    }
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
    result.altText = object.value(QStringLiteral("alt")).toString();
    result.author = object.value(QStringLiteral("author")).toString();
    result.customAppearance =
        QByteArray::fromBase64(object.value(QStringLiteral("customAppearance")).toString().toLatin1());
    result.customAppearanceResources = QByteArray::fromBase64(
        object.value(QStringLiteral("customAppearanceResources")).toString().toLatin1());

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
