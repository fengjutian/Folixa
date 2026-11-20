#include "pdf/content/TextSplice.h"

#include "pdf/content/ContentStream.h"
#include "pdf/content/TrueTypeFace.h"
#include "pdf/content/TrueTypeSubset.h"
#include "pdf/content/FontResource.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <memory>

#include <QFile>
#include <QHash>
#include <QSet>

namespace aipdf::pdf {
namespace {

#ifdef AIPDF_WITH_QPDF

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

QByteArray decodeLiteral(const QByteArray& raw) {
    QByteArray out;
    for (int i = 1; i + 1 < raw.size(); ++i) {
        char c = raw.at(i);
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 >= raw.size()) break;
        c = raw.at(i);
        if (c == 'n') out += '\n';
        else if (c == 'r') out += '\r';
        else if (c == 't') out += '\t';
        else if (c == 'b') out += '\b';
        else if (c == 'f') out += '\f';
        else if (c == '\n') continue;
        else if (c == '\r') {
            if (i + 1 < raw.size() && raw.at(i + 1) == '\n') ++i;
        } else if (c >= '0' && c <= '7') {
            int value = c - '0';
            for (int n = 0; n < 2 && i + 1 < raw.size() - 1 && raw.at(i + 1) >= '0' &&
                            raw.at(i + 1) <= '7';
                 ++n) {
                value = value * 8 + (raw.at(++i) - '0');
            }
            out += static_cast<char>(value);
        } else {
            out += c;
        }
    }
    return out;
}

QByteArray decodeHex(const QByteArray& raw) {
    QByteArray out;
    int high = -1;
    for (char c : raw) {
        const int digit = hexDigit(c);
        if (digit < 0) continue;
        if (high < 0) high = digit;
        else {
            out += static_cast<char>(high * 16 + digit);
            high = -1;
        }
    }
    return out;
}

QByteArray encodeLiteral(const QByteArray& bytes) {
    QByteArray out("(");
    for (char raw : bytes) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 32 || c > 126) {
            out += '\\';
            out += QByteArray::number(c, 8).rightJustified(3, '0');
        } else {
            out += static_cast<char>(c);
        }
    }
    out += ')';
    return out;
}

QString unicodeName(char32_t code) {
    return QStringLiteral("U+%1").arg(code, 4, 16, QLatin1Char('0')).toUpper();
}

// 用页面字体把文字编码成内容流里的字符串操作数。
// 写不出任何一个字就失败——绝不静默换成别的字体。
bool encodeWithFont(const FontResource& font, const QString& text, QByteArray* out,
                    QString* error) {
    out->clear();
    if (text.isEmpty()) return true;
    const QVector<FontGlyph> glyphs = font.encode(text);
    QByteArray codes;
    for (const FontGlyph& glyph : glyphs) {
        if (!glyph.covered) {
            if (error) {
                *error = QStringLiteral("字体 %1（%2）写不出「%3」（%4）")
                             .arg(font.resourceName, font.baseFont,
                                  QString::fromUcs4(&glyph.unicode, 1), unicodeName(glyph.unicode));
            }
            return false;
        }
        if (font.codeBytes == 2) {
            codes += static_cast<char>((glyph.code >> 8) & 0xFF);
            codes += static_cast<char>(glyph.code & 0xFF);
        } else {
            codes += static_cast<char>(glyph.code & 0xFF);
        }
    }
    if (font.codeBytes == 2) {
        QByteArray hex;
        for (char c : codes) hex += QByteArray::number(static_cast<unsigned char>(c), 16)
                                       .rightJustified(2, '0')
                                       .toUpper();
        *out = "<" + hex + ">";
    } else {
        *out = encodeLiteral(codes);
    }
    return true;
}

QString objectNameOf(const QPDFObjectHandle& object) {
    if (!object.isName()) return QString();
    std::string name = object.getName();
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    return QString::fromStdString(name);
}

qint64 objectKeyOf(const QPDFObjectHandle& object) {
    const QPDFObjGen id = object.getObjGen();
    if (id.getObj() <= 0) return 0;
    return (static_cast<qint64>(id.getObj()) << 32) | static_cast<quint32>(id.getGen());
}

// 页面 /Resources 可以挂在 /Pages 上，沿 /Parent 找。
QPDFObjectHandle pageResources(QPDFObjectHandle page) {
    for (int depth = 0; depth < 64 && page.isDictionary(); ++depth) {
        if (page.hasKey("/Resources")) {
            const QPDFObjectHandle resources = page.getKey("/Resources");
            if (resources.isDictionary()) return resources;
        }
        if (!page.hasKey("/Parent")) break;
        page = page.getKey("/Parent");
    }
    return {};
}

