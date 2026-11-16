#include "pdf/FormScriptStore.h"

#include <QHash>
#include <QVector>

#include <algorithm>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <memory>
#include <string>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

struct ObjectSpan {
    int number = 0;
    qsizetype bodyBegin = 0;
    qsizetype bodyEnd = 0;
};

// 扫出文件里的所有间接对象。
//
// 为什么不用交叉引用表：增量更新会让同一个对象号出现多次，交叉引用表只说
// "最后一个有效"。这里按出现顺序全收，用的时候再取最后一个版本。
QVector<ObjectSpan> scanObjects(const QByteArray& pdf) {
    QVector<ObjectSpan> result;
    qsizetype pos = 0;
    while ((pos = pdf.indexOf(" obj", pos)) >= 0) {
        qsizetype lineStart = pdf.lastIndexOf('\n', pos);
        lineStart = lineStart < 0 ? 0 : lineStart + 1;
        const QByteArray header = pdf.mid(lineStart, pos - lineStart).trimmed().simplified();
        const QList<QByteArray> parts = header.split(' ');
        int number = 0;
        bool ok = false;
        if (parts.size() == 2) number = parts.at(0).toInt(&ok);
        if (!ok) {
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

// 在 [begin, end) 里找一个名字键。
//
// 为什么要限范围：字段字典里到处是名字（/Type /Widget /Rect /AP …），
// 不限范围就会把别的字典里的同名键当成自己的。
qsizetype findNameIn(const QByteArray& body, const QByteArray& name, qsizetype begin,
                     qsizetype end) {
    qsizetype at = begin;
    while ((at = body.indexOf(name, at)) >= 0 && at < end) {
        const qsizetype keyEnd = at + name.size();
        // PDF 名称以 '/' 自身起界，前一个名称可以无空格直接结束（/Type/Page）。
        const bool beginOk = name.startsWith('/') || at == 0 || isNameBoundary(body.at(at - 1));
        if (beginOk && (keyEnd >= end || isNameBoundary(body.at(keyEnd)))) return at;
        at = keyEnd;
    }
    return -1;
}

void skipWhitespace(const QByteArray& body, qsizetype* at) {
    while (*at < body.size() && body.at(*at) <= ' ') ++(*at);
}

// 解析 "N G R" 引用，只认紧跟在键后的那种。
bool parseReferenceAfter(const QByteArray& body, qsizetype at, int* number) {
    skipWhitespace(body, &at);
    if (at >= body.size() || body.at(at) < '0' || body.at(at) > '9') return false;
    const qsizetype begin = at;
    while (at < body.size() && body.at(at) >= '0' && body.at(at) <= '9') ++at;
    if (begin == at) return false;
    bool ok = false;
    const int value = body.mid(begin, at - begin).toInt(&ok);
    skipWhitespace(body, &at);
    while (at < body.size() && body.at(at) >= '0' && body.at(at) <= '9') ++at;
    skipWhitespace(body, &at);
    if (!ok || at >= body.size() || body.at(at) != 'R') return false;
    *number = value;
    return true;
}

// 找与 openAt 处括号配对的右括号。
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

// 返回从 openAt 处开始的那个字典的结束位置（">>" 之后的下标）。
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

// 文本串解码：优先 UTF-16BE（带 BOM），其次 UTF-8，最后按 Latin-1 兜底。
QString decodeText(const QByteArray& raw) {
    if (raw.size() >= 2 && quint8(raw.at(0)) == 0xFE && quint8(raw.at(1)) == 0xFF) {
        QString result;
        for (qsizetype i = 2; i + 1 < raw.size(); i += 2) {
            const ushort unit =
                static_cast<ushort>((quint8(raw.at(i)) << 8) | quint8(raw.at(i + 1)));
            result.append(QChar(unit));
        }
        return result;
    }
    if (raw.size() >= 3 && quint8(raw.at(0)) == 0xEF && quint8(raw.at(1)) == 0xBB &&
        quint8(raw.at(2)) == 0xBF) {
        return QString::fromUtf8(raw.mid(3));
    }
    const QString utf8 = QString::fromUtf8(raw);
    if (!utf8.contains(QChar::ReplacementCharacter)) return utf8;
    return QString::fromLatin1(raw);
}

// 解析一个 PDF 字符串：字面串 (...) 或十六进制串 <...>。
bool parseStringValue(const QByteArray& body, qsizetype at, QString* out, qsizetype* endAt) {
    if (at >= body.size()) return false;
    const char ch = body.at(at);
    if (ch == '(') {
        QByteArray raw;
        int depth = 0;
        bool escaped = false;
        qsizetype i = at;
        for (; i < body.size(); ++i) {
            const char c = body.at(i);
            if (escaped) {
                // 续行反斜杠：反斜杠后跟换行表示这一行没结束，直接忽略。
                if (c == '\n') { escaped = false; continue; }
                if (c == '\r') {
                    escaped = false;
                    if (i + 1 < body.size() && body.at(i + 1) == '\n') ++i;
                    continue;
                }
                raw.append(c);
                escaped = false;
                continue;
            }
            if (c == '\\') { escaped = true; continue; }
            if (c == '(') {
                ++depth;
                if (depth > 1) raw.append(c);
                continue;
            }
            if (c == ')') {
                --depth;
                if (depth == 0) { ++i; break; }
                raw.append(c);
                continue;
            }
            raw.append(c);
        }
        if (out != nullptr) *out = decodeText(raw);
        if (endAt != nullptr) *endAt = i;
        return true;
    }
    if (ch == '<' && at + 1 < body.size() && body.at(at + 1) != '<') {
        const qsizetype close = body.indexOf('>', at + 1);
        if (close < 0) return false;
        const QByteArray raw = QByteArray::fromHex(body.mid(at + 1, close - at - 1));
        if (out != nullptr) *out = decodeText(raw);
        if (endAt != nullptr) *endAt = close + 1;
        return true;
    }
    return false;
}

// 取字段对象里的 /T（字段名）。
QString fieldNameOf(const QByteArray& body) {
    const qsizetype at = findNameIn(body, "/T", 0, body.size());
    if (at < 0) return QString();
    qsizetype valueAt = at + 2;
    skipWhitespace(body, &valueAt);
    QString name;
    qsizetype endAt = 0;
    if (!parseStringValue(body, valueAt, &name, &endAt)) return QString();
    return name;
}

// 读一个字典里的三个脚本键（/C /F /V），值可以是串，也可以是引用。
void readScriptKeys(const QByteArray& dictBody, qsizetype begin, qsizetype end,
                    const QVector<ObjectSpan>& objects, const QByteArray& pdf,
                    QString* calculate, QString* format, QString* validate) {
    const QByteArray keys[3] = {QByteArrayLiteral("/C"), QByteArrayLiteral("/F"),
                                 QByteArrayLiteral("/V")};
    QString* targets[3] = {calculate, format, validate};
    for (int i = 0; i < 3; ++i) {
        const qsizetype at = findNameIn(dictBody, keys[i], begin, end);
        if (at < 0) continue;
        qsizetype valueAt = at + keys[i].size();
        skipWhitespace(dictBody, &valueAt);
        QString text;
        qsizetype textEnd = 0;
        if (parseStringValue(dictBody, valueAt, &text, &textEnd)) {
            *targets[i] = text;
            continue;
        }
        // 引用到别的对象：那个对象通常就是放脚本的字符串对象。
        int reference = 0;
        if (!parseReferenceAfter(dictBody, valueAt, &reference)) continue;
        ObjectSpan span;
        if (!findObject(objects, reference, &span)) continue;
        const QByteArray referenced = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
        QString referencedText;
        qsizetype referencedEnd = 0;
        if (parseStringValue(referenced, 0, &referencedText, &referencedEnd))
            *targets[i] = referencedText;
    }
}

// 读 /AA（附加动作）里的脚本。
void readAdditionalActions(const QByteArray& body, const QVector<ObjectSpan>& objects,
                           const QByteArray& pdf, FormFieldScript* out) {
    const qsizetype at = findNameIn(body, "/AA", 0, body.size());
    if (at < 0) return;
    qsizetype valueAt = at + 3;
    skipWhitespace(body, &valueAt);
    if (valueAt + 1 < body.size() && body.at(valueAt) == '<' && body.at(valueAt + 1) == '<') {
        const qsizetype close = dictClose(body, valueAt);
        if (close > 0)
            readScriptKeys(body, valueAt, close, objects, pdf, &out->calculate, &out->format,
                           &out->validate);
        return;
    }
    int reference = 0;
    if (!parseReferenceAfter(body, valueAt, &reference)) return;
    ObjectSpan span;
    if (!findObject(objects, reference, &span)) return;
    const QByteArray referenced = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
    const qsizetype close = dictClose(referenced, 0);
    readScriptKeys(referenced, 0, close > 0 ? close : referenced.size(), objects, pdf,
                   &out->calculate, &out->format, &out->validate);
}

// 读 /A（动作）里的 JavaScript。
void readActionScript(const QByteArray& body, const QVector<ObjectSpan>& objects,
                      const QByteArray& pdf, FormFieldScript* out) {
    const qsizetype at = findNameIn(body, "/A", 0, body.size());
    if (at < 0) return;
    qsizetype valueAt = at + 2;
    skipWhitespace(body, &valueAt);
    QByteArray dictBody = body;
    qsizetype begin = valueAt;
    qsizetype end = body.size();
    if (valueAt + 1 < body.size() && body.at(valueAt) == '<' && body.at(valueAt + 1) == '<') {
        const qsizetype close = dictClose(body, valueAt);
        if (close > 0) end = close;
    } else {
        int reference = 0;
        if (!parseReferenceAfter(body, valueAt, &reference)) return;
        ObjectSpan span;
        if (!findObject(objects, reference, &span)) return;
        dictBody = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
        begin = 0;
        const qsizetype close = dictClose(dictBody, 0);
        end = close > 0 ? close : dictBody.size();
    }
    const qsizetype jsAt = findNameIn(dictBody, "/JS", begin, end);
    if (jsAt < 0) return;
    qsizetype jsValue = jsAt + 3;
    skipWhitespace(dictBody, &jsValue);
    QString text;
    qsizetype textEnd = 0;
    if (parseStringValue(dictBody, jsValue, &text, &textEnd)) {
        out->action = text;
        return;
    }
    int reference = 0;
    if (!parseReferenceAfter(dictBody, jsValue, &reference)) return;
    ObjectSpan span;
    if (!findObject(objects, reference, &span)) return;
    const QByteArray referenced = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
    QString referencedText;
    qsizetype referencedEnd = 0;
    if (parseStringValue(referenced, 0, &referencedText, &referencedEnd))
        out->action = referencedText;
}

// 找根目录对象号：经典 trailer 或交叉引用流的字典里都有 /Root。
bool findRootNumber(const QByteArray& pdf, const QVector<ObjectSpan>& objects, int* rootNumber) {
    const qsizetype trailerAt = pdf.lastIndexOf("trailer");
    if (trailerAt >= 0) {
        const qsizetype startxrefAt = pdf.indexOf("startxref", trailerAt);
        const qsizetype end = startxrefAt > trailerAt ? startxrefAt : pdf.size();
        const QByteArray trailer = pdf.mid(trailerAt, end - trailerAt);
        const qsizetype rootAt = findNameIn(trailer, "/Root", 0, trailer.size());
        if (rootAt >= 0 && parseReferenceAfter(trailer, rootAt + 5, rootNumber)) return true;
    }
    // 交叉引用流：/Root 与 /Prev 都在它的字典里，不需要解压流本身。
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        const QByteArray body = pdf.mid(it->bodyBegin, it->bodyEnd - it->bodyBegin);
        if (!body.contains("/XRef")) continue;
        const qsizetype rootAt = findNameIn(body, "/Root", 0, body.size());
        if (rootAt >= 0 && parseReferenceAfter(body, rootAt + 5, rootNumber)) return true;
    }
    return false;
}

}  // namespace

QVector<FormFieldScript> readFormFieldScripts(const QByteArray& pdf) {
    QVector<FormFieldScript> result;
    if (pdf.isEmpty()) return result;
    const QVector<ObjectSpan> objects = scanObjects(pdf);
    // 同一个对象号只取最后一个版本（增量更新里后出现的才是有效的）。
    QVector<int> numbers;
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        if (numbers.contains(it->number)) continue;
        numbers.append(it->number);
    }
    std::sort(numbers.begin(), numbers.end());
    for (int number : numbers) {
        ObjectSpan span;
        if (!findObject(objects, number, &span)) continue;
        const QByteArray body = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
        // 只有带 /FT 的才是 AcroForm 字段；纯外观注释不算。
        if (findNameIn(body, "/FT", 0, body.size()) < 0) continue;
        FormFieldScript script;
        script.fieldName = fieldNameOf(body);
        if (script.fieldName.isEmpty()) continue;
        const qsizetype dvAt = findNameIn(body, "/DV", 0, body.size());
        if (dvAt >= 0) {
            qsizetype dvValue = dvAt + 3;
            skipWhitespace(body, &dvValue);
            QString defaultValue;
            qsizetype dvEnd = 0;
            if (parseStringValue(body, dvValue, &defaultValue, &dvEnd))
                script.defaultValue = defaultValue;
        }
        readAdditionalActions(body, objects, pdf, &script);
        readActionScript(body, objects, pdf, &script);
        if (script.calculate.isEmpty() && script.format.isEmpty() && script.validate.isEmpty() &&
            script.action.isEmpty() && script.defaultValue.isEmpty()) {
            continue;
        }
        result.append(script);
    }
    return result;
}

