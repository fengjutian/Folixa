#include "pdf/forms/FormFieldWriter.h"

#include "pdf/forms/BarcodeEncoder.h"
#include "pdf/kernel/StandardFonts.h"

#include <QVector>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <memory>
#endif

namespace aipdf::pdf {
namespace {

struct ObjectSpan {
    int number = 0;
    // "N G obj" 第一个字符的字节位置：写交叉引用流时它就是对象在文件里的偏移。
    qsizetype objectStart = 0;
    qsizetype bodyBegin = 0;
    qsizetype bodyEnd = 0;
};

QByteArray padded(qsizetype value) {
    QByteArray text = QByteArray::number(value);
    while (text.size() < 10) text.prepend('0');
    return text;
}

bool parseObjectHeader(const QByteArray& header, int* number) {
    const QList<QByteArray> parts = header.trimmed().simplified().split(' ');
    if (parts.size() != 2) return false;
    bool okNumber = false;
    bool okGeneration = false;
    const int parsed = parts.at(0).toInt(&okNumber);
    parts.at(1).toInt(&okGeneration);
    if (!okNumber || !okGeneration) return false;
    *number = parsed;
    return true;
}

QVector<ObjectSpan> scanObjects(const QByteArray& pdf) {
    QVector<ObjectSpan> result;
    qsizetype pos = 0;
    while ((pos = pdf.indexOf(" obj", pos)) >= 0) {
        qsizetype lineStart = pdf.lastIndexOf('\n', pos);
        lineStart = lineStart < 0 ? 0 : lineStart + 1;
        int number = 0;
        if (!parseObjectHeader(pdf.mid(lineStart, pos - lineStart), &number)) {
            pos += 4;
            continue;
        }
        const qsizetype begin = pos + 4;
        const qsizetype end = pdf.indexOf("endobj", begin);
        if (end < 0) break;
        // 对象起点要从 "N G obj" 的头一个字符算起（前面可能有空白），
        // 后面写交叉引用流时要用它当偏移。
        qsizetype objectStart = lineStart;
        while (objectStart < pos && pdf.at(objectStart) <= ' ') ++objectStart;
        result.append({number, objectStart, begin, end});
        pos = end + 6;
    }
    return result;
}

bool findObject(const QVector<ObjectSpan>& objects, int number, ObjectSpan* out) {
    // 增量更新里后出现的同号对象才是有效版本。
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        if (it->number == number) {
            *out = *it;
            return true;
        }
    }
    return false;
}

bool isNameBoundary(char ch) {
    return ch <= ' ' || ch == '/' || ch == '<' || ch == '>' || ch == '[' || ch == ']' ||
           ch == '(' || ch == ')';
}

qsizetype findName(const QByteArray& body, const QByteArray& name, qsizetype from = 0) {
    qsizetype at = from;
    while ((at = body.indexOf(name, at)) >= 0) {
        const qsizetype end = at + name.size();
        // PDF 名称以 '/' 自身起界，前一个名称可以无空格直接结束（/Type/Page）。
        // 因此搜索以 '/' 开头的键时只需检查尾部边界。
        const bool beginOk = name.startsWith('/') || at == 0 || isNameBoundary(body.at(at - 1));
        if (beginOk && (end >= body.size() || isNameBoundary(body.at(end)))) return at;
        at = end;
    }
    return -1;
}

qsizetype matching(const QByteArray& body, qsizetype openAt, char open, char close) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (qsizetype i = openAt; i < body.size(); ++i) {
        const char ch = body.at(i);
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == ')') inString = false;
            continue;
        }
        if (ch == '(') {
            inString = true;
            continue;
        }
        if (ch == open) ++depth;
        else if (ch == close && --depth == 0) return i;
    }
    return -1;
}

qsizetype dictClose(const QByteArray& body, qsizetype openAt = 0) {
    const qsizetype start = body.indexOf("<<", openAt);
    if (start < 0) return -1;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (qsizetype i = start; i + 1 < body.size(); ++i) {
        const char ch = body.at(i);
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == ')') inString = false;
            continue;
        }
        if (ch == '(') { inString = true; continue; }
        if (body.mid(i, 2) == "<<") { ++depth; ++i; }
        else if (body.mid(i, 2) == ">>") {
            --depth;
            ++i;
            if (depth == 0) return i + 1;
        }
    }
    return -1;
}

bool findPage(const QByteArray& pdf, const QVector<ObjectSpan>& objects, int pageIndex,
              ObjectSpan* out) {
    int seen = 0;
    // 只遍历每个对象号的最后版本，避免增量更新后的页面被重复计数。
    QVector<int> visited;
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        if (visited.contains(it->number)) continue;
        visited.append(it->number);
    }
    std::sort(visited.begin(), visited.end());
    for (int number : visited) {
        ObjectSpan object;
        findObject(objects, number, &object);
        const QByteArray body = pdf.mid(object.bodyBegin, object.bodyEnd - object.bodyBegin);
        // 对象流（/Type /ObjStm）里装着压缩后的对象，不能当成页面：
        // 它的载荷里可能恰好出现 "/Type /Page" 字样，会把页面数算错。
        if (body.contains("/Type /ObjStm") || body.contains("/Type/ObjStm")) continue;
        if ((body.contains("/Type /Page") || body.contains("/Type/Page")) &&
            !(body.contains("/Type /Pages") || body.contains("/Type/Pages"))) {
            if (seen++ == pageIndex) { *out = object; return true; }
        }
    }
    return false;
}

bool parseReferenceAfter(const QByteArray& body, qsizetype keyAt, int* number) {
    qsizetype i = keyAt;
    while (i < body.size() && body.at(i) <= ' ') ++i;
    // 只接受紧跟在键后的 “N G R”。不能越过直接字典去捞其中 /Fields 的引用。
    if (i >= body.size() || body.at(i) < '0' || body.at(i) > '9') return false;
    const qsizetype begin = i;
    while (i < body.size() && body.at(i) >= '0' && body.at(i) <= '9') ++i;
    if (begin == i) return false;
    bool ok = false;
    const int value = body.mid(begin, i - begin).toInt(&ok);
    while (i < body.size() && body.at(i) <= ' ') ++i;
    while (i < body.size() && body.at(i) >= '0' && body.at(i) <= '9') ++i;
    while (i < body.size() && body.at(i) <= ' ') ++i;
    if (!ok || i >= body.size() || body.at(i) != 'R') return false;
    *number = value;
    return true;
}

