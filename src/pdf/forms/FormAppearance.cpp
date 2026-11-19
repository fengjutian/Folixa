#include "pdf/forms/FormAppearance.h"

#include "pdf/content/TrueTypeFace.h"
#include "pdf/content/TrueTypeSubset.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QRectF>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>
#include <memory>

namespace aipdf::pdf {
namespace {

QString objectName(const QPDFObjectHandle& object) {
    if (!object.isName()) return QString();
    std::string name = object.getName();
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    return QString::fromStdString(name);
}

QString objectText(const QPDFObjectHandle& object) {
    if (object.isString()) return QString::fromStdString(object.getStringValue());
    if (object.isName()) return objectName(object);
    return QString();
}

bool isWidget(const QPDFObjectHandle& node) {
    return node.isDictionary() && objectName(node.getKey("/Subtype")) == QLatin1String("Widget");
}

QVector<QPDFObjectHandle> childObjects(const QPDFObjectHandle& node) {
    QVector<QPDFObjectHandle> kids;
    const QPDFObjectHandle array = node.getKey("/Kids");
    if (!array.isArray()) return kids;
    const int count = array.getArrayNItems();
    for (int i = 0; i < count; ++i) kids.append(array.getArrayItem(i));
    return kids;
}

QVector<QPDFObjectHandle> widgetsOf(const QPDFObjectHandle& node) {
    if (isWidget(node)) return {node};
    QVector<QPDFObjectHandle> widgets;
    for (const QPDFObjectHandle& kid : childObjects(node)) {
        if (isWidget(kid)) widgets.append(kid);
    }
    return widgets;
}

struct RawField {
    QPDFObjectHandle node;
    QVector<QPDFObjectHandle> widgets;
    QString appearance;
    bool text = false;
    bool button = false;
    bool readOnly = false;
    bool multiline = false;
    // /Q：0 左、1 居中、2 右。
    int quadding = 0;
};

struct FieldContext {
    QString prefix;
    QString inheritedDa;
    int quadding = 0;
};

void collectFields(const QPDFObjectHandle& node, const FieldContext& parent,
                   QHash<QString, RawField>* out) {
    if (!node.isDictionary()) return;
    FieldContext context = parent;
    if (node.hasKey("/DA")) context.inheritedDa = objectText(node.getKey("/DA"));
    if (node.hasKey("/Q")) context.quadding = static_cast<int>(node.getKey("/Q").getIntValue());

    QString name = context.prefix;
    if (node.hasKey("/T")) {
        const QString part = objectText(node.getKey("/T"));
        if (!part.isEmpty()) name = context.prefix.isEmpty() ? part : context.prefix + QLatin1Char('.') + part;
    }
    context.prefix = name;

    const QVector<QPDFObjectHandle> kids = childObjects(node);
    bool hasFieldChild = false;
    for (const QPDFObjectHandle& kid : kids) {
        if (kid.isDictionary() && kid.hasKey("/T")) hasFieldChild = true;
    }

    const QString type = objectName(node.getKey("/FT"));
    const bool terminal = !type.isEmpty() && (kids.isEmpty() || !hasFieldChild);
    if (terminal && !name.isEmpty()) {
        RawField field;
        field.node = node;
        field.widgets = widgetsOf(node);
        field.appearance = context.inheritedDa;
        field.text = type == QLatin1String("Tx");
        field.button = type == QLatin1String("Btn");
        field.readOnly = (static_cast<int>(node.getKey("/Ff").getIntValue()) & 1) != 0;
        field.multiline =
            (static_cast<int>(node.getKey("/Ff").getIntValue()) & (1 << 12)) != 0;
        field.quadding = context.quadding;
        out->insert(name, field);
        return;
    }
    for (const QPDFObjectHandle& kid : kids) collectFields(kid, context, out);
}

QRectF widgetRect(const QPDFObjectHandle& widget) {
    const QPDFObjectHandle rect = widget.getKey("/Rect");
    if (!rect.isArray() || rect.getArrayNItems() < 4) return {};
    double values[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        if (!rect.getArrayItem(i).isNumber()) return {};
        values[i] = rect.getArrayItem(i).getNumericValue();
    }
    const double left = qMin(values[0], values[2]);
    const double bottom = qMin(values[1], values[3]);
    return QRectF(left, bottom, qAbs(values[2] - values[0]), qAbs(values[3] - values[1]));
}

QByteArray escapePdfString(const QByteArray& bytes) {
    QByteArray out;
    for (char raw : bytes) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32 || c > 126) {
            out += '\\';
            out += QByteArray::number(c, 8).rightJustified(3, '0');
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// /DA 形如 "/Helv 12 Tf 0 g"。
void parseDefaultAppearance(const QString& da, QString* fontName, double* fontSize) {
    static const QRegularExpression pattern(QStringLiteral("/([^\\s/]+)\\s+([0-9.]+)\\s+Tf"));
    const QRegularExpressionMatch match = pattern.match(da);
    if (match.hasMatch()) {
        *fontName = match.captured(1);
        *fontSize = match.captured(2).toDouble();
    }
}

struct ResolvedRun {
    QByteArray text;
    QString fontName;
    double fontSize = 10.0;
    QColor color;
    bool hasColor = false;
    // text 是十六进制字形串（Identity-H），不是字面量。
    bool hex = false;
};

bool needsUnicodeFont(const QString& text) {
    for (const QChar c : text) {
        if (c.unicode() > 0xFF) return true;
    }
    return false;
}

QString systemFontPath() {
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

QByteArray encodeGidHex(const QVector<uint16_t>& gids) {
    QByteArray out;
    for (uint16_t gid : gids) {
        out += QByteArray::number(gid, 16).rightJustified(4, '0').toUpper();
    }
    return out;
}

struct EmbeddedFont {
    bool ok = false;
    QString error;
    QString resourceName;
    QPDFObjectHandle ref;
    TrueTypeFace face;
};

// 把系统字体按「这段文字用到的字形」子集化后嵌进 PDF，返回资源名。
EmbeddedFont embedUnicodeFont(QPDF& pdf, QPDFObjectHandle acroForm, const QString& text) {
    EmbeddedFont result;
    const QString path = systemFontPath();
    if (path.isEmpty()) {
        result.error = QStringLiteral("本机没有可用的中文字体（可设置 AIPDF_CJK_FONT）");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("打不开字体：%1").arg(path);
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
            result.error = QStringLiteral("字体 %1 里没有这个字（U+%2）")
                               .arg(path)
                               .arg(code, 4, 16, QLatin1Char('0'));
            return result;
        }
        usedGids.insert(gid);
        gidToUnicode.insert(gid, code);
    }
    usedGids.insert(0);

    const TrueTypeSubsetResult subset = subsetTrueTypeFont(fontBytes, usedGids);
    if (!subset.ok) {
        result.error = QStringLiteral("字体子集化失败：%1").arg(subset.error);
        return result;
    }

    QPDFObjectHandle fontFile = pdf.newStream(subset.bytes.toStdString());
    QPDFObjectHandle fontFileDict = fontFile.getDict();
    fontFileDict.replaceKey("/Length1", QPDFObjectHandle::newInteger(subset.bytes.size()));
    const QPDFObjectHandle fontFileRef = pdf.makeIndirectObject(fontFile);

    const double scale = 1000.0 / qMax(1, result.face.unitsPerEm);
    QPDFObjectHandle descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey("/FontName", QPDFObjectHandle::newName("/AIPDFCJK"));
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
        one.appendItem(QPDFObjectHandle::newInteger(
            qRound(result.face.widthForGid(gid))));
        widths.appendItem(one);
    }

    QPDFObjectHandle systemInfo = QPDFObjectHandle::newDictionary();
    systemInfo.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    systemInfo.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
    systemInfo.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));

