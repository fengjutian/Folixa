#include "pdf/forms/XfaSupport.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

#include <cmath>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

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
    // 字段是不是只读（模板里 access="readOnly" / lock="true"）。
    bool readOnly = false;
};

// 一个 XML 数据块的解析结果。
struct BlobResult {
    bool ok = false;
    QString error;
    QVector<FieldAcc> fields;
    QStringList markers;
    // 只读字段的名字。
    QSet<QString> readOnlyFields;
    // 这一块里的脚本。
    QVector<XfaScriptUnit> scripts;
    // 这一块里有没有 <datasets>（有数据集才有值可写）。
    bool hasDatasets = false;
};

// ------------------------------------------------------------------ 脚本 ----

// 脚本的角色：看祖先里有没有 calculate / event / validation。
//
// 为什么按祖先判断：XFA 的脚本节点本身不说明自己是干什么的，
// 挂在 <calculate> 下面的是计算脚本，挂在 <event> 下面的是事件脚本。
QString scriptRole(const QStringList& path) {
    for (int i = path.size() - 1; i >= 0; --i) {
        const QString& name = path.at(i);
        if (name == QLatin1String("calculate")) return QStringLiteral("calculate");
        if (name == QLatin1String("event")) return QStringLiteral("event");
        if (name == QLatin1String("validation")) return QStringLiteral("validation");
        if (name == QLatin1String("field")) return QStringLiteral("script");
    }
    return QStringLiteral("script");
}

// 读完一个 <script> 子树，把里面的代码收成脚本单元。
//
// XFA 的脚本代码有两种放法：直接放在 <script> 的文本里，
// 或者放在 <exObject> 子元素里（生成器爱这么写，还带个名字）。
// 两种都收：放在 exObject 里时按名字分开记，名字对不上也不影响求值。
//
// reader 停在 <script> 的 StartElement 上；读完整个子树后返回。
void readScriptSubtree(QXmlStreamReader& reader, QVector<XfaScriptUnit>* units) {
    struct Capture {
        QString name;
        QString text;
    };
    QVector<Capture> captures;
    QString ownText;
    int openExObject = 0;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (localElementName(reader) == QLatin1String("exObject")) {
                Capture capture;
                capture.name = reader.attributes().value(QLatin1String("name")).toString().trimmed();
                captures.append(capture);
                ++openExObject;
            }
        } else if (token == QXmlStreamReader::Characters ||
                   token == QXmlStreamReader::EntityReference) {
            if (openExObject > 0 && !captures.isEmpty()) {
                captures.last().text += reader.text().toString();
            } else if (openExObject == 0) {
                ownText += reader.text().toString();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString local = localElementName(reader);
            if (local == QLatin1String("exObject") && openExObject > 0) --openExObject;
            if (local == QLatin1String("script")) break;
        }
    }
    if (captures.isEmpty()) {
        const QString code = ownText.trimmed();
        if (code.isEmpty()) return;
        XfaScriptUnit unit;
        unit.code = code;
        units->append(unit);
        return;
    }
    for (const Capture& capture : captures) {
        const QString code = capture.text.trimmed();
        if (code.isEmpty()) continue;
        XfaScriptUnit unit;
        unit.name = capture.name;
        unit.code = code;
        units->append(unit);
    }
}

