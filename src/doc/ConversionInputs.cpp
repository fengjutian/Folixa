#include "doc/ConversionInputs.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

#include "doc/BlankDocument.h"
#include "pdf/FormData.h"
#include "pdf/ImageToPdf.h"
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

// 单次转换最多带多少张图。
//
// 为什么设上限：一个页面塞几百张图时，下载与排版都会拖很久，
// 而且最后拿到的 PDF 大得没意义。超出的图不下载，但会逐条记进结果里。
constexpr int kMaxWebImages = 50;

// 正文里的图片位置标记。
//
// 为什么用私用区字符（U+E000）：真实正文里不会出现它，
// 它也不会被 HTML 实体解码或空白折叠吃掉，排版前摘掉即可。
const QChar kImageMarker(0xE000);

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

// 扫一遍标签：正文文本 + 链接 + 图片。
struct HtmlScanResult {
    QString text;
    QStringList links;
    // 正文里的图片：绝对地址，去重后按出现顺序。
    QStringList imageUrls;
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
        if (name == QLatin1String("img")) {
            // 只取 src：srcset 是一组候选（按屏幕宽度挑），
            // 本程序不渲染 CSS，也就无从判断该挑哪一张 —— 边界写在这里。
            const QString src = attributeValue(tagText, QStringLiteral("src"));
            if (!src.isEmpty()) {
                QUrl resolved(src);
                if (baseUrl.isValid() && !baseUrl.isEmpty()) {
                    resolved = baseUrl.resolved(resolved);
                }
                const QString absolute = resolved.isValid() ? resolved.toString() : QString();
                // 同一个地址只留第一次：重复的图不必下载两遍。
                if (!absolute.isEmpty() && !result.imageUrls.contains(absolute)) {
                    result.imageUrls.append(absolute);
                    // 位置标记：这张图在正文里的落点，排版前会摘掉。
                    text.append(kImageMarker);
                }
            }
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

// 摘掉正文里的图片位置标记，并算出每张图在正文里的偏移。
//
// 为什么要有偏移：图片要插在**它出现的位置附近**，光有地址不够，
// 还得知道它在正文文字的哪儿。
QString extractImageMarkers(const QString& text, QVector<int>* offsets) {
    QString clean;
    clean.reserve(text.size());
    int removed = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch != kImageMarker) {
            clean.append(ch);
            continue;
        }
        if (offsets != nullptr) {
            offsets->append(index - removed);
        }
        ++removed;
        // 摘掉标记后别留下两个空格。
        if (!clean.isEmpty() && clean.endsWith(QLatin1Char(' ')) && index + 1 < text.size() &&
            text.at(index + 1) == QLatin1Char(' ')) {
            clean.chop(1);
            ++removed;
        }
    }
    return clean;
}

// ============================================================ 图片进 PDF ====

// 一页图片的素材：图像对象 + 把它画到页面上的内容流。
struct ImagePageData {
    // 图像 XObject 的字典（自带 /Length）。
    QByteArray imageDict;
    // 图像 XObject 的数据。
    QByteArray imageStream;
    // 页面内容流。
    QByteArray contentStream;
};

// PDF 里的数字：整数就写整数，带小数保留 4 位。
//
// 与 pdf/ImageToPdf 里那份同名函数不是同一份实现，但格式必须一致 ——
// 两处写出来的数字长得不一样的话，同一份 PDF 里会出现两种精度。
QByteArray formatPoints(double value) {
    const double rounded = qRound64(value * 10000.0) / 10000.0;
    const qlonglong asInteger = qRound64(rounded);
    if (qAbs(rounded - static_cast<double>(asInteger)) < 1e-9) {
        return QByteArray::number(asInteger);
    }
    return QByteArray::number(rounded, 'f', 4);
}

// qCompress 的输出是「4 字节未压缩长度 + zlib 数据」，
// 而 PDF 的 /FlateDecode 只认后半段 zlib 数据；把 Qt 私有长度头带进去会让严格阅读器拒收。
//
// 为什么与 pdf/ImageToPdf 里那份各写各的：那份是那个文件的匿名命名空间里的
// 静态函数，没有对外暴露；为这四行去改公共接口不值得（各写一份的坏处是
// 格式可能漂移，所以这里的注释与那边保持一致）。
QByteArray flateData(const QByteArray& raw) {
    QByteArray compressed = qCompress(raw, 6);
    if (compressed.size() >= 4) {
        compressed.remove(0, 4);
    }
    return compressed;
}