QByteArray escapedPdfString(const QString& value) {
    QByteArray utf16("\xFE\xFF", 2);
    for (QChar ch : value) {
        const ushort u = ch.unicode();
        utf16.append(char((u >> 8) & 0xff));
        utf16.append(char(u & 0xff));
    }
    QByteArray result;
    for (unsigned char ch : utf16) {
        if (ch == '(' || ch == ')' || ch == '\\') result.append('\\');
        result.append(char(ch));
    }
    return result;
}

QByteArray number(double value) {
    return QByteArray::number(value, 'f', 3);
}

// 说明文字用的 PDF 字符串：只写 WinAnsi 能表示的字节，其它字符退化成 '?'。
//
// 为什么不为中文去挂外部字体资源：引入字体文件会破坏 SBOM 与许可证清单，
// 而条码本身只需要标准 14 号字体就够画得出来。
QByteArray pdfLatin1String(const QString& value) {
    QByteArray result;
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        const char byte = code < 256 ? char(code) : '?';
        if (byte == '(' || byte == ')' || byte == '\\') result.append('\\');
        result.append(byte);
    }
    return result;
}

// 把分组名净化成能安全写进 PDF 名字对象的导出名前缀。
// PDF 名字不能含空白与分隔符，非法字符一律换成 '_'；
// 全是非法字符时回退到 "Option"，免得导出一个空名字。
QString sanitizeExportPrefix(const QString& groupName) {
    QString result;
    bool hasLetterOrDigit = false;
    for (const QChar ch : groupName) {
        const ushort code = ch.unicode();
        const bool letter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool digit = code >= '0' && code <= '9';
        if (letter || digit) hasLetterOrDigit = true;
        const bool keep = letter || digit || ch == QLatin1Char('_') || ch == QLatin1Char('-');
        result.append(keep ? ch : QLatin1Char('_'));
    }
    if (!hasLetterOrDigit) return QStringLiteral("Option");
    return result;
}

// 建域时用到的额外上下文。
struct FieldExtras {
    // 单选分组的父字段对象号；0 表示不分组（行为与原来完全一致）。
    int parentObject = 0;
    // 分组单选的导出名：它是 /AP /N 里的"选中"状态名，父字段的 /V 记的就是它。
    QByteArray exportName;
    // 条形码字段的 /V 值：文本域的值必须和条码内容一致。
    QString barcodeText;
};

QByteArray fieldDictionary(const FormFieldInsert& field, int pageObject,
                           int normalAppearance, int onAppearance = 0,
                           const FieldExtras& extras = FieldExtras()) {
    QByteArray ft = "/Tx";
    int flags = (field.readOnly ? 1 : 0) | (field.required ? (1 << 1) : 0);
    QByteArray value = "()";
    QByteArray extra;
    // 分组单选：/FT、/Ff、/V、/T 全部由父字段继承，widget 自己只留位置与外观状态。
    const bool grouped = extras.parentObject > 0;
    switch (field.kind) {
        case FormFieldKind::CheckBox:
            ft = "/Btn"; value = "/Off";
            extra = "/AS /Off /AP << /N << /Off " + QByteArray::number(normalAppearance) +
                    " 0 R /Yes " + QByteArray::number(onAppearance) + " 0 R >> >> ";
            break;
        case FormFieldKind::RadioButton:
            ft = "/Btn"; flags = 1 << 15; value = "/Off";
            {
                // 分组按钮的"选中"状态名就是它的导出名（父字段的 /V 记的就是它）；
                // 不分组时沿用 /Yes。
                const QByteArray onState = grouped ? ("/" + extras.exportName) : QByteArray("/Yes");
                extra = "/AS /Off /AP << /N << /Off " + QByteArray::number(normalAppearance) +
                        " 0 R " + onState + " " + QByteArray::number(onAppearance) + " 0 R >> >> ";
            }
            break;
        case FormFieldKind::ComboBox:
            ft = "/Ch"; flags |= 1 << 17; break;
        case FormFieldKind::ListBox:
            ft = "/Ch"; break;
        case FormFieldKind::PushButton:
            ft = "/Btn"; flags = 1 << 16; extra = "/MK << /CA (按钮) >> "; break;
        case FormFieldKind::Barcode:
            ft = "/Tx"; extra = "/TU (Barcode value) /MaxLen 128 ";
            // 条形码字段就是文本域，值必须和条码内容一致，
            // 否则读表单数据拿到的是空串，而外观上明明画着条码。
            value = "(" + pdfLatin1String(extras.barcodeText) + ")";
            break;
        case FormFieldKind::Text:
            break;
    }
    if ((field.kind == FormFieldKind::Text || field.kind == FormFieldKind::Barcode) &&
        field.multiline) flags |= 1 << 12;
    if (field.kind == FormFieldKind::ComboBox || field.kind == FormFieldKind::ListBox) {
        const QStringList options = field.options.isEmpty()
                                        ? QStringList{QStringLiteral("选项 1"), QStringLiteral("选项 2")}
                                        : field.options;
        extra += "/Opt [";
        for (const QString& option : options)
            extra += "(" + escapedPdfString(option) + ") ";
        extra += "] ";
    }
    if (field.kind != FormFieldKind::CheckBox && field.kind != FormFieldKind::RadioButton)
        extra += "/AP << /N " + QByteArray::number(normalAppearance) + " 0 R >> ";
    const QRectF rect = field.rect.normalized();
    QByteArray dict = "<< /Type /Annot /Subtype /Widget";
    dict += " /P " + QByteArray::number(pageObject) + " 0 R";
    if (grouped) {
        dict += " /Parent " + QByteArray::number(extras.parentObject) + " 0 R";
    } else {
        dict += " /FT " + ft + " /T (" + escapedPdfString(field.name) + ")";
    }
    if (!field.toolTip.isEmpty()) dict += "/TU (" + escapedPdfString(field.toolTip) + ") ";
    dict += "/Rect [" + number(rect.left()) + " " + number(rect.top()) + " " +
            number(rect.right()) + " " + number(rect.bottom()) + "] /F 4";
    if (!grouped) dict += " /Ff " + QByteArray::number(flags) + " /V " + value;
    // 外观串：留空时用默认的 Helvetica 10 号黑字，「表单工具设置」可以改。
    const QByteArray appearance = field.appearance.isEmpty()
                                      ? QByteArray("(/Helv 10 Tf 0 g)")
                                      : field.appearance.toLatin1();
    dict += " /DA " + appearance + " " + extra + ">>";
    return dict;
}

