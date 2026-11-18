#include "doc/batch/ActionWizard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSettings>
#include <QUuid>

namespace aipdf::doc {
namespace {

// 失败统一走这里：把中文原因写进 error，返回 false。
void setError(QString* error, const QString& reason) {
    if (error != nullptr) *error = reason;
}

// 动作类型的三张表：枚举、英文名（JSON 里的稳定标识）、中文名（给人看的）。
//
// 为什么英文名与中文名分开存：中文名会改（文案润色），英文名是文件格式的一部分，
// 改了会让用户存好的向导读不出来。
struct ActionTypeEntry {
    WizardActionType type;
    const char* name;
    const char* title;
};

const ActionTypeEntry kActionTypes[] = {
    {WizardActionType::OpenFile, "openFile", "打开文件"},
    {WizardActionType::MergeDocuments, "mergeDocuments", "合并文档"},
    {WizardActionType::SplitDocument, "splitDocument", "拆分文档"},
    {WizardActionType::AddBookmark, "addBookmark", "添加书签"},
    {WizardActionType::AddPageNumbers, "addPageNumbers", "添加页码"},
    {WizardActionType::AddWatermark, "addWatermark", "添加水印"},
    {WizardActionType::Encrypt, "encrypt", "加密"},
    {WizardActionType::ConvertToFormat, "convertToFormat", "转换格式"},
    {WizardActionType::Print, "print", "打印"},
    {WizardActionType::End, "end", "结束"},
};

// 参数类型的英文名与中文名。
const char* paramTypeName(WizardParamType type) {
    switch (type) {
    case WizardParamType::String: return "string";
    case WizardParamType::StringList: return "stringList";
    case WizardParamType::Integer: return "integer";
    case WizardParamType::Number: return "number";
    case WizardParamType::Boolean: return "boolean";
    }
    return "string";
}

QString paramTypeTitle(WizardParamType type) {
    switch (type) {
    case WizardParamType::String: return QStringLiteral("字符串");
    case WizardParamType::StringList: return QStringLiteral("字符串列表");
    case WizardParamType::Integer: return QStringLiteral("整数");
    case WizardParamType::Number: return QStringLiteral("小数");
    case WizardParamType::Boolean: return QStringLiteral("布尔值");
    }
    return QStringLiteral("字符串");
}

// 一个参数的规格：叫什么、什么类型、必填与否、取值约束。
struct ParamSpec {
    QString name;
    WizardParamType type = WizardParamType::String;
    bool required = false;
    // 非空：字符串参数只能取这些值之一。
    QStringList allowed;
    // 取值范围（整数与小数参数用）。
    bool hasRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
};

// 页码位置白名单（加页码与加水印共用一套方位词）。
QStringList pagePositions() {
    return {QStringLiteral("topLeft"), QStringLiteral("topCenter"),
            QStringLiteral("topRight"), QStringLiteral("bottomLeft"),
            QStringLiteral("bottomCenter"), QStringLiteral("bottomRight")};
}

// 每个动作认识的参数。
//
// 为什么把参数表写死在这里：向导的价值就在于「这一步需要什么参数」是确定的。
// 有了这张表，校验才能说清「缺哪个参数」「类型对不对」，
// 而不是让上层各自解释一遍。
QVector<ParamSpec> specsFor(WizardActionType type) {
    switch (type) {
    case WizardActionType::OpenFile:
        return {ParamSpec{QStringLiteral("path"), WizardParamType::String, true}};
    case WizardActionType::MergeDocuments:
        return {ParamSpec{QStringLiteral("inputPaths"), WizardParamType::StringList, true},
                ParamSpec{QStringLiteral("outputPath"), WizardParamType::String, true}};
    case WizardActionType::SplitDocument:
        return {ParamSpec{QStringLiteral("outputDirectory"), WizardParamType::String, true},
                ParamSpec{QStringLiteral("mode"), WizardParamType::String, false,
                          QStringList{QStringLiteral("range"), QStringLiteral("each")}},
                ParamSpec{QStringLiteral("ranges"), WizardParamType::StringList, false}};
    case WizardActionType::AddBookmark:
        return {ParamSpec{QStringLiteral("title"), WizardParamType::String, true},
                ParamSpec{QStringLiteral("pageIndex"), WizardParamType::Integer, false, QStringList(), true,
                          1.0, 1000000.0}};
    case WizardActionType::AddPageNumbers:
        return {ParamSpec{QStringLiteral("position"), WizardParamType::String, true, pagePositions()},
                ParamSpec{QStringLiteral("startNumber"), WizardParamType::Integer, false, QStringList(), true,
                          1.0, 1000000.0}};
    case WizardActionType::AddWatermark:
        return {ParamSpec{QStringLiteral("text"), WizardParamType::String, true},
                ParamSpec{QStringLiteral("opacity"), WizardParamType::Number, false, QStringList(), true, 0.0,
                          1.0},
                ParamSpec{QStringLiteral("position"), WizardParamType::String, false,
                          pagePositions() + QStringList{QStringLiteral("center")}}};
    case WizardActionType::Encrypt:
        return {ParamSpec{QStringLiteral("userPassword"), WizardParamType::String, true},
                ParamSpec{QStringLiteral("ownerPassword"), WizardParamType::String, true},
                ParamSpec{QStringLiteral("allowPrinting"), WizardParamType::Boolean, false}};
    case WizardActionType::ConvertToFormat:
        return {ParamSpec{QStringLiteral("format"), WizardParamType::String, true,
                          QStringList{QStringLiteral("pdf"), QStringLiteral("docx"),
                                      QStringLiteral("xlsx"), QStringLiteral("pptx"),
                                      QStringLiteral("ofd"), QStringLiteral("txt"),
                                      QStringLiteral("md"), QStringLiteral("html")}},
                ParamSpec{QStringLiteral("outputPath"), WizardParamType::String, true}};
    case WizardActionType::Print:
        // 打印没有必填参数：打印到默认打印机什么参数都不需要。
        return {ParamSpec{QStringLiteral("printerName"), WizardParamType::String, false},
                ParamSpec{QStringLiteral("copies"), WizardParamType::Integer, false, QStringList(), true,
                          1.0, 99.0},
                ParamSpec{QStringLiteral("paperSize"), WizardParamType::String, false},
                ParamSpec{QStringLiteral("pageRange"), WizardParamType::String, false}};
    case WizardActionType::End:
    case WizardActionType::Unknown:
        return {};
    }
    return {};
}

// 找一份参数规格。找不到返回 nullptr。
const ParamSpec* findSpec(const QVector<ParamSpec>& specs, const QString& name) {
    for (const ParamSpec& spec : specs) {
        if (spec.name == name) return &spec;
    }
    return nullptr;
}

// 校验一步。index 是 0 基下标，报错时转成「第 N 步」。
bool validateStep(const WizardStep& step, int index, QString* error) {
    const QString position = QStringLiteral("第 %1 步").arg(index + 1);

    // 动作类型：以 typeName 为准（它是文件里存的东西），typeName 为空才看枚举。
    WizardActionType type = step.type;
    const QString rawName = step.typeName.trimmed();
    if (!rawName.isEmpty()) {
        bool ok = false;
        const WizardActionType parsed = wizardActionTypeFromName(rawName, &ok);
        if (!ok) {
            setError(error, QStringLiteral("%1的动作类型「%2」不受支持").arg(position, rawName));
            return false;
        }
        type = parsed;
    }
    if (type == WizardActionType::Unknown) {
        setError(error, QStringLiteral("%1的动作类型未知").arg(position));
        return false;
    }

    const QVector<ParamSpec> specs = specsFor(type);
    const QString actionTitle = wizardActionTypeTitle(type);

    for (const WizardParam& param : step.params) {
        const ParamSpec* spec = findSpec(specs, param.name);
        if (spec == nullptr) {
            setError(error, QStringLiteral("%1动作「%2」含有未知参数「%3」")
                              .arg(position, actionTitle, param.name));
            return false;
        }
        if (param.type != spec->type) {
            setError(error, QStringLiteral("%1动作「%2」的参数「%3」类型应为%4，实际为%5")
                              .arg(position, actionTitle, param.name, paramTypeTitle(spec->type),
                                   paramTypeTitle(param.type)));
            return false;
        }
        if (!spec->allowed.isEmpty() && !spec->allowed.contains(param.stringValue)) {
            setError(error,
                     QStringLiteral("%1动作「%2」的参数「%3」取值必须是 %4 之一，当前为「%5」")
                         .arg(position, actionTitle, param.name,
                              spec->allowed.join(QStringLiteral("、")), param.stringValue));
            return false;
        }
        if (spec->hasRange) {
            double value = 0.0;
            if (param.type == WizardParamType::Integer) {
                value = static_cast<double>(param.integerValue);
            } else if (param.type == WizardParamType::Number) {
                value = param.numberValue;
            }
            if (value < spec->minimum || value > spec->maximum) {
                setError(error, QStringLiteral("%1动作「%2」的参数「%3」取值必须在 %4 到 %5 之间，当前为 %6")
                                  .arg(position, actionTitle, param.name)
                                  .arg(spec->minimum)
                                  .arg(spec->maximum)
                                  .arg(value));
                return false;
            }
        }
        if (param.type == WizardParamType::String && spec->required &&
            param.stringValue.trimmed().isEmpty()) {
            setError(error, QStringLiteral("%1动作「%2」的参数「%3」不能为空")
                              .arg(position, actionTitle, param.name));
            return false;
        }
        if (param.type == WizardParamType::StringList && spec->required && param.listValue.isEmpty()) {
            setError(error, QStringLiteral("%1动作「%2」的参数「%3」不能是空列表")
                              .arg(position, actionTitle, param.name));
            return false;
        }
    }

    // 必填参数一个都不能少。
    for (const ParamSpec& spec : specs) {
        if (!spec.required) continue;
        bool found = false;
        for (const WizardParam& param : step.params) {
            if (param.name == spec.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            setError(error, QStringLiteral("%1动作「%2」缺少必填参数「%3」")
                              .arg(position, actionTitle, spec.name));
            return false;
        }
    }
    return true;
}

// 存向导的设置文件。
//
// 固定用 IniFormat（用户范围）：Windows 上默认的 NativeFormat 是注册表，
// 那是用户自己那份设置，单元测试碰不得；Ini 还能用 QSettings::setPath
// 把整个存储目录指到临时位置，测试因此可以完全隔离。
QSettings openSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("AI PDF Editor Project"), QStringLiteral("ActionWizards"));
}

// 设置文件里的分组：<settingsKey>/wizards/<id>。
QString wizardsGroup(const QString& settingsKey) {
    return settingsKey + QStringLiteral("/wizards");
}

QString wizardGroup(const QString& settingsKey, const QString& id) {
    return wizardsGroup(settingsKey) + QLatin1Char('/') + id;
}

}  // namespace