QStringList readCalculationOrder(const QByteArray& pdf) {
    QStringList order;
    if (pdf.isEmpty()) return order;
    const QVector<ObjectSpan> objects = scanObjects(pdf);
    int rootNumber = 0;
    if (!findRootNumber(pdf, objects, &rootNumber)) return order;
    ObjectSpan root;
    if (!findObject(objects, rootNumber, &root)) return order;
    const QByteArray rootBody = pdf.mid(root.bodyBegin, root.bodyEnd - root.bodyBegin);
    const qsizetype acroAt = findNameIn(rootBody, "/AcroForm", 0, rootBody.size());
    if (acroAt < 0) return order;
    qsizetype valueAt = acroAt + 9;  // "/AcroForm" 是 9 个字符
    skipWhitespace(rootBody, &valueAt);
    QByteArray acroBody = rootBody;
    qsizetype acroBegin = 0;
    qsizetype acroEnd = rootBody.size();
    if (valueAt + 1 < rootBody.size() && rootBody.at(valueAt) == '<' &&
        rootBody.at(valueAt + 1) == '<') {
        acroBegin = valueAt;
        const qsizetype close = dictClose(rootBody, valueAt);
        if (close > 0) acroEnd = close;
    } else {
        int reference = 0;
        if (!parseReferenceAfter(rootBody, valueAt, &reference)) return order;
        ObjectSpan span;
        if (!findObject(objects, reference, &span)) return order;
        acroBody = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
        acroBegin = 0;
        const qsizetype close = dictClose(acroBody, 0);
        acroEnd = close > 0 ? close : acroBody.size();
    }
    const qsizetype coAt = findNameIn(acroBody, "/CO", acroBegin, acroEnd);
    if (coAt < 0) return order;
    qsizetype coValue = coAt + 3;
    skipWhitespace(acroBody, &coValue);
    if (coValue >= acroBody.size() || acroBody.at(coValue) != '[') return order;
    const qsizetype close = matching(acroBody, coValue, '[', ']');
    if (close < 0) return order;
    qsizetype cursor = coValue + 1;
    while (cursor < close) {
        skipWhitespace(acroBody, &cursor);
        if (cursor >= close) break;
        if (acroBody.at(cursor) == '[') {
            // 层级字段的 /CO 里会出现嵌套数组，这里跳过不处理。
            const qsizetype inner = matching(acroBody, cursor, '[', ']');
            if (inner < 0) break;
            cursor = inner + 1;
            continue;
        }
        const qsizetype digitsBegin = cursor;
        while (cursor < close && acroBody.at(cursor) >= '0' && acroBody.at(cursor) <= '9') ++cursor;
        if (digitsBegin == cursor) {
            ++cursor;
            continue;
        }
        bool ok = false;
        const int reference = acroBody.mid(digitsBegin, cursor - digitsBegin).toInt(&ok);
        if (!ok) continue;
        while (cursor < close && acroBody.at(cursor) != 'R') ++cursor;
        if (cursor < close) ++cursor;
        ObjectSpan span;
        if (!findObject(objects, reference, &span)) continue;
        const QByteArray fieldBody = pdf.mid(span.bodyBegin, span.bodyEnd - span.bodyBegin);
        const QString name = fieldNameOf(fieldBody);
        if (!name.isEmpty()) order.append(name);
    }
    return order;
}

