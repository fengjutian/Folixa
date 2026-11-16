#include "pdf/IncrementalWriter.h"

#include "i18n/Translate.h"

#include <algorithm>
#include <map>
#include <utility>

namespace aipdf::pdf {

namespace {

// 只认识「经典 xref 表 + 直接对象」这一种文件形态。
//
// 这不是一个 PDF 解析器，也不打算变成解析器：它只处理 PDFium 刚写出来的字节
// （我们的保存路径天然满足），遇到对象流、交叉引用流、加密文件一律失败退出。
// 完整对象层（含对象流与加密）留给后续的 qpdf 接入。

struct ObjectRef {
    int object = 0;
    int generation = 0;
};

QByteArray padded(qsizetype value) {
    QByteArray text = QByteArray::number(static_cast<qlonglong>(value));
    while (text.size() < 10) {
        text.prepend('0');
    }
    return text;
}

bool digitsOnly(const QByteArray& text) {
    bool sawDigit = false;
    for (char c : text) {
        if (c == ' ') {
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        sawDigit = true;
    }
    return sawDigit;
}

// 对象头形如 "3 0"（对象号 + 代号）。只取第一个数。
//
// 这里不能对整个字符串调 toInt()：QByteArray::toInt() 要求全串都是数字，
// "3 0" 会解析失败返回 0，结果写出一个 "0 0 obj" 把文件结构写坏。
int objectNumberFromHeader(const QByteArray& header) {
    const QList<QByteArray> parts = header.trimmed().split(' ');
    for (const QByteArray& part : parts) {
        if (!part.isEmpty()) {
            return part.toInt();
        }
    }
    return 0;
}

bool parseObjectRef(const QByteArray& text, qsizetype from, ObjectRef* out) {
    qsizetype i = from;
    auto skipSpace = [&]() {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' ||
                                   text[i] == '\t')) {
            ++i;
        }
    };
    auto readInt = [&](int* value) {
        skipSpace();
        const qsizetype start = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
        }
        if (i == start) {
            return false;
        }
        *value = text.mid(start, i - start).toInt();
        return true;
    };

    if (!readInt(&out->object) || !readInt(&out->generation)) {
        return false;
    }
    skipSpace();
    return i < text.size() && text[i] == 'R';
}

// 找到字典最外层的闭合位置（返回 ">>" 之后的下标），跳过字符串与嵌套字典。
qsizetype findDictClose(const QByteArray& pdf, qsizetype from, qsizetype to) {
    int depth = 0;
    bool started = false;
    for (qsizetype i = from; i + 1 < to; ++i) {
        if (pdf[i] == '(') {
            int nesting = 1;
            for (++i; i < to && nesting > 0; ++i) {
                if (pdf[i] == '\\') {
                    ++i;
                } else if (pdf[i] == '(') {
                    ++nesting;
                } else if (pdf[i] == ')') {
                    --nesting;
                }
            }
            --i;
            continue;
        }
        if (pdf[i] == '<' && pdf[i + 1] == '<') {
            ++depth;
            started = true;
            ++i;
            continue;
        }
        if (pdf[i] == '>' && pdf[i + 1] == '>') {
            --depth;
            ++i;
            if (started && depth == 0) {
                return i + 1;
            }
        }
    }
    return -1;
}

struct Target {
    int object = 0;
    qsizetype bodyBegin = 0;
    qsizetype bodyEnd = 0;
    qsizetype insertAt = 0;
};

bool findMarkerDict(const QByteArray& pdf, qsizetype from, qsizetype to,
                    const QByteArray& marker, qsizetype* dictClose) {
    qsizetype cursor = from;
    while (true) {
        const qsizetype dictAt = pdf.indexOf("<<", cursor);
        if (dictAt < 0 || dictAt >= to) {
            return false;
        }
        const qsizetype close = findDictClose(pdf, dictAt, to);
        if (close < 0) {
            return false;
        }
        const QByteArray dict = pdf.mid(dictAt, close - dictAt);
        if (dict.contains(marker) && dict.contains("/Subtype")) {
            *dictClose = close;
            return true;
        }
        cursor = close;
    }
}