    QPDFObjectHandle cidFont = QPDFObjectHandle::newDictionary();
    cidFont.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    cidFont.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType2"));
    cidFont.replaceKey("/BaseFont", QPDFObjectHandle::newName("/AIPDFCJK"));
    cidFont.replaceKey("/CIDSystemInfo", systemInfo);
    cidFont.replaceKey("/FontDescriptor", descriptorRef);
    cidFont.replaceKey("/DW", QPDFObjectHandle::newInteger(1000));
    cidFont.replaceKey("/W", widths);
    cidFont.replaceKey("/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));
    const QPDFObjectHandle cidRef = pdf.makeIndirectObject(cidFont);

    QByteArray cmap;
    cmap += "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n";
    cmap += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
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
            QByteArray target = QByteArray::number(code, 16).rightJustified(4, '0').toUpper();
            if (code > 0xFFFF) {
                const uint32_t value = code - 0x10000;
                target = QByteArray::number(0xD800 + (value >> 10), 16).rightJustified(4, '0').toUpper();
                target += QByteArray::number(0xDC00 + (value & 0x3FF), 16)
                              .rightJustified(4, '0')
                              .toUpper();
            }
            cmap += "<" + QByteArray::number(gid, 16).rightJustified(4, '0').toUpper() + "> <" +
                    target + ">\n";
        }
        cmap += "endbfchar\n";
    }
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    const QPDFObjectHandle toUnicodeRef =
        pdf.makeIndirectObject(pdf.newStream(cmap.toStdString()));

    QPDFObjectHandle type0 = QPDFObjectHandle::newDictionary();
    type0.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    type0.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    type0.replaceKey("/BaseFont", QPDFObjectHandle::newName("/AIPDFCJK"));
    type0.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    QPDFObjectHandle descendants = QPDFObjectHandle::newArray();
    descendants.appendItem(cidRef);
    type0.replaceKey("/DescendantFonts", descendants);
    type0.replaceKey("/ToUnicode", toUnicodeRef);
    const QPDFObjectHandle type0Ref = pdf.makeIndirectObject(type0);

    QPDFObjectHandle dr = acroForm.getKey("/DR");
    if (!dr.isDictionary()) {
        dr = QPDFObjectHandle::newDictionary();
        acroForm.replaceKey("/DR", dr);
    }
    QPDFObjectHandle drFonts = dr.getKey("/Font");
    if (!drFonts.isDictionary()) {
        drFonts = QPDFObjectHandle::newDictionary();
        dr.replaceKey("/Font", drFonts);
    }
    QString resourceName = QStringLiteral("FCJK");
    int suffix = 1;
    while (drFonts.hasKey("/" + resourceName.toStdString())) {
        resourceName = QStringLiteral("FCJK%1").arg(++suffix);
    }
    drFonts.replaceKey("/" + resourceName.toStdString(), type0Ref);

    result.resourceName = resourceName;
    result.ref = type0Ref;
    result.ok = true;
    return result;
}