// 页面 /Resources 下所有 Form XObject（递归，防环）。
void collectFormXObjects(QPDFObjectHandle resources, QVector<QPDFObjectHandle>* out,
                         QSet<qint64>* seen, int depth) {
    if (!resources.isDictionary() || depth > 8) return;
    const QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if (!xobjects.isDictionary()) return;
    for (const std::string& key : xobjects.getKeys()) {
        const QPDFObjectHandle object = xobjects.getKey(key);
        if (!object.isStream()) continue;
        QPDFObjectHandle dict = object.getDict();
        if (objectNameOf(dict.getKey("/Subtype")) != QLatin1String("Form")) continue;
        const qint64 id = objectKeyOf(object);
        if (id != 0 && seen->contains(id)) continue;
        if (id != 0) seen->insert(id);
        out->append(object);
        QPDFObjectHandle child = dict.getKey("/Resources");
        if (child.isDictionary()) collectFormXObjects(child, out, seen, depth + 1);
    }
}

QString systemFallbackFontPath() {
    const QString configured = qEnvironmentVariable("AIPDF_CJK_FONT");
    if (!configured.isEmpty() && QFile::exists(configured)) return configured;
    static const QStringList candidates = {
        QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
        QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
        QStringLiteral("C:/Windows/Fonts/simsun.ttc"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        QStringLiteral("/System/Library/Fonts/PingFang.ttc")};
    for (const QString& path : candidates) {
        if (QFile::exists(path)) return path;
    }
    return QString();
}

QByteArray encodeGidHexCodes(const QVector<uint16_t>& gids) {
    QByteArray out;
    for (uint16_t gid : gids) {
        out += QByteArray::number(gid, 16).rightJustified(4, '0').toUpper();
    }
    return out;
}

struct EmbeddedFallback {
    bool ok = false;
    QString error;
    QString resourceName;
    TrueTypeFace face;
};

// 原字体写不出替换文字时，按用到的字形子集化一份系统字体，挂到页面资源里。
EmbeddedFallback embedFallbackFont(QPDF& pdf, QPDFObjectHandle resources, const QString& text) {
    EmbeddedFallback result;
    const QString path = systemFallbackFontPath();
    if (path.isEmpty()) {
        result.error = QStringLiteral("本机没有可用的回退字体（可设置 AIPDF_CJK_FONT）");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("打不开回退字体：%1").arg(path);
        return result;
    }
    const QByteArray fontBytes = file.readAll();
    result.face = TrueTypeFace::load(fontBytes);
    if (!result.face.ok) {
        result.error = result.face.error;
        return result;
    }

    QSet<int> usedGids;
    QHash<uint16_t, uint32_t> gidToUnicode;
    for (const uint code : text.toUcs4()) {
        const uint16_t gid = result.face.gidFor(static_cast<char32_t>(code));
        if (gid == 0) {
            result.error = QStringLiteral("回退字体里也没有这个字（U+%1）")
                               .arg(code, 4, 16, QLatin1Char('0'));
            return result;
        }
        usedGids.insert(gid);
        gidToUnicode.insert(gid, code);
    }
    usedGids.insert(0);
    const TrueTypeSubsetResult subset = subsetTrueTypeFont(fontBytes, usedGids);
    if (!subset.ok) {
        result.error = QStringLiteral("回退字体子集化失败：%1").arg(subset.error);
        return result;
    }

    QPDFObjectHandle fontFile = pdf.newStream(subset.bytes.toStdString());
    QPDFObjectHandle fontFileDict = fontFile.getDict();
    fontFileDict.replaceKey("/Length1", QPDFObjectHandle::newInteger(subset.bytes.size()));
    const QPDFObjectHandle fontFileRef = pdf.makeIndirectObject(fontFile);

    const double scale = 1000.0 / qMax(1, result.face.unitsPerEm);
    QPDFObjectHandle descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey("/FontName", QPDFObjectHandle::newName("/AIPDFFALLBACK"));
    descriptor.replaceKey("/Flags", QPDFObjectHandle::newInteger(4));
    QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
    bbox.appendItem(QPDFObjectHandle::newInteger(-200));
    bbox.appendItem(QPDFObjectHandle::newInteger(-300));
    bbox.appendItem(QPDFObjectHandle::newInteger(1200));
    bbox.appendItem(QPDFObjectHandle::newInteger(1000));
    descriptor.replaceKey("/FontBBox", bbox);
    descriptor.replaceKey("/ItalicAngle", QPDFObjectHandle::newInteger(0));
    descriptor.replaceKey("/Ascent",
                          QPDFObjectHandle::newInteger(qRound(result.face.ascent * scale)));
    descriptor.replaceKey("/Descent",
                          QPDFObjectHandle::newInteger(qRound(result.face.descent * scale)));
    descriptor.replaceKey("/CapHeight",
                          QPDFObjectHandle::newInteger(qRound(result.face.ascent * scale)));
    descriptor.replaceKey("/StemV", QPDFObjectHandle::newInteger(80));
    descriptor.replaceKey("/FontFile2", fontFileRef);
    const QPDFObjectHandle descriptorRef = pdf.makeIndirectObject(descriptor);

    QPDFObjectHandle widths = QPDFObjectHandle::newArray();
    QList<uint16_t> gids = QList<uint16_t>(usedGids.begin(), usedGids.end());
    std::sort(gids.begin(), gids.end());
    for (uint16_t gid : gids) {
        if (gid == 0) continue;
        widths.appendItem(QPDFObjectHandle::newInteger(gid));
        QPDFObjectHandle one = QPDFObjectHandle::newArray();
        one.appendItem(QPDFObjectHandle::newInteger(qRound(result.face.widthForGid(gid))));
        widths.appendItem(one);
    }

    QPDFObjectHandle systemInfo = QPDFObjectHandle::newDictionary();
    systemInfo.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    systemInfo.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
    systemInfo.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));

    QPDFObjectHandle cidFont = QPDFObjectHandle::newDictionary();
    cidFont.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    cidFont.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType2"));
    cidFont.replaceKey("/BaseFont", QPDFObjectHandle::newName("/AIPDFFALLBACK"));
    cidFont.replaceKey("/CIDSystemInfo", systemInfo);
    cidFont.replaceKey("/FontDescriptor", descriptorRef);
    cidFont.replaceKey("/DW", QPDFObjectHandle::newInteger(1000));
    cidFont.replaceKey("/W", widths);
    cidFont.replaceKey("/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));
    const QPDFObjectHandle cidRef = pdf.makeIndirectObject(cidFont);

    QByteArray cmap;
    cmap += "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n";
    cmap += "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n";
    cmap += "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    QList<uint16_t> mapped = gidToUnicode.keys();
    std::sort(mapped.begin(), mapped.end());
    for (int start = 0; start < mapped.size(); start += 100) {
        const int count = qMin(100, mapped.size() - start);
        cmap += QByteArray::number(count) + " beginbfchar\n";
        for (int i = 0; i < count; ++i) {
            const uint16_t gid = mapped.at(start + i);
            const uint32_t code = gidToUnicode.value(gid);
            cmap += "<" + QByteArray::number(gid, 16).rightJustified(4, '0').toUpper() + "> <" +
                    QByteArray::number(code, 16).rightJustified(4, '0').toUpper() + ">\n";
        }
        cmap += "endbfchar\n";
    }
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    const QPDFObjectHandle toUnicodeRef =
        pdf.makeIndirectObject(pdf.newStream(cmap.toStdString()));

    QPDFObjectHandle type0 = QPDFObjectHandle::newDictionary();
    type0.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    type0.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    type0.replaceKey("/BaseFont", QPDFObjectHandle::newName("/AIPDFFALLBACK"));
    type0.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    QPDFObjectHandle descendants = QPDFObjectHandle::newArray();
    descendants.appendItem(cidRef);
    type0.replaceKey("/DescendantFonts", descendants);
    type0.replaceKey("/ToUnicode", toUnicodeRef);
    const QPDFObjectHandle type0Ref = pdf.makeIndirectObject(type0);

    QPDFObjectHandle fonts = resources.getKey("/Font");
    if (!fonts.isDictionary()) {
        fonts = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/Font", fonts);
    }
    QString name = QStringLiteral("FB0");
    int suffix = 0;
    while (fonts.hasKey("/" + name.toStdString())) name = QStringLiteral("FB%1").arg(++suffix);
    fonts.replaceKey("/" + name.toStdString(), type0Ref);

    result.resourceName = name;
    result.ok = true;
    return result;
}

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (tokens.at(i).type != ContentTokenType::Whitespace &&
            tokens.at(i).type != ContentTokenType::Comment) {
            return i;
        }
    }
    return -1;
}

