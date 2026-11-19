#include "pdf/content/FontResource.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <algorithm>
#include <memory>

namespace aipdf::pdf {
namespace {

// ---------------------------------------------------------------------------
// 字体的解码表：ToUnicode CMap、/Differences、WinAnsi 兜底
// ---------------------------------------------------------------------------

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t hexToCode(const QByteArray& hex) {
    uint32_t value = 0;
    for (char c : hex) {
        const int digit = hexValue(c);
        if (digit < 0) continue;
        value = value * 16 + static_cast<uint32_t>(digit);
    }
    return value;
}

// ToUnicode 的目标值是 UTF-16BE；一个字节时按 Latin-1 处理。
QString hexToText(const QByteArray& hex) {
    if (hex.isEmpty()) return QString();
    if (hex.size() == 1) {
        return QString(QChar(static_cast<char16_t>(static_cast<unsigned char>(hex.at(0)))));
    }
    QString out;
    for (int i = 0; i + 1 < hex.size(); i += 2) {
        const int code = (static_cast<unsigned char>(hex.at(i)) << 8) |
                         static_cast<unsigned char>(hex.at(i + 1));
        out += QChar(static_cast<char16_t>(code));
    }
    return out;
}

bool nextHexToken(const QByteArray& data, int* pos, QByteArray* out) {
    while (*pos < data.size() && data.at(*pos) != '<') ++(*pos);
    if (*pos >= data.size()) return false;
    const int end = data.indexOf('>', *pos);
    if (end < 0) return false;
    *out = data.mid(*pos + 1, end - *pos - 1);
    *pos = end + 1;
    return true;
}

void parseBfChars(const QByteArray& section, QHash<uint32_t, QString>* out) {
    int pos = 0;
    while (true) {
        QByteArray source;
        QByteArray target;
        if (!nextHexToken(section, &pos, &source)) break;
        if (!nextHexToken(section, &pos, &target)) break;
        out->insert(hexToCode(source), hexToText(target));
    }
}

void parseBfRanges(const QByteArray& section, QHash<uint32_t, QString>* out) {
    int pos = 0;
    while (true) {
        QByteArray low;
        QByteArray high;
        if (!nextHexToken(section, &pos, &low)) break;
        if (!nextHexToken(section, &pos, &high)) break;
        while (pos < section.size() &&
               (section.at(pos) == ' ' || section.at(pos) == '\n' ||
                section.at(pos) == '\r' || section.at(pos) == '\t')) {
            ++pos;
        }
        if (pos < section.size() && section.at(pos) == '[') {
            ++pos;
            int offset = 0;
            while (pos < section.size() && section.at(pos) != ']') {
                QByteArray item;
                if (!nextHexToken(section, &pos, &item)) break;
                out->insert(hexToCode(low) + static_cast<uint32_t>(offset), hexToText(item));
                ++offset;
            }
            if (pos < section.size()) ++pos;
            continue;
        }
        QByteArray target;
        if (!nextHexToken(section, &pos, &target)) break;
        const uint32_t start = hexToCode(low);
        const uint32_t end = hexToCode(high);
        const QString base = hexToText(target);
        if (base.size() != 1) continue;
        const ushort baseCode = base.at(0).unicode();
        for (uint32_t code = start; code <= end && code - start < 65536; ++code) {
            out->insert(code, QString(QChar(static_cast<char16_t>(baseCode + (code - start)))));
        }
    }
}

void parseToUnicodeMap(const QByteArray& cmap, QHash<uint32_t, QString>* out) {
    int position = 0;
    while (true) {
        const int charStart = cmap.indexOf("beginbfchar", position);
        const int rangeStart = cmap.indexOf("beginbfrange", position);
        if (charStart < 0 && rangeStart < 0) break;
        if (charStart >= 0 && (rangeStart < 0 || charStart < rangeStart)) {
            const int end = cmap.indexOf("endbfchar", charStart);
            if (end < 0) break;
            parseBfChars(cmap.mid(charStart + 11, end - charStart - 11), out);
            position = end + 9;
        } else {
            const int end = cmap.indexOf("endbfrange", rangeStart);
            if (end < 0) break;
            parseBfRanges(cmap.mid(rangeStart + 12, end - rangeStart - 12), out);
            position = end + 10;
        }
    }
}

// 字形名 → 文字（只覆盖常见的那一批；认不出来返回空串）。
QString glyphNameToUnicode(const QByteArray& name) {
    if (name.isEmpty()) return QString();
    if ((name.startsWith("uni") || name.startsWith("u")) && name.size() >= 5) {
        bool ok = false;
        const int code = name.mid(name.startsWith("uni") ? 3 : 1, 4).toInt(&ok, 16);
        if (ok && code > 0) return QString(QChar(static_cast<char16_t>(code)));
    }
    static const QHash<QByteArray, ushort> table = {
        {QByteArrayLiteral("space"), 0x0020},    {QByteArrayLiteral("exclam"), 0x0021},
        {QByteArrayLiteral("quotedbl"), 0x0022},  {QByteArrayLiteral("numbersign"), 0x0023},
        {QByteArrayLiteral("dollar"), 0x0024},    {QByteArrayLiteral("percent"), 0x0025},
        {QByteArrayLiteral("ampersand"), 0x0026}, {QByteArrayLiteral("quotesingle"), 0x0027},
        {QByteArrayLiteral("parenleft"), 0x0028}, {QByteArrayLiteral("parenright"), 0x0029},
        {QByteArrayLiteral("asterisk"), 0x002A},  {QByteArrayLiteral("plus"), 0x002B},
        {QByteArrayLiteral("comma"), 0x002C},     {QByteArrayLiteral("hyphen"), 0x002D},
        {QByteArrayLiteral("period"), 0x002E},    {QByteArrayLiteral("slash"), 0x002F},
        {QByteArrayLiteral("colon"), 0x003A},     {QByteArrayLiteral("semicolon"), 0x003B},
        {QByteArrayLiteral("less"), 0x003C},      {QByteArrayLiteral("equal"), 0x003D},
        {QByteArrayLiteral("greater"), 0x003E},   {QByteArrayLiteral("question"), 0x003F},
        {QByteArrayLiteral("at"), 0x0040},        {QByteArrayLiteral("bracketleft"), 0x005B},
        {QByteArrayLiteral("backslash"), 0x005C}, {QByteArrayLiteral("bracketright"), 0x005D},
        {QByteArrayLiteral("asciicircum"), 0x005E},{QByteArrayLiteral("underscore"), 0x005F},
        {QByteArrayLiteral("grave"), 0x0060},     {QByteArrayLiteral("braceleft"), 0x007B},
        {QByteArrayLiteral("bar"), 0x007C},       {QByteArrayLiteral("braceright"), 0x007D},
        {QByteArrayLiteral("asciitilde"), 0x007E},{QByteArrayLiteral("bullet"), 0x2022},
        {QByteArrayLiteral("endash"), 0x2013},    {QByteArrayLiteral("emdash"), 0x2014},
        {QByteArrayLiteral("quotedblleft"), 0x201C},{QByteArrayLiteral("quotedblright"), 0x201D},
    };
    const auto found = table.constFind(name);
    if (found != table.constEnd()) return QString(QChar(found.value()));
    if (name.size() == 1) {
        const char c = name.at(0);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            return QString(QChar(static_cast<char16_t>(static_cast<unsigned char>(c))));
        }
    }
    return QString();
}

QString winAnsiToUnicode(uint32_t code) {
    if (code >= 0x20 && code <= 0x7E) return QString(QChar(static_cast<char16_t>(code)));
    if (code >= 0xA0 && code <= 0xFF) return QString(QChar(static_cast<char16_t>(code)));
    return QString(QChar(QChar::ReplacementCharacter));
}

// 一张表建反查：只收单码点映射，连字（"fi" 这种）不进反查。
void buildReverseMap(FontResource* font, bool includeWinAnsiFallback) {
    font->unicodeToCode.clear();
    for (auto it = font->codeToUnicode.constBegin(); it != font->codeToUnicode.constEnd(); ++it) {
        const QString& text = it.value();
        if (text.size() == 1) {
            font->unicodeToCode.insert(text.at(0).unicode(), it.key());
        } else if (text.size() == 2 && text.at(0).isHighSurrogate()) {
            // 代理对：还原成一个码点。
            const char32_t code = QChar::surrogateToUcs4(text.at(0), text.at(1));
            font->unicodeToCode.insert(code, it.key());
        }
    }
    if (!includeWinAnsiFallback) return;
    // 没有 ToUnicode 的非嵌入简单字体，按 WinAnsi 承认 0x20..0xFF 的可写性。
    for (uint32_t code = 0x20; code <= 0xFF; ++code) {
        const QString text = winAnsiToUnicode(code);
        if (text.isEmpty() || text.at(0).unicode() == QChar::ReplacementCharacter) continue;
        const char32_t point = text.at(0).unicode();
        if (!font->unicodeToCode.contains(point)) font->unicodeToCode.insert(point, code);
    }
}

// ---------------------------------------------------------------------------
// PDF 对象 → FontResource
// ---------------------------------------------------------------------------

QString nameOf(const QPDFObjectHandle& object) {
    if (!object.isName()) return QString();
    const std::string name = object.getName();
    const auto slash = name.find('/');
    return QString::fromStdString(slash == std::string::npos ? name : name.substr(slash + 1));
}

QString cleanBaseFont(const QString& raw) {
    // 去掉子集前缀 "ABCDEF+"。
    const int plus = raw.indexOf(QLatin1Char('+'));
    if (plus == 6 && raw.size() > 7) return raw.mid(plus + 1);
    return raw;
}

void readSimpleWidths(const QPDFObjectHandle& font, FontResource* out) {
    const QPDFObjectHandle first = font.getKey("/FirstChar");
    const QPDFObjectHandle widths = font.getKey("/Widths");
    if (!first.isInteger() || !widths.isArray()) return;
    const int start = static_cast<int>(first.getIntValue());
    const int count = widths.getArrayNItems();
    for (int i = 0; i < count; ++i) {
        const QPDFObjectHandle width = widths.getArrayItem(i);
        if (!width.isNumber()) continue;
        out->widths.insert(static_cast<uint32_t>(start + i), width.getNumericValue());
    }
    out->widthsApproximate = out->widths.isEmpty();
}

void readCidWidths(const QPDFObjectHandle& font, FontResource* out) {
    const QPDFObjectHandle array = font.getKey("/W");
    if (!array.isArray()) return;
    const int count = array.getArrayNItems();
    int i = 0;
    while (i < count) {
        const QPDFObjectHandle first = array.getArrayItem(i);
        if (!first.isInteger()) {
            ++i;
            continue;
        }
        const uint32_t start = static_cast<uint32_t>(first.getIntValue());
        if (i + 1 >= count) break;
        const QPDFObjectHandle second = array.getArrayItem(i + 1);
        if (second.isArray()) {
            const int widths = second.getArrayNItems();
            for (int j = 0; j < widths; ++j) {
                const QPDFObjectHandle width = second.getArrayItem(j);
                if (width.isNumber()) {
                    out->widths.insert(start + static_cast<uint32_t>(j), width.getNumericValue());
                }
            }
            i += 2;
            continue;
        }
        if (i + 2 >= count) break;
        const QPDFObjectHandle last = second;
        const QPDFObjectHandle width = array.getArrayItem(i + 2);
        if (!last.isInteger() || !width.isNumber()) {
            ++i;
            continue;
        }
        const uint32_t end = static_cast<uint32_t>(last.getIntValue());
        for (uint32_t code = start; code <= end && code - start < 65536; ++code) {
            out->widths.insert(code, width.getNumericValue());
        }
        i += 3;
    }
    out->widthsApproximate = out->widths.isEmpty();
}

void readEncodingDifferences(const QPDFObjectHandle& font, FontResource* out) {
    const QPDFObjectHandle encoding = font.getKey("/Encoding");
    if (!encoding.isDictionary()) return;
    const QPDFObjectHandle differences = encoding.getKey("/Differences");
    if (!differences.isArray()) return;
    uint32_t code = 0;
    const int count = differences.getArrayNItems();
    for (int i = 0; i < count; ++i) {
        const QPDFObjectHandle item = differences.getArrayItem(i);
        if (item.isInteger()) {
            code = static_cast<uint32_t>(item.getIntValue());
            continue;
        }
        if (!item.isName()) continue;
        const QString text = glyphNameToUnicode(nameOf(item).toLatin1());
        if (!text.isEmpty()) out->codeToUnicode.insert(code, text);
        ++code;
    }
}

QByteArray streamBytes(const QPDFObjectHandle& object) {
    if (!object.isStream()) return {};
    const std::shared_ptr<Buffer> data = object.getStreamData(qpdf_dl_generalized);
    if (!data) return {};
    return QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                      static_cast<qsizetype>(data->getSize()));
}

