#include "pdf/forms/FormAppearance.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

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
};

struct FieldContext {
    QString prefix;
    QString inheritedDa;
};

void collectFields(const QPDFObjectHandle& node, const FieldContext& parent,
                   QHash<QString, RawField>* out) {
    if (!node.isDictionary()) return;
    FieldContext context = parent;
    if (node.hasKey("/DA")) context.inheritedDa = objectText(node.getKey("/DA"));

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
};

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
        content += "(" + escapePdfString(run.text) + ") Tj\n";
    }
    content += "ET\n";
    content += "Q\n";
    Q_UNUSED(width)
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
            // 单字节编码检查：超出 0xFF 的字符需要嵌入字体，这一层不接。
            for (uint point : plain.toUcs4()) {
                if (point > 0xFF) {
                    result.report.error =
                        QStringLiteral("字段 %1 的值里有非单字节字符（U+%2），"
                                       "需要嵌入字体，本层不支持")
                            .arg(update.fieldName)
                            .arg(point, 4, 16, QLatin1Char('0'));
                    return result;
                }
            }
            const QByteArray encoded = plain.toLatin1();
            field.node.replaceKey("/V", QPDFObjectHandle::newString(encoded.toStdString()));

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
            if (runs.isEmpty()) {
                ensureFont(fontName);
                resolved.append(ResolvedRun{encoded, fontName, fontSize, QColor(), false});
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
                streamRef.replaceStreamData(
                    appearanceContent(resolved, rect.width(), rect.height()).toStdString(),
                    QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
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