// 字段是不是只读。
//
// 为什么看 access 和 lock 两个属性：不同生成器写的只读标记不一样，
// 模板里常见 access="readOnly"，也有生成器用 lock="true"。
bool isReadOnlyField(const QXmlStreamReader& reader) {
    const QString access =
        reader.attributes().value(QLatin1String("access")).toString().trimmed().toLower();
    if (access == QLatin1String("readonly") || access == QLatin1String("protected")) return true;
    const QString lock = reader.attributes().value(QLatin1String("lock")).toString().trimmed().toLower();
    return lock == QLatin1String("true") || lock == QLatin1String("1");
}

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
void readFieldSubtree(QXmlStreamReader& reader, FieldAcc* field, QStringList* markers,
                      QVector<XfaScriptUnit>* scripts) {
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
            if (local == QLatin1String("script")) {
                // 脚本整块读完：字段里的脚本归属于这个字段。
                const QString role = scriptRole(path);
                QVector<XfaScriptUnit> units;
                readScriptSubtree(reader, &units);
                for (XfaScriptUnit& unit : units) {
                    unit.owner = field->name;
                    unit.role = role;
                    scripts->append(unit);
                }
                continue;  // 路径不变：script 的子节点已经被整块吃掉
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
            if (local == QLatin1String("datasets")) {
                ++datasetsCount;
                result.hasDatasets = true;
            }
            if (local == QLatin1String("script")) {
                // 脚本整块读完：字段外的脚本（数据集里的计算脚本）在这里收。
                const QString role = scriptRole(path);
                QVector<XfaScriptUnit> units;
                readScriptSubtree(reader, &units);
                for (XfaScriptUnit& unit : units) {
                    unit.role = role;
                    result.scripts.append(unit);
                }
                path.removeLast();  // </script> 已经被 readScriptSubtree 吃掉了
                continue;
            }
            if (local == QLatin1String("field")) {
                FieldAcc field;
                field.name = reader.attributes().value(QLatin1String("name")).toString().trimmed();
                field.required = isTrueAttribute(
                    reader.attributes().value(QLatin1String("required")).toString());
                field.readOnly = isReadOnlyField(reader);
                if (field.readOnly && !field.name.isEmpty()) {
                    result.readOnlyFields.insert(field.name);
                }
                readFieldSubtree(reader, &field, &markers, &result.scripts);
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

// 一个 /XFA 数据块，连同它的流句柄（写回时要用句柄去替换流内容）。
struct XfaBlob {
    QPDFObjectHandle handle;
    QByteArray bytes;
};

void collectXfaBlobHandles(const QPDFObjectHandle& source, QVector<XfaBlob>* blobs) {
    if (source.isArray()) {
        const std::vector<QPDFObjectHandle> items = source.getArrayAsVector();
        for (const QPDFObjectHandle& item : items) {
            if (!item.isStream()) continue;
            XfaBlob blob;
            blob.handle = item;
            blob.bytes = streamBytes(item);
            blobs->append(blob);
        }
    } else if (source.isStream()) {
        XfaBlob blob;
        blob.handle = source;
        blob.bytes = streamBytes(source);
        blobs->append(blob);
    }
}

// 合并之后的字段。
struct MergedField {
    QString name;
    QString label;
    QString type;
    QString value;
    bool required = false;
    bool readOnly = false;
};

// 把各数据块解析出来的字段合并成一份清单。
//
// 为什么要合并：同一个字段会在模板里出现一次（带类型、标签），
// 在数据集里又出现一次（带值）。合并之后才是一份能用的字段表。
void mergeFields(const QVector<BlobResult>& parsed, QVector<MergedField>* fields,
                 int* skippedWithoutName) {
    QHash<QString, int> indexByName;
    for (const BlobResult& blob : parsed) {
        for (const FieldAcc& acc : blob.fields) {
            if (acc.name.isEmpty()) {
                // 没有名字的字段没法导出成有意义的 CSV 行：跳过并计数。
                ++(*skippedWithoutName);
                continue;
            }
            const auto it = indexByName.constFind(acc.name);
            if (it == indexByName.constEnd()) {
                indexByName.insert(acc.name, fields->size());
                MergedField field;
                field.name = acc.name;
                field.label = acc.label.isEmpty() ? acc.name : acc.label;
                field.type = acc.type.isEmpty() ? QStringLiteral("unknown") : acc.type;
                field.value = acc.value;
                field.required = acc.required;
                field.readOnly = acc.readOnly;
                fields->append(field);
                continue;
            }
            // 同一个字段可能出现两次：模板里一次（带类型、标签），数据集里一次（带值）。
            // 后出现的值覆盖先出现的，这样 XDP 包里「模板在前、数据在后」的常见排布
            // 能拿到数据集里的真实值。
            MergedField& field = (*fields)[it.value()];
            if (field.type == QLatin1String("unknown") && !acc.type.isEmpty()) {
                field.type = acc.type;
            }
            if (field.label == field.name && !acc.label.isEmpty()) {
                field.label = acc.label;
            }
            if (!acc.value.isEmpty()) field.value = acc.value;
            if (acc.required) field.required = true;
            if (acc.readOnly) field.readOnly = true;
        }
    }
}

const MergedField* findMergedField(const QVector<MergedField>& fields, const QString& name) {
    for (const MergedField& field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

// ------------------------------------------------------------ 数据集定位 ----

// 字符偏移 → 字节偏移的对照表。
//
// 为什么需要它：QXmlStreamReader 的 characterOffset() 数的是解码后的字符，
// 而我们要改的是原始字节里的那几个位置。中文一个字三个字节，
// 拿字符偏移直接当字节偏移会把 XML 改坏（改出来的是乱码，整个包解析失败）。
class CharToByteMap {
public:
    explicit CharToByteMap(const QString& text) : m_offsets(text.size() + 1) {
        int bytes = 0;
        for (int i = 0; i < text.size(); ++i) {
            m_offsets[i] = bytes;
            const QChar c = text.at(i);
            if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
                // 代理对：一个字符四个字节。
                m_offsets[i + 1] = bytes;
                bytes += 4;
                ++i;
            } else if (c.unicode() < 0x80) {
                bytes += 1;
            } else if (c.unicode() < 0x800) {
                bytes += 2;
            } else {
                bytes += 3;
            }
        }
        m_offsets[text.size()] = bytes;
    }

    int byteOffset(qint64 charOffset) const {
        if (charOffset <= 0) return 0;
        if (charOffset >= m_offsets.size()) return m_offsets.last();
        return m_offsets.at(static_cast<int>(charOffset));
    }

private:
    QVector<int> m_offsets;
};

// 字段在数据集里的位置（全是字符下标，不是字节偏移）。
struct DataNodeLocation {
    bool found = false;
    // 数据元素本身
    int elemStart = -1;     // 元素开始 '<' 的位置
    int elemEnd = -1;       // 元素结束的位置
    int startTagEnd = -1;   // 开始标签 '>' 之后
    bool selfClosing = false;
    QString qualifiedName;  // 元素名（带前缀，如 xfa:TextField1）
    // 值容器
    bool hasValue = false;
    int valueElemStart = -1;  // <value 的 '<'
    int valueEnd = -1;        // </value 的 '<'（插入点）；自闭合时是整个元素的结束
    bool valueSelfClosing = false;
    QString valueQualifiedName;
    // 文本
    bool hasText = false;
    int textElemStart = -1;  // <text 的 '<'
    int textElemEnd = -1;    // 自闭合文本元素的结束
    int textStart = -1;      // 文本内容的开始
    int textEnd = -1;        // 文本内容的结束（</text 的 '<'）
    bool textSelfClosing = false;
    QString textQualifiedName;
};

// 标签是不是自闭合（<x/>）。
//
// 怎么判断：XML 的属性值里不允许出现 '<'，所以从标签结尾往回找的最后一个 '<'
// 一定就是这个标签的开始，不会被属性值里的字符带偏。
bool tagIsSelfClosing(const QString& text, int tagEnd) {
    const int open = text.lastIndexOf(QLatin1Char('<'), tagEnd - 1);
    if (open < 0) return false;
    return text.mid(open, tagEnd - open).endsWith(QLatin1String("/>"));
}

// 读一个元素（以及它的子树），记下值节点的位置。
//
// reader 停在该元素的 StartElement 上；读完整个子树后返回。
void scanDataNode(QXmlStreamReader& reader, const QString& text, DataNodeLocation* loc) {
    // reader 给的偏移是「开始标签 '>' 之后」的位置。
    const int startTagEnd = static_cast<int>(reader.characterOffset());
    loc->elemStart = text.lastIndexOf(QLatin1Char('<'), startTagEnd - 1);
    loc->qualifiedName = reader.name().toString();
    loc->startTagEnd = startTagEnd;
    loc->elemEnd = startTagEnd;
    loc->selfClosing = tagIsSelfClosing(text, startTagEnd);
    if (loc->selfClosing) return;

    int depth = 0;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString local = localElementName(reader);
            const int tagEnd = static_cast<int>(reader.characterOffset());
            ++depth;
            if (depth == 1 && local == QLatin1String("value")) {
                loc->hasValue = true;
                loc->valueQualifiedName = reader.name().toString();
                loc->valueElemStart = text.lastIndexOf(QLatin1Char('<'), tagEnd - 1);
                loc->valueEnd = tagEnd;
                loc->valueSelfClosing = tagIsSelfClosing(text, tagEnd);
            } else if (depth == 2 && local == QLatin1String("text") && loc->hasValue) {
                loc->hasText = true;
                loc->textQualifiedName = reader.name().toString();
                loc->textElemStart = text.lastIndexOf(QLatin1Char('<'), tagEnd - 1);
                loc->textElemEnd = tagEnd;
                loc->textSelfClosing = tagIsSelfClosing(text, tagEnd);
                if (!loc->textSelfClosing) {
                    loc->textStart = tagEnd;  // 文本内容从开始标签之后开始
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString local = localElementName(reader);
            const int tagEnd = static_cast<int>(reader.characterOffset());
            if (depth == 2 && local == QLatin1String("text") && loc->hasText &&
                !loc->textSelfClosing) {
                // </text 的 '<' 位置：文本内容到这里为止。
                loc->textEnd = text.lastIndexOf(QLatin1Char('<'), tagEnd - 1);
            }
            if (depth == 1 && local == QLatin1String("value") && loc->hasValue &&
                !loc->valueSelfClosing) {
                loc->valueEnd = text.lastIndexOf(QLatin1Char('<'), tagEnd - 1);
            }
            if (depth == 0) {
                // 这是目标元素自己的结束标签。
                loc->elemEnd = tagEnd;
                break;
            }
            --depth;
        }
    }
}

// 在数据集里找到字段的数据节点。
//
// 先找 <data> 里的（真正的数据），找不到再退到 <datasets> 里的。
bool locateDataNode(const QString& text, const QString& fieldName, DataNodeLocation* out) {
    QXmlStreamReader reader(text);
    // 故意不开命名空间处理：真实世界的 XFA 里常有没声明的前缀。
    reader.setNamespaceProcessing(false);

    int datasetsDepth = 0;
    int dataDepth = 0;
    DataNodeLocation inDatasets;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString local = localElementName(reader);
            if (local == QLatin1String("datasets")) ++datasetsDepth;
            if (local == QLatin1String("data")) ++dataDepth;
            if (local == fieldName && datasetsDepth > 0) {
                DataNodeLocation location;
                scanDataNode(reader, text, &location);
                if (dataDepth > 0) {
                    *out = location;
                    return true;
                }
                if (!inDatasets.found) inDatasets = location;
                continue;  // scanDataNode 已经把整个元素吃掉了
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString local = localElementName(reader);
            if (local == QLatin1String("datasets") && datasetsDepth > 0) --datasetsDepth;
            if (local == QLatin1String("data") && dataDepth > 0) --dataDepth;
        }
    }

    if (inDatasets.found) {
        *out = inDatasets;
        return true;
    }
    return false;
}

// XML 转义：值里的 & < > 必须转义，不然整个 XFA 包会解析失败。
QString xmlEscape(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) {
            out += QLatin1String("&amp;");
        } else if (c == QLatin1Char('<')) {
            out += QLatin1String("&lt;");
        } else if (c == QLatin1Char('>')) {
            out += QLatin1String("&gt;");
        } else if (c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r') ||
                   c.unicode() >= 0x20) {
            // XML 1.0 不允许大部分控制字符，留着会让整个 XML 解析失败，只能丢掉。
            out += c;
        }
    }
    return out;
}

// 取元素名的前缀（"xfa:TextField1" → "xfa:"）。
QString namePrefix(const QString& qualifiedName) {
    const int colon = qualifiedName.lastIndexOf(QLatin1Char(':'));
    return colon < 0 ? QString() : qualifiedName.left(colon + 1);
}

// 把 XFA 数据块解码成文本。
//
// 为什么只支持 UTF-8：XFA 包按规范就是 UTF-8；
// 遇到 UTF-16（BOM 是 FF FE / FE FF）直接报原因，不去猜编码——
// 猜错了会把整个包改坏，比不改更糟。
bool decodeXfaText(const QByteArray& bytes, QString* text, QString* error) {
    if (bytes.startsWith(QByteArray("\xff\xfe", 2)) || bytes.startsWith(QByteArray("\xfe\xff", 2))) {
        if (error != nullptr) {
            *error = QStringLiteral("这份 XFA 数据是 UTF-16 编码的，本程序只改 UTF-8 的 XFA 数据包。");
        }
        return false;
    }
    *text = QString::fromUtf8(bytes);
    // 解码时出现的替换字符说明原来不是合法 UTF-8。
    if (text->contains(QChar(0xFFFD)) && !bytes.contains(QByteArray("\xef\xbf\xbd", 3))) {
        if (error != nullptr) {
            *error = QStringLiteral("这份 XFA 数据不是合法的 UTF-8 文本，无法安全改值。");
        }
        return false;
    }
    return true;
}

// 把值写进数据集的字节里。
//
// 只替换值所在的那几个字节：XML 的其它部分（前缀、缩进、节点顺序）一个字节都不动。
// 为什么不做重新序列化：重新序列化会把属性顺序、空元素写法、缩进全改掉，
// 生成的 PDF 看着还是 XFA，阅读器却未必还认。
QByteArray applyValueToBytes(const QByteArray& bytes, const QString& text, const CharToByteMap& map,
                              const DataNodeLocation& loc, const QString& value) {
    const QString escaped = xmlEscape(value);
    const QString prefix = namePrefix(loc.qualifiedName);
    const QString textName = prefix + QStringLiteral("text");
    const QString valueName = prefix + QStringLiteral("value");

    // 字符区间换算成字节区间再替换：中间的内容一个字节都不动。
    QByteArray out = bytes;
    const auto replaceRange = [&out, &map](int from, int to, const QString& with) {
        const int byteFrom = map.byteOffset(from);
        const int byteTo = map.byteOffset(to);
        out.replace(byteFrom, byteTo - byteFrom, with.toUtf8());
    };

    if (loc.hasText) {
        if (loc.textSelfClosing) {
            const QString q = loc.textQualifiedName;
            replaceRange(loc.textElemStart, loc.textElemEnd,
                         QStringLiteral("<") + q + QStringLiteral(">") + escaped + QStringLiteral("</") +
                             q + QStringLiteral(">"));
            return out;
        }
        replaceRange(loc.textStart, loc.textEnd, escaped);
        return out;
    }
    if (loc.hasValue) {
        if (loc.valueSelfClosing) {
            const QString q = loc.valueQualifiedName;
            const QString t = prefix + QStringLiteral("text");
            replaceRange(loc.valueElemStart, loc.valueEnd,
                         QStringLiteral("<") + q + QStringLiteral("><") + t + QStringLiteral(">") +
                             escaped + QStringLiteral("</") + t + QStringLiteral("></") + q +
                             QStringLiteral(">"));
            return out;
        }
        replaceRange(loc.valueEnd, loc.valueEnd,
                     QStringLiteral("<") + textName + QStringLiteral(">") + escaped +
                         QStringLiteral("</") + textName + QStringLiteral(">"));
        return out;
    }
    if (loc.selfClosing) {
        const QString q = loc.qualifiedName;
        replaceRange(loc.elemStart, loc.elemEnd,
                     QStringLiteral("<") + q + QStringLiteral("><") + valueName + QStringLiteral("><") +
                         textName + QStringLiteral(">") + escaped + QStringLiteral("</") + textName +
                         QStringLiteral("></") + valueName + QStringLiteral("></") + q +
                         QStringLiteral(">"));
        return out;
    }
    replaceRange(loc.startTagEnd, loc.startTagEnd,
                 QStringLiteral("<") + valueName + QStringLiteral("><") + textName + QStringLiteral(">") +
                     escaped + QStringLiteral("</") + textName + QStringLiteral("></") + valueName +
                     QStringLiteral(">"));
    return out;
}

// ---------------------------------------------------------- 值与字段类型 ----

bool looksLikeDate(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}([T ]\\d{2}:\\d{2}(:\\d{2})?)?$"));
    return pattern.match(text).hasMatch();
}