struct ShowElement {
    int tokenIndex = -1;
    QString text;
};

struct ShowSpan {
    int first = -1;
    int last = -1;  // 操作符 token 下标
    QByteArray op;
    QString fontResource;
    double fontSize = 0.0;
    QVector<ShowElement> elements;
    const FontResource* font = nullptr;
};

QVector<ShowSpan> scanShows(const QVector<ContentToken>& tokens, const PageFontList& fonts) {
    QVector<ShowSpan> spans;
    QString currentFont;
    double currentSize = 0.0;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).type != ContentTokenType::Operator) continue;
        const QByteArray op = tokens.at(i).raw;
        if (op == "Tf") {
            // Tf 的操作数是「字号 资源名」：先遇到字号，再遇到资源名。
            const int sizeIndex = previousSignificant(tokens, i);
            const int nameIndex = sizeIndex >= 0 ? previousSignificant(tokens, sizeIndex) : -1;
            if (nameIndex >= 0 && sizeIndex >= 0 &&
                tokens.at(nameIndex).type == ContentTokenType::Name &&
                tokens.at(sizeIndex).type == ContentTokenType::Number) {
                QByteArray name = tokens.at(nameIndex).raw.mid(1);
                currentFont = QString::fromLatin1(name);
                currentSize = tokens.at(sizeIndex).raw.toDouble();
            }
            continue;
        }
        ShowSpan span;
        span.last = i;
        span.op = op;
        span.fontResource = currentFont;
        span.fontSize = currentSize;
        span.font = findFontResource(fonts, currentFont);

        if (op == "Tj" || op == "'" || op == "\"") {
            const int operand = previousSignificant(tokens, i);
            if (operand < 0) continue;
            if (tokens.at(operand).type != ContentTokenType::LiteralString &&
                tokens.at(operand).type != ContentTokenType::HexString) {
                continue;
            }
            span.first = operand;
            QByteArray bytes = tokens.at(operand).type == ContentTokenType::LiteralString
                                   ? decodeLiteral(tokens.at(operand).raw)
                                   : decodeHex(tokens.at(operand).raw);
            span.elements.append(ShowElement{operand,
                                             span.font != nullptr ? span.font->decode(bytes)
                                                                  : QString()});
            spans.append(span);
            continue;
        }
        if (op == "TJ") {
            const int arrayEnd = previousSignificant(tokens, i);
            if (arrayEnd < 0 || tokens.at(arrayEnd).type != ContentTokenType::ArrayEnd) continue;
            int depth = 0;
            int arrayStart = -1;
            for (int j = arrayEnd; j >= 0; --j) {
                if (tokens.at(j).type == ContentTokenType::ArrayEnd) ++depth;
                else if (tokens.at(j).type == ContentTokenType::ArrayStart) {
                    if (--depth == 0) {
                        arrayStart = j;
                        break;
                    }
                }
            }
            if (arrayStart < 0) continue;
            span.first = arrayStart;
            for (int j = arrayStart + 1; j < arrayEnd; ++j) {
                if (tokens.at(j).type != ContentTokenType::LiteralString &&
                    tokens.at(j).type != ContentTokenType::HexString) {
                    continue;
                }
                QByteArray bytes = tokens.at(j).type == ContentTokenType::LiteralString
                                       ? decodeLiteral(tokens.at(j).raw)
                                       : decodeHex(tokens.at(j).raw);
                span.elements.append(
                    ShowElement{j, span.font != nullptr ? span.font->decode(bytes) : QString()});
            }
            spans.append(span);
        }
    }
    return spans;
}

