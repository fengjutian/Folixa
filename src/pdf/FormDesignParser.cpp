#include "pdf/FormDesignParser.h"

#include "i18n/Translate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace aipdf::pdf {
namespace {

// 坐标兜底值：模型漏给坐标时给一个能看懂的默认位置。
constexpr double kDefaultRelX = 0.1;
constexpr double kDefaultRelY = 0.1;
constexpr double kDefaultRelWidth = 0.3;
constexpr double kDefaultRelHeight = 0.03;

// 比例必须落在 0..1：模型偶尔会给出 85 这种百分数或负数。
double clampRatio(double value, double fallback) {
    if (value < 0.0 || value > 1.0) return fallback;
    return value;
}

}  // namespace

QString designSystemPrompt() {
    return QStringLiteral(
        "你是一个 PDF 表单设计助手。只输出一个 JSON 数组，不要任何解释文字，也不要用代码块包裹。\n"
        "数组的每一项形如：\n"
        "{\"name\":\"字段名（英文或中文均可，但不能为空）\","
        "\"tooltip\":\"给填表人看的提示\","
        "\"kind\":\"text 或 checkbox 或 radio 或 combo 或 list 或 button\","
        "\"x\":0.1,\"y\":0.2,\"width\":0.3,\"height\":0.03}\n"
        "x、y、width、height 都是相对页面尺寸的比例，取 0 到 1 之间的小数；"
        "y 从页面顶部往下算。同一行的字段 y 相同、x 依次增大。");
}

FormFieldKind kindFromName(const QString& name) {
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("checkbox")) return FormFieldKind::CheckBox;
    if (key == QLatin1String("radio")) return FormFieldKind::RadioButton;
    if (key == QLatin1String("combo")) return FormFieldKind::ComboBox;
    if (key == QLatin1String("list")) return FormFieldKind::ListBox;
    if (key == QLatin1String("button")) return FormFieldKind::PushButton;
    return FormFieldKind::Text;
}

QString kindName(FormFieldKind kind) {
    switch (kind) {
        case FormFieldKind::CheckBox:
            return QStringLiteral("checkbox");
        case FormFieldKind::RadioButton:
            return QStringLiteral("radio");
        case FormFieldKind::ComboBox:
            return QStringLiteral("combo");
        case FormFieldKind::ListBox:
            return QStringLiteral("list");
        case FormFieldKind::PushButton:
            return QStringLiteral("button");
        default:
            return QStringLiteral("text");
    }
}

QString stripCodeFence(const QString& text) {
    QString result = text.trimmed();
    if (!result.startsWith(QStringLiteral("```"))) return result;
    const int firstNewline = result.indexOf(QLatin1Char('\n'));
    if (firstNewline < 0) return result;
    const int lastFence = result.lastIndexOf(QStringLiteral("```"));
    if (lastFence <= firstNewline) return result;
    return result.mid(firstNewline + 1, lastFence - firstNewline - 1).trimmed();
}

bool parseDesignedFields(const QString& answer, QVector<DesignedFormField>* out, QString* error) {
    const QString text = stripCodeFence(answer);
    const int begin = text.indexOf(QLatin1Char('['));
    const int end = text.lastIndexOf(QLatin1Char(']'));
    if (begin < 0 || end <= begin) {
        if (error != nullptr) {
            // 上下文沿用「设计助手」对话框：这三句话原来就是对话框里的 tr()，
            // 换上下文会让翻译目录里已有的词条查不到（运行时直接退回中文原文）。
            *error = i18n::tr("aipdf::app::FormDesignAssistantDialog",
                              "模型没有返回可用的 JSON 字段清单。");
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(text.mid(begin, end - begin + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::FormDesignAssistantDialog",
                              "模型返回的字段清单解析失败：%1")
                         .arg(parseError.errorString());
        }
        return false;
    }
    QVector<DesignedFormField> fields;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        DesignedFormField field;
        field.name = name;
        field.toolTip = object.value(QStringLiteral("tooltip")).toString().trimmed();
        field.kind = kindFromName(object.value(QStringLiteral("kind")).toString());
        field.relX = object.value(QStringLiteral("x")).toDouble(kDefaultRelX);
        field.relY = object.value(QStringLiteral("y")).toDouble(kDefaultRelY);
        field.relWidth = object.value(QStringLiteral("width")).toDouble(kDefaultRelWidth);
        field.relHeight = object.value(QStringLiteral("height")).toDouble(kDefaultRelHeight);
        field.relX = clampRatio(field.relX, kDefaultRelX);
        field.relY = clampRatio(field.relY, kDefaultRelY);
        field.relWidth = clampRatio(field.relWidth, kDefaultRelWidth);
        field.relHeight = clampRatio(field.relHeight, kDefaultRelHeight);
        fields.append(field);
    }
    if (fields.isEmpty()) {
        if (error != nullptr) {
            *error = i18n::tr("aipdf::app::FormDesignAssistantDialog",
                              "模型没有给出任何字段。");
        }
        return false;
    }
    if (out != nullptr) *out = fields;
    return true;
}

}  // namespace aipdf::pdf