WizardParam wizardStringParam(const QString& name, const QString& value) {
    WizardParam param;
    param.name = name;
    param.type = WizardParamType::String;
    param.stringValue = value;
    return param;
}

WizardParam wizardStringListParam(const QString& name, const QStringList& value) {
    WizardParam param;
    param.name = name;
    param.type = WizardParamType::StringList;
    param.listValue = value;
    return param;
}

WizardParam wizardIntegerParam(const QString& name, qint64 value) {
    WizardParam param;
    param.name = name;
    param.type = WizardParamType::Integer;
    param.integerValue = value;
    return param;
}

WizardParam wizardNumberParam(const QString& name, double value) {
    WizardParam param;
    param.name = name;
    param.type = WizardParamType::Number;
    param.numberValue = value;
    return param;
}

WizardParam wizardBooleanParam(const QString& name, bool value) {
    WizardParam param;
    param.name = name;
    param.type = WizardParamType::Boolean;
    param.booleanValue = value;
    return param;
}

WizardStep makeWizardStep(WizardActionType type, const QVector<WizardParam>& params) {
    WizardStep step;
    step.type = type;
    step.typeName = wizardActionTypeName(type);
    step.params = params;
    return step;
}

QString wizardActionTypeName(WizardActionType type) {
    for (const ActionTypeEntry& entry : kActionTypes) {
        if (entry.type == type) return QString::fromLatin1(entry.name);
    }
    return QString();
}

