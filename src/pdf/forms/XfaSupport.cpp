#include "pdf/forms/XfaSupport.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <exception>
#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {

namespace {

#ifdef AIPDF_WITH_QPDF

// ---------------------------------------------------------------- 字段类型 ----

// XFA 模板里「控件元素名」→ 对外暴露的类型名。
struct TypeRule {
    const char* element;
    const char* type;
};

// 为什么靠控件元素名反推类型：XFA 不像 AcroForm 那样给字段写 /FT，
// 一个字段是什么类型完全体现在它挂了哪个控件元素上（<textEdit> / <checkBox>…），
// 控件元素就是类型的唯一来源，认不出来的控件只能老实说「未知」。
const TypeRule kTypeRules[] = {
    {"textEdit", "textField"},
    {"textInput", "textField"},
    {"passwordField", "textField"},
    {"checkBox", "checkBox"},
    {"radioButton", "radioButton"},
    {"choiceList", "choiceList"},
    {"dropDown", "choiceList"},
    {"listBox", "choiceList"},
    {"dateField", "dateField"},
    {"numericField", "numberField"},
    {"signatureField", "signatureField"},
    {"imageField", "imageField"},
};

// 动态逻辑节点名。命中这些就说明表单的行为取决于脚本，本程序不执行。
const char* const kDynamicNodes[] = {
    "script", "validation", "calculate", "bind", "rules", "event", "command",
};

QString typeForElement(const QString& element) {
    for (const TypeRule& rule : kTypeRules) {
        if (element == QLatin1String(rule.element)) return QString::fromLatin1(rule.type);
    }
    return QString();
}

bool isDynamicNode(const QString& element) {
    for (const char* node : kDynamicNodes) {
        if (element == QLatin1String(node)) return true;
    }
    return false;
}

void addMarker(QStringList* markers, const QString& marker) {
    if (markers == nullptr) return;
    if (!markers->contains(marker)) markers->append(marker);
}

// XML 属性值是不是「真」。
bool isTrueAttribute(const QString& value) {
    const QString lowered = value.trimmed().toLower();
    return lowered == QLatin1String("true") || lowered == QLatin1String("1") ||
           lowered == QLatin1String("yes");
}

// 取元素的局部名：XFA 里到处是带前缀的写法（xfa:field / xdp:datasets），
// 认节点时只关心冒号后面那半，前缀各家不一样，不可靠。
QString localElementName(const QXmlStreamReader& reader) {
    QString name = reader.name().toString();
    const int colon = name.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) name = name.mid(colon + 1);
    return name;
}

// ------------------------------------------------------------------ 解析 ----

// 解析过程中临时攒一个字段。
struct FieldAcc {
    QString name;
    QString label;
    QString type;
    QString value;
    bool required = false;
};

// 一个 XML 数据块的解析结果。
struct BlobResult {
    bool ok = false;
    QString error;
    QVector<FieldAcc> fields;
    QStringList markers;
};

// 读完一个元素子树，掏出里面的 <value><text>。
//
// 为什么需要它：XFA 的数据集里，值的元素名就是字段名本身
// （<xfa:TextField1> 而不是 <xfa:field name="TextField1">），
// 只能按「元素名匹配已知字段名」的办法把值认出来。
QString readValueFromSubtree(QXmlStreamReader& reader) {
    QStringList path;
    QString buffer;
    bool capturing = false;
    bool insideValue = false;
    QString result;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString local = localElementName(reader);
            if (local == QLatin1String("value")) insideValue = true;
            if (local == QLatin1String("text") && insideValue) {
                capturing = true;
                buffer.clear();
            }
            path.append(local);
        } else if (token == QXmlStreamReader::Characters ||
                   token == QXmlStreamReader::EntityReference) {
            if (capturing) buffer += reader.text().toString();
        } else if (token == QXmlStreamReader::EndElement) {
            const QString local = localElementName(reader);
            if (capturing && local == QLatin1String("text")) {
                const QString text = buffer.trimmed();
                if (!text.isEmpty() && result.isEmpty()) result = text;
                capturing = false;
                buffer.clear();
            }
            if (local == QLatin1String("value")) insideValue = false;
            if (!path.isEmpty()) path.removeLast();
            if (path.isEmpty()) break;
        }
    }
    return result;
}