// 把一张解码好的图变成一页的素材。
//
// 版面按**像素尺寸**算（跟 pdf::imageToPdf 同一路：像素 × 72 ÷ DPI）：
// 网页里的图按 CSS 的 96 DPI 折成点，一页放不下就等比缩小。
// 页面尺寸沿用正文那一套：同一份文档里页面大小应当一致。
bool buildImagePage(const pdf::DecodedImage& decoded, const QSizeF& pageSizePoints,
                    double marginPt, ImagePageData* page, QString* detail) {
    const double kCssDpi = 96.0;
    const double widthPoints = decoded.width * 72.0 / kCssDpi;
    const double heightPoints = decoded.height * 72.0 / kCssDpi;
    if (!(widthPoints > 0.0) || !(heightPoints > 0.0)) {
        *detail = QStringLiteral("image has empty size");
        return false;
    }
    const double usableWidth = pageSizePoints.width() - 2.0 * marginPt;
    const double usableHeight = pageSizePoints.height() - 2.0 * marginPt;
    double scale = 1.0;
    if (usableWidth > 0.0 && widthPoints > usableWidth) {
        scale = usableWidth / widthPoints;
    }
    if (usableHeight > 0.0 && heightPoints * scale > usableHeight) {
        scale = usableHeight / heightPoints;
    }
    if (!(scale > 0.0)) {
        *detail = QStringLiteral("image does not fit the page");
        return false;
    }
    const double drawWidth = widthPoints * scale;
    const double drawHeight = heightPoints * scale;
    const double x = (pageSizePoints.width() - drawWidth) / 2.0;
    const double y = pageSizePoints.height() - marginPt - drawHeight;

    QByteArray dict = "<< /Type /XObject /Subtype /Image /Width " +
                      QByteArray::number(decoded.width) + " /Height " +
                      QByteArray::number(decoded.height) + " /ColorSpace ";
    QByteArray data;
    if (decoded.isJpeg()) {
        // JPEG 原样塞进去：不重编码，一个字节都不动。
        if (decoded.jpegComponents == 1) {
            dict += "/DeviceGray";
        } else if (decoded.jpegComponents == 4) {
            dict += "/DeviceCMYK";
        } else {
            dict += "/DeviceRGB";
        }
        dict += " /BitsPerComponent 8 /Filter /DCTDecode";
        if (decoded.jpegComponents == 4) {
            // 4 分量 JPEG（Adobe 约定，值是反的）：要配 /Decode 反相。
            dict += " /Decode [1 0 1 0 1 0 1 0]";
        }
        data = decoded.jpeg;
    } else {
        dict += "/DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode";
        data = flateData(decoded.rgb);
    }
    dict += " /Length " + QByteArray::number(data.size()) + " >>";
    page->imageDict = dict;
    page->imageStream = data;
    // 内容流：把图铺到算好的位置上（PDF 原点在左下、y 向上）。
    page->contentStream = "q\n" + formatPoints(drawWidth) + " 0 0 " + formatPoints(drawHeight) + " " +
                          formatPoints(x) + " " + formatPoints(y) + " cm\n/Im0 Do\nQ\n";
    return true;
}

// 取一张图并排成页面素材。
//
// 取不到 / 解不开都**不中断**整篇转换：原因写进 issue，调用方照常往下排。
bool fetchImagePage(const QUrl& url, const ImageFetcher& fetcher, const QSizeF& pageSizePoints,
                    double marginPt, ImagePageData* page, HtmlImageIssue* issue) {
    issue->url = url.toString();
    const QByteArray bytes = fetcher(url);
    if (bytes.isEmpty()) {
        issue->kind = HtmlImageIssueKind::DownloadFailed;
        issue->detail = QStringLiteral("no bytes received");
        return false;
    }
    pdf::DecodedImage decoded;
    QString detail;
    if (!pdf::decodeImage(bytes, &decoded, &detail)) {
        // 分清「格式不支持」与「数据坏了」：这两种要分开报，
        // 前者多半是网页用了本程序读不了的格式，后者是下载到的东西坏了。
        issue->kind = pdf::detectImageFormat(bytes) == pdf::ImageFormat::Unknown
                          ? HtmlImageIssueKind::UnsupportedFormat
                          : HtmlImageIssueKind::DecodeFailed;
        issue->detail = detail;
        return false;
    }
    if (!buildImagePage(decoded, pageSizePoints, marginPt, page, &detail)) {
        issue->kind = HtmlImageIssueKind::TooLarge;
        issue->detail = detail;
        return false;
    }
    return true;
}

// ------------------------------------------------------ 解析一份源 PDF ----

// 一个 PDF 对象：字典 + 可选的流数据。
struct PdfObject {
    QByteArray dict;
    QByteArray stream;  // 空 = 不是流对象
};

// 解析出来的一份源 PDF。
//
// 只认本程序自己写出来的那种结构：经典交叉引用表 + 直接对象。
// 遇到对象流、交叉引用流、加密文件直接失败 —— 这份拼装只处理自己排出来的字节。
struct ParsedPdf {
    // 按对象号索引（0 号空着）。
    QVector<PdfObject> objects;
    // Catalog 与页树的对象号。
    int catalogNumber = 0;
    int pagesNumber = 0;
    // 页对象号（文档顺序）。
    QVector<int> pageNumbers;
    bool ok = false;
    QString error;
};