// 值跟字段类型对不对得上。
//
// 为什么当场拦：把 "abc" 写进数字字段，读出来的东西看着像数、其实不是，
// 后面每个用这个值的人都要踩坑；宁可现在报一句中文原因。
QString validateFieldValue(const QString& fieldName, const QString& type, const QString& value) {
    if (type == QLatin1String("numberField")) {
        bool ok = false;
        const double d = value.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(d)) {
            return QStringLiteral("字段「%1」是数字字段，值「%2」不是合法数字").arg(fieldName, value);
        }
        return QString();
    }
    if (type == QLatin1String("checkBox")) {
        const QString v = value.trimmed().toLower();
        if (v != QLatin1String("0") && v != QLatin1String("1") && v != QLatin1String("true") &&
            v != QLatin1String("false")) {
            return QStringLiteral("字段「%1」是复选框，值「%2」不合法（只能是 0 或 1）")
                .arg(fieldName, value);
        }
        return QString();
    }
    if (type == QLatin1String("dateField")) {
        if (!value.trimmed().isEmpty() && !looksLikeDate(value.trimmed())) {
            return QStringLiteral("字段「%1」是日期字段，值「%2」不是合法日期（应形如 2026-01-31）")
                .arg(fieldName, value);
        }
        return QString();
    }
    return QString();
}

