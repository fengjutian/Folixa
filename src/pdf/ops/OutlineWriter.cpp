#include "pdf/ops/OutlineWriter.h"
#include <QRegularExpression>
#include <QUrl>
#include <QVector>
#include <algorithm>
#include <cctype>
namespace aipdf::pdf { namespace {
struct ObjectSpan { int number = 0; qsizetype begin = 0; qsizetype end = 0; };
QByteArray padded(qsizetype value) {
    QByteArray text = QByteArray::number(value);
    while (text.size() < 10) text.prepend('0');
    return text;
}
QVector<ObjectSpan> scanObjects(const QByteArray& pdf) {
    QVector<ObjectSpan> out;
    qsizetype pos = 0;
    while ((pos = pdf.indexOf(" obj", pos)) >= 0) {
        qsizetype line = pdf.lastIndexOf('\n', pos); line = line < 0 ? 0 : line + 1;
        const QList<QByteArray> parts = pdf.mid(line, pos - line).trimmed().simplified().split(' ');
        bool ok = false; const int number = parts.size() == 2 ? parts[0].toInt(&ok) : 0;
        const qsizetype begin = pos + 4, end = pdf.indexOf("endobj", begin);
        if (ok && end >= 0) out.append({number, begin, end});
        if (end < 0) break; pos = end + 6;
    }
    return out;
}
bool isPage(const QByteArray& body) {
    return (body.contains("/Type /Page") || body.contains("/Type/Page")) &&
           !(body.contains("/Type /Pages") || body.contains("/Type/Pages"));
}
QByteArray titleString(const QString& title) {
    QByteArray hex("FEFF");
    for (QChar ch : title) {
        const ushort value = ch.unicode();
        hex += QByteArray::number((value >> 8) & 255, 16).rightJustified(2, '0');
        hex += QByteArray::number(value & 255, 16).rightJustified(2, '0');
    }
    return '<' + hex.toUpper() + '>';
}
QByteArray literal(QByteArray value) {
    value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)");
    return '(' + value + ')';
}
struct Node {
    const OutlineItem* item = nullptr; int parent = -1; int prev = -1; int next = -1;
    int first = -1; int last = -1; int descendants = 0; int object = 0;
};
int appendLevel(const std::vector<OutlineItem>& items, int parent, QVector<Node>* nodes) {
    QVector<int> siblings;
    for (const OutlineItem& item : items) {
        const int index = nodes->size(); nodes->append({&item, parent}); siblings.append(index);
        const int first = appendLevel(item.children, index, nodes);
        if (first >= 0) {
            (*nodes)[index].first = first; int last = first;
            while ((*nodes)[last].next >= 0) last = (*nodes)[last].next;
            (*nodes)[index].last = last;
        }
    }
    for (int i = 0; i < siblings.size(); ++i) {
        if (i) (*nodes)[siblings[i]].prev = siblings[i - 1];
        if (i + 1 < siblings.size()) (*nodes)[siblings[i]].next = siblings[i + 1];
    }
    return siblings.isEmpty() ? -1 : siblings.first();
}
int countChildren(int index, QVector<Node>* nodes) {
    int total = 0;
    for (int child = (*nodes)[index].first; child >= 0; child = (*nodes)[child].next)
        total += 1 + countChildren(child, nodes);
    (*nodes)[index].descendants = total; return total;
}
}  // namespace
OutlineWriteResult rewriteOutline(const QByteArray& pdf, const std::vector<OutlineItem>& outline) {
    OutlineWriteResult result;
    const qsizetype startAt = pdf.lastIndexOf("startxref");
    const qsizetype trailerAt = pdf.lastIndexOf("trailer", startAt);
    if (startAt < 0 || trailerAt < 0) { result.error = QStringLiteral("PDF 没有交叉引用表"); return result; }
    const QByteArray trailer = pdf.mid(trailerAt, startAt - trailerAt);
    const QString trailerText = QString::fromLatin1(trailer);
    auto rootMatch = QRegularExpression(QStringLiteral(R"(/Root\s+(\d+)\s+\d+\s+R)")).match(trailerText);
    if (!rootMatch.hasMatch()) { result.error = QStringLiteral("找不到 PDF Catalog"); return result; }
    const int rootNumber = rootMatch.captured(1).toInt();
    auto sizeMatch = QRegularExpression(QStringLiteral(R"(/Size\s+(\d+))")).match(trailerText);
    const int oldSize = sizeMatch.hasMatch() ? sizeMatch.captured(1).toInt() : 0;
    qsizetype p = startAt + 9; while (p < pdf.size() && !std::isdigit((unsigned char)pdf[p])) ++p;
    qsizetype e = p; while (e < pdf.size() && std::isdigit((unsigned char)pdf[e])) ++e;
    const qsizetype previousXref = pdf.mid(p, e - p).toLongLong();
    const QVector<ObjectSpan> objects = scanObjects(pdf);
    ObjectSpan root; QVector<int> pages; int maximum = 0;
    for (const auto& object : objects) {
        maximum = std::max(maximum, object.number);
        const QByteArray body = pdf.mid(object.begin, object.end - object.begin);
        if (object.number == rootNumber) root = object;
        if (isPage(body)) pages.append(object.number);
    }
    if (!root.number || pages.isEmpty()) { result.error = QStringLiteral("页面树无法解析"); return result; }
    int next = std::max(maximum, oldSize - 1) + 1;
    const int outlineRoot = outline.empty() ? 0 : next++;
    QVector<Node> nodes; appendLevel(outline, -1, &nodes);
    for (Node& node : nodes) node.object = next++;
    for (int i = 0; i < nodes.size(); ++i) countChildren(i, &nodes);
    QByteArray rootBody = pdf.mid(root.begin, root.end - root.begin);
    // 直接在字节上删旧的 /Outlines 引用。先转 QString/Latin-1 再转回去，会把
    // Catalog 里任何非 Latin-1 字节（UTF-8 名字、二进制字符串）替换成 '?'，
    // 相当于把别人的 Catalog 写坏。QByteArray 没有正则替换，这里按 PDF 语法
    // 手工匹配 `/Outlines <num> <gen> R`。
    {
        qsizetype pos = 0;
        while ((pos = rootBody.indexOf("/Outlines", pos)) >= 0) {
            qsizetype i = pos + 9;
            auto skipSpace = [&] {
                while (i < rootBody.size() &&
                       (rootBody.at(i) == ' ' || rootBody.at(i) == '\r' ||
                        rootBody.at(i) == '\n' || rootBody.at(i) == '\t')) {
                    ++i;
                }
            };
            auto readNumber = [&]() -> bool {
                const qsizetype begin = i;
                while (i < rootBody.size() && rootBody.at(i) >= '0' && rootBody.at(i) <= '9') {
                    ++i;
                }
                return i > begin;
            };
            skipSpace();
            if (!readNumber()) {
                pos += 9;
                continue;
            }
            skipSpace();
            if (!readNumber()) {
                pos += 9;
                continue;
            }
            skipSpace();
            if (i < rootBody.size() && rootBody.at(i) == 'R') {
                ++i;
                rootBody.remove(pos, i - pos);
                continue;
            }
            pos += 9;
        }
    }
    if (outlineRoot) {
        const qsizetype close = rootBody.lastIndexOf(">>");
        if (close < 0) { result.error = QStringLiteral("Catalog 不是字典"); return result; }
        rootBody.insert(close, "/Outlines " + QByteArray::number(outlineRoot) + " 0 R ");
    }
    QByteArray tail; QVector<QPair<int, qsizetype>> offsets;
    auto appendObject = [&](int number, const QByteArray& body) {
        offsets.append({number, pdf.size() + tail.size()});
        tail += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
    };
    appendObject(rootNumber, rootBody);
    if (outlineRoot) {
        int last = 0; while (nodes[last].next >= 0) last = nodes[last].next;
        appendObject(outlineRoot, "<< /Type /Outlines /First " + QByteArray::number(nodes[0].object) +
                     " 0 R /Last " + QByteArray::number(nodes[last].object) + " 0 R /Count " +
                     QByteArray::number(nodes.size()) + " >>");
    }
    for (int i = 0; i < nodes.size(); ++i) {
        const Node& n = nodes[i];
        const int parent = n.parent >= 0 ? nodes[n.parent].object : outlineRoot;
        QByteArray body = "<< /Title " + titleString(n.item->title) + " /Parent " + QByteArray::number(parent) + " 0 R ";
        if (n.prev >= 0) body += "/Prev " + QByteArray::number(nodes[n.prev].object) + " 0 R ";
        if (n.next >= 0) body += "/Next " + QByteArray::number(nodes[n.next].object) + " 0 R ";
        if (n.first >= 0) body += "/First " + QByteArray::number(nodes[n.first].object) + " 0 R /Last " + QByteArray::number(nodes[n.last].object) + " 0 R /Count " + QByteArray::number(n.descendants) + ' ';
        if (n.item->pageIndex >= 0 && n.item->pageIndex < pages.size()) body += "/Dest [" + QByteArray::number(pages[n.item->pageIndex]) + " 0 R /Fit] ";
        else if (!n.item->uri.isEmpty()) body += "/A << /S /URI /URI " + literal(QUrl(n.item->uri).toEncoded()) + " >> ";
        appendObject(n.object, body + ">>");
    }
    std::sort(offsets.begin(), offsets.end(), [](auto a, auto b) { return a.first < b.first; });
    const qsizetype xref = pdf.size() + tail.size(); tail += "xref\n";
    for (auto offset : offsets) tail += QByteArray::number(offset.first) + " 1\n" + padded(offset.second) + " 00000 n \n";
    tail += "trailer\n<< /Size " + QByteArray::number(std::max(next, oldSize)) + " /Root " + QByteArray::number(rootNumber) + " 0 R /Prev " + QByteArray::number(previousXref) + " >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    result.bytes = pdf + tail; result.ok = true; return result;
}
}  // namespace aipdf::pdf