QByteArray appearanceStream(double width, double height, bool checked, bool radio) {
    QByteArray commands = "0.75 w 0.375 0.375 " + number(std::max(0.0, width - 0.75)) +
                          " " + number(std::max(0.0, height - 0.75)) + " re S\n";
    if (checked) {
        if (radio) {
            const double r = std::min(width, height) * 0.28;
            const double cx = width / 2.0;
            const double cy = height / 2.0;
            // 四段 Bezier 圆，作为单选按钮的实心圆点。
            const double k = r * 0.55228475;
            commands += number(cx + r) + " " + number(cy) + " m " +
                number(cx + r) + " " + number(cy + k) + " " + number(cx + k) + " " +
                number(cy + r) + " " + number(cx) + " " + number(cy + r) + " c " +
                number(cx - k) + " " + number(cy + r) + " " + number(cx - r) + " " +
                number(cy + k) + " " + number(cx - r) + " " + number(cy) + " c " +
                number(cx - r) + " " + number(cy - k) + " " + number(cx - k) + " " +
                number(cy - r) + " " + number(cx) + " " + number(cy - r) + " c " +
                number(cx + k) + " " + number(cy - r) + " " + number(cx + r) + " " +
                number(cy - k) + " " + number(cx + r) + " " + number(cy) + " c f\n";
        } else {
            commands += "1.5 w 2 2 m " + number(width - 2) + " " + number(height - 2) +
                       " l S 2 " + number(height - 2) + " m " + number(width - 2) + " 2 l S\n";
        }
    }
    return "<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 " + number(width) +
           " " + number(height) + "] /Resources << >> /Length " +
           QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream";
}

// 条码外观流：白底 + 黑色竖条 + 可选的说明文字。
//
// 为什么条宽取整而且绝不拉伸：条码的模块宽度必须保持整数倍关系，
// 拉宽一点点就可能让扫描器读错，生成一张"看着像条码、扫不出来"的图。
// 宽度有富余时留成白边（静区），这正是扫描器需要的。
QByteArray barcodeAppearanceStream(double width, double height,
                                   const QVector<BarcodeModule>& modules, const QString& caption) {
    int totalModules = 0;
    for (const BarcodeModule& module : modules) totalModules += module.width;
    if (totalModules <= 0) return {};
    double unit = std::floor(width / totalModules);
    if (unit < 1.0) unit = 1.0;  // 至少 1 份点，否则细到根本画不出来
    const double barcodeWidth = unit * totalModules;
    const double left = std::max(0.0, (width - barcodeWidth) / 2.0);
    const double fontSize = 8.0;
    const double captionHeight = caption.isEmpty() ? 0.0 : std::min(height * 0.3, fontSize + 4.0);
    const double barHeight = std::max(0.0, height - captionHeight);

    QByteArray commands = "1 1 1 rg\n";
    commands += "0 0 " + number(width) + " " + number(height) + " re f\n";
    commands += "0 0 0 rg\n";
    double x = left;
    for (const BarcodeModule& module : modules) {
        const double moduleWidth = unit * module.width;
        if (module.bar) {
            commands += number(x) + " " + number(captionHeight) + " " + number(moduleWidth) + " " +
                number(barHeight) + " re f\n";
        }
        x += moduleWidth;
    }
    if (!caption.isEmpty()) {
        commands += "BT /Helv " + number(fontSize) + " Tf 0 g " + number(left) + " " +
            number(std::max(1.0, (captionHeight - fontSize) * 0.5 + 2.0)) + " Td " +
            pdfLatin1String(caption) + " Tj ET\n";
    }
    // 说明文字只用标准 14 号字体：/Helv 是 PDF 内置字体，不需要额外资源文件。
    const QByteArray resources =
        "<< /Font << /Helv << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>";
    return "<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 " + number(width) +
           " " + number(height) + "] /Resources " + resources + " /Length " +
           QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream";
}

bool insertReferenceIntoArray(QByteArray* body, const QByteArray& key, int objectNumber) {
    const qsizetype keyAt = findName(*body, key);
    if (keyAt < 0) return false;
    const qsizetype open = body->indexOf('[', keyAt + key.size());
    if (open < 0) return false;
    const qsizetype close = matching(*body, open, '[', ']');
    if (close < 0) return false;
    body->insert(close, " " + QByteArray::number(objectNumber) + " 0 R");
    return true;
}

// 交叉引用上下文：决定这次增量更新要写经典 xref 表还是交叉引用流。
struct XrefContext {
    // 原文件用的是交叉引用流（PDF 32000-1 §7.5.8.3：用过交叉引用流的文件，
    // 后续更新必须继续用交叉引用流，混用是非法的）。
    bool stream = false;
    int rootNumber = 0;          // 根目录对象号
    qsizetype previousXref = 0;  // 被本次更新取代的那一版交叉引用节的偏移
    int size = 0;                // 原文件声明的 /Size（对象号上界 + 1）
};

// 读 "/Size 12"、"/Prev 3456" 这类键后面的整数。
bool parseIntegerAfter(const QByteArray& body, qsizetype keyAt, qint64* value) {
    if (keyAt < 0) return false;
    qsizetype i = keyAt;
    while (i < body.size() && body.at(i) <= ' ') ++i;
    const qsizetype begin = i;
    while (i < body.size() && body.at(i) >= '0' && body.at(i) <= '9') ++i;
    if (begin == i) return false;
    bool ok = false;
    *value = body.mid(begin, i - begin).toLongLong(&ok);
    return ok;
}