// 跳过空白。
void skipSpaces(const QByteArray& data, qsizetype* pos) {
    while (*pos < data.size()) {
        const char ch = data.at(*pos);
        if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t') {
            break;
        }
        ++(*pos);
    }
}

// 读一个非负整数。
bool readNumber(const QByteArray& data, qsizetype* pos, int* value) {
    skipSpaces(data, pos);
    if (*pos >= data.size() || data.at(*pos) < '0' || data.at(*pos) > '9') {
        return false;
    }
    int result = 0;
    while (*pos < data.size() && data.at(*pos) >= '0' && data.at(*pos) <= '9') {
        result = result * 10 + (data.at(*pos) - '0');
        ++(*pos);
    }
    *value = result;
    return true;
}

// 读交叉引用表：对象号 → 字节偏移。
bool parseXrefTable(const QByteArray& pdf, qsizetype offset, QHash<int, qint64>* offsets) {
    qsizetype pos = offset;
    if (!pdf.mid(pos, 4).startsWith("xref")) {
        return false;
    }
    pos += 4;
    while (true) {
        int start = 0;
        int count = 0;
        if (!readNumber(pdf, &pos, &start) || !readNumber(pdf, &pos, &count)) {
            break;  // 到 trailer 了
        }
        if (count <= 0 || count > 1000000) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            int objectOffset = 0;
            int generation = 0;
            if (!readNumber(pdf, &pos, &objectOffset) || !readNumber(pdf, &pos, &generation)) {
                return false;
            }
            skipSpaces(pdf, &pos);
            if (pos >= pdf.size()) {
                return false;
            }
            const char kind = pdf.at(pos);
            ++pos;
            if (kind == 'n') {
                offsets->insert(start + index, objectOffset);
            }
        }
    }
    return true;
}

// 读一个对象（按交叉引用表给的偏移）。
bool parseObjectAt(const QByteArray& pdf, qint64 offset, int expectedNumber, PdfObject* out) {
    if (offset <= 0 || offset >= pdf.size()) {
        return false;
    }
    qsizetype pos = static_cast<qsizetype>(offset);
    int number = 0;
    int generation = 0;
    if (!readNumber(pdf, &pos, &number) || !readNumber(pdf, &pos, &generation)) {
        return false;
    }
    if (number != expectedNumber) {
        return false;
    }
    skipSpaces(pdf, &pos);
    if (!pdf.mid(pos, 3).startsWith("obj")) {
        return false;
    }
    pos += 3;
    skipSpaces(pdf, &pos);
    if (!pdf.mid(pos, 2).startsWith("<<")) {
        return false;
    }
    // 找字典的结尾（按 << >> 配对）。
    int depth = 0;
    qsizetype dictEnd = -1;
    for (qsizetype index = pos; index + 1 < pdf.size(); ++index) {
        if (pdf.at(index) == '<' && pdf.at(index + 1) == '<') {
            ++depth;
            ++index;
            continue;
        }
        if (pdf.at(index) == '>' && pdf.at(index + 1) == '>') {
            --depth;
            ++index;
            if (depth == 0) {
                dictEnd = index + 1;
                break;
            }
            continue;
        }
    }
    if (dictEnd < 0) {
        return false;
    }
    out->dict = pdf.mid(pos, dictEnd - pos);
    out->stream.clear();
    // 流对象：字典后面跟着 stream。
    qsizetype afterDict = dictEnd;
    skipSpaces(pdf, &afterDict);
    if (!pdf.mid(afterDict, 6).startsWith("stream")) {
        return true;
    }
    afterDict += 6;
    // stream 后面是行尾，数据从行尾之后开始。
    if (afterDict < pdf.size() && pdf.at(afterDict) == '\r') {
        ++afterDict;
    }
    if (afterDict < pdf.size() && pdf.at(afterDict) == '\n') {
        ++afterDict;
    }
    // 长度优先用字典里的 /Length（直接整数）。
    int length = -1;
    static const QRegularExpression lengthPattern(QStringLiteral("/Length\\s+(\\d+)"));
    const QRegularExpressionMatch lengthMatch = lengthPattern.match(out->dict);
    if (lengthMatch.hasMatch()) {
        length = lengthMatch.captured(1).toInt();
    }
    if (length >= 0 && afterDict + length <= pdf.size()) {
        out->stream = pdf.mid(afterDict, length);
        return true;
    }
    // 没有 /Length（或不可信）：一直找到 endstream。
    const qsizetype endAt = pdf.indexOf("endstream", afterDict);
    if (endAt < 0) {
        return false;
    }
    qsizetype dataEnd = endAt;
    if (dataEnd > afterDict && pdf.at(dataEnd - 1) == '\n') {
        --dataEnd;
    }
    if (dataEnd > afterDict && pdf.at(dataEnd - 1) == '\r') {
        --dataEnd;
    }
    out->stream = pdf.mid(afterDict, dataEnd - afterDict);
    return true;
}