FormScriptWriteResult writeFormScripts(const QByteArray& pdf,
                                        const QVector<FormFieldScript>& scripts,
                                        const QStringList& order) {
    FormScriptWriteResult result;
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF qpdf;
        qpdf.processMemoryFile("form-scripts.pdf", pdf.constData(),
                                static_cast<size_t>(pdf.size()));
        QPDFObjectHandle root = qpdf.getRoot();
        QPDFObjectHandle acro = root.getKey("/AcroForm");
        if (!acro.isDictionary()) {
            result.error = QStringLiteral("这份 PDF 没有 AcroForm 目录，无法写入表单脚本");
            return result;
        }
        QPDFObjectHandle fields = acro.getKey("/Fields");
        if (!fields.isArray()) {
            result.error = QStringLiteral("AcroForm 的 /Fields 不是数组，无法写入表单脚本");
            return result;
        }
        // 名字 → 字段对象与它的对象号。字段名重名时以第一个为准（PDF 也不允许重名）。
        QHash<QString, QPDFObjectHandle> byName;
        QHash<QString, QPDFObjGen> byId;
        for (const QPDFObjectHandle& element : fields.getArrayAsVector()) {
            if (!element.isDictionary()) continue;
            QPDFObjectHandle title = element.getKey("/T");
            if (!title.isString()) continue;
            const QString name = QString::fromUtf8(title.getUTF8Value().c_str());
            if (name.isEmpty() || byName.contains(name)) continue;
            byName.insert(name, element);
            byId.insert(name, element.getObjGen());
        }
        for (const FormFieldScript& script : scripts) {
            // 三项都空就别碰这个字段：否则会给它挂上一个空的 /AA。
            if (script.calculate.isEmpty() && script.format.isEmpty() &&
                script.validate.isEmpty())
                continue;
            auto found = byName.find(script.fieldName);
            if (found == byName.end()) continue;
            QPDFObjectHandle dict = found.value();
            QPDFObjectHandle actions = dict.getKey("/AA");
            if (!actions.isDictionary()) {
                actions = QPDFObjectHandle::newDictionary();
                dict.replaceKey("/AA", actions);
            }
            const auto put = [&actions](const char* key, const QString& value) {
                if (value.isEmpty()) return;
                actions.replaceKey(
                    key, QPDFObjectHandle::newUnicodeString(std::string(value.toUtf8().constData())));
            };
            put("/C", script.calculate);
            put("/F", script.format);
            put("/V", script.validate);
        }
        if (!order.isEmpty()) {
            std::vector<QPDFObjectHandle> references;
            for (const QString& name : order) {
                auto found = byId.find(name);
                if (found == byId.end()) continue;
                const QPDFObjGen id = found.value();
                if (id.getObj() == 0) continue;
                references.push_back(qpdf.getObject(id));
            }
            if (!references.empty())
                acro.replaceKey("/CO", QPDFObjectHandle::newArray(references));
        }
        QPDFWriter writer(qpdf);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            result.error = QStringLiteral("qpdf 没有写出脚本更新结果");
            return result;
        }
        result.bytes = QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                                  static_cast<int>(buffer->getSize()));
        result.ok = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = QStringLiteral("写入表单脚本失败：%1")
                           .arg(QString::fromUtf8(exception.what()));
        return result;
    }
#else
    Q_UNUSED(pdf);
    Q_UNUSED(scripts);
    Q_UNUSED(order);
    result.error = QStringLiteral("本次构建未启用 qpdf，无法把脚本写回 PDF（读取不受影响）");
    return result;
#endif
}

}  // namespace aipdf::pdf