// 读完一个 <field> 子树。
//
// reader 停在 <field> 的 StartElement 上；函数会把这个字段的整棵子树读完。
//
// 为什么自己走而不是用 readElementText：字段里嵌着 <ui> / <caption> / <value>，
// 我们要分别认出「标签」「当前值」「控件类型」，XML 树的形状本身就是信息。
void readFieldSubtree(QXmlStreamReader& reader, FieldAcc* field, QStringList* markers) {
    // 祖先链：末尾是当前元素，末尾的倒数第二个是父元素。
    QStringList path;
    path.append(QStringLiteral("field"));

    QString buffer;      // 当前文本节点的字符累积
    bool capturing = false;
    // 文本的归属：1=标签 2=字段自己的值 3=控件里的默认值 4=default 里的值
    int role = 0;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString local = localElementName(reader);
            if (isDynamicNode(local)) addMarker(markers, local);

            if (field->type.isEmpty()) {
                const QString type = typeForElement(local);
                if (!type.isEmpty()) field->type = type;
            }

            const QString parent = path.isEmpty() ? QString() : path.last();
            const QString grandParent = path.size() >= 2 ? path[path.size() - 2] : QString();

            if (local == QLatin1String("text")) {
                capturing = true;
                buffer.clear();
                if (parent == QLatin1String("caption")) {
                    role = 1;
                } else if (parent == QLatin1String("value") &&
                           grandParent == QLatin1String("caption")) {
                    role = 1;
                } else if (parent == QLatin1String("value") &&
                           grandParent == QLatin1String("field")) {
                    role = 2;
                } else if (parent == QLatin1String("value")) {
                    role = 3;
                } else if (parent == QLatin1String("default")) {
                    role = 4;
                } else {
                    role = 0;
                }
            }
            path.append(local);
        } else if (token == QXmlStreamReader::Characters ||
                   token == QXmlStreamReader::EntityReference) {
            if (capturing) buffer += reader.text().toString();
        } else if (token == QXmlStreamReader::EndElement) {
            const QString local = localElementName(reader);
            if (capturing && local == QLatin1String("text")) {
                const QString text = buffer.trimmed();
                if (!text.isEmpty()) {
                    if (role == 1 && field->label.isEmpty()) {
                        field->label = text;
                    } else if (role == 2 && field->value.isEmpty()) {
                        field->value = text;
                    } else if ((role == 3 || role == 4) && field->value.isEmpty()) {
                        field->value = text;
                    }
                }
                capturing = false;
                buffer.clear();
            }
            if (!path.isEmpty()) path.removeLast();
            if (path.isEmpty()) break;  // </field>
        }
    }
}

// 扫一个 XFA 数据块（XDP 包或单独的数据片段）。
BlobResult scanXfaXml(const QByteArray& data) {
    BlobResult result;

    QXmlStreamReader reader(data);
    // 故意不开命名空间处理：真实世界的 XFA 里常有没声明的前缀，
    // 开了反而会报「前缀未定义」；我们只按局部名认节点，用不着命名空间。
    reader.setNamespaceProcessing(false);

    QStringList path;      // 祖先链，用来判断「当前在不在 <datasets> 里」
    QStringList markers;
    int datasetsCount = 0;     // <datasets> 的嵌套层数（XDP 包里有 xdp:datasets 和 xfa:datasets 两层）
    bool datasetsHasLogic = false;
    QSet<QString> fieldNames;  // 已经见过的字段名，用来在数据集里按名字认值
    QVector<FieldAcc> fields;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString local = localElementName(reader);
            path.append(local);

            if (isDynamicNode(local)) {
                addMarker(&markers, local);
                if (datasetsCount > 0) datasetsHasLogic = true;
            }
            if (local == QLatin1String("datasets")) ++datasetsCount;
            if (local == QLatin1String("field")) {
                FieldAcc field;
                field.name = reader.attributes().value(QLatin1String("name")).toString().trimmed();
                field.required = isTrueAttribute(
                    reader.attributes().value(QLatin1String("required")).toString());
                readFieldSubtree(reader, &field, &markers);
                fields.append(field);
                if (!field.name.isEmpty()) fieldNames.insert(field.name);
                path.removeLast();  // </field> 已经被 readFieldSubtree 吃掉了
                continue;
            }
            // 数据集里的值元素用字段名做元素名：<xfa:TextField1>…</xfa:TextField1>。
            if (datasetsCount > 0 && fieldNames.contains(local)) {
                FieldAcc field;
                field.name = local;
                field.value = readValueFromSubtree(reader);
                fields.append(field);
                path.removeLast();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString local = localElementName(reader);
            if (local == QLatin1String("datasets") && datasetsCount > 0) --datasetsCount;
            if (!path.isEmpty()) path.removeLast();
        }
    }

    if (datasetsHasLogic) addMarker(&markers, QStringLiteral("datasets"));

    if (reader.hasError()) {
        result.ok = false;
        result.error = QStringLiteral("XFA 数据里的 XML 解析失败：%1（第 %2 行）")
                           .arg(reader.errorString())
                           .arg(reader.lineNumber());
        return result;
    }

    result.ok = true;
    result.fields = fields;
    result.markers = markers;
    return result;
}

