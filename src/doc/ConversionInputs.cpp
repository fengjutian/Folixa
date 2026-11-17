#include "doc/ConversionInputs.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

#include "pdf/FormData.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/Xfdf.h"

namespace aipdf::doc {

namespace {

// ============================================================ 文本 → PDF ====

// 把「标题 + 正文」拼成一段纯文本。
//
// 为什么标题也走纯文本而不是 Markdown：正文里可能本来就含 #、* 这类字符，
// 当成 Markdown 解释会把用户的原话改掉。宁可标题不粗，也不改用户内容。
QString buildPlainText(const QString& title, const QString& body) {
    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty()) {
        return body;
    }
    if (body.trimmed().isEmpty()) {
        return trimmedTitle;
    }
    return trimmedTitle + QStringLiteral("\n\n") + body;
}

// 排版：纯文本 → PDF。
//
// 复用 doc/UniversalConverter：分页、折行、字体嵌入、xref 表都在那边，
// 这里不重写一套——重写的结果一定是行为不一致的第二套排版。
ConversionResult textToPdf(const QString& text, const QString& title,
                           const UniversalConverterOptions& options) {
    ConversionResult result;
    result.title = title.trimmed();
    const UniversalConversionResult converted =
        convertToPdf(text.toUtf8(), UniversalFileKind::PlainText, QStringLiteral("文档.pdf"), options);
    if (!converted.ok) {
        result.error = converted.error;
        return result;
    }
    result.ok = true;
    result.bytes = converted.bytes;
    result.warning = converted.warning;
    result.pageCount = converted.pageCount;
    result.replacedCharacterCount = converted.replacedCharacterCount;
    return result;
}

// ============================================================ HTML 解析 ====

// 要整段丢掉的元素：脚本、样式、导航、页眉页脚。
//
// 为什么不留下 <nav>：抓回来的网页正文之外总有一条菜单栏，排进 PDF 里
// 就是几十行没用的链接。页眉页脚同理。
const QStringList& droppedElements() {
    static const QStringList kElements = {
        QStringLiteral("script"),  QStringLiteral("style"),   QStringLiteral("noscript"),
        QStringLiteral("svg"),     QStringLiteral("iframe"),  QStringLiteral("object"),
        QStringLiteral("embed"),   QStringLiteral("template"), QStringLiteral("form"),
        QStringLiteral("nav"),     QStringLiteral("header"),  QStringLiteral("footer"),
        QStringLiteral("aside"),   QStringLiteral("head"),
    };
    return kElements;
}

// 块级元素：开始或结束都要换行，免得两段挤成一段。
const QStringList& blockElements() {
    static const QStringList kElements = {
        QStringLiteral("p"),         QStringLiteral("div"),      QStringLiteral("section"),
        QStringLiteral("article"),   QStringLiteral("main"),     QStringLiteral("h1"),
        QStringLiteral("h2"),        QStringLiteral("h3"),       QStringLiteral("h4"),
        QStringLiteral("h5"),        QStringLiteral("h6"),       QStringLiteral("ul"),
        QStringLiteral("ol"),        QStringLiteral("dl"),       QStringLiteral("li"),
        QStringLiteral("blockquote"), QStringLiteral("pre"),     QStringLiteral("table"),
        QStringLiteral("tr"),        QStringLiteral("td"),       QStringLiteral("th"),
        QStringLiteral("figure"),    QStringLiteral("figcaption"), QStringLiteral("address"),
        QStringLiteral("br"),        QStringLiteral("hr"),       QStringLiteral("dt"),
        QStringLiteral("dd"),
    };
    return kElements;
}

// 容器元素：只是把别的元素装起来，自己不是字段。
const QStringList& xmlContainerElements() {
    static const QStringList kElements = {
        QStringLiteral("form-data"), QStringLiteral("form"),    QStringLiteral("data"),
        QStringLiteral("root"),      QStringLiteral("fields"),  QStringLiteral("xfdf"),
        QStringLiteral("pdf"),       QStringLiteral("document"),
    };
    return kElements;
}

// HTML 实体解码：只认常见的那几个 + 数字实体。
//
// 为什么要自己解：正文里残留的 &amp; 直接排进 PDF 会很难看，
// 而把不认识的实体原样留着又会被别的工具再解一次。
QString decodeHtmlEntities(const QString& text) {
    if (!text.contains(QLatin1Char('&'))) {
        return text;
    }
    static const QHash<QString, QString> kNamed = {
        {QStringLiteral("amp"), QStringLiteral("&")},
        {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},
        {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},
        {QStringLiteral("nbsp"), QStringLiteral(" ")},
        {QStringLiteral("hellip"), QStringLiteral("…")},
        {QStringLiteral("mdash"), QStringLiteral("—")},
        {QStringLiteral("ndash"), QStringLiteral("–")},
        {QStringLiteral("laquo"), QStringLiteral("«")},
        {QStringLiteral("raquo"), QStringLiteral("»")},
        {QStringLiteral("copy"), QStringLiteral("©")},
        {QStringLiteral("reg"), QStringLiteral("®")},
        {QStringLiteral("trade"), QStringLiteral("™")},
        {QStringLiteral("middot"), QStringLiteral("·")},
    };
    QString out;
    out.reserve(text.size());
    int index = 0;
    while (index < text.size()) {
        const QChar ch = text.at(index);
        if (ch != QLatin1Char('&')) {
            out.append(ch);
            ++index;
            continue;
        }
        const int semicolon = text.indexOf(QLatin1Char(';'), index + 1);
        // 实体不会很长；找不到分号就当普通字符处理。
        if (semicolon < 0 || semicolon - index > 12) {
            out.append(ch);
            ++index;
            continue;
        }
        const QString entity = text.mid(index + 1, semicolon - index - 1);
        QString decoded;
        if (entity.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            uint code = 0;
            if (entity.size() > 2 && (entity.at(1) == QLatin1Char('x') || entity.at(1) == QLatin1Char('X'))) {
                code = entity.mid(2).toUInt(&ok, 16);
            } else {
                code = entity.mid(1).toUInt(&ok, 10);
            }
            if (ok && code > 0 && code <= 0x10FFFF) {
                const char32_t value = static_cast<char32_t>(code);
                decoded = QString::fromUcs4(&value, 1);
            }
        } else {
            decoded = kNamed.value(entity.toLower());
        }
        if (decoded.isEmpty()) {
            out.append(ch);
            ++index;
            continue;
        }
        out.append(decoded);
        index = semicolon + 1;
    }
    return out;
}

// 去掉标签名以外的部分：<a href="x" class="y"> → a
QString tagNameOf(const QString& tagText) {
    QString name;
    for (const QChar ch : tagText) {
        if (ch.isLetterOrNumber()) {
            name.append(ch);
        } else {
            break;
        }
    }
    return name.toLower();
}

// 从标签文本里取属性值：<a href="https://x"> → https://x
QString attributeValue(const QString& tagText, const QString& attribute) {
    const QRegularExpression pattern(
        QStringLiteral("(?i)\\b%1\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(attribute));
    const QRegularExpressionMatch match = pattern.match(tagText);
    if (!match.hasMatch()) {
        return QString();
    }
    for (int group = 2; group <= 4; ++group) {
        const QString captured = match.captured(group);
        if (!captured.isEmpty()) {
            return captured;
        }
    }
    return QString();
}

// 找某个元素的内部内容。
//
// 返回 false 表示**这个元素不存在**；返回 true 但内容为空表示「元素在，
// 但是空的」。这两种情况必须分清：<body></body> 是「有正文区但正文为空」，
// 如果当成「没有 body」就退回整份 HTML，标题里的字会被当正文排进 PDF。
bool findElement(const QString& html, const QString& tag, QString* content) {
    const QRegularExpression start(QStringLiteral("(?i)<%1(\\s[^>]*)?>").arg(tag));
    const QRegularExpressionMatch startMatch = start.match(html);
    if (!startMatch.hasMatch()) {
        return false;
    }
    const int contentStart = startMatch.capturedEnd();
    const QRegularExpression end(QStringLiteral("(?i)</%1\\s*>").arg(tag));
    const QRegularExpressionMatch endMatch = end.match(html, contentStart);
    if (!endMatch.hasMatch()) {
        return false;
    }
    if (content != nullptr) {
        *content = html.mid(contentStart, endMatch.capturedStart() - contentStart);
    }
    return true;
}

// 取某个元素的内部内容（<title>…</title>）。没有闭合标签时返回空串。
QString extractElement(const QString& html, const QString& tag) {
    QString content;
    if (!findElement(html, tag, &content)) {
        return QString();
    }
    return content;
}

// 整段丢掉某些元素（连内容一起丢）。
QString dropElements(const QString& html, const QStringList& tags) {
    QString working = html;
    for (const QString& tag : tags) {
        working.remove(QRegularExpression(QStringLiteral("(?is)<%1\\b[^>]*>.*?</%1\\s*>").arg(tag)));
        working.remove(QRegularExpression(QStringLiteral("(?is)<%1\\b[^>]*/?>").arg(tag)));
    }
    return working;
}

// 元素的纯文本（去标签 + 解实体）。
QString elementText(const QString& html, const QString& tag) {
    const QString inner = extractElement(html, tag);
    if (inner.isEmpty()) {
        return QString();
    }
    QString stripped = inner;
    stripped.remove(QRegularExpression(QStringLiteral("(?s)<[^>]*>")));
    return decodeHtmlEntities(stripped).trimmed();
}

// 收拾正文：折叠空白、去空行。
QString normalizeHtmlText(const QString& raw) {
    static const QRegularExpression kBlanks(QStringLiteral("[ \\t\\x{00A0}]+"));
    QStringList lines = raw.split(QLatin1Char('\n'));
    QStringList kept;
    bool lastWasBlank = true;  // 开头不留空行
    for (const QString& line : lines) {
        QString cleaned = line;
        cleaned.replace(kBlanks, QStringLiteral(" "));
        cleaned = cleaned.trimmed();
        if (cleaned.isEmpty()) {
            if (lastWasBlank) {
                continue;
            }
            lastWasBlank = true;
            kept.append(QString());
            continue;
        }
        lastWasBlank = false;
        kept.append(cleaned);
    }
    while (!kept.isEmpty() && kept.last().isEmpty()) {
        kept.removeLast();
    }
    return kept.join(QLatin1Char('\n'));
}

// 扫一遍标签：正文文本 + 链接。
struct HtmlScanResult {
    QString text;
    QStringList links;
};

HtmlScanResult scanHtmlBody(const QString& region, const QUrl& baseUrl) {
    HtmlScanResult result;
    QString text;
    int index = 0;
    const int size = region.size();
    while (index < size) {
        if (region.at(index) != QLatin1Char('<')) {
            const int next = region.indexOf(QLatin1Char('<'), index);
            const int end = next < 0 ? size : next;
            text.append(decodeHtmlEntities(region.mid(index, end - index)));
            index = end;
            continue;
        }
        // 注释：整段丢掉。
        if (region.mid(index, 4) == QLatin1String("<!--")) {
            const int end = region.indexOf(QLatin1String("-->"), index + 4);
            index = end < 0 ? size : end + 3;
            continue;
        }
        // 声明（<!DOCTYPE …>）：丢掉。
        if (region.mid(index, 2) == QLatin1String("<!")) {
            const int end = region.indexOf(QLatin1Char('>'), index);
            index = end < 0 ? size : end + 1;
            continue;
        }
        // 结束标签：块级元素要补一个换行。
        if (region.mid(index, 2) == QLatin1String("</")) {
            const int end = region.indexOf(QLatin1Char('>'), index);
            const QString name = tagNameOf(end < 0 ? region.mid(index + 2)
                                                  : region.mid(index + 2, end - index - 2));
            if (blockElements().contains(name)) {
                text.append(QLatin1Char('\n'));
            }
            index = end < 0 ? size : end + 1;
            continue;
        }
        // 普通标签。
        const int end = region.indexOf(QLatin1Char('>'), index);
        if (end < 0) {
            // 残缺标签：剩下的当普通文本，别把内容丢了。
            text.append(decodeHtmlEntities(region.mid(index)));
            index = size;
            continue;
        }
        const QString tagText = region.mid(index + 1, end - index - 1).trimmed();
        index = end + 1;
        const QString name = tagNameOf(tagText);
        if (name == QLatin1String("br") || name == QLatin1String("hr")) {
            text.append(QLatin1Char('\n'));
            continue;
        }
        if (name == QLatin1String("li")) {
            text.append(QStringLiteral("\n• "));
            continue;
        }
        if (name == QLatin1String("a")) {
            const QString href = attributeValue(tagText, QStringLiteral("href"));
            if (!href.isEmpty()) {
                QUrl resolved(href);
                if (baseUrl.isValid() && !baseUrl.isEmpty()) {
                    resolved = baseUrl.resolved(resolved);
                }
                const QString absolute = resolved.isValid() ? resolved.toString() : QString();
                if (!absolute.isEmpty() && !result.links.contains(absolute)) {
                    result.links.append(absolute);
                }
            }
            continue;
        }
        if (blockElements().contains(name)) {
            text.append(QLatin1Char('\n'));
        }
    }
    result.text = normalizeHtmlText(text);
    return result;
}

// ============================================================ XML 表单 ====

// XML 节点（只保留表单解析要用的信息）。
struct XmlFormNode {
    QString name;
    QString attributeName;
    QString text;
    QVector<XmlFormNode> children;
};

// 读一个元素：它的文本与子元素。
bool readXmlNode(QXmlStreamReader& reader, XmlFormNode* node) {
    node->name = reader.name().toString();
    node->attributeName = reader.attributes().value(QStringLiteral("name")).toString();
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            XmlFormNode child;
            if (!readXmlNode(reader, &child)) {
                return false;
            }
            node->children.append(child);
            continue;
        }
        if (token == QXmlStreamReader::EndElement) {
            return true;
        }
        if (token == QXmlStreamReader::Characters && !reader.isWhitespace()) {
            node->text.append(reader.text().toString());
        }
    }
    return false;
}

// 把 XML 树里的字段收成「名字 → 值」。
//
// 认三种写法：
//   * <field name="名字"><value>值</value></field>
//   * <field name="名字">值</field>
//   * <名字><value>值</value></名字>（元素名当字段名）
//
// 名字取不到的条目直接跳过：猜不出它是谁，填进去只会填错地方。
void collectXmlFormFields(const XmlFormNode& node, QVector<FormFieldValue>* values) {
    for (const XmlFormNode& child : node.children) {
        if (child.name == QLatin1String("field")) {
            if (child.attributeName.isEmpty()) {
                continue;
            }
            QString value = child.text.trimmed();
            if (value.isEmpty()) {
                for (const XmlFormNode& sub : child.children) {
                    if (sub.name == QLatin1String("value")) {
                        value = sub.text.trimmed();
                        break;
                    }
                }
            }
            values->append(FormFieldValue{child.attributeName, value});
            continue;
        }
        if (xmlContainerElements().contains(child.name)) {
            collectXmlFormFields(child, values);
            continue;
        }
        if (!child.attributeName.isEmpty()) {
            values->append(FormFieldValue{child.attributeName, child.text.trimmed()});
            continue;
        }
        bool hasValueChild = false;
        QString value;
        for (const XmlFormNode& sub : child.children) {
            if (sub.name == QLatin1String("value")) {
                value = sub.text.trimmed();
                hasValueChild = true;
                break;
            }
        }
        if (hasValueChild) {
            values->append(FormFieldValue{child.name, value});
            continue;
        }
        collectXmlFormFields(child, values);
    }
}

// 解析 XML 表单数据。解析失败时 error 写明原因。
QVector<FormFieldValue> parseXmlFormData(const QString& xml, QString* error) {
    QVector<FormFieldValue> values;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        XmlFormNode root;
        if (!readXmlNode(reader, &root)) {
            if (error != nullptr) {
                *error = QStringLiteral("这个 XML 文件解析失败：%1").arg(reader.errorString());
            }
            return {};
        }
        collectXmlFormFields(root, &values);
        break;
    }
    if (reader.hasError()) {
        if (error != nullptr) {
            *error = QStringLiteral("这个 XML 文件解析失败：%1").arg(reader.errorString());
        }
        return {};
    }
    return values;
}