// 取对象体的字典部分（到第一个配对的 ">>" 为止）。
// 交叉引用流的对象体后面还跟着二进制流数据，必须在字典处截断，
// 否则 /Root、/Prev 有可能从流数据里被误认出来。
QByteArray dictionaryOf(const QByteArray& pdf, const ObjectSpan& span) {
    const QByteArray body = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
    const qsizetype close = dictClose(body);
    return close < 0 ? body : body.left(close);
}

// 找指向交叉引用流的对象。
//
// 先按 startxref 里的偏移精确匹配：偏移对得上时它就是当前生效的那一版。
// 对不上（有些生成器写出的 startxref 不精确）时退回到最后一个带 /Type /XRef 的对象。
bool findXrefStreamObject(const QByteArray& pdf, const QVector<ObjectSpan>& objects,
                          qsizetype xrefOffset, ObjectSpan* out) {
    for (const ObjectSpan& span : objects) {
        if (span.objectStart != xrefOffset) continue;
        const QByteArray dict = dictionaryOf(pdf, span);
        if (dict.contains("/Type /XRef") || dict.contains("/Type/XRef")) {
            *out = span;
            return true;
        }
    }
    bool found = false;
    for (const ObjectSpan& span : objects) {
        const QByteArray dict = dictionaryOf(pdf, span);
        if (dict.contains("/Type /XRef") || dict.contains("/Type/XRef")) {
            *out = span;
            found = true;
        }
    }
    return found;
}

// 解析原文件的交叉引用信息：根目录对象号、被取代的那一版交叉引用节的偏移、原文件的 /Size。
bool resolveXrefContext(const QByteArray& pdf, const QVector<ObjectSpan>& objects,
                        XrefContext* context, QString* error) {
    const qsizetype startxrefAt = pdf.lastIndexOf("startxref");
    if (startxrefAt < 0) {
        *error = QStringLiteral("找不到 PDF 的交叉引用信息，无法建域");
        return false;
    }
    qint64 xrefOffset = 0;
    if (!parseIntegerAfter(pdf, startxrefAt + 9, &xrefOffset)) {
        *error = QStringLiteral("startxref 里的偏移无法解析");
        return false;
    }
    // startxref 指的就是当前生效的那一版交叉引用节，
    // 新写的这一版的 /Prev 要指回它，读者才能顺着链条把老对象找回来。
    context->previousXref = static_cast<qsizetype>(xrefOffset);

    ObjectSpan xrefSpan;
    if (findXrefStreamObject(pdf, objects, context->previousXref, &xrefSpan)) {
        context->stream = true;
        // 交叉引用流即使带 /Filter（FlateDecode）也不用解压：
        // 我们只要字典里的 /Root 与 /Size，流数据是读者自己的事。
        const QByteArray dict = dictionaryOf(pdf, xrefSpan);
        const qsizetype rootAt = findName(dict, "/Root");
        if (rootAt < 0 || !parseReferenceAfter(dict, rootAt + 5, &context->rootNumber)) {
            *error = QStringLiteral("无法解析 PDF 根目录");
            return false;
        }
        qint64 size = 0;
        if (parseIntegerAfter(dict, findName(dict, "/Size"), &size))
            context->size = static_cast<int>(size);
        return true;
    }

    const qsizetype trailerAt = pdf.lastIndexOf("trailer", startxrefAt);
    if (trailerAt < 0) {
        *error = QStringLiteral("找不到 PDF 的交叉引用信息，无法建域");
        return false;
    }
    const QByteArray trailer = pdf.mid(trailerAt, startxrefAt - trailerAt);
    const qsizetype rootAt = findName(trailer, "/Root");
    if (rootAt < 0 || !parseReferenceAfter(trailer, rootAt + 5, &context->rootNumber)) {
        *error = QStringLiteral("无法解析 PDF 根目录");
        return false;
    }
    qint64 size = 0;
    if (parseIntegerAfter(trailer, findName(trailer, "/Size"), &size))
        context->size = static_cast<int>(size);
    return true;
}

// 文件里有没有对象流：页面对象可能藏在对象流里，扫描器看不到它们。
bool hasObjectStream(const QByteArray& pdf) {
    return pdf.contains("/Type /ObjStm") || pdf.contains("/Type/ObjStm");
}

// 判断 "/Fields [...]" 这样的数组里是不是已经有某个对象的引用。
bool arrayHasReference(const QByteArray& body, const QByteArray& key, int objectNumber) {
    const qsizetype keyAt = findName(body, key);
    if (keyAt < 0) return false;
    const qsizetype open = body.indexOf('[', keyAt + key.size());
    if (open < 0) return false;
    const qsizetype close = matching(body, open, '[', ']');
    if (close < 0) return false;
    // 按空白切词，避免 "1 0 R" 误命中 "11 0 R"。
    const QVector<QByteArray> tokens =
        body.mid(open + 1, close - open - 1).simplified().split(' ');
    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens.at(i) == QByteArray::number(objectNumber) && tokens.at(i + 1) == "0" &&
            tokens.at(i + 2) == "R")
            return true;
    }
    return false;
}

// 数一数 "/Kids [...]" 里已经有几个孩子：新按钮的导出名要接着编号。
int countReferences(const QByteArray& body, const QByteArray& key) {
    const qsizetype keyAt = findName(body, key);
    if (keyAt < 0) return 0;
    const qsizetype open = body.indexOf('[', keyAt + key.size());
    if (open < 0) return 0;
    const qsizetype close = matching(body, open, '[', ']');
    if (close < 0) return 0;
    const QVector<QByteArray> tokens =
        body.mid(open + 1, close - open - 1).simplified().split(' ');
    int count = 0;
    for (int i = 0; i + 2 < tokens.size(); i += 3) {
        if (tokens.at(i + 1) == "0" && tokens.at(i + 2) == "R") ++count;
    }
    return count;
}