FontResource parseFont(const std::string& key, const QPDFObjectHandle& font) {
    FontResource out;
    out.resourceName = QString::fromStdString(key);
    out.baseFont = cleanBaseFont(nameOf(font.getKey("/BaseFont")));
    const QString subtype = nameOf(font.getKey("/Subtype"));
    if (subtype == QLatin1String("Type1")) out.kind = FontKind::Type1;
    else if (subtype == QLatin1String("TrueType")) out.kind = FontKind::TrueType;
    else if (subtype == QLatin1String("Type0")) out.kind = FontKind::Type0;
    else if (subtype == QLatin1String("Type3")) out.kind = FontKind::Type3;

    QPDFObjectHandle descriptor;
    if (out.kind == FontKind::Type0) {
        out.composite = true;
        out.codeBytes = 2;
        out.defaultWidth = 1000.0;
        const QString encoding = nameOf(font.getKey("/Encoding"));
        out.vertical = encoding.endsWith(QLatin1String("-V")) || encoding == QLatin1String("Identity-V");
        const QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
        if (descendants.isArray() && descendants.getArrayNItems() > 0) {
            const QPDFObjectHandle descendant = descendants.getArrayItem(0);
            if (descendant.isDictionary()) {
                const QPDFObjectHandle dw = descendant.getKey("/DW");
                if (dw.isNumber()) out.defaultWidth = dw.getNumericValue();
                readCidWidths(descendant, &out);
                descriptor = descendant.getKey("/FontDescriptor");
            }
        }
    } else {
        out.codeBytes = 1;
        out.defaultWidth = 500.0;
        readSimpleWidths(font, &out);
        readEncodingDifferences(font, &out);
        descriptor = font.getKey("/FontDescriptor");
    }

    if (descriptor.isDictionary()) {
        const QPDFObjectHandle missing = descriptor.getKey("/MissingWidth");
        if (missing.isNumber()) out.missingWidth = missing.getNumericValue();
        out.embedded = descriptor.hasKey("/FontFile") || descriptor.hasKey("/FontFile2") ||
                       descriptor.hasKey("/FontFile3");
    }

    const QPDFObjectHandle toUnicode = font.getKey("/ToUnicode");
    if (toUnicode.isStream()) {
        const QByteArray cmap = streamBytes(toUnicode);
        if (!cmap.isEmpty()) {
            parseToUnicodeMap(cmap, &out.codeToUnicode);
            out.toUnicodePresent = true;
        }
    }

    // 非嵌入的简单字体可以按 WinAnsi 承认可写性；嵌入又没有 ToUnicode 的
    // 不猜——猜错了就是把「写不出来」说成「写出来了」。
    const bool winAnsiFallback = out.codeBytes == 1 && !out.toUnicodePresent && !out.embedded;
    buildReverseMap(&out, winAnsiFallback);
    return out;
}

}  // namespace