// 一次替换可能横跨同一个 TJ 数组里的多个字符串元素：
// 每个元素记一条「删哪一段、塞什么」，替换文本只塞进第一个元素。
struct ElementEdit {
    int pos = 0;
    int length = 0;
    QString replacement;
};

struct MatchGroup {
    int spanIndex = -1;
    QVector<QVector<ElementEdit>> perElement;
};

QString pageTextOf(const QVector<ShowSpan>& spans) {
    QString text;
    for (const ShowSpan& span : spans) {
        for (const ShowElement& element : span.elements) text += element.text;
    }
    return text;
}

QString formatAdjustment(double value) {
    if (std::abs(value) < 0.0005) return QStringLiteral("0");
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    return text;
}

QString applyEdits(const QString& text, QVector<ElementEdit> edits) {
    std::sort(edits.begin(), edits.end(),
              [](const ElementEdit& left, const ElementEdit& right) {
                  return left.pos < right.pos;
              });
    QString out;
    int cursor = 0;
    for (const ElementEdit& edit : edits) {
        if (edit.pos < cursor) continue;
        out += text.mid(cursor, edit.pos - cursor);
        out += edit.replacement;
        cursor = edit.pos + edit.length;
    }
    out += text.mid(cursor);
    return out;
}

// 把一段文字（可能含多处替换）编码成 TJ 数组里的元素序列。
bool buildElementBody(const ShowSpan& span, const QString& text,
                      const QVector<ElementEdit>& edits, QByteArray* out,
                      int* removedGlyphs, int* insertedGlyphs, QString* error) {
    out->clear();
    if (span.font == nullptr) {
        if (error) *error = QStringLiteral("页面上找不到字体资源：%1").arg(span.fontResource);
        return false;
    }
    int cursor = 0;
    for (const ElementEdit& edit : edits) {
        if (edit.pos < cursor) continue;
        const QString unchanged = text.mid(cursor, edit.pos - cursor);
        QByteArray encoded;
        if (!encodeWithFont(*span.font, unchanged, &encoded, error)) return false;
        *out += encoded;

        QByteArray replacement;
        if (!encodeWithFont(*span.font, edit.replacement, &replacement, error)) return false;
        *out += replacement;
        *removedGlyphs += edit.length;
        *insertedGlyphs += edit.replacement.size();

        const QString removed = text.mid(edit.pos, edit.length);
        const double removedWidth = span.font->measure(removed, span.fontSize);
        const double insertedWidth = span.font->measure(edit.replacement, span.fontSize);
        const double adjustment =
            -(removedWidth - insertedWidth) * 1000.0 / span.fontSize;
        *out += ' ';
        *out += formatAdjustment(adjustment).toLatin1();
        *out += ' ';
        cursor = edit.pos + edit.length;
    }
    QByteArray tail;
    if (!encodeWithFont(*span.font, text.mid(cursor), &tail, error)) return false;
    *out += tail;
    return true;
}