// 在字典里找 "/Key N 0 R" 里的 N。
bool findIndirectNumber(const QByteArray& dict, const QByteArray& key, int* number) {
    const qsizetype keyAt = dict.indexOf(key);
    if (keyAt < 0) {
        return false;
    }
    qsizetype pos = keyAt + key.size();
    int value = 0;
    int generation = 0;
    if (!readNumber(dict, &pos, &value) || !readNumber(dict, &pos, &generation)) {
        return false;
    }
    *number = value;
    return true;
}

// 解析一份源 PDF（只认本程序自己写出来的那种结构）。
ParsedPdf parseSourcePdf(const QByteArray& bytes) {
    ParsedPdf result;
    const qsizetype startxrefAt = bytes.lastIndexOf("startxref");
    if (startxrefAt < 0) {
        result.error = QStringLiteral("startxref not found");
        return result;
    }
    qsizetype pos = startxrefAt + 9;
    int xrefOffset = 0;
    if (!readNumber(bytes, &pos, &xrefOffset) || xrefOffset <= 0) {
        result.error = QStringLiteral("bad startxref value");
        return result;
    }
    QHash<int, qint64> offsets;
    if (!parseXrefTable(bytes, xrefOffset, &offsets) || offsets.isEmpty()) {
        result.error = QStringLiteral("unsupported cross reference table");
        return result;
    }
    const qsizetype trailerAt = bytes.lastIndexOf("trailer");
    if (trailerAt < 0) {
        result.error = QStringLiteral("trailer not found");
        return result;
    }
    int rootNumber = 0;
    if (!findIndirectNumber(bytes.mid(trailerAt), QByteArrayLiteral("/Root"), &rootNumber)) {
        result.error = QStringLiteral("trailer has no /Root");
        return result;
    }

    int maxNumber = 0;
    for (auto it = offsets.constBegin(); it != offsets.constEnd(); ++it) {
        maxNumber = qMax(maxNumber, it.key());
    }
    result.objects.resize(maxNumber + 1);
    for (auto it = offsets.constBegin(); it != offsets.constEnd(); ++it) {
        PdfObject object;
        if (!parseObjectAt(bytes, it.value(), it.key(), &object)) {
            result.error = QStringLiteral("failed to parse object %1").arg(it.key());
            return result;
        }
        result.objects[it.key()] = object;
    }
    result.catalogNumber = rootNumber;
    if (rootNumber <= 0 || rootNumber >= result.objects.size()) {
        result.error = QStringLiteral("catalog object is missing");
        return result;
    }
    int pagesNumber = 0;
    if (!findIndirectNumber(result.objects.at(rootNumber).dict, QByteArrayLiteral("/Pages"),
                            &pagesNumber)) {
        result.error = QStringLiteral("catalog has no /Pages");
        return result;
    }
    result.pagesNumber = pagesNumber;
    if (pagesNumber <= 0 || pagesNumber >= result.objects.size()) {
        result.error = QStringLiteral("page tree object is missing");
        return result;
    }
    const QByteArray pagesDict = result.objects.at(pagesNumber).dict;
    const qsizetype kidsAt = pagesDict.indexOf("/Kids");
    const qsizetype openAt = kidsAt < 0 ? -1 : pagesDict.indexOf('[', kidsAt);
    const qsizetype closeAt = openAt < 0 ? -1 : pagesDict.indexOf(']', openAt);
    if (kidsAt < 0 || openAt < 0 || closeAt < 0) {
        result.error = QStringLiteral("page tree has no /Kids");
        return result;
    }
    const QByteArray kids = pagesDict.mid(openAt + 1, closeAt - openAt - 1);
    qsizetype kidPos = 0;
    while (true) {
        skipSpaces(kids, &kidPos);
        if (kidPos >= kids.size()) {
            break;
        }
        int number = 0;
        int generation = 0;
        if (!readNumber(kids, &kidPos, &number) || !readNumber(kids, &kidPos, &generation)) {
            break;
        }
        skipSpaces(kids, &kidPos);
        if (kidPos < kids.size() && kids.at(kidPos) == 'R') {
            ++kidPos;
        }
        result.pageNumbers.append(number);
    }
    if (result.pageNumbers.isEmpty()) {
        result.error = QStringLiteral("page tree is empty");
        return result;
    }
    result.ok = true;
    return result;
}

// 字典里有没有间接引用（形如 "12 0 R"）。
bool hasIndirectReference(const QByteArray& dict) {
    qsizetype index = 0;
    while (index < dict.size()) {
        const char ch = dict.at(index);
        const bool startsNumber =
            ch >= '0' && ch <= '9' && (index == 0 || dict.at(index - 1) < '0' || dict.at(index - 1) > '9');
        if (!startsNumber) {
            ++index;
            continue;
        }
        qsizetype digitsEnd = index;
        while (digitsEnd < dict.size() && dict.at(digitsEnd) >= '0' && dict.at(digitsEnd) <= '9') {
            ++digitsEnd;
        }
        qsizetype probe = digitsEnd;
        int generation = 0;
        if (readNumber(dict, &probe, &generation) && generation == 0) {
            skipSpaces(dict, &probe);
            if (probe < dict.size() && dict.at(probe) == 'R') {
                return true;
            }
        }
        index = digitsEnd;
    }
    return false;
}