QString FontResource::decode(const QByteArray& bytes) const {
    QString out;
    const int size = codeBytes > 0 ? codeBytes : 1;
    for (int i = 0; i + size <= bytes.size(); i += size) {
        uint32_t code = 0;
        for (int b = 0; b < size; ++b) {
            code = (code << 8) | static_cast<unsigned char>(bytes.at(i + b));
        }
        const auto mapped = codeToUnicode.constFind(code);
        if (mapped != codeToUnicode.constEnd()) {
            out += mapped.value();
            continue;
        }
        if (size == 1) {
            out += winAnsiToUnicode(code);
        } else {
            out += QChar(QChar::ReplacementCharacter);
        }
    }
    return out;
}

FontGlyph FontResource::glyphFor(char32_t unicode) const {
    FontGlyph glyph;
    glyph.unicode = unicode;
    glyph.codeBytes = codeBytes;
    const auto found = unicodeToCode.constFind(unicode);
    if (found == unicodeToCode.constEnd()) {
        glyph.covered = false;
        glyph.width = missingWidth > 0.0 ? missingWidth : defaultWidth;
        return glyph;
    }
    glyph.code = found.value();
    glyph.covered = true;
    const auto width = widths.constFind(glyph.code);
    glyph.width = width != widths.constEnd() ? width.value() : defaultWidth;
    return glyph;
}