bool findTarget(const QByteArray& pdf, const QByteArray& marker, Target* out) {
    qsizetype pos = 0;
    while ((pos = pdf.indexOf(" obj", pos)) >= 0) {
        qsizetype lineStart = pdf.lastIndexOf('\n', pos);
        lineStart = (lineStart < 0) ? 0 : lineStart + 1;
        const QByteArray header = pdf.mid(lineStart, pos - lineStart);
        if (!digitsOnly(header)) {
            pos += 4;
            continue;
        }

        const qsizetype bodyBegin = pos + 4;
        const qsizetype endobj = pdf.indexOf("endobj", bodyBegin);
        if (endobj < 0) {
            return false;
        }

        // a) 注释内联在页面的 /Annots 数组里（PDFium 自己写出来的形态）。
        const qsizetype annotsAt = pdf.indexOf("/Annots", bodyBegin);
        if (annotsAt >= 0 && annotsAt < endobj) {
            qsizetype dictClose = 0;
            if (findMarkerDict(pdf, annotsAt, endobj, marker, &dictClose)) {
                out->object = objectNumberFromHeader(header);
                out->bodyBegin = bodyBegin;
                out->bodyEnd = endobj;
                out->insertAt = dictClose - 2;
                return true;
            }
        }

        // b) 注释是独立对象。
        const QByteArray body = pdf.mid(bodyBegin, endobj - bodyBegin);
        if (body.contains(marker) && body.contains("/Subtype")) {
            const qsizetype close = findDictClose(pdf, bodyBegin, endobj);
            if (close < 0) {
                return false;
            }
            out->object = objectNumberFromHeader(header);
            out->bodyBegin = bodyBegin;
            out->bodyEnd = endobj;
            out->insertAt = close - 2;
            return true;
        }

        pos = endobj;
    }
    return false;
}

struct PendingObject {
    Target target;
    QVector<QPair<qsizetype, QByteArray>> inserts;  // (插入点, 要插入的片段)
};

QByteArray buildAppearanceBody(const Appearance& appearance) {
    QByteArray body =
        "<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 " +
        QByteArray::number(appearance.bbox.width(), 'f', 3) + " " +
        QByteArray::number(appearance.bbox.height(), 'f', 3) + "] ";
    if (!appearance.resources.isEmpty()) {
        body += "/Resources << " + appearance.resources + " >> ";
    }
    body += "/Length " + QByteArray::number(appearance.content.size()) + " >>\nstream\n";
    body += appearance.content;
    body += "endstream";
    return body;
}

}  // namespace