// 把字典里的间接引用换成新的对象号。
QByteArray remapReferences(const QByteArray& dict, const QHash<int, int>& mapping) {
    if (mapping.isEmpty()) {
        return dict;
    }
    QByteArray out;
    out.reserve(dict.size());
    qsizetype index = 0;
    while (index < dict.size()) {
        const char ch = dict.at(index);
        const bool startsNumber =
            ch >= '0' && ch <= '9' && (index == 0 || dict.at(index - 1) < '0' || dict.at(index - 1) > '9');
        if (!startsNumber) {
            out.append(ch);
            ++index;
            continue;
        }
        qsizetype digitsEnd = index;
        while (digitsEnd < dict.size() && dict.at(digitsEnd) >= '0' && dict.at(digitsEnd) <= '9') {
            ++digitsEnd;
        }
        const int number = dict.mid(index, digitsEnd - index).toInt();
        qsizetype probe = digitsEnd;
        int generation = 0;
        if (readNumber(dict, &probe, &generation) && generation == 0) {
            skipSpaces(dict, &probe);
            if (probe < dict.size() && dict.at(probe) == 'R' &&
                (probe + 1 >= dict.size() || dict.at(probe + 1) < '0' || dict.at(probe + 1) > '9')) {
                const auto it = mapping.constFind(number);
                if (it != mapping.constEnd()) {
                    out.append(QByteArray::number(it.value()));
                    out.append(" 0 R");
                    index = probe + 1;
                    continue;
                }
            }
        }
        out.append(ch);
        ++index;
    }
    return out;
}

// 对象的完整字节（字典 + 流）。
QByteArray serializeObject(const PdfObject& object) {
    if (object.stream.isEmpty()) {
        return object.dict;
    }
    return object.dict + "\nstream\n" + object.stream + "\nendstream";
}

// 合成时的一段：要么是一段正文（各自的源 PDF），要么是一张图片页。
struct DocumentPiece {
    bool isImage = false;
    // 正文段：源 PDF 的字节。
    QByteArray sourcePdf;
    // 图片页：图像对象与内容流。
    ImagePageData image;
};