QString xmlEscape(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) out += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<')) out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>')) out += QStringLiteral("&gt;");
        else if (c == QLatin1Char('"')) out += QStringLiteral("&quot;");
        else out += c;
    }
    return out;
}

// /RV 用的是 Acrobat 的富文本 XHTML 子集：body > p > span(style)。
QString buildRichTextXml(const QVector<FormTextRun>& runs, double baseSize) {
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\"?><body xmlns=\"http://www.w3.org/1999/xhtml\"><p>");
    for (const FormTextRun& run : runs) {
        QString style;
        if (run.bold) style += QStringLiteral("font-weight:bold;");
        if (run.italic) style += QStringLiteral("font-style:italic;");
        if (run.color.isValid()) {
            style += QStringLiteral("color:%1;").arg(run.color.name(QColor::HexRgb));
        }
        if (run.fontSize > 0.0 && !qFuzzyCompare(run.fontSize, baseSize)) {
            style += QStringLiteral("font-size:%1pt;").arg(run.fontSize);
        }
        const QString text = xmlEscape(run.text);
        if (style.isEmpty()) {
            xml += text;
        } else {
            xml += QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text);
        }
    }
    xml += QStringLiteral("</p></body>");
    return xml;
}

QString baseFontFamily(const QString& name) {
    static const QStringList suffixes = {QStringLiteral("-BoldOblique"),
                                         QStringLiteral("-BoldItalic"), QStringLiteral("-Bold"),
                                         QStringLiteral("-Oblique"), QStringLiteral("-Italic")};
    for (const QString& suffix : suffixes) {
        if (name.endsWith(suffix)) return name.left(name.size() - suffix.size());
    }
    return name;
}