// 找到同名的单选分组父字段对象。
//
// 为什么用自定义键 /FolixaRadioGroup 做标记：分组要跨多次增量更新保持一致，
// 只看 /T 会误认同名的既有单选组，把别人的按钮并进我们的组里。
int findRadioGroupParent(const QByteArray& pdf, const QVector<ObjectSpan>& objects,
                         const QString& groupName) {
    const QByteArray marker = "/FolixaRadioGroup (" + escapedPdfString(groupName) + ")";
    int found = 0;
    for (const ObjectSpan& span : objects) {
        const QByteArray body = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
        if (body.contains(marker)) found = span.number;  // 取最后一个版本
    }
    return found;
}

// 分组父字段字典：/FT、/Ff（radio 位）、/V 与 /Kids 都在这里。
//
// 父字段不进页面 /Annots，但必须进 AcroForm /Fields —— Acrobat 是从 /Fields
// 认出"这是一个单选组"的；按钮自己通过 /Parent 指回来。
QByteArray radioGroupParentDictionary(const QString& groupName, int widgetNumber) {
    return "<< /FT /Btn /Ff " + QByteArray::number(1 << 15) + " /T (" +
           escapedPdfString(groupName) + ") /V /Off /FolixaRadioGroup (" +
           escapedPdfString(groupName) + ") /Kids [" + QByteArray::number(widgetNumber) +
           " 0 R] >>";
}

// 把新按钮接进父字段的 /Kids。
bool appendToKidsArray(QByteArray* body, int widgetNumber) {
    const qsizetype keyAt = findName(*body, "/Kids");
    if (keyAt < 0) {
        const qsizetype close = dictClose(*body);
        if (close < 0) return false;
        body->insert(close - 2, "/Kids [" + QByteArray::number(widgetNumber) + " 0 R] ");
        return true;
    }
    const qsizetype open = body->indexOf('[', keyAt + 5);
    if (open < 0) return false;
    const qsizetype close = matching(*body, open, '[', ']');
    if (close < 0) return false;
    body->insert(close, " " + QByteArray::number(widgetNumber) + " 0 R");
    return true;
}

// 把对象写成 "N 0 obj ... endobj"，并记下每个对象在文件里的字节偏移。
QByteArray serializeObjects(const QVector<QPair<int, QByteArray>>& objects, qsizetype baseOffset,
                             QVector<QPair<int, qsizetype>>* offsets) {
    QByteArray tail;
    for (const auto& object : objects) {
        offsets->append({object.first, baseOffset + tail.size()});
        tail += QByteArray::number(object.first) + " 0 obj\n" + object.second + "\nendobj\n";
    }
    return tail;
}

// 经典 xref 表 + trailer。
QByteArray classicXrefSection(const QVector<QPair<int, qsizetype>>& offsets, int size,
                               int rootNumber, qsizetype previousXref) {
    QByteArray section = "xref\n";
    for (const auto& offset : offsets)
        section += QByteArray::number(offset.first) + " 1\n" + padded(offset.second) + " 00000 n \n";
    section += "trailer\n<< /Size " + QByteArray::number(size) + " /Root " +
               QByteArray::number(rootNumber) + " 0 R /Prev " + QByteArray::number(previousXref) +
               " >>\n";
    return section;
}

// 交叉引用流（PDF 32000-1 §7.5.8）。
//
// 为什么 /W 用 [1 4 2]：1 字节条目类型、4 字节偏移、2 字节代数，是最常见的布局，
// 4 字节偏移够 4 GB 的文件，解析器也都认。
// 条目类型 1 表示"未压缩对象"，偏移是对象在文件里的字节位置，代数写 0。
//
// 为什么分多段写 /Index：这次增量更新只动了少数几个对象，对象号通常不连续；
// /Index 用"起始号 + 个数"描述每一段，读者照着查就能定位。
//
// 为什么不压缩：不写 /Filter 就少一层依赖，读者直接读原始字节即可。
QByteArray xrefStreamSection(int streamObject, int size, int rootNumber, qsizetype previousXref,
                              const QVector<QPair<int, qsizetype>>& offsets) {
    QByteArray index;
    QByteArray data;
    int i = 0;
    while (i < offsets.size()) {
        int j = i;
        while (j + 1 < offsets.size() && offsets.at(j + 1).first == offsets.at(j).first + 1) ++j;
        index += " " + QByteArray::number(offsets.at(i).first) + " " + QByteArray::number(j - i + 1);
        for (int k = i; k <= j; ++k) {
            data.append(char(1));  // 条目类型 1 = 未压缩对象
            const quint32 offset = static_cast<quint32>(offsets.at(k).second);
            data.append(char((offset >> 24) & 0xff));
            data.append(char((offset >> 16) & 0xff));
            data.append(char((offset >> 8) & 0xff));
            data.append(char(offset & 0xff));
            data.append(char(0));  // 代数 0
            data.append(char(0));
        }
        i = j + 1;
    }
    const QByteArray dict = "<< /Type /XRef /Size " + QByteArray::number(size) + " /Root " +
                            QByteArray::number(rootNumber) + " 0 R /Prev " +
                            QByteArray::number(previousXref) + " /Index [" + index +
                            "] /W [1 4 2] /Length " + QByteArray::number(data.size()) + " >>";
    return QByteArray::number(streamObject) + " 0 obj\n" + dict + "\nstream\n" + data +
           "\nendstream\nendobj\n";
}

// 标准 14 号字体在 /Font 字典里的条目：<< /Helv << ... >> /HeBo << ... >> ... >>
//
// 为什么拆成「条目」与「字典」两截：/DR 的值是一个字典，
// 字典里的 /Font 又是一个字典。补字体时要往**里层**那个字典里插，
// 两截混用就会插出 /Font << /Font << ... >> >> 这种坏结构。
QByteArray acroFormFontEntries() {
    QByteArray entries = "<<";
    const QStringList names = standardFormFontNames();
    for (const QString& name : names) {
        const QString base = standardFormBaseFont(name);
        if (base.isEmpty()) {
            continue;
        }
        entries += " /" + name.toLatin1() + " << /Type /Font /Subtype /Type1 /BaseFont /" +
                   base.toLatin1() + " >>";
    }
    entries += " >>";
    return entries;
}