// 合成一份 PDF：若干段正文（各自的源 PDF）与若干张图片页，按给定顺序拼起来。
//
// 为什么自己拼而不借 qpdf：qpdf 在本仓库是**可选依赖**（默认不编进来），
// 「网页转 PDF 带图片」不能因为它没编就做不成。
//
// 为什么用「复制对象 + 改引用」而不是「增量更新」：增量更新要重写交叉引用表、
// 还要接 /Prev 链；这里把各段的字节收进一份新文件，头只有一个，读起来也简单。
bool composeDocument(const QVector<DocumentPiece>& pieces, const QByteArray& mediaBox,
                     QByteArray* out, int* pageCount, QString* error) {
    // 1) 解析每一段正文。
    QVector<ParsedPdf> sources;
    QVector<int> pieceSource;
    for (const DocumentPiece& piece : pieces) {
        if (piece.isImage) {
            pieceSource.append(-1);
            continue;
        }
        ParsedPdf parsed = parseSourcePdf(piece.sourcePdf);
        if (!parsed.ok) {
            *error = parsed.error;
            return false;
        }
        pieceSource.append(sources.size());
        sources.append(parsed);
    }

    // 2) 分配对象号：1 = Catalog，2 = 页树，其余从 3 开始。
    const int catalogNumber = 1;
    const int pagesNumber = 2;
    int nextNumber = 3;
    QVector<QHash<int, int>> mappings(sources.size());  // 旧对象号 → 新对象号
    QVector<QHash<int, int>> newToOld(sources.size());  // 新对象号 → 旧对象号
    QHash<QByteArray, int> reused;                      // 对象体 → 已占用的对象号
    for (int index = 0; index < sources.size(); ++index) {
        const ParsedPdf& source = sources.at(index);
        mappings[index].insert(source.catalogNumber, catalogNumber);
        mappings[index].insert(source.pagesNumber, pagesNumber);
        for (int number = 1; number < source.objects.size(); ++number) {
            if (number == source.catalogNumber || number == source.pagesNumber) {
                continue;
            }
            const PdfObject& object = source.objects.at(number);
            // 只有**不含间接引用**的对象才允许复用一份：字典里带引用的对象换号之后
            // 就不再相同，而字体文件这类对象没有引用，重复一份只是白占体积
            // （正文被切成几段时，同一份字体会被写进好几个段）。
            const bool shareable = !hasIndirectReference(object.dict);
            const QByteArray body = serializeObject(object);
            if (shareable) {
                const auto it = reused.constFind(body);
                if (it != reused.constEnd()) {
                    mappings[index].insert(number, it.value());
                    continue;
                }
            }
            const int assigned = nextNumber++;
            mappings[index].insert(number, assigned);
            newToOld[index].insert(assigned, number);
            if (shareable) {
                reused.insert(body, assigned);
            }
        }
    }

    // 3) 写对象体（引用换成新号）。
    QHash<int, QByteArray> bodies;
    for (int index = 0; index < sources.size(); ++index) {
        const ParsedPdf& source = sources.at(index);
        const QHash<int, int>& mapping = mappings.at(index);
        for (auto it = newToOld.at(index).constBegin(); it != newToOld.at(index).constEnd(); ++it) {
            const PdfObject& object = source.objects.at(it.value());
            QByteArray body = remapReferences(object.dict, mapping);
            if (!object.stream.isEmpty()) {
                body += "\nstream\n" + object.stream + "\nendstream";
            }
            bodies.insert(it.key(), body);
        }
    }

    // 4) 页顺序：正文段的页 + 图片页，按给的顺序。
    QVector<int> kids;
    for (int pieceIndex = 0; pieceIndex < pieces.size(); ++pieceIndex) {
        const DocumentPiece& piece = pieces.at(pieceIndex);
        if (piece.isImage) {
            const int imageNumber = nextNumber++;
            const int contentNumber = nextNumber++;
            const int pageNumber = nextNumber++;
            bodies.insert(imageNumber, piece.image.imageDict + "\nstream\n" + piece.image.imageStream +
                                     "\nendstream");
            bodies.insert(contentNumber, "<< /Length " +
                                             QByteArray::number(piece.image.contentStream.size()) +
                                             " >>\nstream\n" + piece.image.contentStream + "\nendstream");
            bodies.insert(pageNumber, "<< /Type /Page /Parent " + QByteArray::number(pagesNumber) +
                                          " 0 R /MediaBox " + mediaBox + " /Resources << /XObject << /Im0 " +
                                          QByteArray::number(imageNumber) +
                                          " 0 R >> >> /Contents " + QByteArray::number(contentNumber) +
                                          " 0 R >>");
            kids.append(pageNumber);
            continue;
        }
        const int sourceIndex = pieceSource.at(pieceIndex);
        const ParsedPdf& source = sources.at(sourceIndex);
        const QHash<int, int>& mapping = mappings.at(sourceIndex);
        for (int pageNumber : source.pageNumbers) {
            const int assigned = mapping.value(pageNumber, 0);
            if (assigned > 0) {
                kids.append(assigned);
            }
        }
    }
    if (kids.isEmpty()) {
        *error = QStringLiteral("no page to write");
        return false;
    }

    // 5) 写文件。
    QByteArray kidsText;
    for (int index = 0; index < kids.size(); ++index) {
        if (index > 0) {
            kidsText += ' ';
        }
        kidsText += QByteArray::number(kids.at(index)) + " 0 R";
    }
    const QByteArray pagesDict = "<< /Type /Pages /Kids [" + kidsText + "] /Count " +
                                 QByteArray::number(kids.size()) + " >>";
    QByteArray bytes = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    QVector<qint64> offsets(nextNumber, 0);
    for (int number = 1; number < nextNumber; ++number) {
        offsets[number] = bytes.size();
        bytes += QByteArray::number(number) + " 0 obj\n";
        if (number == catalogNumber) {
            bytes += "<< /Type /Catalog /Pages " + QByteArray::number(pagesNumber) + " 0 R >>";
        } else if (number == pagesNumber) {
            bytes += pagesDict;
        } else {
            bytes += bodies.value(number);
        }
        bytes += "\nendobj\n";
    }
    const qint64 xrefOffset = bytes.size();
    bytes += "xref\n0 " + QByteArray::number(nextNumber) + "\n";
    bytes += "0000000000 65535 f \n";
    for (int number = 1; number < nextNumber; ++number) {
        bytes += QByteArray::number(offsets.at(number), 10).rightJustified(10, '0') + " 00000 n \n";
    }
    bytes += "trailer\n<< /Size " + QByteArray::number(nextNumber) + " /Root " +
             QByteArray::number(catalogNumber) + " 0 R >>\nstartxref\n" + QByteArray::number(xrefOffset) +
             "\n%%EOF\n";
    *out = bytes;
    *pageCount = kids.size();
    return true;
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
        result.errorKind = ConversionErrorKind::NoContent;
        result.error = QStringLiteral("剪贴板里没有文本，没法生成 PDF。");
        return result;
    }
    result = textToPdf(buildPlainText(title, text), title, options);
    result.sourceKind = ConversionSourceKind::PlainText;
    return result;
}

