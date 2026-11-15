#include "pdf/AnnotationDictionary.h"
#include <QChar>

namespace aipdf::pdf {

namespace {

QByteArray number(double value) {
    // 坐标保留三位小数：PDF 单位是点，0.001 点远小于任何可见误差。
    return QByteArray::number(value, 'f', 3);
}

QByteArray pointPair(double x, double y) {
    return number(x) + ' ' + number(y);
}

// PDF 的文本字符串只有两种写法：纯 ASCII 的 (...)，或者 UTF-16BE 的 <FEFF...>。
// 中文文件名没法塞进 Latin-1，必须走后者，否则别的阅读器读出来是乱码。
QByteArray utf16HexString(const QString& text) {
    QByteArray hex = "<FEFF";
    for (const QChar character : text) {
        const ushort value = character.unicode();
        hex += QByteArray::number(value >> 8, 16).rightJustified(2, '0').toUpper();
        hex += QByteArray::number(value & 0xFF, 16).rightJustified(2, '0').toUpper();
    }
    hex += ">";
    return hex;
}

bool needsUtf16(const QString& text) {
    for (const QChar character : text) {
        if (character.unicode() > 0x7E) {
            return true;
        }
    }
    return false;
}

QByteArray fileSpecObject(const QString& name) {
    QByteArray body = "<< /Type /Filespec /F ";
    if (needsUtf16(name)) {
        body += utf16HexString(name);
    } else {
        body += "(";
        body += name.toLatin1().replace("(", "\\(").replace(")", "\\)");
        body += ")";
    }
    body += " /UF ";
    body += utf16HexString(name);  // /UF 一律 UTF-16：它就是给"现代阅读器"看的全名
    body += " /EF << /F @EXTRA1 0 R >> >>";
    return body;
}

QByteArray embeddedFileObject(const QByteArray& data) {
    QByteArray body = "<< /Type /EmbeddedFile /Length ";
    body += QByteArray::number(data.size());
    body += " >>\nstream\n";
    body += data;
    body += "\nendstream";
    return body;
}

}  // namespace

AnnotationDictionaryParts annotationDictionaryParts(const Annotation& annotation) {
    AnnotationDictionaryParts parts;
    parts.entries = annotationDictionaryExtras(annotation);
    if (annotation.type == AnnotationType::FileAttachment && !annotation.attachmentData.isEmpty()) {
        parts.entries += "/FS @EXTRA0 0 R ";
        parts.objects.append(fileSpecObject(annotation.attachmentName));
        parts.objects.append(embeddedFileObject(annotation.attachmentData));
    }
    if (annotation.type == AnnotationType::Sound && !annotation.soundData.isEmpty()) {
        QByteArray body = "<< /Type /Sound /R ";
        body += QByteArray::number(annotation.soundRate);
        body += " /C ";
        body += QByteArray::number(annotation.soundChannels);
        body += " /B ";
        body += QByteArray::number(annotation.soundBits);
        body += " /Length ";
        body += QByteArray::number(annotation.soundData.size());
        body += " >>\nstream\n";
        body += annotation.soundData;
        body += "\nendstream";
        parts.objects.append(body);
    }
    return parts;
}

QByteArray textString(const QString& text) {
    // PDF 文本串：能塞进 ASCII 就用 (...)，否则用 UTF-16BE 十六进制串。
    bool ascii = true;
    for (const QChar character : text) {
        if (character.unicode() > 0x7E || character.unicode() < 0x20) {
            ascii = false;
            break;
        }
    }
    if (!ascii) {
        return utf16HexString(text);
    }
    QByteArray escaped;
    for (const QChar character : text) {
        const char ch = static_cast<char>(character.unicode());
        if (ch == '(' || ch == ')' || ch == '\\') {
            escaped += '\\';
        }
        escaped += ch;
    }
    return "(" + escaped + ")";
}

QByteArray pdfDate(const QDateTime& value) {
    const QDateTime stamp = value.isValid() ? value : QDateTime::currentDateTime();
    return "(" + stamp.toUTC().toString(QStringLiteral("'D:'yyyyMMddHHmmss'Z'")).toLatin1() + ")";
}

// 所有批注共用的元数据：作者、时间、审阅状态、回复。
QByteArray commonExtras(const Annotation& annotation) {
    QByteArray extras;
    if (!annotation.author.isEmpty()) {
        extras += "/T ";
        extras += textString(annotation.author);
        extras += ' ';
    }
    extras += "/M ";
    extras += pdfDate(annotation.createdAt);
    extras += ' ';
    if (!annotation.reviewState.isEmpty()) {
        extras += "/State ";
        extras += textString(annotation.reviewState);
        extras += " /StateModel /Review ";
    }
    // 外观属性：/BS /W 是线宽，/CA 是整体不透明度。别的阅读器靠这两个键
    // 复现我们在外观流里画的粗细与透明。
    if (annotation.lineWidth > 0.0) {
        extras += "/BS << /W ";
        extras += number(annotation.lineWidth);
        extras += " >> ";
    }
    if (annotation.opacity > 0.0 && annotation.opacity < 1.0) {
        extras += "/CA ";
        extras += number(annotation.opacity);
        extras += ' ';
    }
    if (annotation.replyToId != 0) {
        // 占位符由写入器换成父批注的对象号：/IRT 必须是对象引用，不能是 id。
        extras += "/IRT @ANNOTFolixa:";
        extras += QByteArray::number(annotation.replyToId);
        extras += " 0 R /RT /R ";
    }
    return extras;
}

QByteArray typeSpecificExtras(const Annotation& annotation);

QByteArray annotationDictionaryExtras(const Annotation& annotation) {
    return commonExtras(annotation) + typeSpecificExtras(annotation);
}

QByteArray typeSpecificExtras(const Annotation& annotation) {
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
        case AnnotationType::Measure: {
            if (annotation.vertices.size() < 2) {
                return {};
            }
            const QPointF from = annotation.vertices.at(0);
            const QPointF to = annotation.vertices.at(1);
            QByteArray extras = "/L [";
            extras += pointPair(from.x(), from.y());
            extras += ' ';
            extras += pointPair(to.x(), to.y());
            extras += "] ";
            // 距离测量：/Measure 里 /R 是"多少点等于一个单位"，/X 是单位名。
            const double scale = (annotation.measureScale > 0.0) ? annotation.measureScale : 1.0;
            extras += "/Measure << /Type /Measure /Subtype /RL /R ";
            extras += number(scale);
            extras += " /X (pt) >> ";
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
        case AnnotationType::Sound: {
            if (annotation.soundData.isEmpty()) {
                return {};
            }
            return "/Sound @EXTRA0 0 R ";
        }
        case AnnotationType::FileAttachment: {
            // /FS 指向 Filespec，Filespec 里再指向内嵌文件流——三者由写入器一次建好。
            return "/FS @EXTRA0 0 R ";
        }
        case AnnotationType::Stamp: {
            // 图章的 /Name 是标准名：福昕/Acrobat 靠它决定"这是哪一类图章"。
            const QString label = annotation.contents.trimmed().toUpper();
            QByteArray name = "Draft";
            if (label == QStringLiteral("APPROVED")) {
                name = "Approved";
            } else if (label == QStringLiteral("CONFIDENTIAL")) {
                name = "Confidential";
            } else if (label == QStringLiteral("REVIEWED")) {
                name = "Reviewed";
            }
            QByteArray extras = "/Name /";
            extras += name;
            extras += ' ';
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