QString fontVariantName(const QString& base, bool bold, bool italic) {
    QString name = baseFontFamily(base);
    if (bold && italic) name += QStringLiteral("-BoldOblique");
    else if (bold) name += QStringLiteral("-Bold");
    else if (italic) name += QStringLiteral("-Oblique");
    return name;
}

QString standardBaseFont(const QString& variantName) {
    if (variantName.endsWith(QLatin1String("-BoldOblique"))) return QStringLiteral("Helvetica-BoldOblique");
    if (variantName.endsWith(QLatin1String("-Bold"))) return QStringLiteral("Helvetica-Bold");
    if (variantName.endsWith(QLatin1String("-Oblique"))) return QStringLiteral("Helvetica-Oblique");
    return QStringLiteral("Helvetica");
}

QByteArray appearanceContent(const QVector<ResolvedRun>& runs, double width, double height) {
    const double baseSize = runs.isEmpty() ? 10.0 : runs.first().fontSize;
    const double baseline = qMax(2.0, (height - baseSize) / 2.0 + baseSize * 0.2);
    QByteArray content;
    content += "q\n";
    content += "BT\n";
    content += "2 " + QByteArray::number(baseline, 'f', 2) + " Td\n";
    for (const ResolvedRun& run : runs) {
        content += "/" + run.fontName.toLatin1() + " " +
                   QByteArray::number(run.fontSize, 'f', 2) + " Tf\n";
        if (run.hasColor) {
            content += QByteArray::number(run.color.redF(), 'f', 3) + " " +
                       QByteArray::number(run.color.greenF(), 'f', 3) + " " +
                       QByteArray::number(run.color.blueF(), 'f', 3) + " rg\n";
        } else {
            content += "0 g\n";
        }
        if (run.hex) {
            content += "<" + run.text + "> Tj\n";
        } else {
            content += "(" + escapePdfString(run.text) + ") Tj\n";
        }
    }
    content += "ET\n";
    content += "Q\n";
    Q_UNUSED(width)
    return content;
}

// 单行/多行排版：按字宽贪心断行（中文逐字断，西文在空格处断）。
double measureGlyph(const EmbeddedFont* embedded, QChar c, double fontSize) {
    if (embedded != nullptr && embedded->ok) {
        const uint16_t gid = embedded->face.gidFor(static_cast<char32_t>(c.unicode()));
        return embedded->face.widthForGid(gid) * fontSize / 1000.0;
    }
    QFont font(QStringLiteral("Arial"));
    font.setPointSizeF(qMax(1.0, fontSize));
    const QFontMetricsF metrics(font);
    const double advance = metrics.horizontalAdvance(QString(c));
    // 没有可用字体引擎时按半个字宽估，至少能断行，不会把整段画成一行。
    return advance > 0.0 ? advance : fontSize * 0.5;
}

QVector<QString> wrapLines(const QString& text, double maxWidth, bool multiline,
                           double fontSize, const EmbeddedFont* embedded) {
    QVector<QString> lines;
    if (!multiline) {
        lines.append(text);
        return lines;
    }
    QString current;
    double width = 0.0;
    for (const QChar c : text) {
        if (c == QLatin1Char('\n')) {
            lines.append(current);
            current.clear();
            width = 0.0;
            continue;
        }
        const double glyphWidth = measureGlyph(embedded, c, fontSize);
        if (width + glyphWidth > maxWidth && !current.isEmpty()) {
            lines.append(current);
            current.clear();
            width = 0.0;
        }
        current += c;
        width += glyphWidth;
    }
    if (!current.isEmpty() || lines.isEmpty()) lines.append(current);
    return lines;
}