// AcroForm 的 /DR 字典：<< /Font <条目> >>。
//
// 为什么全登记：表单域的 /DA 里写的是**资源名**，阅读器要按这个名字去
// /DR /Font 里找；只登记 /Helv 的话，选了别的字体的域在别的阅读器里
// 会退回默认字体 —— 用户设的粗体就没了。
QByteArray acroFormFontDict() {
    return "<< /Font " + acroFormFontEntries() + " >>";
}

// 值的位置上是不是紧跟着一个内联字典（"<< ..."）。
//
// 为什么要单独判：/DR 的值可能是间接引用（"/DR 5 0 R"）。
// 那时直接找下一个 "<<" 会找到**别处**的字典，
// 往里插字体就等于把字体写进了不相干的字典里。
qsizetype inlineDictAt(const QByteArray& body, qsizetype valueAt) {
    qsizetype at = valueAt;
    while (at < body.size() && (body.at(at) == ' ' || body.at(at) == '\r' ||
                                 body.at(at) == '\n' || body.at(at) == '\t')) {
        ++at;
    }
    if (body.mid(at, 2) != "<<") {
        return -1;
    }
    return at;
}

// 把标准 14 号字体补进已有的 AcroForm（只补缺的，不覆盖用户自己配的）。
//
// /DR 是间接引用（"/DR 5 0 R"）时改不了 —— 那要改另一个对象，
// 这里老老实实跳过，不去动它。
bool ensureAcroFormFonts(QByteArray* acroBody) {
    if (acroBody == nullptr) {
        return false;
    }
    const qsizetype drAt = findName(*acroBody, "/DR");
    if (drAt < 0) {
        const qsizetype close = dictClose(*acroBody);
        if (close < 0) {
            return false;
        }
        acroBody->insert(close - 2, "/DR " + acroFormFontDict() + " ");
        return true;
    }
    const qsizetype drStart = inlineDictAt(*acroBody, drAt + 3);
    if (drStart < 0) {
        // /DR 不是内联字典（多半是间接引用）：跳过，不去动它。
        return false;
    }
    const qsizetype drClose = dictClose(*acroBody, drStart);
    if (drClose < 0) {
        return false;
    }
    const qsizetype fontAt = findName(*acroBody, "/Font", drStart);
    if (fontAt < 0 || fontAt > drClose) {
        // /DR 里没有 /Font：补一个。
        acroBody->insert(drClose - 2, "/Font " + acroFormFontEntries() + " ");
        return true;
    }
    const qsizetype fontStart = inlineDictAt(*acroBody, fontAt + 5);
    if (fontStart < 0) {
        // /Font 不是内联字典：跳过。
        return false;
    }
    const qsizetype fontClose = dictClose(*acroBody, fontStart);
    if (fontClose < 0) {
        return false;
    }
    QByteArray missing;
    const QStringList names = standardFormFontNames();
    for (const QString& name : names) {
        const QByteArray key = "/" + name.toLatin1();
        const qsizetype existing = findName(*acroBody, key, fontStart);
        if (existing >= 0 && existing < fontClose) {
            continue;  // 已经有了
        }
        const QString base = standardFormBaseFont(name);
        if (base.isEmpty()) {
            continue;
        }
        missing += " " + key + " << /Type /Font /Subtype /Type1 /BaseFont /" + base.toLatin1() + " >>";
    }
    if (missing.isEmpty()) {
        return true;
    }
    acroBody->insert(fontClose - 2, missing);
    return true;
}

}  // namespace