// ---------------------------------------------------------------- 文档 ----

// 一份 XFA 文档的内部视图。
struct XfaDocument {
    bool isXfa = false;
    QVector<MergedField> fields;
    QVector<XfaScriptUnit> scripts;
    // 数据集在哪个数据块里（-1 表示这份表单没有数据集）。
    int datasetsBlob = -1;
    QVector<XfaBlob> blobs;
};

// 读一份 PDF 里的 XFA 数据：字段、脚本、数据集在哪个数据块。
void collectXfaDocument(QPDF& qpdf, XfaDocument* doc) {
    const QPDFObjectHandle root = qpdf.getRoot();
    collectXfaBlobHandles(root.getKey("/XFA"), &doc->blobs);
    const QPDFObjectHandle acroForm = root.getKey("/AcroForm");
    if (acroForm.isDictionary()) {
        collectXfaBlobHandles(acroForm.getKey("/XFA"), &doc->blobs);
    }
    if (doc->blobs.isEmpty()) return;
    doc->isXfa = true;

    QVector<BlobResult> parsed;
    for (int i = 0; i < doc->blobs.size(); ++i) {
        const BlobResult result = scanXfaXml(doc->blobs.at(i).bytes);
        if (!result.ok) continue;
        parsed.append(result);
        for (const XfaScriptUnit& unit : result.scripts) doc->scripts.append(unit);
        if (doc->datasetsBlob < 0 && result.hasDatasets) doc->datasetsBlob = i;
    }
    int skipped = 0;
    mergeFields(parsed, &doc->fields, &skipped);
}