QByteArray buildReplacement(const QVector<ContentToken>& tokens, const ShowSpan& span,
                            const QVector<QVector<ElementEdit>>& editsPerElement,
                            int* removedGlyphs, int* insertedGlyphs, QString* error) {
    QByteArray expression = "[";
    if (span.op == "TJ") {
        // 保留数组里原有的数字位移，只替换字符串元素。
        for (int i = span.first + 1; i < span.last; ++i) {
            if (tokens.at(i).type == ContentTokenType::ArrayEnd) break;
            const auto elementIt = std::find_if(
                span.elements.constBegin(), span.elements.constEnd(),
                [&](const ShowElement& element) { return element.tokenIndex == i; });
            if (elementIt != span.elements.constEnd()) {
                const int elementIndex = static_cast<int>(elementIt - span.elements.constBegin());
                QByteArray body;
                if (!buildElementBody(span, elementIt->text, editsPerElement.at(elementIndex),
                                      &body, removedGlyphs, insertedGlyphs, error)) {
                    return {};
                }
                expression += body;
                expression += ' ';
            } else {
                expression += tokens.at(i).raw;
                expression += ' ';
            }
        }
    } else {
        QByteArray body;
        if (!buildElementBody(span, span.elements.first().text, editsPerElement.value(0), &body,
                              removedGlyphs, insertedGlyphs, error)) {
            return {};
        }
        expression += body;
    }
    expression += "] TJ";
    return expression;
}