QVector<FontGlyph> FontResource::encode(const QString& text) const {
    QVector<FontGlyph> glyphs;
    const QVector<uint> points = text.toUcs4();
    glyphs.reserve(points.size());
    for (uint point : points) glyphs.append(glyphFor(static_cast<char32_t>(point)));
    return glyphs;
}

double FontResource::measure(const QString& text, double fontSize) const {
    double total = 0.0;
    for (const FontGlyph& glyph : encode(text)) total += glyph.width;
    return total * fontSize / 1000.0;
}

bool FontResource::covers(const QString& text) const {
    const QVector<uint> points = text.toUcs4();
    for (uint point : points) {
        if (!unicodeToCode.contains(static_cast<char32_t>(point))) return false;
    }
    return true;
}

PageFontList readPageFonts(const QByteArray& pdfBytes, int pageIndex) {
    PageFontList result;
    if (pdfBytes.isEmpty()) {
        result.error = QStringLiteral("PDF 字节为空");
        return result;
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("page-fonts.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
            result.error = QStringLiteral("页码越界：%1（共 %2 页）")
                               .arg(pageIndex)
                               .arg(static_cast<int>(pages.size()));
            return result;
        }
        QPDFObjectHandle resources;
        QPDFObjectHandle current = pages.at(static_cast<size_t>(pageIndex));
        for (int depth = 0; depth < 64 && current.isDictionary(); ++depth) {
            if (current.hasKey("/Resources")) {
                resources = current.getKey("/Resources");
                break;
            }
            if (!current.hasKey("/Parent")) break;
            current = current.getKey("/Parent");
        }
        result.ok = true;
        if (!resources.isDictionary()) return result;
        const QPDFObjectHandle fonts = resources.getKey("/Font");
        if (!fonts.isDictionary()) return result;
        for (const std::string& key : fonts.getKeys()) {
            const QPDFObjectHandle font = fonts.getKey(key);
            if (!font.isDictionary()) continue;
            result.fonts.append(parseFont(key, font));
        }
    } catch (const std::exception& exception) {
        result.error = QString::fromUtf8(exception.what());
        result.fonts.clear();
        result.ok = false;
    }
    return result;
}

const FontResource* findFontResource(const PageFontList& list, const QString& resourceName) {
    for (const FontResource& font : list.fonts) {
        if (font.resourceName == resourceName) return &font;
    }
    return nullptr;
}

}  // namespace aipdf::pdf