// 把改好的 PDF 写出来。
bool writeOutPdf(QPDF& qpdf, QByteArray* outPdf, QString* error) {
    try {
        QPDFWriter writer(qpdf);
        writer.setOutputMemory();
        writer.write();
        const std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) *error = QStringLiteral("qpdf 没有写出内容。");
            return false;
        }
        if (outPdf != nullptr) {
            *outPdf = QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                                 static_cast<int>(buffer->getSize()));
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = QStringLiteral("写回 PDF 失败：%1").arg(QString::fromUtf8(exception.what()));
        }
        return false;
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
        QVector<BlobResult> parsedBlobs;
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
            parsedBlobs.append(parsed);
        }

        if (!anyParsed) {
            report.status = XfaStatus::Error;
            report.error = firstError.isEmpty()
                               ? QStringLiteral("XFA 数据里没有能解析的 XML 内容。")
                               : firstError;
            if (error != nullptr) *error = report.error;
            return report;
        }

        // 合并模板与数据集里的字段：模板给类型和标签，数据集给值。
        QVector<MergedField> merged;
        int skipped = 0;
        mergeFields(parsedBlobs, &merged, &skipped);
        report.skippedWithoutName = skipped;
        for (const MergedField& field : merged) {
            XfaField out;
            out.name = field.name;
            out.label = field.label;
            out.type = field.type;
            out.value = field.value;
            out.required = field.required;
            report.fields.append(out);
        }
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