QByteArray buildFallbackExpression(const ShowSpan& span, const QString& elementText,
                                   const ElementEdit& edit, const EmbeddedFallback& fallback,
                                   double fontSize, int* removedGlyphs, int* insertedGlyphs,
                                   QString* error) {
    QByteArray expression;
    const QString prefix = elementText.left(edit.pos);
    const QString suffix = elementText.mid(edit.pos + edit.length);
    if (!prefix.isEmpty()) {
        QByteArray encoded;
        if (!encodeWithFont(*span.font, prefix, &encoded, error)) return {};
        expression += encoded + " Tj\n";
    }
    expression += "/" + fallback.resourceName.toLatin1() + " " +
                  QByteArray::number(fontSize, 'f', 2) + " Tf\n";
    QVector<uint16_t> gids;
    for (const uint code : edit.replacement.toUcs4()) {
        gids.append(fallback.face.gidFor(static_cast<char32_t>(code)));
    }
    expression += "<" + encodeGidHexCodes(gids) + "> Tj\n";
    expression += "/" + span.fontResource.toLatin1() + " " +
                  QByteArray::number(fontSize, 'f', 2) + " Tf\n";

    const QString removed = elementText.mid(edit.pos, edit.length);
    const double removedWidth = span.font->measure(removed, fontSize);
    double insertedWidth = 0.0;
    for (uint16_t gid : gids) {
        insertedWidth += fallback.face.widthForGid(gid) * fontSize / 1000.0;
    }
    const double adjustment = -(removedWidth - insertedWidth) * 1000.0 / fontSize;
    *removedGlyphs += edit.length;
    *insertedGlyphs += edit.replacement.size();
    QByteArray tail;
    if (!suffix.isEmpty()) {
        if (!encodeWithFont(*span.font, suffix, &tail, error)) return {};
    }
    expression += "[" + formatAdjustment(adjustment).toLatin1() + " " + tail + "] TJ";
    return expression;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

TextSpliceResult splicePageText(const QByteArray& pdfBytes, const TextSpliceTarget& target) {
    TextSpliceResult result;
    result.report.pageIndex = target.pageIndex;
    if (target.find.isEmpty()) {
        result.report.error = QStringLiteral("要替换的文字为空");
        return result;
    }
    if (target.pageIndex < 0) {
        result.report.error = QStringLiteral("页码不能为负");
        return result;
    }
    if (pdfBytes.isEmpty()) {
        result.report.error = QStringLiteral("PDF 字节为空");
        return result;
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("text-splice.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (target.pageIndex >= static_cast<int>(pages.size())) {
            result.report.error = QStringLiteral("页码越界：%1（共 %2 页）")
                                      .arg(target.pageIndex)
                                      .arg(static_cast<int>(pages.size()));
            return result;
        }
        QPDFObjectHandle page = pages.at(static_cast<size_t>(target.pageIndex));
        page.coalesceContentStreams();
        PageFontList scanFonts = readPageFonts(pdfBytes, target.pageIndex);

        // 文本可能藏在页内容流里，也可能藏在 Form XObject 里。
        // 两类流都收进 containers，改写时按各自的对象写回。
        struct StreamContainer {
            QPDFObjectHandle handle;
            QByteArray bytes;
        };
        QVector<StreamContainer> containers;
        QVector<QVector<ContentToken>> tokens;
        QString collectError;
        const auto addContainer = [&](QPDFObjectHandle handle) {
            if (!handle.isStream() || !collectError.isEmpty()) return;
            const std::shared_ptr<Buffer> data = handle.getStreamData(qpdf_dl_generalized);
            if (!data) return;
            StreamContainer container;
            container.handle = handle;
            container.bytes = QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                         static_cast<qsizetype>(data->getSize()));
            const ContentParseResult parsed = ContentStream::tokenize(container.bytes);
            if (!parsed.isValid()) {
                collectError = QStringLiteral("内容流解析失败：%1").arg(parsed.error);
                return;
            }
            containers.append(container);
            tokens.append(parsed.tokens);
        };

        std::vector<QPDFObjectHandle> contents = page.getPageContents();
        for (QPDFObjectHandle& stream : contents) addContainer(stream);
        QSet<qint64> seenObjects;
        QVector<QPDFObjectHandle> formXObjects;
        collectFormXObjects(pageResources(page), &formXObjects, &seenObjects, 0);
        for (QPDFObjectHandle& object : formXObjects) addContainer(object);
        if (!collectError.isEmpty()) {
            result.report.error = collectError;
            return result;
        }

        // 先扫一遍：把每段显示操作与它们的文字都列出来。
        QVector<QVector<ShowSpan>> streamSpans;
        QVector<ShowSpan> allSpans;
        QVector<int> spanStream;
        for (int index = 0; index < containers.size(); ++index) {
            QVector<ShowSpan> spans = scanShows(tokens.at(index), scanFonts);
            for (const ShowSpan& span : spans) {
                allSpans.append(span);
                spanStream.append(index);
            }
            streamSpans.append(spans);
        }

        // 按显示顺序找匹配；一次匹配可以横跨同一个 TJ 数组里的多个字符串元素。
        QVector<MatchGroup> groups;
        for (int spanIndex = 0; spanIndex < allSpans.size(); ++spanIndex) {
            const ShowSpan& span = allSpans.at(spanIndex);
            if (span.elements.isEmpty()) continue;
            QString joined;
            for (const ShowElement& element : span.elements) joined += element.text;
            if (span.op == "'" || span.op == "\"") {
                // 这两个操作符带换行语义，不能简单换成 TJ。
                if (joined.contains(target.find, target.sensitivity)) {
                    result.report.error =
                        QStringLiteral("匹配落在 ' 或 \" 操作符里，暂不支持替换");
                    return result;
                }
                continue;
            }
            if (span.font == nullptr) continue;
            int position = 0;
            while (true) {
                const int found = joined.indexOf(target.find, position, target.sensitivity);
                if (found < 0) break;
                const int matchEnd = found + target.find.size();
                position = matchEnd;

                // 把 [found, matchEnd) 映射到元素区间。
                int consumed = 0;
                int startElement = -1;
                int startOffset = 0;
                int endElement = -1;
                int endOffset = 0;
                for (int e = 0; e < span.elements.size(); ++e) {
                    const int length = span.elements.at(e).text.size();
                    const int elementStart = consumed;
                    const int elementEnd = consumed + length;
                    if (startElement < 0 && found >= elementStart && found < elementEnd) {
                        startElement = e;
                        startOffset = found - elementStart;
                    }
                    if (matchEnd > elementStart && matchEnd <= elementEnd) {
                        endElement = e;
                        endOffset = matchEnd - elementStart;
                    }
                    consumed = elementEnd;
                }
                if (startElement < 0 || endElement < 0) continue;

                MatchGroup group;
                group.spanIndex = spanIndex;
                group.perElement.resize(span.elements.size());
                for (int e = startElement; e <= endElement; ++e) {
                    const int textLength = span.elements.at(e).text.size();
                    ElementEdit edit;
                    edit.pos = (e == startElement) ? startOffset : 0;
                    edit.length = ((e == endElement) ? endOffset : textLength) - edit.pos;
                    edit.replacement = (e == startElement) ? target.replace : QString();
                    group.perElement[e].append(edit);
                }
                groups.append(group);
            }
        }
        if (groups.isEmpty()) {
            result.report.error = QStringLiteral("这一页上没有找到要替换的文字：%1").arg(target.find);
            return result;
        }

        QVector<MatchGroup> selected;
        if (target.occurrence < 0) {
            selected = groups;
        } else if (target.occurrence < groups.size()) {
            selected.append(groups.at(target.occurrence));
        } else {
            result.report.error = QStringLiteral("这一页只有 %1 处匹配，找不到第 %2 处")
                                      .arg(groups.size())
                                      .arg(target.occurrence);
            return result;
        }

        // 替换文字里有原字体写不出的字：给这一页嵌一份子集化的回退字体。
        QSet<int> fallbackSpans;
        EmbeddedFallback fallback;
        for (const MatchGroup& group : selected) {
            const ShowSpan& span = allSpans.at(group.spanIndex);
            if (span.font == nullptr || span.font->covers(target.replace)) continue;
            if (span.elements.size() != 1 || group.perElement.size() != 1 ||
                group.perElement.first().size() != 1) {
                result.report.error = QStringLiteral(
                    "替换文字里有原字体写不出的字，且匹配跨了多个文本元素；"
                    "回退字体暂只支持单个文本元素内的整段替换");
                return result;
            }
            const int containerIndex = spanStream.value(group.spanIndex);
            if (containerIndex >= static_cast<int>(contents.size())) {
                const QPDFObjectHandle ownResources =
                    containers.at(containerIndex).handle.getDict().getKey("/Resources");
                if (ownResources.isDictionary()) {
                    result.report.error = QStringLiteral(
                        "XObject 自带 /Resources，回退字体暂不支持这种嵌套");
                    return result;
                }
            }
            fallbackSpans.insert(group.spanIndex);
        }
        if (!fallbackSpans.isEmpty()) {
            QPDFObjectHandle resources = pageResources(page);
            if (!resources.isDictionary()) {
                result.report.error =
                    QStringLiteral("这一页没有 /Resources，放不下回退字体");
                return result;
            }
            fallback = embedFallbackFont(pdf, resources, target.replace);
            if (!fallback.ok) {
                result.report.error = fallback.error;
                return result;
            }
            FontResource decoded;
            decoded.resourceName = fallback.resourceName;
            decoded.composite = true;
            decoded.codeBytes = 2;
            decoded.toUnicodePresent = true;
            for (const uint code : target.replace.toUcs4()) {
                const uint16_t gid = fallback.face.gidFor(static_cast<char32_t>(code));
                decoded.codeToUnicode.insert(gid, QString::fromUcs4(&code, 1));
                decoded.unicodeToCode.insert(code, gid);
                decoded.widths.insert(gid, fallback.face.widthForGid(gid));
            }
            scanFonts.fonts.append(decoded);
        }

        // 期望的文字：每个元素按编辑列表重排，再按显示顺序拼起来。
        QString rebuilt;
        for (int spanIndex = 0; spanIndex < allSpans.size(); ++spanIndex) {
            const ShowSpan& span = allSpans.at(spanIndex);
            for (int elementIndex = 0; elementIndex < span.elements.size(); ++elementIndex) {
                QVector<ElementEdit> edits;
                for (const MatchGroup& group : selected) {
                    if (group.spanIndex != spanIndex) continue;
                    edits += group.perElement.value(elementIndex);
                }
                rebuilt += applyEdits(span.elements.at(elementIndex).text, edits);
            }
        }
        const QString expected = rebuilt;

        // 每个 span 生成一条替换后的表达式；同一个 span 里的多处匹配一起处理。
        QHash<int, QVector<QVector<ElementEdit>>> editsBySpan;
        for (const MatchGroup& group : selected) {
            QVector<QVector<ElementEdit>>& perElement = editsBySpan[group.spanIndex];
            if (perElement.isEmpty()) perElement.resize(allSpans.at(group.spanIndex).elements.size());
            for (int e = 0; e < group.perElement.size(); ++e) {
                perElement[e] += group.perElement.at(e);
            }
        }

        QHash<int, QByteArray> expressionBySpan;
        QHash<int, QPair<int, int>> rangeBySpan;
        for (auto it = editsBySpan.constBegin(); it != editsBySpan.constEnd(); ++it) {
            const int spanIndex = it.key();
            const ShowSpan& span = allSpans.at(spanIndex);
            QVector<QVector<ElementEdit>> perElement = it.value();
            for (QVector<ElementEdit>& edits : perElement) {
                std::sort(edits.begin(), edits.end(),
                          [](const ElementEdit& left, const ElementEdit& right) {
                              return left.pos < right.pos;
                          });
            }
            int removed = 0;
            int inserted = 0;
            QString error;
            QByteArray expression;
            if (fallbackSpans.contains(spanIndex)) {
                expression = buildFallbackExpression(
                    span, span.elements.first().text, perElement.value(0).value(0), fallback,
                    span.fontSize, &removed, &inserted, &error);
            } else {
                expression = buildReplacement(tokens.at(spanStream.at(spanIndex)), span,
                                              perElement, &removed, &inserted, &error);
            }
            if (expression.isEmpty()) {
                result.report.error = error.isEmpty() ? QStringLiteral("改写失败") : error;
                return result;
            }
            expressionBySpan.insert(spanIndex, expression);
            rangeBySpan.insert(spanIndex, {span.first, span.last});
            result.report.glyphsRemoved += removed;
            result.report.glyphsInserted += inserted;
            if (result.report.fontResource.isEmpty()) {
                result.report.fontResource = span.fontResource;
                result.report.baseFont = span.font != nullptr ? span.font->baseFont : QString();
            }
        }
        result.report.occurrences = selected.size();
        for (int i = 0; i < result.report.occurrences; ++i) {
            result.report.removed.append(target.find);
            result.report.inserted.append(target.replace);
        }

        // 逐流重建：同一条流里按 token 顺序走，遇到被替换的 span 就整段换掉。
        QVector<QByteArray> rewritten;
        rewritten.reserve(containers.size());
        for (const StreamContainer& container : containers) rewritten.append(container.bytes);
        for (int streamIndex = 0; streamIndex < containers.size(); ++streamIndex) {
            QVector<int> spans;
            for (auto it = expressionBySpan.constBegin(); it != expressionBySpan.constEnd(); ++it) {
                if (spanStream.value(it.key()) == streamIndex) spans.append(it.key());
            }
            if (spans.isEmpty()) continue;
            std::sort(spans.begin(), spans.end(), [&](int left, int right) {
                return rangeBySpan.value(left).first < rangeBySpan.value(right).first;
            });
            QByteArray output;
            int tokenIndex = 0;
            int next = 0;
            const int tokenCount = tokens.at(streamIndex).size();
            while (tokenIndex < tokenCount) {
                if (next < spans.size() &&
                    rangeBySpan.value(spans.at(next)).first == tokenIndex) {
                    output += expressionBySpan.value(spans.at(next));
                    tokenIndex = rangeBySpan.value(spans.at(next)).second + 1;
                    ++next;
                    continue;
                }
                output += tokens.at(streamIndex).at(tokenIndex).raw;
                ++tokenIndex;
            }
            rewritten[streamIndex] = output;
        }

        // 写回 qpdf：页内容流与 Form XObject 各自写回自己的对象。
        for (int index = 0; index < containers.size(); ++index) {
            if (index >= rewritten.size()) break;
            if (rewritten.at(index) == containers.at(index).bytes) continue;
            containers[index].handle.replaceStreamData(
                rewritten.at(index).toStdString(), QPDFObjectHandle::newNull(),
                QPDFObjectHandle::newNull());
        }

        // 门禁：把改写后的流重新解析、重新解码，与期望串逐字比对。
        QString afterText;
        for (const QByteArray& streamBytes : rewritten) {
            const ContentParseResult parsed = ContentStream::tokenize(streamBytes);
            if (!parsed.isValid()) {
                result.report.error =
                    QStringLiteral("改写后的内容流解析失败：%1").arg(parsed.error);
                return result;
            }
            afterText += pageTextOf(scanShows(parsed.tokens, scanFonts));
        }
        if (afterText != expected) {
            result.report.error = QStringLiteral("改写后回读不一致：期望「%1」，实际「%2」")
                                      .arg(expected, afterText);
            return result;
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            result.report.error = QStringLiteral("qpdf 写出的 PDF 为空");
            return result;
        }
        result.bytes = QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                                  static_cast<qsizetype>(output->getSize()));
    } catch (const std::exception& exception) {
        result.report.error = QString::fromUtf8(exception.what());
    }
#else
    Q_UNUSED(pdfBytes)
    Q_UNUSED(target)
    result.report.error = QStringLiteral("qpdf is unavailable");
#endif
    return result;
}

}  // namespace aipdf::pdf