// 取一个流对象的字节。
//
// 按值传：qpdf 的 getStreamData() 是非 const 方法。
QByteArray streamBytes(QPDFObjectHandle stream) {
    std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
    if (!data) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                      static_cast<int>(data->getSize()));
}

// 把一个 /XFA 取值摊平成若干个数据块。
//
// /XFA 按规范是个数组：第一个流是 XDP 包（里面带 <template>），
// 后面可能还有别的片段；也有生成器把它写成单个流，或者把它放在 /AcroForm 里。
// 两种位置都找，两种形态都收，才不至于漏掉真表单。
void collectXfaBlobs(const QPDFObjectHandle& source, QVector<QByteArray>* blobs) {
    if (source.isArray()) {
        const std::vector<QPDFObjectHandle> items = source.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (!item.isStream()) continue;
            blobs->append(streamBytes(item));
        }
    } else if (source.isStream()) {
        blobs->append(streamBytes(source));
    }
}

#endif  // AIPDF_WITH_QPDF

// ------------------------------------------------------------------ 导出 ----

// CSV 字段转义：含逗号、引号或换行时用双引号包起来，里面的引号写成两个。
QString csvEscape(const QString& value) {
    const bool needsQuote = value.contains(QLatin1Char(',')) ||
                            value.contains(QLatin1Char('"')) ||
                            value.contains(QLatin1Char('\n')) ||
                            value.contains(QLatin1Char('\r'));
    if (!needsQuote) return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

}  // namespace

QString XfaReport::summary() const {
    switch (status) {
    case XfaStatus::NotXfa:
        return QStringLiteral("这份 PDF 不是 XFA 表单：没有找到 /XFA 数据。");
    case XfaStatus::Static:
        return QStringLiteral("XFA 静态表单：共 %1 个字段（另有 %2 个没有名称的字段已跳过）。")
            .arg(fields.size())
            .arg(skippedWithoutName);
    case XfaStatus::Dynamic:
        return QStringLiteral("XFA 表单含动态逻辑：共 %1 个字段，命中 %2 处动态节点（%3）。"
                              "本程序不执行脚本，字段值只是模板里的静态内容。")
            .arg(fields.size())
            .arg(dynamicMarkers.size())
            .arg(dynamicMarkers.join(QStringLiteral("、")));
    case XfaStatus::Error:
        return QStringLiteral("XFA 表单解析失败：%1").arg(error);
    }
    return QString();
}