// ---------------------------------------------------------------- 写值 ----

QString XfaWriteReport::summary() const {
    int changed = 0;
    QStringList problems;
    for (const XfaFieldUpdate& update : updates) {
        if (update.ok()) {
            ++changed;
        } else {
            problems.append(update.error);
        }
    }
    if (!error.isEmpty()) {
        return QStringLiteral("XFA 写值失败：%1").arg(error);
    }
    if (problems.isEmpty()) {
        return QStringLiteral("XFA 写值：改了 %1 个字段。").arg(changed);
    }
    return QStringLiteral("XFA 写值：改了 %1 个字段，%2 个没改成（%3）。")
        .arg(changed)
        .arg(problems.size())
        .arg(problems.join(QStringLiteral("；")));
}

XfaWriteReport writeXfaFieldValues(const QByteArray& pdfBytes,
                                  const QVector<QPair<QString, QString>>& values,
                                  QByteArray* outPdf, QString* error) {
    XfaWriteReport report;
    if (pdfBytes.isEmpty()) {
        report.error = QStringLiteral("输入是空的，没有可改的 XFA 数据。");
        if (error != nullptr) *error = report.error;
        return report;
    }

#ifndef AIPDF_WITH_QPDF
    report.error = QStringLiteral(
        "本次构建没有启用 qpdf，无法写回 XFA 数据（/XFA 是对象级的间接引用，只有真正的 "
        "PDF 解析器才能按对象定位并替换它）。");
    if (error != nullptr) *error = report.error;
    return report;
#else
    try {
        QPDF qpdf;
        qpdf.setSuppressWarnings(true);
        qpdf.processMemoryFile("xfa-edit.pdf", pdfBytes.constData(),
                               static_cast<size_t>(pdfBytes.size()));

        XfaDocument doc;
        collectXfaDocument(qpdf, &doc);
        if (!doc.isXfa) {
            report.error = QStringLiteral("这份 PDF 不是 XFA 表单：没有找到 /XFA 数据。");
            if (error != nullptr) *error = report.error;
            return report;
        }
        if (doc.datasetsBlob < 0) {
            report.error = QStringLiteral("这份 XFA 表单里没有数据集，字段值无处可写。");
            if (error != nullptr) *error = report.error;
            return report;
        }

        QByteArray blob = doc.blobs.at(doc.datasetsBlob).bytes;
        int changed = 0;
        for (const QPair<QString, QString>& item : values) {
            XfaFieldUpdate update;
            update.field = item.first;
            update.value = item.second;

            const MergedField* field = findMergedField(doc.fields, item.first);
            if (field == nullptr) {
                update.error = QStringLiteral("字段「%1」不存在这份 XFA 表单里").arg(item.first);
                report.updates.append(update);
                continue;
            }
            if (field->readOnly) {
                update.error = QStringLiteral("字段「%1」是只读字段，不能改值").arg(item.first);
                report.updates.append(update);
                continue;
            }
            const QString invalid = validateFieldValue(field->name, field->type, item.second);
            if (!invalid.isEmpty()) {
                update.error = invalid;
                report.updates.append(update);
                continue;
            }
            DataNodeLocation location;
            QString blobText;
            QString decodeError;
            if (!decodeXfaText(blob, &blobText, &decodeError)) {
                update.error = decodeError;
                report.updates.append(update);
                continue;
            }
            const CharToByteMap map(blobText);
            if (!locateDataNode(blobText, item.first, &location)) {
                update.error = QStringLiteral("字段「%1」在 XFA 数据集里没有数据节点，值无处可写")
                                   .arg(item.first);
                report.updates.append(update);
                continue;
            }
            blob = applyValueToBytes(blob, blobText, map, location, item.second);
            ++changed;
            report.updates.append(update);
        }

        if (changed == 0) {
            report.error = report.updates.isEmpty()
                               ? QStringLiteral("没有要改的字段。")
                               : QStringLiteral("没有字段被改动：%1").arg(report.updates.first().error);
            if (error != nullptr) *error = report.error;
            return report;
        }

        // 拷一份可写的句柄：qpdf 的改流接口不是 const 方法。
        QPDFObjectHandle datasetStream = doc.blobs.at(doc.datasetsBlob).handle;
        datasetStream.replaceStreamData(blob.toStdString(), QPDFObjectHandle::newNull(),
                                        QPDFObjectHandle::newNull());
        if (!writeOutPdf(qpdf, outPdf, &report.error)) {
            if (error != nullptr) *error = report.error;
            return report;
        }

        // 有失败的项就整体不算成功，但成功的项已经写进 PDF 了。
        report.ok = true;
        for (const XfaFieldUpdate& update : report.updates) {
            if (!update.ok()) report.ok = false;
        }
        report.error.clear();
        if (error != nullptr) error->clear();
        return report;
    } catch (const std::exception& exception) {
        report.error = QStringLiteral("写回这份 PDF 失败：%1")
                           .arg(QString::fromUtf8(exception.what()));
        if (error != nullptr) *error = report.error;
        return report;
    }
#endif  // AIPDF_WITH_QPDF
}

