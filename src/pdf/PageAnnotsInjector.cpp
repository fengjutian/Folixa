#include "pdf/PageAnnotsInjector.h"

#include <QVector>

#include <algorithm>
#include <map>

namespace aipdf::pdf {

namespace {

struct ObjectSpan {
    int number = 0;
    qsizetype bodyBegin = 0;
    qsizetype bodyEnd = 0;
};

QByteArray padded(qsizetype value) {
    QByteArray text = QByteArray::number(value);
    while (text.size() < 10) {
        text.prepend('0');
    }
    return text;
}

// 对象头是「对象号 代号」，例如 "3 0"；两段都必须是数字。
bool parseObjectHeader(const QByteArray& rawText, int* number) {
    const QList<QByteArray> parts = rawText.trimmed().simplified().split(' ');
    if (parts.size() != 2) {
        return false;
    }
    for (const QByteArray& part : parts) {
        if (part.isEmpty()) {
            return false;
        }
        for (const char ch : part) {
            if (ch < '0' || ch > '9') {
                return false;
            }
        }
    }
    *number = parts.first().toInt();
    return true;
}

// 扫描所有 "N 0 obj ... endobj" 段。
QVector<ObjectSpan> scanObjects(const QByteArray& pdf) {
    QVector<ObjectSpan> objects;
    qsizetype pos = 0;
    while ((pos = pdf.indexOf(" obj", pos)) >= 0) {
        qsizetype lineStart = pdf.lastIndexOf('\n', pos);
        lineStart = (lineStart < 0) ? 0 : lineStart + 1;
        const QByteArray header = pdf.mid(lineStart, pos - lineStart);
        int objectNumber = 0;
        if (!parseObjectHeader(header, &objectNumber)) {
            pos += 4;
            continue;
        }
        const qsizetype bodyBegin = pos + 4;
        const qsizetype endobj = pdf.indexOf("endobj", bodyBegin);
        if (endobj < 0) {
            break;
        }
        objects.append({ objectNumber, bodyBegin, endobj });
        pos = endobj;
    }
    return objects;
}

// 找到页面对象（按文档顺序的第 index 个 /Type /Page）。
bool findPageObject(const QByteArray& pdf, const QVector<ObjectSpan>& objects, int pageIndex,
                    ObjectSpan* out) {
    int seen = 0;
    for (const ObjectSpan& object : objects) {
        const QByteArray body = pdf.mid(object.bodyBegin, object.bodyEnd - object.bodyBegin);
        if (!body.contains("/Type")) {
            continue;
        }
        const bool isPage = body.contains("/Type /Page") || body.contains("/Type/Page");
        const bool isPages = body.contains("/Type /Pages") || body.contains("/Type/Pages");
        if (!isPage || isPages) {
            continue;
        }
        if (seen == pageIndex) {
            *out = object;
            return true;
        }
        ++seen;
    }
    return false;
}

// 在 [from, to) 里找与 '[' 匹配的 ']'。
qsizetype findMatchingBracket(const QByteArray& pdf, qsizetype openAt, qsizetype to) {
    int depth = 0;
    for (qsizetype i = openAt; i < to; ++i) {
        const char ch = pdf.at(i);
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

qsizetype findDictClose(const QByteArray& pdf, qsizetype from, qsizetype to) {
    int depth = 0;
    bool started = false;
    for (qsizetype i = from; i + 1 < to; ++i) {
        if (pdf.at(i) == '<' && pdf.at(i + 1) == '<') {
            ++depth;
            started = true;
            ++i;
            continue;
        }
        if (pdf.at(i) == '>' && pdf.at(i + 1) == '>') {
            --depth;
            ++i;
            if (started && depth == 0) {
                return i + 1;
            }
        }
    }
    return -1;
}

QByteArray substitute(QByteArray body, const QVector<int>& numbers) {
    for (int i = 0; i < numbers.size(); ++i) {
        body.replace("@EXTRA" + QByteArray::number(i), QByteArray::number(numbers.at(i)));
    }
    return body;
}

}  // namespace

InlineWriteResult injectInlineAnnotations(const QByteArray& pdf,
                                          const QVector<InlineAnnotationInsert>& inserts) {
    InlineWriteResult result;
    if (inserts.isEmpty()) {
        result.error = QStringLiteral("没有要插入的注释");
        return result;
    }

    const qsizetype startxrefAt = pdf.lastIndexOf("startxref");
    if (startxrefAt < 0) {
        result.error = QStringLiteral("找不到 startxref：输入不是完整的 PDF");
        return result;
    }
    const qsizetype trailerAt = pdf.lastIndexOf("trailer", startxrefAt);
    if (trailerAt < 0) {
        result.error = QStringLiteral("找不到 trailer：可能是交叉引用流，暂不支持");
        return result;
    }
    const QByteArray trailerDict = pdf.mid(trailerAt, startxrefAt - trailerAt);

    const qsizetype rootAt = trailerDict.indexOf("/Root");
    if (rootAt < 0) {
        result.error = QStringLiteral("trailer 里没有 /Root");
        return result;
    }
    qsizetype i = rootAt + 5;
    while (i < trailerDict.size() && (trailerDict[i] < '0' || trailerDict[i] > '9')) {
        ++i;
    }
    const qsizetype rootBegin = i;
    while (i < trailerDict.size() && trailerDict[i] >= '0' && trailerDict[i] <= '9') {
        ++i;
    }
    const QByteArray rootObject = trailerDict.mid(rootBegin, i - rootBegin);
    if (rootObject.isEmpty()) {
        result.error = QStringLiteral("/Root 解析失败");
        return result;
    }

    int declaredSize = 0;
    const qsizetype sizeAt = trailerDict.indexOf("/Size");
    if (sizeAt >= 0) {
        qsizetype j = sizeAt + 5;
        while (j < trailerDict.size() && (trailerDict[j] < '0' || trailerDict[j] > '9')) {
            ++j;
        }
        const qsizetype begin = j;
        while (j < trailerDict.size() && trailerDict[j] >= '0' && trailerDict[j] <= '9') {
            ++j;
        }
        declaredSize = trailerDict.mid(begin, j - begin).toInt();
    }

    qsizetype previousXref = 0;
    {
        qsizetype j = startxrefAt + 9;
        while (j < pdf.size() && (pdf[j] < '0' || pdf[j] > '9')) {
            ++j;
        }
        const qsizetype begin = j;
        while (j < pdf.size() && pdf[j] >= '0' && pdf[j] <= '9') {
            ++j;
        }
        previousXref = pdf.mid(begin, j - begin).toLongLong();
    }
    if (previousXref <= 0) {
        result.error = QStringLiteral("startxref 数值无效");
        return result;
    }

    const QVector<ObjectSpan> objects = scanObjects(pdf);
    int maxObject = 0;
    for (const ObjectSpan& object : objects) {
        maxObject = std::max(maxObject, object.number);
    }
    int nextObject = std::max(maxObject, declaredSize > 0 ? declaredSize - 1 : 0) + 1;

    // 一次性分配对象号并生成：字典（带 @EXTRA 替换）与额外对象体。
    std::map<int, QVector<QByteArray>> byPage;  // pageIndex -> 字典列表
    QVector<QPair<int, QByteArray>> bodies;
    for (const InlineAnnotationInsert& insert : inserts) {
        QVector<int> numbers;
        for (int k = 0; k < insert.extraObjects.size(); ++k) {
            numbers.append(nextObject++);
        }
        byPage[insert.pageIndex].append(substitute(insert.dictionary, numbers));
        for (int k = 0; k < insert.extraObjects.size(); ++k) {
            bodies.append({ numbers.at(k), substitute(insert.extraObjects.at(k), numbers) });
        }
    }
    QByteArray tail;
    QVector<QPair<int, qsizetype>> offsets;

    for (const auto& entry : byPage) {
        ObjectSpan page;
        if (!findPageObject(pdf, objects, entry.first, &page)) {
            result.error = QStringLiteral("找不到第 %1 页的对象").arg(entry.first + 1);
            return result;
        }
        QByteArray body = pdf.mid(page.bodyBegin, page.bodyEnd - page.bodyBegin);
        QByteArray annots;
        for (const QByteArray& dict : entry.second) {
            annots += " ";
            annots += dict;
        }

        const qsizetype annotsAt = body.indexOf("/Annots");
        if (annotsAt >= 0) {
            const qsizetype openAt = body.indexOf('[', annotsAt);
            const qsizetype closeAt =
                openAt >= 0 ? findMatchingBracket(body, openAt, body.size()) : -1;
            if (openAt < 0 || closeAt < 0) {
                result.error = QStringLiteral("/Annots 结构无法解析");
                return result;
            }
            body.insert(closeAt, annots);
        } else {
            const qsizetype dictClose = findDictClose(body, 0, body.size());
            if (dictClose < 0) {
                result.error = QStringLiteral("页面对象不是字典");
                return result;
            }
            body.insert(dictClose - 2, "/Annots [" + annots + " ] ");
        }

        offsets.append({ page.number, pdf.size() + tail.size() });
        tail += QByteArray::number(page.number) + " 0 obj\n" + body + "\nendobj\n";
        result.inserted += entry.second.size();
    }

    for (const auto& entry : bodies) {
        offsets.append({ entry.first, pdf.size() + tail.size() });
        tail += QByteArray::number(entry.first) + " 0 obj\n" + entry.second + "\nendobj\n";
    }

    std::sort(offsets.begin(), offsets.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const qsizetype xrefOffset = pdf.size() + tail.size();
    tail += "xref\n";
    for (const auto& offset : offsets) {
        tail += QByteArray::number(offset.first) + " 1\n" + padded(offset.second) + " 00000 n \n";
    }
    const int newSize = std::max(offsets.last().first, declaredSize) + 1;
    tail += "trailer\n<< /Size " + QByteArray::number(newSize) + " /Root " + rootObject +
            " 0 R /Prev " + QByteArray::number(previousXref) + " >>\n";
    tail += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";

    result.bytes = pdf + tail;
    result.ok = true;
    return result;
}

}  // namespace aipdf::pdf
