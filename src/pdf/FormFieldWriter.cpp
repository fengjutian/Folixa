#include "pdf/FormFieldWriter.h"

#include <QVector>

#include <algorithm>

namespace aipdf::pdf {
namespace {

struct ObjectSpan {
    int number = 0;
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
        result.append({number, begin, end});
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

QByteArray fieldDictionary(const FormFieldInsert& field, int pageObject,
                           int normalAppearance, int onAppearance = 0) {
    QByteArray ft = "/Tx";
    int flags = (field.readOnly ? 1 : 0) | (field.required ? (1 << 1) : 0);
    QByteArray value = "()";
    QByteArray extra;
    switch (field.kind) {
        case FormFieldKind::CheckBox:
            ft = "/Btn"; value = "/Off";
            extra = "/AS /Off /AP << /N << /Off " + QByteArray::number(normalAppearance) +
                    " 0 R /Yes " + QByteArray::number(onAppearance) + " 0 R >> >> ";
            break;
        case FormFieldKind::RadioButton:
            ft = "/Btn"; flags = 1 << 15; value = "/Off";
            extra = "/AS /Off /AP << /N << /Off " + QByteArray::number(normalAppearance) +
                    " 0 R /Yes " + QByteArray::number(onAppearance) + " 0 R >> >> ";
            break;
        case FormFieldKind::ComboBox:
            ft = "/Ch"; flags |= 1 << 17; break;
        case FormFieldKind::ListBox:
            ft = "/Ch"; break;
        case FormFieldKind::PushButton:
            ft = "/Btn"; flags = 1 << 16; extra = "/MK << /CA (按钮) >> "; break;
        case FormFieldKind::Barcode:
            ft = "/Tx"; extra = "/TU (Barcode value) /MaxLen 128 "; break;
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
    QByteArray dict = "<< /Type /Annot /Subtype /Widget /P " + QByteArray::number(pageObject) +
        " 0 R /FT " + ft + " /T (" + escapedPdfString(field.name) + ") ";
    if (!field.toolTip.isEmpty()) dict += "/TU (" + escapedPdfString(field.toolTip) + ") ";
    dict += "/Rect [" + number(rect.left()) + " " + number(rect.top()) + " " +
            number(rect.right()) + " " + number(rect.bottom()) + "] /F 4 /Ff " +
            QByteArray::number(flags) + " /V " + value + " /DA (/Helv 10 Tf 0 g) " + extra + ">>";
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

}  // namespace

FormFieldWriteResult appendFormField(const QByteArray& pdf, const FormFieldInsert& field) {
    FormFieldWriteResult result;
    if (field.pageIndex < 0 || field.name.trimmed().isEmpty() || field.rect.width() <= 0 ||
        field.rect.height() <= 0) {
        result.error = QStringLiteral("字段名称、页码或矩形无效");
        return result;
    }
    const qsizetype startxrefAt = pdf.lastIndexOf("startxref");
    const qsizetype trailerAt = pdf.lastIndexOf("trailer", startxrefAt);
    if (startxrefAt < 0 || trailerAt < 0) {
        result.error = QStringLiteral("只支持带经典 trailer 的 PDF");
        return result;
    }
    const QByteArray trailer = pdf.mid(trailerAt, startxrefAt - trailerAt);
    int rootNumber = 0;
    const qsizetype rootAt = findName(trailer, "/Root");
    if (rootAt < 0 || !parseReferenceAfter(trailer, rootAt + 5, &rootNumber)) {
        result.error = QStringLiteral("无法解析 PDF 根目录");
        return result;
    }
    qsizetype previousXref = 0;
    {
        qsizetype i = startxrefAt + 9;
        while (i < pdf.size() && (pdf.at(i) < '0' || pdf.at(i) > '9')) ++i;
        const qsizetype begin = i;
        while (i < pdf.size() && pdf.at(i) >= '0' && pdf.at(i) <= '9') ++i;
        previousXref = pdf.mid(begin, i - begin).toLongLong();
    }
    const QVector<ObjectSpan> objects = scanObjects(pdf);
    ObjectSpan page;
    ObjectSpan root;
    if (!findPage(pdf, objects, field.pageIndex, &page) || !findObject(objects, rootNumber, &root)) {
        result.error = QStringLiteral("找不到目标页或根目录对象");
        return result;
    }
    int maxObject = 0;
    for (const ObjectSpan& object : objects) maxObject = std::max(maxObject, object.number);
    const int widgetNumber = maxObject + 1;
    const bool needsButtonAppearance = field.kind == FormFieldKind::CheckBox ||
                                       field.kind == FormFieldKind::RadioButton;
    const int normalAppearance = widgetNumber + 1;
    const int onAppearance = needsButtonAppearance ? widgetNumber + 2 : 0;
    const int finalObject = needsButtonAppearance ? onAppearance : normalAppearance;
    result.fieldObject = widgetNumber;

    QByteArray pageBody = pdf.mid(page.bodyBegin, page.bodyEnd - page.bodyBegin);
    if (!insertReferenceIntoArray(&pageBody, "/Annots", widgetNumber)) {
        const qsizetype close = dictClose(pageBody);
        if (close < 0) { result.error = QStringLiteral("页面对象不是字典"); return result; }
        pageBody.insert(close - 2, "/Annots [" + QByteArray::number(widgetNumber) + " 0 R] ");
    }

    QByteArray rootBody = pdf.mid(root.bodyBegin, root.bodyEnd - root.bodyBegin);
    QByteArray acroBody;
    int acroObject = 0;
    bool separateAcroObject = false;
    const qsizetype acroAt = findName(rootBody, "/AcroForm");
    if (acroAt < 0) {
        const qsizetype close = dictClose(rootBody);
        if (close < 0) { result.error = QStringLiteral("根目录对象不是字典"); return result; }
        const QByteArray acro = "/AcroForm << /NeedAppearances true /Fields [" +
            QByteArray::number(widgetNumber) +
            " 0 R] /DR << /Font << /Helv << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> "
            "/DA (/Helv 10 Tf 0 g) >> ";
        rootBody.insert(close - 2, acro);
    } else if (parseReferenceAfter(rootBody, acroAt + 9, &acroObject)) {
        ObjectSpan acroSpan;
        if (!findObject(objects, acroObject, &acroSpan)) {
            result.error = QStringLiteral("找不到 AcroForm 对象"); return result;
        }
        acroBody = pdf.mid(acroSpan.bodyBegin, acroSpan.bodyEnd - acroSpan.bodyBegin);
        separateAcroObject = true;
        if (!insertReferenceIntoArray(&acroBody, "/Fields", widgetNumber)) {
            const qsizetype close = dictClose(acroBody);
            if (close < 0) { result.error = QStringLiteral("AcroForm 对象不是字典"); return result; }
            acroBody.insert(close - 2, "/Fields [" + QByteArray::number(widgetNumber) + " 0 R] ");
        }
    } else {
        const qsizetype fieldsAt = findName(rootBody, "/Fields", acroAt);
        if (fieldsAt < 0 || !insertReferenceIntoArray(&rootBody, "/Fields", widgetNumber)) {
            result.error = QStringLiteral("无法更新直接 AcroForm 字典"); return result;
        }
    }

    QVector<QPair<int, QByteArray>> rewrites{{page.number, pageBody}, {root.number, rootBody}};
    if (separateAcroObject) rewrites.append({acroObject, acroBody});
    rewrites.append({widgetNumber, fieldDictionary(field, page.number, normalAppearance, onAppearance)});
    {
        const QRectF rect = field.rect.normalized();
        rewrites.append({normalAppearance, appearanceStream(rect.width(), rect.height(), false, false)});
        if (needsButtonAppearance) {
        rewrites.append({onAppearance, appearanceStream(rect.width(), rect.height(), true,
                                                         field.kind == FormFieldKind::RadioButton)});
        }
    }
    std::sort(rewrites.begin(), rewrites.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    QByteArray tail;
    QVector<QPair<int, qsizetype>> offsets;
    for (const auto& rewrite : rewrites) {
        offsets.append({rewrite.first, pdf.size() + tail.size()});
        tail += QByteArray::number(rewrite.first) + " 0 obj\n" + rewrite.second + "\nendobj\n";
    }
    const qsizetype xrefOffset = pdf.size() + tail.size();
    tail += "xref\n";
    for (const auto& offset : offsets)
        tail += QByteArray::number(offset.first) + " 1\n" + padded(offset.second) + " 00000 n \n";
    tail += "trailer\n<< /Size " + QByteArray::number(finalObject + 1) + " /Root " +
            QByteArray::number(rootNumber) + " 0 R /Prev " + QByteArray::number(previousXref) +
            " >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    result.bytes = pdf + tail;
    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