QByteArray appearanceLines(const QVector<QByteArray>& lines, const QVector<double>& widths,
                           double fontSize, const QString& fontName, bool hex,
                           const QColor& color, int quadding, double width, double height) {
    QByteArray content;
    content += "q\nBT\n";
    double baseline = height - 2.0 - fontSize;
    for (int i = 0; i < lines.size(); ++i) {
        if (baseline < 0.0) break;
        double x = 2.0;
        if (quadding == 1) {
            x = qMax(2.0, (width - widths.value(i)) / 2.0);
        } else if (quadding == 2) {
            x = qMax(2.0, width - 2.0 - widths.value(i));
        }
        content += "1 0 0 1 " + QByteArray::number(x, 'f', 2) + " " +
                   QByteArray::number(baseline, 'f', 2) + " Tm\n";
        content += "/" + fontName.toLatin1() + " " + QByteArray::number(fontSize, 'f', 2) + " Tf\n";
        if (color.isValid()) {
            content += QByteArray::number(color.redF(), 'f', 3) + " " +
                       QByteArray::number(color.greenF(), 'f', 3) + " " +
                       QByteArray::number(color.blueF(), 'f', 3) + " rg\n";
        } else {
            content += "0 g\n";
        }
        if (hex) {
            content += "<" + lines.at(i) + "> Tj\n";
        } else {
            content += "(" + escapePdfString(lines.at(i)) + ") Tj\n";
        }
        baseline -= fontSize * 1.2;
    }
    content += "ET\nQ\n";
    return content;
}

}  // namespace

