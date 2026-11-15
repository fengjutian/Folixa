#include "pdf/AnnotationDictionary.h"

namespace aipdf::pdf {

namespace {

QByteArray number(double value) {
    // 坐标保留三位小数：PDF 单位是点，0.001 点远小于任何可见误差。
    return QByteArray::number(value, 'f', 3);
}

QByteArray pointPair(double x, double y) {
    return number(x) + ' ' + number(y);
}

}  // namespace

QByteArray annotationDictionaryExtras(const Annotation& annotation) {
    switch (annotation.type) {
        case AnnotationType::Line:
        case AnnotationType::Arrow: {
            if (annotation.vertices.size() < 2) {
                return {};
            }
            QByteArray extras = "/L [";
            extras += pointPair(annotation.vertices.at(0).x(), annotation.vertices.at(0).y());
            extras += ' ';
            extras += pointPair(annotation.vertices.at(1).x(), annotation.vertices.at(1).y());
            extras += "] ";
            if (annotation.type == AnnotationType::Arrow) {
                extras += "/LE [/None /OpenArrow] ";
            }
            return extras;
        }
        case AnnotationType::Polygon:
        case AnnotationType::Cloud: {
            if (annotation.vertices.size() < 3) {
                return {};
            }
            QByteArray extras = "/Vertices [";
            for (int i = 0; i < annotation.vertices.size(); ++i) {
                if (i > 0) {
                    extras += ' ';
                }
                extras += pointPair(annotation.vertices.at(i).x(), annotation.vertices.at(i).y());
            }
            extras += "] ";
            if (annotation.type == AnnotationType::Cloud) {
                // /C = cloudy border，/I = 起伏强度。
                extras += "/BE << /S /C /I 1 >> ";
            }
            return extras;
        }
        case AnnotationType::FreeText: {
            // 文本框的默认外观：/DA 告诉阅读器"用这个字体和字号画 /Contents"。
            // 中文没法塞进 WinAnsi，所以这里只声明字体与字号；正文本身仍在 /Contents 里，
            // 由阅读器用它自己的字体回退去排版。
            const double size = (annotation.fontSize > 0.0) ? annotation.fontSize : 12.0;
            QByteArray extras = "/DA (";
            extras += QStringLiteral("/Helvetica %1 Tf 0 g").arg(size, 0, 'f', 2).toLatin1();
            extras += ") ";
            return extras;
        }
        default:
            return {};
    }
}

}  // namespace aipdf::pdf