QString wizardActionTypeTitle(WizardActionType type) {
    for (const ActionTypeEntry& entry : kActionTypes) {
        if (entry.type == type) return QString::fromUtf8(entry.title);
    }
    return QStringLiteral("未知动作");
}

WizardActionType wizardActionTypeFromName(const QString& name, bool* ok) {
    const QString trimmed = name.trimmed();
    for (const ActionTypeEntry& entry : kActionTypes) {
        // 大小写不敏感：外部导入的向导未必用规范大小写，
        // 但「认得出」比「严格」重要——真认不出时 ok 会是 false，调用方会报错。
        if (QString::fromLatin1(entry.name).compare(trimmed, Qt::CaseInsensitive) == 0) {
            if (ok != nullptr) *ok = true;
            return entry.type;
        }
    }
    if (ok != nullptr) *ok = false;
    return WizardActionType::Unknown;
}

bool validateActionWizard(const ActionWizard& wizard, QString* error) {
    if (error != nullptr) error->clear();
    if (wizard.name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("动作向导的名称不能为空"));
        return false;
    }
    if (wizard.steps.isEmpty()) {
        setError(error, QStringLiteral("动作向导「%1」没有任何步骤").arg(wizard.name));
        return false;
    }
    for (int i = 0; i < wizard.steps.size(); ++i) {
        if (!validateStep(wizard.steps.at(i), i, error)) return false;
    }
    return true;
}