XfaReport inspectXfa(const QByteArray& pdfBytes, QString* error) {
    XfaReport report;

    if (pdfBytes.isEmpty()) {
        report.status = XfaStatus::Error;
        report.error = QStringLiteral("输入是空的，没有可检查的 XFA 数据。");
        if (error != nullptr) *error = report.error;
        return report;
    }

#ifndef AIPDF_WITH_QPDF
    report.status = XfaStatus::Error;
    report.error = QStringLiteral(
        "本次构建没有启用 qpdf，无法按对象定位 /XFA 数据（找 /XFA 需要真正的 PDF 解析器）。");
    if (error != nullptr) *error = report.error;
    return report;
#else
    try {
        QPDF qpdf;
        qpdf.setSuppressWarnings(true);
        qpdf.processMemoryFile("xfa-inspect.pdf", pdfBytes.constData(),
                               static_cast<size_t>(pdfBytes.size()));

        const QPDFObjectHandle root = qpdf.getRoot();
        QVector<QByteArray> blobs;
        collectXfaBlobs(root.getKey("/XFA"), &blobs);
        const QPDFObjectHandle acroForm = root.getKey("/AcroForm");
        if (acroForm.isDictionary()) {
            collectXfaBlobs(acroForm.getKey("/XFA"), &blobs);
        }

        if (blobs.isEmpty()) {
            // 不是 XFA 表单不是错误，只是一个事实。
            report.status = XfaStatus::NotXfa;
            report.isXfa = false;
            if (error != nullptr) error->clear();
            return report;
        }

        report.isXfa = true;

        QStringList markers;
        QVector<XfaField> fields;
        QHash<QString, int> indexByName;
        bool anyParsed = false;
        QString firstError;

        for (const QByteArray& blob : blobs) {
            if (blob.trimmed().isEmpty()) continue;
            const BlobResult parsed = scanXfaXml(blob);
            if (!parsed.ok) {
                // /XFA 数组里可能混着非 XML 的附件流，某个块坏了不代表整个表单坏了，
                // 所以先记下原因，继续看别的块。
                if (firstError.isEmpty()) firstError = parsed.error;
                continue;
            }
            anyParsed = true;
            for (const QString& marker : parsed.markers) addMarker(&markers, marker);

            for (const FieldAcc& acc : parsed.fields) {
                if (acc.name.isEmpty()) {
                    // 没有名字的字段没法导出成有意义的 CSV 行：跳过并计数。
                    ++report.skippedWithoutName;
                    continue;
                }
                const auto it = indexByName.constFind(acc.name);
                if (it == indexByName.constEnd()) {
                    indexByName.insert(acc.name, fields.size());
                    XfaField field;
                    field.name = acc.name;
                    field.label = acc.label.isEmpty() ? acc.name : acc.label;
                    field.type = acc.type.isEmpty() ? QStringLiteral("unknown") : acc.type;
                    field.value = acc.value;
                    field.required = acc.required;
                    fields.append(field);
                    continue;
                }
                // 同一个字段可能出现两次：模板里一次（带类型、标签），数据集里一次（带值）。
                // 后出现的值覆盖先出现的，这样 XDP 包里「模板在前、数据在后」的常见排布
                // 能拿到数据集里的真实值。
                XfaField& field = fields[it.value()];
                if (field.type == QLatin1String("unknown") && !acc.type.isEmpty()) {
                    field.type = acc.type;
                }
                if (field.label == field.name && !acc.label.isEmpty()) {
                    field.label = acc.label;
                }
                if (!acc.value.isEmpty()) field.value = acc.value;
                if (acc.required) field.required = true;
            }
        }

        if (!anyParsed) {
            report.status = XfaStatus::Error;
            report.error = firstError.isEmpty()
                               ? QStringLiteral("XFA 数据里没有能解析的 XML 内容。")
                               : firstError;
            if (error != nullptr) *error = report.error;
            return report;
        }

        report.fields = fields;
        report.dynamicMarkers = markers;
        report.hasDynamicLogic = !markers.isEmpty();
        report.status = report.hasDynamicLogic ? XfaStatus::Dynamic : XfaStatus::Static;
        if (error != nullptr) error->clear();
        return report;
    } catch (const std::exception& exception) {
        report.status = XfaStatus::Error;
        report.error = QStringLiteral("解析这份 PDF 失败：%1")
                           .arg(QString::fromUtf8(exception.what()));
        if (error != nullptr) *error = report.error;
        return report;
    }
#endif  // AIPDF_WITH_QPDF
}

QByteArray exportXfaFieldsToCsv(const XfaReport& report) {
    QStringList lines;
    lines.append(QStringLiteral("名称,标签,类型,当前值,是否必填"));
    for (const XfaField& field : report.fields) {
        QStringList cells;
        cells.append(csvEscape(field.name));
        cells.append(csvEscape(field.label));
        cells.append(csvEscape(field.type));
        cells.append(csvEscape(field.value));
        cells.append(field.required ? QStringLiteral("是") : QStringLiteral("否"));
        lines.append(cells.join(QLatin1Char(',')));
    }

    QByteArray out;
    // BOM：Windows 上的 Excel 靠 BOM 认 UTF-8，不然中文表头是乱码。
    out.append("\xEF\xBB\xBF");
    for (const QString& line : lines) {
        out.append(line.toUtf8());
        out.append("\r\n");
    }
    return out;
}

}  // namespace aipdf::pdf