ConversionResult clipboardHtmlToPdf(const QString& html, const QString& title,
                                    const UniversalConverterOptions& options) {
    const HtmlBody body = extractHtmlBody(html);
    if (body.text.trimmed().isEmpty()) {
        ConversionResult result;
        result.errorKind = ConversionErrorKind::NoHtmlBody;
        // 领域层只给技术细节：给用户看的中文句子由 app 层按 errorKind 拼。
        result.error = QStringLiteral("clipboard html has no body text");
        return result;
    }
    // 标题用调用方给的：剪贴板里的 <title> 是来源文档的标题，不是这一份的标题。
    ConversionResult result = htmlBodyToPdf(body, title, options);
    result.sourceKind = ConversionSourceKind::RichText;
    return result;
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
    body.links = scanned.links;
    // 图片：摘掉正文里的位置标记，并算出每张图在正文里的偏移。
    QVector<int> imageOffsets;
    body.text = extractImageMarkers(scanned.text, &imageOffsets);
    for (int index = 0; index < scanned.imageUrls.size(); ++index) {
        HtmlImageRef reference;
        reference.url = scanned.imageUrls.at(index);
        reference.textOffset = index < imageOffsets.size() ? imageOffsets.at(index) : -1;
        body.images.append(reference);
    }

    // 没有 <title> 就拿正文里的第一个 <h1> 当标题。
    if (body.title.isEmpty()) {
        body.title = elementText(region, QStringLiteral("h1"));
    }
    return body;
}

namespace {

// 正文里的链接清单（排在正文最后）。
//
// 为什么链接也要排进去：PDF 是纯文本的，链接不写出来就丢了。
QString buildLinksAppendix(const QStringList& links) {
    if (links.isEmpty()) {
        return QString();
    }
    QString text = QStringLiteral("\n\n") + QStringLiteral("页面里的链接：");
    int shown = 0;
    for (const QString& link : links) {
        if (shown >= 50) {
            break;
        }
        text += QLatin1Char('\n') + link;
        ++shown;
    }
    if (links.size() > shown) {
        text += QStringLiteral("\n…还有 %1 个链接没列出来。").arg(links.size() - shown);
    }
    return text;
}

// 只排文字（正文里没有图片，或这次明确不下载图片）。
//
// 这一条与「从前」的行为完全一致：网页里没有图，或者调用方没给取字节的回调，
// 出来的 PDF 与从前一模一样。
ConversionResult htmlBodyToPdfTextOnly(const HtmlBody& body, const QString& title,
                                       const UniversalConverterOptions& options) {
    if (body.text.trimmed().isEmpty()) {
        ConversionResult result;
        result.errorKind = ConversionErrorKind::NoHtmlBody;
        result.error = QStringLiteral("html has no body text");
        return result;
    }
    QString text = buildPlainText(title, body.text) + buildLinksAppendix(body.links);
    ConversionResult result = textToPdf(text, title, options);
    if (!result.ok) {
        result.errorKind = ConversionErrorKind::LayoutFailed;
    }
    return result;
}

}  // namespace