QByteArray serializeActionWizard(const ActionWizard& wizard, QString* error) {
    if (error != nullptr) error->clear();

    QJsonObject root;
    root.insert(QStringLiteral("id"), wizard.id);
    root.insert(QStringLiteral("name"), wizard.name);
    root.insert(QStringLiteral("description"), wizard.description);

    QJsonArray steps;
    for (const WizardStep& step : wizard.steps) {
        QJsonObject stepObject;
        stepObject.insert(QStringLiteral("type"),
                          step.typeName.isEmpty() ? wizardActionTypeName(step.type) : step.typeName);
        if (!step.title.isEmpty()) stepObject.insert(QStringLiteral("title"), step.title);

        QJsonArray params;
        for (const WizardParam& param : step.params) {
            QJsonObject paramObject;
            paramObject.insert(QStringLiteral("name"), param.name);
            paramObject.insert(QStringLiteral("type"), QString::fromLatin1(paramTypeName(param.type)));
            switch (param.type) {
            case WizardParamType::String:
                paramObject.insert(QStringLiteral("value"), param.stringValue);
                break;
            case WizardParamType::StringList:
                paramObject.insert(QStringLiteral("value"), QJsonArray::fromStringList(param.listValue));
                break;
            case WizardParamType::Integer:
                paramObject.insert(QStringLiteral("value"), QJsonValue(param.integerValue));
                break;
            case WizardParamType::Number:
                paramObject.insert(QStringLiteral("value"), param.numberValue);
                break;
            case WizardParamType::Boolean:
                paramObject.insert(QStringLiteral("value"), param.booleanValue);
                break;
            }
            params.append(paramObject);
        }
        stepObject.insert(QStringLiteral("params"), params);
        steps.append(stepObject);
    }
    root.insert(QStringLiteral("steps"), steps);

    // Indented：向导是给人看也要给机器读的，缩进两格便于手工排查。
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool deserializeActionWizard(const QByteArray& json, ActionWizard* out, QString* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) {
        setError(error, QStringLiteral("读取动作向导失败：输出参数为空"));
        return false;
    }
    if (json.isEmpty()) {
        setError(error, QStringLiteral("读取动作向导失败：内容为空"));
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("动作向导不是合法的 JSON：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    ActionWizard wizard;
    wizard.id = root.value(QStringLiteral("id")).toString();
    wizard.name = root.value(QStringLiteral("name")).toString();
    wizard.description = root.value(QStringLiteral("description")).toString();

    const QJsonValue stepsValue = root.value(QStringLiteral("steps"));
    if (!stepsValue.isArray()) {
        setError(error, QStringLiteral("动作向导缺少步骤列表"));
        return false;
    }
    const QJsonArray steps = stepsValue.toArray();
    for (int i = 0; i < steps.size(); ++i) {
        const QJsonValue stepValue = steps.at(i);
        if (!stepValue.isObject()) {
            setError(error, QStringLiteral("动作向导的第 %1 步不是对象").arg(i + 1));
            return false;
        }
        const QJsonObject stepObject = stepValue.toObject();
        WizardStep step;
        step.typeName = stepObject.value(QStringLiteral("type")).toString();
        // 认不出来的动作类型也要留住原名：报错时必须说清是哪种动作不对。
        bool known = false;
        step.type = wizardActionTypeFromName(step.typeName, &known);
        if (!known) step.type = WizardActionType::Unknown;
        step.title = stepObject.value(QStringLiteral("title")).toString();

        const QJsonValue paramsValue = stepObject.value(QStringLiteral("params"));
        if (paramsValue.isArray()) {
            const QJsonArray params = paramsValue.toArray();
            for (int p = 0; p < params.size(); ++p) {
                const QJsonValue paramValue = params.at(p);
                if (!paramValue.isObject()) {
                    setError(error, QStringLiteral("动作向导第 %1 步的第 %2 个参数不是对象")
                                      .arg(i + 1)
                                      .arg(p + 1));
                    return false;
                }
                const QJsonObject paramObject = paramValue.toObject();
                WizardParam param;
                param.name = paramObject.value(QStringLiteral("name")).toString();
                const QString typeName = paramObject.value(QStringLiteral("type")).toString();
                const QJsonValue value = paramObject.value(QStringLiteral("value"));

                if (typeName == QLatin1String("string")) {
                    param.type = WizardParamType::String;
                    if (!value.isString()) {
                        setError(error, QStringLiteral("参数「%1」的值类型不对（应为字符串）").arg(param.name));
                        return false;
                    }
                    param.stringValue = value.toString();
                } else if (typeName == QLatin1String("stringList")) {
                    param.type = WizardParamType::StringList;
                    if (!value.isArray()) {
                        setError(error,
                                 QStringLiteral("参数「%1」的值类型不对（应为字符串列表）").arg(param.name));
                        return false;
                    }
                    const QJsonArray array = value.toArray();
                    for (const QJsonValue& item : array) param.listValue.append(item.toString());
                } else if (typeName == QLatin1String("integer")) {
                    param.type = WizardParamType::Integer;
                    if (!value.isDouble()) {
                        setError(error, QStringLiteral("参数「%1」的值类型不对（应为整数）").arg(param.name));
                        return false;
                    }
                    param.integerValue = static_cast<qint64>(value.toDouble());
                } else if (typeName == QLatin1String("number")) {
                    param.type = WizardParamType::Number;
                    if (!value.isDouble()) {
                        setError(error, QStringLiteral("参数「%1」的值类型不对（应为小数）").arg(param.name));
                        return false;
                    }
                    param.numberValue = value.toDouble();
                } else if (typeName == QLatin1String("boolean")) {
                    param.type = WizardParamType::Boolean;
                    if (!value.isBool()) {
                        setError(error, QStringLiteral("参数「%1」的值类型不对（应为布尔值）").arg(param.name));
                        return false;
                    }
                    param.booleanValue = value.toBool();
                } else {
                    setError(error, QStringLiteral("参数「%1」的类型「%2」不认识")
                                      .arg(param.name, typeName));
                    return false;
                }
                step.params.append(param);
            }
        }
        wizard.steps.append(step);
    }

    *out = wizard;
    return true;
}

bool saveActionWizard(ActionWizard* wizard, const QString& settingsKey, QString* error) {
    if (error != nullptr) error->clear();
    if (wizard == nullptr) {
        setError(error, QStringLiteral("保存动作向导失败：输出参数为空"));
        return false;
    }
    QString validationError;
    if (!validateActionWizard(*wizard, &validationError)) {
        setError(error, QStringLiteral("保存动作向导失败：%1").arg(validationError));
        return false;
    }
    if (settingsKey.trimmed().isEmpty()) {
        setError(error, QStringLiteral("保存动作向导失败：设置键不能为空"));
        return false;
    }
    if (wizard->id.contains(QLatin1Char('/'))) {
        setError(error, QStringLiteral("保存动作向导失败：向导 id 不能包含斜杠"));
        return false;
    }
    if (wizard->id.trimmed().isEmpty()) {
        // 调用方没给 id 就自己生成一个：向导列表靠 id 指认条目，
        // 没有 id 的向导存进去就再也找不回来了。
        wizard->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    const QByteArray json = serializeActionWizard(*wizard, error);
    if (json.isEmpty()) return false;

    QSettings settings = openSettings();
    settings.setValue(wizardGroup(settingsKey, wizard->id) + QStringLiteral("/json"),
                      QString::fromUtf8(json));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("保存动作向导失败：写入设置文件出错"));
        return false;
    }
    return true;
}

bool loadActionWizard(const QString& id, const QString& settingsKey, ActionWizard* out,
                       QString* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) {
        setError(error, QStringLiteral("读取动作向导失败：输出参数为空"));
        return false;
    }
    if (id.trimmed().isEmpty()) {
        setError(error, QStringLiteral("读取动作向导失败：向导 id 为空"));
        return false;
    }
    if (settingsKey.trimmed().isEmpty()) {
        setError(error, QStringLiteral("读取动作向导失败：设置键不能为空"));
        return false;
    }

    QSettings settings = openSettings();
    const QString group = wizardGroup(settingsKey, id);
    if (!settings.contains(group + QStringLiteral("/json"))) {
        setError(error, QStringLiteral("找不到动作向导：%1").arg(id));
        return false;
    }
    const QString json = settings.value(group + QStringLiteral("/json")).toString();
    QString parseError;
    if (!deserializeActionWizard(json.toUtf8(), out, &parseError)) {
        setError(error, QStringLiteral("读取动作向导失败：%1").arg(parseError));
        return false;
    }
    // 分组名就是 id：以设置文件里的为准，避免 JSON 里的 id 与实际位置对不上。
    out->id = id;
    return true;
}