// 扩展名认不出来时，按内容猜格式。
FormDataFormat guessFormDataFormat(const QByteArray& bytes) {
    const QString text = QString::fromUtf8(bytes);
    if (text.contains(QLatin1String("%FDF-"))) {
        return FormDataFormat::Fdf;
    }
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QLatin1Char('<'))) {
        return FormDataFormat::Unknown;
    }
    if (trimmed.contains(QLatin1String("<xfdf")) || trimmed.contains(QLatin1String("<fields"))) {
        return FormDataFormat::Xfdf;
    }
    return FormDataFormat::Xml;
}

}  // namespace

// ============================================================ 剪贴板 =====

ConversionResult clipboardTextToPdf(const QString& text, const QString& title,
                                    const UniversalConverterOptions& options) {
    ConversionResult result;
    if (text.trimmed().isEmpty()) {
        result.error = QStringLiteral("剪贴板里没有文本，没法生成 PDF。");
        return result;
    }
    return textToPdf(buildPlainText(title, text), title, options);
}

// ============================================================ 网页 =====

HtmlBody extractHtmlBody(const QString& html, const QUrl& baseUrl) {
    HtmlBody body;
    if (html.trimmed().isEmpty()) {
        return body;
    }

    body.title = elementText(html, QStringLiteral("title"));

    // 正文区域：<article> > <main> > <body> > 整份。
    //
    // 用 findElement 而不是「拿到空串就当没有」：<body></body> 是正文为空，
    // 不是「没有 body」——当成没有就会退回整份 HTML，把 <title> 当正文排进去。
    QString region;
    if (!findElement(html, QStringLiteral("article"), &region)) {
        if (!findElement(html, QStringLiteral("main"), &region)) {
            if (!findElement(html, QStringLiteral("body"), &region)) {
                region = html;
            }
        }
    }

    // 脚本、样式、导航块整段丢掉。
    region = dropElements(region, droppedElements());

    const HtmlScanResult scanned = scanHtmlBody(region, baseUrl);
    body.text = scanned.text;
    body.links = scanned.links;

    // 没有 <title> 就拿正文里的第一个 <h1> 当标题。
    if (body.title.isEmpty()) {
        body.title = elementText(region, QStringLiteral("h1"));
    }
    return body;
}

