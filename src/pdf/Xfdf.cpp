#include "pdf/Xfdf.h"

#include <QDateTime>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace aipdf::pdf {

namespace {

QString typeTag(AnnotationType type) {
    switch (type) {
        case AnnotationType::Highlight:
            return QStringLiteral("highlight");
        case AnnotationType::Underline:
            return QStringLiteral("underline");
        case AnnotationType::StrikeOut:
            return QStringLiteral("strikeout");
        case AnnotationType::Squiggly:
            return QStringLiteral("squiggly");
        case AnnotationType::Note:
            return QStringLiteral("text");
        case AnnotationType::Square:
            return QStringLiteral("square");
        case AnnotationType::Ellipse:
            return QStringLiteral("circle");
        case AnnotationType::Line:
            return QStringLiteral("line");
        case AnnotationType::Arrow:
            return QStringLiteral("line");
        case AnnotationType::Polygon:
            return QStringLiteral("polygon");
        case AnnotationType::Cloud:
            return QStringLiteral("polygon");
        case AnnotationType::Ink:
            return QStringLiteral("ink");
        case AnnotationType::FreeText:
            return QStringLiteral("freetext");
        case AnnotationType::Stamp:
            return QStringLiteral("stamp");
        case AnnotationType::Measure:
            return QStringLiteral("line");
    }
    return QStringLiteral("square");
}

bool typeFromTag(const QString& tag, AnnotationType* out) {
    static const QHash<QString, AnnotationType> map{
        { QStringLiteral("highlight"), AnnotationType::Highlight },
        { QStringLiteral("underline"), AnnotationType::Underline },
        { QStringLiteral("strikeout"), AnnotationType::StrikeOut },
        { QStringLiteral("squiggly"), AnnotationType::Squiggly },
        { QStringLiteral("text"), AnnotationType::Note },
        { QStringLiteral("square"), AnnotationType::Square },
        { QStringLiteral("circle"), AnnotationType::Ellipse },
        { QStringLiteral("line"), AnnotationType::Line },
        { QStringLiteral("polygon"), AnnotationType::Polygon },
        { QStringLiteral("ink"), AnnotationType::Ink },
        { QStringLiteral("freetext"), AnnotationType::FreeText },
        { QStringLiteral("stamp"), AnnotationType::Stamp },
    };
    const auto it = map.constFind(tag);
    if (it == map.constEnd()) {
        return false;
    }
    *out = it.value();
    return true;
}

QString colorToHex(const QColor& color) {
    return color.name(QColor::HexRgb);
}

QColor colorFromHex(const QString& text, const QColor& fallback) {
    const QColor color(text);
    return color.isValid() ? color : fallback;
}

}  // namespace

QString annotationsToXfdf(const QVector<Annotation>& annotations, int pageCount) {
    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("xfdf"));
    writer.writeAttribute(QStringLiteral("xmlns"),
                          QStringLiteral("http://ns.adobe.com/xfdf/"));
    writer.writeStartElement(QStringLiteral("annots"));

    for (const Annotation& annotation : annotations) {
        if (annotation.pageIndex < 0 || annotation.pageIndex >= pageCount) {
            continue;
        }
        writer.writeStartElement(typeTag(annotation.type));
        writer.writeAttribute(QStringLiteral("page"),
                              QString::number(annotation.pageIndex));
        const QRectF rect = annotation.rect;
        writer.writeAttribute(QStringLiteral("rect"),
                              QStringLiteral("%1,%2,%3,%4")
                                  .arg(rect.left(), 0, 'f', 2)
                                  .arg(rect.top(), 0, 'f', 2)
                                  .arg(rect.right(), 0, 'f', 2)
                                  .arg(rect.bottom(), 0, 'f', 2));
        writer.writeAttribute(QStringLiteral("color"), colorToHex(annotation.color));
        // 审阅元数据：作者、状态、回复关系。XFDF 里它们是属性，不是元素。
        if (!annotation.author.isEmpty()) {
            writer.writeAttribute(QStringLiteral("author"), annotation.author);
        }
        if (!annotation.reviewState.isEmpty()) {
            writer.writeAttribute(QStringLiteral("state"), annotation.reviewState);
        }
        if (annotation.replyToId != 0) {
            writer.writeAttribute(QStringLiteral("replyto"),
                                  QString::number(annotation.replyToId));
        }
        if (annotation.createdAt.isValid()) {
            writer.writeAttribute(QStringLiteral("date"),
                                  annotation.createdAt.toUTC().toString(Qt::ISODate));
        }

        for (const QPointF& vertex : annotation.vertices) {
            writer.writeTextElement(QStringLiteral("vertex"),
                                    QStringLiteral("%1,%2")
                                        .arg(vertex.x(), 0, 'f', 2)
                                        .arg(vertex.y(), 0, 'f', 2));
        }
        for (const QVector<QPointF>& stroke : annotation.strokes) {
            writer.writeStartElement(QStringLiteral("ink"));
            for (const QPointF& point : stroke) {
                writer.writeTextElement(QStringLiteral("point"),
                                        QStringLiteral("%1,%2")
                                            .arg(point.x(), 0, 'f', 2)
                                            .arg(point.y(), 0, 'f', 2));
            }
            writer.writeEndElement();
        }
        if (!annotation.contents.isEmpty()) {
            writer.writeTextElement(QStringLiteral("contents"), annotation.contents);
        }
        writer.writeEndElement();
    }

    writer.writeEndElement();  // annots
    writer.writeEndElement();  // xfdf
    writer.writeEndDocument();
    return xml;
}