QVector<ActionWizard> listActionWizards(const QString& settingsKey) {
    QVector<ActionWizard> wizards;
    if (settingsKey.trimmed().isEmpty()) return wizards;

    QSettings settings = openSettings();
    settings.beginGroup(wizardsGroup(settingsKey));
    QStringList ids = settings.childGroups();
    settings.endGroup();
    // 按 id 排序：childGroups() 的顺序不保证稳定，列表每次刷新顺序变了很难查。
    ids.sort();

    for (const QString& id : ids) {
        ActionWizard wizard;
        if (loadActionWizard(id, settingsKey, &wizard, nullptr)) wizards.append(wizard);
    }
    return wizards;
}

bool findActionWizardByName(const QString& settingsKey, const QString& name, ActionWizard* out) {
    if (out == nullptr) return false;
    if (name.trimmed().isEmpty()) return false;
    const QVector<ActionWizard> wizards = listActionWizards(settingsKey);
    for (const ActionWizard& wizard : wizards) {
        if (wizard.name == name) {
            *out = wizard;
            return true;
        }
    }
    return false;
}

bool deleteActionWizard(const QString& id, const QString& settingsKey) {
    if (id.trimmed().isEmpty() || settingsKey.trimmed().isEmpty()) return false;

    QSettings settings = openSettings();
    const QString group = wizardGroup(settingsKey, id);
    if (!settings.contains(group + QStringLiteral("/json"))) {
        // 删一个不存在的向导不是错误，但也确实没删掉任何东西：返回 false。
        return false;
    }
    settings.remove(group);
    settings.sync();
    return true;
}