ConversionResult htmlToPdf(const QString& html, const QUrl& baseUrl,
                           const UniversalConverterOptions& options) {
    const HtmlBody body = extractHtmlBody(html, baseUrl);
    if (body.text.trimmed().isEmpty()) {
        ConversionResult result;
        result.error = QStringLiteral("这个网页里没有正文（脚本、样式和导航都去掉之后就没东西了）。");
        return result;
    }

    // 正文里的链接一并排进去：PDF 是纯文本的，链接不写出来就丢了。
    QString text = buildPlainText(body.title, body.text);
    if (!body.links.isEmpty()) {
        text += QStringLiteral("\n\n") + QStringLiteral("页面里的链接：");
        int shown = 0;
        for (const QString& link : body.links) {
            if (shown >= 50) {
                break;
            }
            text += QLatin1Char('\n') + link;
            ++shown;
        }
        if (body.links.size() > shown) {
            text += QStringLiteral("\n…还有 %1 个链接没列出来。").arg(body.links.size() - shown);
        }
    }
    return textToPdf(text, body.title, options);
}

// ============================================================ 表单文件 ====

FormDataFormat formDataFormatForFileName(const QString& fileName) {
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("fdf")) {
        return FormDataFormat::Fdf;
    }
    if (suffix == QLatin1String("xfdf")) {
        return FormDataFormat::Xfdf;
    }
    if (suffix == QLatin1String("xml")) {
        return FormDataFormat::Xml;
    }
    return FormDataFormat::Unknown;
}