XfaWriteReport writeXfaFieldValue(const QByteArray& pdfBytes, const QString& fieldName,
                                  const QString& value, QByteArray* outPdf, QString* error) {
    return writeXfaFieldValues(pdfBytes, QVector<QPair<QString, QString>>{qMakePair(fieldName, value)},
                               outPdf, error);
}

// ---------------------------------------------------------------- 脚本 ----

XfaScriptListing listXfaScripts(const QByteArray& pdfBytes) {
    XfaScriptListing listing;
    if (pdfBytes.isEmpty()) {
        listing.error = QStringLiteral("输入是空的，没有可读的 XFA 脚本。");
        return listing;
    }

#ifndef AIPDF_WITH_QPDF
    listing.error = QStringLiteral(
        "本次构建没有启用 qpdf，无法按对象定位 /XFA 数据（找 /XFA 需要真正的 PDF 解析器）。");
    return listing;
#else
    try {
        QPDF qpdf;
        qpdf.setSuppressWarnings(true);
        qpdf.processMemoryFile("xfa-scripts.pdf", pdfBytes.constData(),
                               static_cast<size_t>(pdfBytes.size()));
        XfaDocument doc;
        collectXfaDocument(qpdf, &doc);
        if (!doc.isXfa) {
            listing.error = QStringLiteral("这份 PDF 不是 XFA 表单：没有找到 /XFA 数据。");
            return listing;
        }
        listing.ok = true;
        listing.scripts = doc.scripts;
        return listing;
    } catch (const std::exception& exception) {
        listing.error = QStringLiteral("读这份 PDF 的 XFA 脚本失败：%1")
                            .arg(QString::fromUtf8(exception.what()));
        return listing;
    }
#endif  // AIPDF_WITH_QPDF
}

}  // namespace aipdf::pdf