FormFieldWriteResult appendFormField(const QByteArray& pdf, const FormFieldInsert& field) {
    FormFieldWriteResult result;
    if (field.pageIndex < 0 || field.name.trimmed().isEmpty() || field.rect.width() <= 0 ||
        field.rect.height() <= 0) {
        result.error = QStringLiteral("字段名称、页码或矩形无效");
        return result;
    }
    const QVector<ObjectSpan> objects = scanObjects(pdf);
    XrefContext context;
    QString contextError;
    if (!resolveXrefContext(pdf, objects, &context, &contextError)) {
        result.error = contextError;
        return result;
    }
    ObjectSpan root;
    if (!findObject(objects, context.rootNumber, &root)) {
        result.error = QStringLiteral("找不到 PDF 根目录对象");
        return result;
    }
    ObjectSpan page;
    if (!findPage(pdf, objects, field.pageIndex, &page)) {
        // 页面对象藏在对象流里时扫描器看不到：这时必须明说，
        // 不能默默写出一份页面丢失的坏文件。
        result.error = hasObjectStream(pdf)
                           ? QStringLiteral("这份 PDF 的页面在对象流里，暂时无法建域")
                           : QStringLiteral("找不到目标页");
        return result;
    }

    // 条形码：先编码，失败就报错，绝不画一张空白条码。
    QString barcodeText;
    QVector<BarcodeModule> barcodeModules;
    if (field.kind == FormFieldKind::Barcode) {
        // barcodeText 留空时回退用字段名当条码内容；
        // 字段名不是 ASCII 时同样走下面的报错路径——扫不出来的条码比没有条码更糟。
        barcodeText = field.barcodeText.isEmpty() ? field.name : field.barcodeText;
        const BarcodeCode128 code = encodeCode128(barcodeText);
        if (!code.ok) {
            result.error = QStringLiteral("条形码内容无法编码：%1").arg(code.error);
            return result;
        }
        barcodeModules = code.modules;
    }

    int maxObject = 0;
    for (const ObjectSpan& object : objects) maxObject = std::max(maxObject, object.number);
    // 新对象号必须躲开原文件里已有的对象：对象流里的对象扫不到，
    // 只按扫描结果编号会撞号，撞号等于把别人的对象顶掉。
    int nextObject = std::max(maxObject + 1, context.size);

    // 单选分组：同名的按钮要指向同一个父字段，父字段也要进 AcroForm /Fields。
    const bool grouped = field.kind == FormFieldKind::RadioButton && !field.groupName.isEmpty();
    FieldExtras extras;
    int parentObject = 0;
    QByteArray parentBody;
    bool newParent = true;
    int widgetIndex = 1;
    if (grouped) {
        parentObject = findRadioGroupParent(pdf, objects, field.groupName);
        if (parentObject > 0) {
            ObjectSpan parentSpan;
            if (findObject(objects, parentObject, &parentSpan)) {
                parentBody = pdf.mid(parentSpan.bodyBegin, parentSpan.bodyEnd - parentSpan.bodyBegin);
                // 导出名要接着组里已有的按钮往下编号。
                widgetIndex = countReferences(parentBody, "/Kids") + 1;
                newParent = false;
            } else {
                parentObject = 0;
            }
        }
        if (parentObject <= 0) parentObject = nextObject++;
        extras.parentObject = parentObject;
        // 导出名必须是合法的 PDF 名字（不能含空格与分隔符），由分组名派生。
        extras.exportName = sanitizeExportPrefix(field.groupName).toLatin1() + "_" +
                            QByteArray::number(widgetIndex);
    }

    const int widgetNumber = nextObject++;
    const bool needsButtonAppearance = field.kind == FormFieldKind::CheckBox ||
                                       field.kind == FormFieldKind::RadioButton;
    const int normalAppearance = nextObject++;
    const int onAppearance = needsButtonAppearance ? nextObject++ : 0;
    result.fieldObject = widgetNumber;
    extras.barcodeText = barcodeText;
    if (grouped) {
        if (newParent) {
            parentBody = radioGroupParentDictionary(field.groupName, widgetNumber);
        } else if (!appendToKidsArray(&parentBody, widgetNumber)) {
            result.error = QStringLiteral("单选分组的父字段无法更新");
            return result;
        }
    }

    QByteArray pageBody = pdf.mid(page.bodyBegin, page.bodyEnd - page.bodyBegin);
    if (!insertReferenceIntoArray(&pageBody, "/Annots", widgetNumber)) {
        const qsizetype close = dictClose(pageBody);
        if (close < 0) { result.error = QStringLiteral("页面对象不是字典"); return result; }
        pageBody.insert(close - 2, "/Annots [" + QByteArray::number(widgetNumber) + " 0 R] ");
    }

    // 分组时登记的是父字段：组里的按钮通过 /Parent 指回它，
    // 它们不再是顶层字段，不该再单独进 /Fields。
    const int fieldReference = grouped ? parentObject : widgetNumber;
    QByteArray rootBody = pdf.mid(root.bodyBegin, root.bodyEnd - root.bodyBegin);
    QByteArray acroBody;
    int acroObject = 0;
    bool separateAcroObject = false;
    const qsizetype acroAt = findName(rootBody, "/AcroForm");
    if (acroAt < 0) {
        const qsizetype close = dictClose(rootBody);
        if (close < 0) { result.error = QStringLiteral("根目录对象不是字典"); return result; }
        const QByteArray acro = "/AcroForm << /NeedAppearances true /Fields [" +
            QByteArray::number(fieldReference) +
            " 0 R] /DR " + acroFormFontDict() + " "
            "/DA (/Helv 10 Tf 0 g) >> ";
        rootBody.insert(close - 2, acro);
    } else if (parseReferenceAfter(rootBody, acroAt + 9, &acroObject)) {
        ObjectSpan acroSpan;
        if (!findObject(objects, acroObject, &acroSpan)) {
            result.error = QStringLiteral("找不到 AcroForm 对象"); return result;
        }
        acroBody = pdf.mid(acroSpan.bodyBegin, acroSpan.bodyEnd - acroSpan.bodyBegin);
        separateAcroObject = true;
        // 已有 AcroForm：把标准 14 号字体补进 /DR，
        // 否则用户选的非默认字体在别的阅读器里会退回默认字体。
        ensureAcroFormFonts(&acroBody);
        // 同一个分组只登记一次父字段。
        if (!arrayHasReference(acroBody, "/Fields", fieldReference)) {
            if (!insertReferenceIntoArray(&acroBody, "/Fields", fieldReference)) {
                const qsizetype close = dictClose(acroBody);
                if (close < 0) { result.error = QStringLiteral("AcroForm 对象不是字典"); return result; }
                acroBody.insert(close - 2, "/Fields [" + QByteArray::number(fieldReference) + " 0 R] ");
            }
        }
    } else {
        const qsizetype fieldsAt = findName(rootBody, "/Fields", acroAt);
        if (fieldsAt < 0) {
            result.error = QStringLiteral("无法更新直接 AcroForm 字典"); return result;
        }
        if (!arrayHasReference(rootBody, "/Fields", fieldReference)) {
            if (!insertReferenceIntoArray(&rootBody, "/Fields", fieldReference)) {
                result.error = QStringLiteral("无法更新直接 AcroForm 字典"); return result;
            }
        }
    }

    QVector<QPair<int, QByteArray>> rewrites{{page.number, pageBody}, {root.number, rootBody}};
    if (separateAcroObject) rewrites.append({acroObject, acroBody});
    if (grouped) rewrites.append({parentObject, parentBody});
    rewrites.append({widgetNumber, fieldDictionary(field, page.number, normalAppearance, onAppearance, extras)});
    {
        const QRectF rect = field.rect.normalized();
        if (field.kind == FormFieldKind::Barcode) {
            // 条形码字段的外观流里画的是真条码，别的阅读器才能看见、扫描器才能扫出来。
            rewrites.append({normalAppearance, barcodeAppearanceStream(rect.width(), rect.height(),
                                                                     barcodeModules, field.barcodeCaption)});
        } else {
            rewrites.append({normalAppearance, appearanceStream(rect.width(), rect.height(), false, false)});
        }
        if (needsButtonAppearance) {
        rewrites.append({onAppearance, appearanceStream(rect.width(), rect.height(), true,
                                                         field.kind == FormFieldKind::RadioButton)});
        }
    }
    std::sort(rewrites.begin(), rewrites.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    QByteArray tail;
    QVector<QPair<int, qsizetype>> offsets;
    tail += serializeObjects(rewrites, pdf.size(), &offsets);
    const int finalObject = offsets.isEmpty() ? 0 : offsets.last().first;
    const qsizetype xrefOffset = pdf.size() + tail.size();
    if (context.stream) {
        // 用过交叉引用流的文件必须继续用交叉引用流（PDF 32000-1 §7.5.8.3）。
        tail += xrefStreamSection(finalObject + 1, finalObject + 2, context.rootNumber,
                                  context.previousXref, offsets);
    } else {
        tail += classicXrefSection(offsets, finalObject + 1, context.rootNumber,
                                   context.previousXref);
    }
    tail += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    result.bytes = pdf + tail;
    result.ok = true;
    return result;
}

FormFieldWriteResult updateFormFieldOptions(const QByteArray& pdf,
                                            const QString& fieldName,
                                            const QStringList& options) {
    FormFieldWriteResult result;
    if (fieldName.isEmpty() || options.isEmpty()) {
        result.error = QStringLiteral("字段名称和选项不能为空");
        return result;
    }
    const QVector<ObjectSpan> objects = scanObjects(pdf);
    XrefContext context;
    QString contextError;
    if (!resolveXrefContext(pdf, objects, &context, &contextError)) {
        result.error = contextError;
        return result;
    }
    const QByteArray encodedName = "/T (" + escapedPdfString(fieldName) + ")";
    QByteArray utf16Name("\xFE\xFF", 2);
    for (QChar ch : fieldName) {
        const ushort u = ch.unicode();
        utf16Name.append(char((u >> 8) & 0xff));
        utf16Name.append(char(u & 0xff));
    }
    const QByteArray hexName = "/T <" + utf16Name.toHex() + ">";
    QByteArray latinName = fieldName.toLatin1();
    latinName.replace("\\", "\\\\");
    latinName.replace("(", "\\(");
    latinName.replace(")", "\\)");
    latinName = "/T (" + latinName + ")";

    ObjectSpan target;
    QByteArray body;
    QVector<int> visited;
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        if (visited.contains(it->number)) continue;
        visited.append(it->number);
        const QByteArray candidate = pdf.mid(it->bodyBegin, it->bodyEnd - it->bodyBegin);
        if ((candidate.contains(encodedName) || candidate.contains(hexName) ||
             candidate.contains(latinName)) &&
            (candidate.contains("/FT /Ch") || candidate.contains("/FT/Ch"))) {
            target = *it;
            body = candidate;
            break;
        }
    }
    if (target.number <= 0) {
#ifdef AIPDF_WITH_QPDF
        try {
            QPDF qpdf;
            qpdf.processMemoryFile("form-options.pdf", pdf.constData(),
                                   static_cast<size_t>(pdf.size()));
            bool updated = false;
            for (QPDFObjectHandle object : qpdf.getAllObjects()) {
                if (!object.isDictionary()) continue;
                QPDFObjectHandle ft = object.getKey("/FT");
                QPDFObjectHandle name = object.getKey("/T");
                if (!ft.isName() || ft.getName() != "/Ch" || !name.isString() ||
                    QString::fromUtf8(name.getUTF8Value()) != fieldName) continue;
                std::vector<QPDFObjectHandle> values;
                values.reserve(static_cast<size_t>(options.size()));
                for (const QString& option : options)
                    values.push_back(QPDFObjectHandle::newUnicodeString(
                        option.trimmed().toUtf8().toStdString()));
                object.replaceKey("/Opt", QPDFObjectHandle::newArray(values));
                updated = true;
                break;
            }
            if (!updated) {
                result.error = QStringLiteral("找不到可编辑的组合框或列表框字段");
                return result;
            }
            QPDFWriter writer(qpdf);
            writer.setOutputMemory();
            writer.write();
            std::unique_ptr<Buffer> buffer(writer.getBuffer());
            if (!buffer || buffer->getSize() == 0) {
                result.error = QStringLiteral("qpdf 没有写出选项更新结果");
                return result;
            }
            result.bytes = QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                                      static_cast<int>(buffer->getSize()));
            result.ok = true;
            return result;
        } catch (const std::exception& exception) {
            result.error = QStringLiteral("更新字段选项失败：%1")
                               .arg(QString::fromUtf8(exception.what()));
            return result;
        }
#else
        result.error = QStringLiteral("找不到可编辑的组合框或列表框字段");
        return result;
#endif
    }

    QByteArray replacement = "/Opt [";
    for (const QString& option : options)
        replacement += "(" + escapedPdfString(option.trimmed()) + ") ";
    replacement += "]";
    const qsizetype optAt = findName(body, "/Opt");
    if (optAt >= 0) {
        const qsizetype open = body.indexOf('[', optAt + 4);
        const qsizetype close = open >= 0 ? matching(body, open, '[', ']') : -1;
        if (open < 0 || close < 0) {
            result.error = QStringLiteral("字段 /Opt 数组无法解析");
            return result;
        }
        body.replace(optAt, close - optAt + 1, replacement);
    } else {
        const qsizetype close = dictClose(body);
        if (close < 0) {
            result.error = QStringLiteral("字段对象不是字典");
            return result;
        }
        body.insert(close - 2, replacement + " ");
    }

    const qsizetype objectOffset = pdf.size();
    QByteArray tail = QByteArray::number(target.number) + " 0 obj\n" + body + "\nendobj\n";
    const qsizetype xrefOffset = pdf.size() + tail.size();
    int maxObject = 0;
    for (const ObjectSpan& object : objects) maxObject = std::max(maxObject, object.number);
    // 对象号要从原文件的 /Size 之后接着排，避免和对象流里的对象撞号。
    const int size = std::max(maxObject + 1, context.size);
    QVector<QPair<int, qsizetype>> offsets;
    offsets.append({target.number, objectOffset});
    if (context.stream) {
        // 用过交叉引用流的文件必须继续用交叉引用流（PDF 32000-1 §7.5.8.3）。
        tail += xrefStreamSection(size, size + 1, context.rootNumber, context.previousXref, offsets);
    } else {
        tail += classicXrefSection(offsets, size, context.rootNumber, context.previousXref);
    }
    tail += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    result.bytes = pdf + tail;
    result.ok = true;
    result.fieldObject = target.number;
    return result;
}

}  // namespace aipdf::pdf