FormDataParseResult parseFormDataFile(const QByteArray& bytes, const QString& fileName) {
    FormDataParseResult result;
    if (bytes.isEmpty()) {
        result.error = QStringLiteral("这个表单数据文件是空的，没有内容可填。");
        return result;
    }

    FormDataFormat format = formDataFormatForFileName(fileName);
    if (format == FormDataFormat::Unknown) {
        format = guessFormDataFormat(bytes);
    }
    result.format = format;

    switch (format) {
    case FormDataFormat::Fdf: {
        // FDF 的解析复用 pdf/FormData：它是仓库里既有的实现，别写第二套。
        const QVector<pdf::FormValue> values = pdf::formValuesFromFdf(bytes);
        for (const pdf::FormValue& value : values) {
            result.values.append(FormFieldValue{value.name, value.value});
        }
        if (result.values.isEmpty()) {
            result.error = QStringLiteral("这个 FDF 文件里没有读到字段（FDF 的字段写法是 /T (字段名) /V (值)）。");
        }
        break;
    }
    case FormDataFormat::Xfdf: {
        // XFDF 的解析复用 pdf/Xfdf。
        const QVector<pdf::FormFieldData> values = pdf::formFieldsFromXfdf(QString::fromUtf8(bytes));
        for (const pdf::FormFieldData& value : values) {
            result.values.append(FormFieldValue{value.name, value.value});
        }
        if (result.values.isEmpty()) {
            result.error = QStringLiteral(
                "这个 XFDF 文件里没有读到字段（XFDF 的写法是 "
                "<fields><field name=\"字段名\"><value>值</value></field></fields>）。");
        }
        break;
    }
    case FormDataFormat::Xml: {
        QString error;
        result.values = parseXmlFormData(QString::fromUtf8(bytes), &error);
        if (result.values.isEmpty()) {
            result.error = error.isEmpty()
                ? QStringLiteral("这个 XML 文件里没有读到字段"
                                 "（写法是 <form-data><field name=\"字段名\">值</field></form-data>）。")
                : error;
        }
        break;
    }
    case FormDataFormat::Unknown:
        result.error = QStringLiteral("认不出这个表单数据文件的格式（支持 .fdf / .xfdf / .xml）。");
        break;
    }

    result.ok = !result.values.isEmpty() && result.error.isEmpty();
    return result;
}