// 把抽好的正文排成 PDF（网页与剪贴板富文本共用这一份）。
//
// title 是**生效的标题**：网页传自己的 <title>，剪贴板传调用方给的标题
// （剪贴板里的 <title> 是来源文档的标题，不是这一份的）。
//
// fetcher 非空时连图片一起带进去：正文按图片出现的位置切成几段，
// 图片单独排成一页插在中间。图取不到 / 解不开都**不中断**整篇转换，
// 原因逐条记进 result.imageOutcome.issues。
//
// 空 fetcher = 不下载图片（与从前完全一致），此时 imageOutcome.attempted 为 false。
ConversionResult htmlBodyToPdf(const HtmlBody& body, const QString& title,
                               const UniversalConverterOptions& options, const ImageFetcher& fetcher) {
    ConversionResult result;
    result.imageOutcome.found = body.images.size();
    // 没有图片，或者这次明确不下载图片：走原来那条路。
    if (body.images.isEmpty() || !fetcher) {
        result = htmlBodyToPdfTextOnly(body, title, options);
        result.imageOutcome.found = body.images.size();
        result.imageOutcome.attempted = false;
        return result;
    }
    result.imageOutcome.attempted = true;

    const int textLength = static_cast<int>(body.text.size());
    const QSizeF pageSize = pageSizeInPoints(options.pageSize, options.orientation);
    const QByteArray mediaBox = "[0 0 " + formatPoints(pageSize.width()) + " " +
                                formatPoints(pageSize.height()) + "]";

    // 先把所有图取回来、解码。取不到 / 解不开都**不中断**整篇转换，
    // 原因逐条记进 issues（用户要看得见「哪一张没进来、为什么」）。
    struct PreparedImage {
        int textOffset = -1;
        ImagePageData page;
    };
    QVector<PreparedImage> prepared;
    for (const HtmlImageRef& image : body.images) {
        if (prepared.size() + result.imageOutcome.issues.size() >= kMaxWebImages) {
            // 页面里的图太多：后面的不下了，逐条记下来（不静默丢弃）。
            HtmlImageIssue issue;
            issue.url = image.url;
            issue.kind = HtmlImageIssueKind::LimitReached;
            issue.detail = QStringLiteral("too many images in one page");
            result.imageOutcome.issues.append(issue);
            continue;
        }
        ImagePageData imagePage;
        HtmlImageIssue issue;
        if (!fetchImagePage(QUrl(image.url), fetcher, pageSize, options.marginPt, &imagePage, &issue)) {
            result.imageOutcome.issues.append(issue);
            continue;
        }
        PreparedImage entry;
        entry.textOffset = image.textOffset;
        entry.page = imagePage;
        prepared.append(entry);
        ++result.imageOutcome.embedded;
    }

    // 正文按**成功的图**的位置切段。
    //
    // 为什么只按成功的图切：图没带进来时正文不该因此多出一页空白
    // —— 那会让「一张图下载失败」变成「整篇排版变形」。
    QVector<int> cutOffsets;
    for (const PreparedImage& image : prepared) {
        const int offset = image.textOffset < 0
                               ? textLength
                               : qMin(qMax(0, image.textOffset), textLength);
        cutOffsets.append(offset);
    }
    // 偏移必须递增（正文里的图本来就按顺序出现，这里兜个底）。
    for (int index = 1; index < cutOffsets.size(); ++index) {
        cutOffsets[index] = qMax(cutOffsets.at(index), cutOffsets.at(index - 1));
    }

    // 每一段的文字。
    QVector<QString> segmentTexts;
    segmentTexts.reserve(cutOffsets.size() + 1);
    int cursor = 0;
    for (const int offset : cutOffsets) {
        segmentTexts.append(body.text.mid(cursor, offset - cursor));
        cursor = offset;
    }
    segmentTexts.append(body.text.mid(cursor));
    // 标题排在第一段有字的后面；链接清单排在最后一段有字的后面。
    const QString trimmedTitle = title.trimmed();
    if (!trimmedTitle.isEmpty()) {
        for (int index = 0; index < segmentTexts.size(); ++index) {
            if (segmentTexts.at(index).trimmed().isEmpty()) {
                continue;
            }
            segmentTexts[index] = trimmedTitle + QStringLiteral("\n\n") + segmentTexts.at(index);
            break;
        }
    }
    const QString appendix = buildLinksAppendix(body.links);
    if (!appendix.isEmpty()) {
        for (int index = segmentTexts.size() - 1; index >= 0; --index) {
            if (segmentTexts.at(index).trimmed().isEmpty()) {
                continue;
            }
            segmentTexts[index] += appendix;
            break;
        }
    }

    QVector<DocumentPiece> pieces;
    QString warning;
    int replaced = 0;

    for (int index = 0; index < segmentTexts.size(); ++index) {
        if (!segmentTexts.at(index).trimmed().isEmpty()) {
            const UniversalConversionResult converted =
                convertToPdf(segmentTexts.at(index).toUtf8(), UniversalFileKind::PlainText,
                             QStringLiteral("文档.pdf"), options);
            if (!converted.ok) {
                ConversionResult failed;
                failed.errorKind = ConversionErrorKind::LayoutFailed;
                failed.error = converted.error;
                failed.imageOutcome = result.imageOutcome;
                return failed;
            }
            if (warning.isEmpty()) {
                warning = converted.warning;
            }
            replaced += converted.replacedCharacterCount;
            DocumentPiece piece;
            piece.sourcePdf = converted.bytes;
            pieces.append(piece);
        }
        // 这一段之后的图片。
        if (index >= cutOffsets.size()) {
            continue;
        }
        DocumentPiece piece;
        piece.isImage = true;
        piece.image = prepared.at(index).page;
        pieces.append(piece);
    }

    QByteArray composed;
    int composedPages = 0;
    QString composeError;
    if (!composeDocument(pieces, mediaBox, &composed, &composedPages, &composeError)) {
        // 合成失败：退回「只有文字」那份，图逐条记成没带进来。
        // 为什么退回而不是整篇失败：文字已经排好了，为了一张图把整篇丢掉不划算。
        result = htmlBodyToPdfTextOnly(body, title, options);
        result.imageOutcome.found = body.images.size();
        result.imageOutcome.attempted = true;
        result.imageOutcome.embedded = 0;
        result.imageOutcome.issues.clear();
        for (const HtmlImageRef& image : body.images) {
            HtmlImageIssue issue;
            issue.url = image.url;
            issue.kind = HtmlImageIssueKind::LayoutFailed;
            issue.detail = composeError;
            result.imageOutcome.issues.append(issue);
        }
        return result;
    }
    result.ok = true;
    result.bytes = composed;
    result.title = trimmedTitle;
    result.warning = warning;
    result.replacedCharacterCount = replaced;
    result.pageCount = composedPages;
    return result;
}

ConversionResult htmlToPdf(const QString& html, const QUrl& baseUrl,
                           const UniversalConverterOptions& options, const ImageFetcher& fetcher) {
    const HtmlBody body = extractHtmlBody(html, baseUrl);
    return htmlBodyToPdf(body, body.title, options, fetcher);
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