QVector<Annotation> annotationsFromXfdf(const QString& xml, int pageCount) {
    QVector<Annotation> result;
    QXmlStreamReader reader(xml);

    Annotation current;
    QString currentTag;
    bool inAnnotation = false;
    int strokeIndex = -1;
    bool inStrokeElement = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            if (reader.isEndElement()) {
                const QString closing = reader.name().toString();
                if (inStrokeElement && closing == QLatin1String("ink")) {
                    // 这是笔画子元素的收尾，不是批注的收尾。
                    inStrokeElement = false;
                    strokeIndex = -1;
                    continue;
                }
                // 只认"与当前批注标签同名"的结束标签：<ink> 既是墨迹批注的标签，
                // 也是多边形批注里笔画子元素的标签，按名字判类型会把它误当成一条新批注。
                if (inAnnotation && closing == currentTag) {
                    result.append(current);
                    inAnnotation = false;
                    currentTag.clear();
                }
            }
            continue;
        }

        const QString name = reader.name().toString();
        AnnotationType type = AnnotationType::Square;
        if (!inAnnotation && typeFromTag(name, &type)) {
            const int page = reader.attributes().value(QStringLiteral("page")).toInt();
            if (page < 0 || page >= pageCount) {
                // 页号越界：整条跳过（包括它的子元素）。
                reader.skipCurrentElement();
                continue;
            }
            current = Annotation{};
            current.pageIndex = page;
            current.type = type;
            const QStringList parts =
                reader.attributes().value(QStringLiteral("rect")).toString().split(QLatin1Char(','));
            if (parts.size() == 4) {
                current.rect = QRectF(parts.at(0).toDouble(), parts.at(1).toDouble(),
                                      parts.at(2).toDouble() - parts.at(0).toDouble(),
                                      parts.at(3).toDouble() - parts.at(1).toDouble());
            }
            current.color = colorFromHex(reader.attributes().value(QStringLiteral("color")).toString(),
                                         defaultAnnotationColor(type));
            current.author = reader.attributes().value(QStringLiteral("author")).toString();
            current.reviewState = reader.attributes().value(QStringLiteral("state")).toString();
            current.replyToId = reader.attributes().value(QStringLiteral("replyto")).toULongLong();
            const QString date = reader.attributes().value(QStringLiteral("date")).toString();
            if (!date.isEmpty()) {
                current.createdAt = QDateTime::fromString(date, Qt::ISODate);
            }
            inAnnotation = true;
            currentTag = name;
            strokeIndex = -1;
            continue;
        }

        if (!inAnnotation) {
            continue;
        }

        if (name == QLatin1String("vertex")) {
            const QStringList parts = reader.readElementText().split(QLatin1Char(','));
            if (parts.size() == 2) {
                current.vertices.append(QPointF(parts.at(0).toDouble(), parts.at(1).toDouble()));
            }
        } else if (name == QLatin1String("ink")) {
            current.strokes.append(QVector<QPointF>{});
            strokeIndex = current.strokes.size() - 1;
            inStrokeElement = true;
        } else if (name == QLatin1String("point") && strokeIndex >= 0) {
            const QStringList parts = reader.readElementText().split(QLatin1Char(','));
            if (parts.size() == 2) {
                current.strokes[strokeIndex].append(
                    QPointF(parts.at(0).toDouble(), parts.at(1).toDouble()));
            }
        } else if (name == QLatin1String("contents")) {
            current.contents = reader.readElementText();
        }
    }

    if (reader.hasError()) {
        return {};
    }
    return result;
}

}  // namespace aipdf::pdf