FormFillReport applyFormDataToTemplate(const QByteArray& templatePdf,
                                       const QVector<FormFieldValue>& values) {
    FormFillReport report;
    if (templatePdf.isEmpty()) {
        report.error = QStringLiteral("没有模板 PDF 就没法填表：请先选一份带表单域的模板 PDF。");
        return report;
    }
    if (values.isEmpty()) {
        report.error = QStringLiteral("表单数据文件里没有读到任何字段值，没有东西可填。");
        return report;
    }

    // 这里用 PDFium 是安全的：走的是 pdf/PdfiumAdapter，进程级的 PDFium 锁
    // 就在那一层里（doc/FontFlattening 也是这么用的）。
    //
    // opened 不加 const：填字段要调非 const 的 setFormFieldValue。
    pdf::PdfOpenResult opened =
        pdf::openDocumentFromBytes(templatePdf, QStringLiteral("template.pdf"));
    if (!opened.document.isValid()) {
        report.error = QStringLiteral("模板 PDF 打不开：%1").arg(opened.errorMessage);
        return report;
    }

    const QVector<pdf::FormField> fields = opened.document.formFields();
    report.templateFieldCount = fields.size();
    if (fields.isEmpty()) {
        report.error = QStringLiteral("模板 PDF 里没有表单域，填不进去（模板要是一份带可填写域的表单）。");
        return report;
    }

    // 名字 → 模板里的字段下标。同名字段可能在多个页上（跨页的一组单选框）。
    QHash<QString, QVector<int>> byName;
    for (int index = 0; index < fields.size(); ++index) {
        byName[fields.at(index).name].append(index);
    }

    for (const FormFieldValue& value : values) {
        const auto it = byName.constFind(value.name);
        if (it == byName.constEnd()) {
            report.issues.append(
                FormFieldIssue{value.name, QStringLiteral("模板里没有同名字段")});
            continue;
        }
        bool applied = false;
        QStringList reasons;
        for (int index : it.value()) {
            const pdf::FormField& field = fields.at(index);
            // 只读域就是锁住的：数据文件里有也不填。
            // 填进去会让用户以为这个值还能改，而它在表单里根本改不了。
            if (field.readOnly) {
                reasons.append(QStringLiteral("第 %1 页的这个字段是只读的，填不进去")
                                   .arg(field.pageIndex + 1));
                continue;
            }
            if (opened.document.setFormFieldValue(field.pageIndex, field.name, value.value)) {
                applied = true;
                continue;
            }
            reasons.append(QStringLiteral("第 %1 页的同名字段写不进去").arg(field.pageIndex + 1));
        }
        if (applied) {
            report.applied.append(value.name);
            continue;
        }
        report.issues.append(FormFieldIssue{
            value.name,
            reasons.isEmpty() ? QStringLiteral("写不进这个字段") : reasons.join(QStringLiteral("；"))});
    }

    // 模板里有、但表单数据没覆盖的字段：保持原样，但要说明。
    for (const pdf::FormField& field : fields) {
        if (!report.applied.contains(field.name) && !report.untouched.contains(field.name)) {
            bool mentioned = false;
            for (const FormFieldIssue& issue : report.issues) {
                if (issue.name == field.name) {
                    mentioned = true;
                    break;
                }
            }
            if (!mentioned) {
                report.untouched.append(field.name);
            }
        }
    }

    QString serializeError;
    report.bytes = opened.document.serialize(&serializeError);
    if (report.bytes.isEmpty()) {
        report.error = serializeError.isEmpty() ? QStringLiteral("填好的 PDF 存不下来。") : serializeError;
        report.bytes.clear();
        return report;
    }
    report.ok = true;
    report.pageCount = opened.document.pageCount();
    return report;
}

FormFillReport applyFormDataFileToTemplate(const QByteArray& templatePdf,
                                           const QByteArray& dataBytes,
                                           const QString& fileName) {
    const FormDataParseResult parsed = parseFormDataFile(dataBytes, fileName);
    if (!parsed.ok) {
        FormFillReport report;
        report.error = parsed.error;
        return report;
    }
    return applyFormDataToTemplate(templatePdf, parsed.values);
}

}  // namespace aipdf::doc