IncrementalWrite appendAnnotationAppearances(const QByteArray& pdf,
                                             const QVector<AppearanceAttachment>& attachments) {
    IncrementalWrite result;
    if (attachments.isEmpty()) {
        result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "没有要写入的外观流");
        return result;
    }

    const qsizetype startxrefAt = pdf.lastIndexOf("startxref");
    if (startxrefAt < 0) {
        result.error =
            i18n::tr("aipdf::pdf::IncrementalWriter", "找不到 startxref：输入不是完整的 PDF");
        return result;
    }
    const qsizetype trailerAt = pdf.lastIndexOf("trailer", startxrefAt);
    if (trailerAt < 0) {
        result.error =
            i18n::tr("aipdf::pdf::IncrementalWriter", "找不到 trailer：可能是交叉引用流，暂不支持");
        return result;
    }
    const QByteArray trailerDict = pdf.mid(trailerAt, startxrefAt - trailerAt);

    const qsizetype rootAt = trailerDict.indexOf("/Root");
    ObjectRef root;
    if (rootAt < 0 || !parseObjectRef(trailerDict, rootAt + 5, &root)) {
        result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "trailer 里没有可解析的 /Root");
        return result;
    }

    int declaredSize = 0;
    const qsizetype sizeAt = trailerDict.indexOf("/Size");
    if (sizeAt >= 0) {
        qsizetype i = sizeAt + 5;
        while (i < trailerDict.size() && (trailerDict[i] < '0' || trailerDict[i] > '9')) {
            ++i;
        }
        const qsizetype begin = i;
        while (i < trailerDict.size() && trailerDict[i] >= '0' && trailerDict[i] <= '9') {
            ++i;
        }
        declaredSize = trailerDict.mid(begin, i - begin).toInt();
    }

    qsizetype previousXref = 0;
    {
        qsizetype i = startxrefAt + 9;
        while (i < pdf.size() && (pdf[i] < '0' || pdf[i] > '9')) {
            ++i;
        }
        const qsizetype begin = i;
        while (i < pdf.size() && pdf[i] >= '0' && pdf[i] <= '9') {
            ++i;
        }
        previousXref = pdf.mid(begin, i - begin).toLongLong();
    }
    if (previousXref <= 0) {
        result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "startxref 数值无效");
        return result;
    }

    // /Size 不能全信：它可能缺失、可能写小。新对象编号取「扫描到的最大对象号」与
    // 「/Size - 1」的较大者 + 1，否则会和已有对象撞号——撞号表现为文件能打开但没有页。
    int maxObject = 0;
    {
        qsizetype pos = 0;
        while ((pos = pdf.indexOf(" obj", pos)) >= 0) {
            qsizetype lineStart = pdf.lastIndexOf('\n', pos);
            lineStart = (lineStart < 0) ? 0 : lineStart + 1;
            const QByteArray header = pdf.mid(lineStart, pos - lineStart);
            if (digitsOnly(header)) {
                maxObject = std::max(maxObject, objectNumberFromHeader(header));
            }
            pos += 4;
        }
    }

    int nextObject = std::max(maxObject, declaredSize > 0 ? declaredSize - 1 : 0) + 1;

    // 按「承载对象」分组：同一页上的多条批注往往内联在同一个页面对象里，
    // 逐条重写会让前面的写入被后一次覆盖。
    std::map<int, PendingObject> pending;
    QVector<QPair<int, QByteArray>> appearanceBodies;
    QVector<QByteArray> seenMarkers;

    // 先把所有标记对应的对象号摸清楚：/IRT（回复）要引用父批注的对象号，
    // 而对象号只有解析文件之后才知道。
    std::map<QByteArray, int> markerObjects;
    for (const AppearanceAttachment& item : attachments) {
        Target probe;
        if (findTarget(pdf, item.marker, &probe)) {
            markerObjects[item.marker] = probe.object;
        }
    }

    for (const AppearanceAttachment& item : attachments) {
        if (item.appearance.isEmpty() && item.extraObjects.isEmpty() &&
            item.extraEntries.isEmpty()) {
            continue;
        }
        if (seenMarkers.contains(item.marker)) {
            continue;  // 同一个标记只补一次，避免插出两个 /AP 键
        }
        Target target;
        if (!findTarget(pdf, item.marker, &target)) {
            result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "找不到带标记 %1 的批注对象")
                               .arg(QString::fromUtf8(item.marker));
            result.bytes.clear();
            return result;
        }
        seenMarkers.append(item.marker);

        auto it = pending.find(target.object);
        if (it == pending.end()) {
            PendingObject fresh;
            fresh.target = target;
            it = pending.emplace(target.object, fresh).first;
        }

        const int appearanceObject = nextObject++;
        // 先给额外对象分配号，再替换占位符：对象之间可能互相引用。
        QVector<int> extraNumbers;
        for (int i = 0; i < item.extraObjects.size(); ++i) {
            extraNumbers.append(nextObject++);
        }
        auto substitute = [&extraNumbers, &markerObjects](QByteArray body) {
            for (int i = 0; i < extraNumbers.size(); ++i) {
                body.replace("@EXTRA" + QByteArray::number(i),
                             QByteArray::number(extraNumbers.at(i)));
            }
            for (const auto& entry : markerObjects) {
                body.replace("@ANNOT" + entry.first, QByteArray::number(entry.second));
            }
            return body;
        };
        for (int i = 0; i < item.extraObjects.size(); ++i) {
            appearanceBodies.append({ extraNumbers.at(i), substitute(item.extraObjects.at(i)) });
        }
        // 便签这类没有图形外观的批注也要写（/IRT、/T、/State 都在 extraEntries 里），
        // 只是不写 /AP——写一个空 XObject 反而会让阅读器画出一块空白。
        QByteArray inserted;
        if (!item.appearance.isEmpty()) {
            inserted = "/AP << /N " + QByteArray::number(appearanceObject) + " 0 R >> ";
        }
        if (!item.extraEntries.isEmpty()) {
            // 额外字典项（/L、/BE、/Vertices…）与 /AP 一起插进同一次重写。
            // 顺序无所谓：PDF 字典按键查找，重复键才是问题——所以调用方必须
            // 保证这些键原本不存在（PDFium 建出来的注释确实没有它们）。
            inserted += substitute(item.extraEntries);
            inserted += ' ';
        }
        if (inserted.contains("@ANNOT")) {
            // 占位符没被替换掉说明父批注不在这次写入里：宁可失败，
            // 也不要把一个指向不存在对象的 /IRT 写进文件。
            result.error =
                i18n::tr("aipdf::pdf::IncrementalWriter", "回复引用的父批注不在本次写入中：") +
                QByteArray(item.marker);
            return result;
        }
        it->second.inserts.append({ target.insertAt, inserted });
        if (!item.appearance.isEmpty()) {
            // 图片图章的 /Im0、/SMask 会引用上面已经分配并写入的额外对象；
            // 这里只替换外观流中的占位符，不能再次追加同一批对象。
            Appearance styled = item.appearance;
            styled.content = substitute(styled.content);
            styled.resources = substitute(styled.resources);
            appearanceBodies.append({ appearanceObject, buildAppearanceBody(styled) });
        }
    }

    if (appearanceBodies.isEmpty()) {
        result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "所有外观流都为空，没有可写的内容");
        return result;
    }

    QByteArray tail;
    QVector<QPair<int, qsizetype>> offsets;

    for (auto& entry : pending) {
        PendingObject& object = entry.second;
        QByteArray body =
            pdf.mid(object.target.bodyBegin, object.target.bodyEnd - object.target.bodyBegin);
        // 从后往前插：偏移才不会被前一次插入打乱。
        std::sort(object.inserts.begin(), object.inserts.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& insert : object.inserts) {
            const qsizetype relative = insert.first - object.target.bodyBegin;
            if (relative < 0 || relative > body.size()) {
                result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "插入点越界，放弃写入");
                return result;
            }
            body.insert(relative, insert.second);
        }
        offsets.append({ entry.first, pdf.size() + tail.size() });
        tail += QByteArray::number(entry.first) + " 0 obj\n" + body + "\nendobj\n";
    }

    for (const auto& appearance : appearanceBodies) {
        offsets.append({ appearance.first, pdf.size() + tail.size() });
        tail += QByteArray::number(appearance.first) + " 0 obj\n" + appearance.second +
                "\nendobj\n";
    }

    std::sort(offsets.begin(), offsets.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const qsizetype xrefOffset = pdf.size() + tail.size();
    tail += "xref\n";
    for (const auto& offset : offsets) {
        tail += QByteArray::number(offset.first) + " 1\n" + padded(offset.second) + " 00000 n \n";
    }
    const int newSize = std::max(offsets.last().first, declaredSize) + 1;
    tail += "trailer\n<< /Size " + QByteArray::number(newSize) + " /Root " +
            QByteArray::number(root.object) + " " + QByteArray::number(root.generation) +
            " R /Prev " + QByteArray::number(previousXref) + " >>\n";
    tail += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";

    result.bytes = pdf + tail;
    result.ok = true;
    result.appearanceCount = appearanceBodies.size();
    result.rewrittenObjects = static_cast<int>(pending.size());
    return result;
}

IncrementalWrite appendAnnotationAppearance(const QByteArray& pdf,
                                            const QByteArray& marker,
                                            const Appearance& appearance) {
    if (marker.isEmpty()) {
        IncrementalWrite result;
        result.error = i18n::tr("aipdf::pdf::IncrementalWriter", "缺少批注标记，无法定位注释");
        return result;
    }
    return appendAnnotationAppearances(pdf, { AppearanceAttachment{ marker, appearance } });
}

}  // namespace aipdf::pdf