WizardRunReport runActionWizard(const ActionWizard& wizard, const WizardStepExecutor& executor,
                                WizardFailurePolicy policy, QString* error) {
    WizardRunReport report;
    report.totalSteps = wizard.steps.size();
    if (error != nullptr) error->clear();

    QString validationError;
    if (!validateActionWizard(wizard, &validationError)) {
        // 校验不过就一步都不跑：跑了半截再报错，用户看到的报告是残的。
        if (error != nullptr) *error = validationError;
        report.summary = QStringLiteral("动作向导「%1」校验未通过，没有执行任何步骤：%2")
                             .arg(wizard.name, validationError);
        return report;
    }
    if (!executor) {
        const QString reason = QStringLiteral("没有提供执行回调");
        if (error != nullptr) *error = reason;
        report.summary = QStringLiteral("动作向导「%1」没有执行：%2").arg(wizard.name, reason);
        return report;
    }

    // 未执行步骤的原因：前面失败停下来，还是「结束」动作收尾，两种说法不一样。
    QString skipReason;
    for (int i = 0; i < wizard.steps.size(); ++i) {
        const WizardStep& step = wizard.steps.at(i);
        WizardStepResult result;
        result.index = i;
        result.type = step.type;
        result.typeName = step.typeName.isEmpty() ? wizardActionTypeName(step.type) : step.typeName;

        if (!skipReason.isEmpty()) {
            result.reason = skipReason;
            report.skippedSteps += 1;
            report.results.append(result);
            continue;
        }

        QString reason;
        const bool succeeded = executor(step, &reason);
        result.executed = true;
        result.succeeded = succeeded;
        report.executedSteps += 1;
        if (succeeded) {
            report.succeededSteps += 1;
        } else {
            report.failedSteps += 1;
            result.reason = reason.isEmpty() ? QStringLiteral("步骤执行失败（没有给出原因）") : reason;
            if (policy == WizardFailurePolicy::StopOnFirstFailure) {
                skipReason = QStringLiteral("前一步失败，后续步骤未执行");
            }
        }
        report.results.append(result);

        // 「结束」动作跑完就收尾：它是向导自己声明的终点，后面的步骤不再执行。
        if (succeeded && step.type == WizardActionType::End) {
            skipReason = QStringLiteral("向导已结束，后续步骤未执行");
        }
    }

    report.ok = report.failedSteps == 0;

    QStringList lines;
    lines.append(QStringLiteral("动作向导「%1」执行结果：共 %2 步，成功 %3 步，失败 %4 步，未执行 %5 步。")
                     .arg(wizard.name)
                     .arg(report.totalSteps)
                     .arg(report.succeededSteps)
                     .arg(report.failedSteps)
                     .arg(report.skippedSteps));
    for (const WizardStepResult& result : report.results) {
        const QString title = result.typeName.isEmpty() ? wizardActionTypeTitle(result.type)
                                                        : result.typeName;
        if (!result.executed) {
            lines.append(QStringLiteral("第 %1 步 %2：未执行（%3）")
                             .arg(result.index + 1)
                             .arg(title, result.reason));
        } else if (result.succeeded) {
            lines.append(QStringLiteral("第 %1 步 %2：成功").arg(result.index + 1).arg(title));
        } else {
            lines.append(QStringLiteral("第 %1 步 %2：失败（%3）")
                             .arg(result.index + 1)
                             .arg(title, result.reason));
        }
    }
    report.summary = lines.join(QLatin1Char('\n'));
    return report;
}

}  // namespace aipdf::doc