FormAppearanceResult writeFormValues(const QByteArray& pdfBytes,
                                     const QVector<FormValueUpdate>& updates) {
    FormAppearanceResult result;
    if (pdfBytes.isEmpty()) {
        result.report.error = QStringLiteral("PDF 字节为空");
        return result;
    }
    if (updates.isEmpty()) {
        result.report.error = QStringLiteral("没有要写回的字段");
        return result;
    }
    try {
        QPDF pdf;
        pdf.processMemoryFile("form-values.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        QPDFObjectHandle acroForm = pdf.getRoot().getKey("/AcroForm");
        if (!acroForm.isDictionary()) {
            result.report.error = QStringLiteral("这份 PDF 没有 AcroForm");
            return result;
        }

        QHash<QString, RawField> fields;
        const QPDFObjectHandle fieldArray = acroForm.getKey("/Fields");
        if (fieldArray.isArray()) {
            const int count = fieldArray.getArrayNItems();
            for (int i = 0; i < count; ++i) collectFields(fieldArray.getArrayItem(i), {}, &fields);
        }

        QPDFObjectHandle fonts;
        const QPDFObjectHandle dr = acroForm.getKey("/DR");
        if (dr.isDictionary()) fonts = dr.getKey("/Font");

        for (const FormValueUpdate& update : updates) {
            const auto found = fields.constFind(update.fieldName);
            if (found == fields.constEnd()) {
                result.report.error =
                    QStringLiteral("找不到字段：%1").arg(update.fieldName);
                return result;
            }
            RawField field = found.value();
            if (field.readOnly) {
                result.report.error = QStringLiteral("只读域不能改：%1").arg(update.fieldName);
                return result;
            }

            if (field.button) {
                // 复选 / 单选：写 /V 与每个 widget 的 /AS。
                field.node.replaceKey("/V", QPDFObjectHandle::newName(
                                                "/" + update.value.toStdString()));
                for (QPDFObjectHandle widget : field.widgets) {
                    widget.replaceKey("/AS", QPDFObjectHandle::newName(
                                                 "/" + update.value.toStdString()));
                }
                result.report.fieldsUpdated += 1;
                continue;
            }
            if (!field.text) {
                result.report.error =
                    QStringLiteral("只支持文本域与按钮域：%1").arg(update.fieldName);
                return result;
            }

            QVector<FormTextRun> runs = update.runs;
            QString plain = update.value;
            if (!runs.isEmpty()) {
                plain.clear();
                for (const FormTextRun& run : runs) plain += run.text;
            }
            const bool unicodeText = needsUnicodeFont(plain);
            EmbeddedFont embedded;
            if (unicodeText) {
                embedded = embedUnicodeFont(pdf, acroForm, plain);
                if (!embedded.ok) {
                    result.report.error =
                        QStringLiteral("字段 %1 需要嵌入字体，但没成功：%2")
                            .arg(update.fieldName, embedded.error);
                    return result;
                }
            } else {
                // 单字节路径：超出 0xFF 的字符才需要嵌入字体，这里只是兜底。
                for (uint point : plain.toUcs4()) {
                    if (point > 0xFF) {
                        result.report.error =
                            QStringLiteral("字段 %1 的值里有非单字节字符（U+%2）")
                                .arg(update.fieldName)
                                .arg(point, 4, 16, QLatin1Char('0'));
                        return result;
                    }
                }
            }
            const QByteArray encoded = plain.toLatin1();
            if (unicodeText) {
                // 含非 Latin-1 字符时 /V 必须是 UTF-16BE（qpdf 会补 BOM）。
                field.node.replaceKey(
                    "/V", QPDFObjectHandle::newUnicodeString(plain.toUtf8().toStdString()));
            } else {
                field.node.replaceKey("/V",
                                      QPDFObjectHandle::newString(encoded.toStdString()));
            }

            QString fontName = QStringLiteral("Helv");
            double fontSize = 10.0;
            parseDefaultAppearance(field.appearance, &fontName, &fontSize);
            if (fontSize <= 0.0) fontSize = 10.0;

            if (!runs.isEmpty()) {
                const QString richText = buildRichTextXml(runs, fontSize);
                field.node.replaceKey("/RV",
                                      QPDFObjectHandle::newString(richText.toUtf8().toStdString()));
                result.report.richTextFields += 1;
            }

            QHash<QString, QPDFObjectHandle> apFonts;
            const auto ensureFont = [&](const QString& name) {
                const auto existing = apFonts.constFind(name);
                if (existing != apFonts.constEnd()) return existing.value();
                QPDFObjectHandle font;
                if (fonts.isDictionary()) font = fonts.getKey("/" + name.toStdString());
                if (!font.isDictionary()) {
                    font = QPDFObjectHandle::newDictionary();
                    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
                    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
                    font.replaceKey("/BaseFont",
                                    QPDFObjectHandle::newName(
                                        "/" + standardBaseFont(name).toStdString()));
                }
                const QPDFObjectHandle ref = pdf.makeIndirectObject(font);
                apFonts.insert(name, ref);
                return ref;
            };

            QVector<ResolvedRun> resolved;
            QVector<QByteArray> lineOperands;
            QVector<double> lineWidths;
            const bool useLines = runs.isEmpty();
            if (useLines) {
                // 纯文本：走单行/多行排版，支持中文与 /Q 对齐。
                const EmbeddedFont* embeddedPtr = unicodeText ? &embedded : nullptr;
                if (unicodeText) {
                    apFonts.insert(embedded.resourceName, embedded.ref);
                } else {
                    ensureFont(fontName);
                }
                double fieldWidth = 200.0;
                if (!field.widgets.isEmpty()) {
                    const QRectF firstWidget = widgetRect(field.widgets.first());
                    if (!firstWidget.isEmpty()) fieldWidth = firstWidget.width();
                }
                const double maxWidth = qMax(20.0, fieldWidth - 4.0);
                const QVector<QString> lines =
                    wrapLines(plain, maxWidth, field.multiline, fontSize, embeddedPtr);
                for (const QString& line : lines) {
                    double lineWidth = 0.0;
                    for (const QChar c : line) {
                        lineWidth += measureGlyph(embeddedPtr, c, fontSize);
                    }
                    if (unicodeText) {
                        QVector<uint16_t> gids;
                        for (const uint code : line.toUcs4()) {
                            gids.append(embedded.face.gidFor(static_cast<char32_t>(code)));
                        }
                        lineOperands.append(encodeGidHex(gids));
                    } else {
                        lineOperands.append(line.toLatin1());
                    }
                    lineWidths.append(lineWidth);
                }
            } else if (unicodeText) {
                apFonts.insert(embedded.resourceName, embedded.ref);
                for (const FormTextRun& run : runs) {
                    QVector<uint16_t> gids;
                    for (const uint code : run.text.toUcs4()) {
                        gids.append(embedded.face.gidFor(static_cast<char32_t>(code)));
                    }
                    ResolvedRun item;
                    item.text = encodeGidHex(gids);
                    item.hex = true;
                    item.fontName = embedded.resourceName;
                    item.fontSize = run.fontSize > 0.0 ? run.fontSize : fontSize;
                    item.color = run.color;
                    item.hasColor = run.color.isValid();
                    resolved.append(item);
                }
            } else {
                for (const FormTextRun& run : runs) {
                    const QString name = fontVariantName(fontName, run.bold, run.italic);
                    ensureFont(name);
                    ResolvedRun item;
                    item.text = run.text.toLatin1();
                    item.fontName = name;
                    item.fontSize = run.fontSize > 0.0 ? run.fontSize : fontSize;
                    item.color = run.color;
                    item.hasColor = run.color.isValid();
                    resolved.append(item);
                }
            }

            for (QPDFObjectHandle widget : field.widgets) {
                const QRectF rect = widgetRect(widget);
                if (rect.isEmpty()) continue;
                // 先建一个空的间接流，再在**同一个间接对象**上写数据与字典项。
                // 直接对象转间接时 qpdf 会复制一份，之后再改原 handle 不会进新对象
                // （踩过：写出来的外观流只剩 /Length）。
                QPDFObjectHandle streamRef = pdf.makeIndirectObject(pdf.newStream());
                const QByteArray appearanceContentBytes =
                    useLines ? appearanceLines(lineOperands, lineWidths, fontSize,
                                                         unicodeText ? embedded.resourceName : fontName,
                                                         unicodeText, QColor(), field.quadding,
                                                         rect.width(), rect.height())
                            : appearanceContent(resolved, rect.width(), rect.height());
                streamRef.replaceStreamData(appearanceContentBytes.toStdString(),
                                           QPDFObjectHandle::newNull(),
                                           QPDFObjectHandle::newNull());
                QPDFObjectHandle streamDict = streamRef.getDict();
                streamDict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
                streamDict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
                QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
                bbox.appendItem(QPDFObjectHandle::newReal(0));
                bbox.appendItem(QPDFObjectHandle::newReal(0));
                bbox.appendItem(QPDFObjectHandle::newReal(rect.width()));
                bbox.appendItem(QPDFObjectHandle::newReal(rect.height()));
                streamDict.replaceKey("/BBox", bbox);
                QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
                QPDFObjectHandle fontDict = QPDFObjectHandle::newDictionary();
                for (auto it = apFonts.constBegin(); it != apFonts.constEnd(); ++it) {
                    fontDict.replaceKey("/" + it.key().toStdString(), it.value());
                }
                resources.replaceKey("/Font", fontDict);
                streamDict.replaceKey("/Resources", resources);

                QPDFObjectHandle ap = widget.getKey("/AP");
                if (!ap.isDictionary()) {
                    ap = QPDFObjectHandle::newDictionary();
                    widget.replaceKey("/AP", ap);
                }
                ap.replaceKey("/N", streamRef);
                result.report.appearancesWritten += 1;
            }
            result.report.fieldsUpdated += 1;
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
    return result;
}

}  // namespace aipdf::pdf
