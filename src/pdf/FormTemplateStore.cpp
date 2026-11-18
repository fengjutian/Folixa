#include "pdf/FormTemplateStore.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace aipdf::pdf {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyName = "name";
constexpr auto kKeyFields = "fields";
constexpr auto kKeyToolTip = "toolTip";
constexpr auto kKeyKind = "kind";
constexpr auto kKeyRelX = "relX";
constexpr auto kKeyRelY = "relY";
constexpr auto kKeyRelWidth = "relWidth";
constexpr auto kKeyRelHeight = "relHeight";
constexpr auto kKeyOptions = "options";
constexpr auto kKeyReadOnly = "readOnly";
constexpr auto kKeyRequired = "required";
constexpr auto kKeyMultiline = "multiline";
constexpr int kFormatVersion = 1;

void setError(QString* error, const QString& text) {
    if (error) {
        *error = text;
    }
}

// Windows 文件名里不能用的字符，保存时替换成下划线。
QString sanitizeStem(const QString& name) {
    static const QString illegal = QStringLiteral("\\/:*?\"<>|");
    QString stem = name;
    for (const QChar& character : illegal) {
        stem.replace(character, QLatin1Char('_'));
    }
    // Windows 不允许文件名以空格或点结尾，否则某些工具打不开。
    while (!stem.isEmpty() &&
           (stem.endsWith(QLatin1Char(' ')) || stem.endsWith(QLatin1Char('.')))) {
        stem.chop(1);
    }
    return stem;
}

// 域类型用字符串存，而不是枚举序号：以后往枚举中间插类型也不会把老模板读坏。
QString kindToText(FormFieldKind kind) {
    switch (kind) {
    case FormFieldKind::Text:
        return QStringLiteral("Text");
    case FormFieldKind::CheckBox:
        return QStringLiteral("CheckBox");
    case FormFieldKind::RadioButton:
        return QStringLiteral("RadioButton");
    case FormFieldKind::ComboBox:
        return QStringLiteral("ComboBox");
    case FormFieldKind::ListBox:
        return QStringLiteral("ListBox");
    case FormFieldKind::PushButton:
        return QStringLiteral("PushButton");
    case FormFieldKind::Barcode:
        return QStringLiteral("Barcode");
    }
    return QStringLiteral("Text");
}

bool kindFromText(const QString& text, FormFieldKind* out) {
    static const QHash<QString, FormFieldKind> kinds = {
        { QStringLiteral("Text"), FormFieldKind::Text },
        { QStringLiteral("CheckBox"), FormFieldKind::CheckBox },
        { QStringLiteral("RadioButton"), FormFieldKind::RadioButton },
        { QStringLiteral("ComboBox"), FormFieldKind::ComboBox },
        { QStringLiteral("ListBox"), FormFieldKind::ListBox },
        { QStringLiteral("PushButton"), FormFieldKind::PushButton },
        { QStringLiteral("Barcode"), FormFieldKind::Barcode },
    };
    const auto it = kinds.constFind(text);
    if (it == kinds.constEnd()) {
        return false;
    }
    if (out) {
        *out = it.value();
    }
    return true;
}

QByteArray readWholeFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

QStringList jsonFilesIn(const QString& directory) {
    QDir dir(directory);
    if (!dir.exists()) {
        return QStringList();
    }
    // 只认 .json：写模板时的临时文件是 .tmp，不会被当成模板。
    return dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
}

bool parseTemplate(const QByteArray& data, FormTemplate* out, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("模板文件不是有效的 JSON"));
        return false;
    }
    const QJsonObject root = document.object();
    const QString name = root.value(QString::fromLatin1(kKeyName)).toString().trimmed();
    if (name.isEmpty()) {
        setError(error, QStringLiteral("模板文件缺少名字"));
        return false;
    }

    FormTemplate tmpl;
    tmpl.name = name;
    const QJsonArray fields = root.value(QString::fromLatin1(kKeyFields)).toArray();
    for (const QJsonValue& value : fields) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();

        FormTemplateField field;
        field.kind = FormFieldKind::Text;
        if (object.contains(QString::fromLatin1(kKeyKind))) {
            FormFieldKind kind = FormFieldKind::Text;
            if (!kindFromText(object.value(QString::fromLatin1(kKeyKind)).toString(), &kind)) {
                // 认识的域类型才装进来；不认识的那个域跳过，模板其余部分照常可用。
                continue;
            }
            field.kind = kind;
        }
        field.name = object.value(QString::fromLatin1(kKeyName)).toString();
        field.toolTip = object.value(QString::fromLatin1(kKeyToolTip)).toString();
        field.relX = object.value(QString::fromLatin1(kKeyRelX)).toDouble(field.relX);
        field.relY = object.value(QString::fromLatin1(kKeyRelY)).toDouble(field.relY);
        field.relWidth = object.value(QString::fromLatin1(kKeyRelWidth)).toDouble(field.relWidth);
        field.relHeight = object.value(QString::fromLatin1(kKeyRelHeight)).toDouble(field.relHeight);
        for (const QJsonValue& option : object.value(QString::fromLatin1(kKeyOptions)).toArray()) {
            field.options.append(option.toString());
        }
        field.readOnly = object.value(QString::fromLatin1(kKeyReadOnly)).toBool(field.readOnly);
        field.required = object.value(QString::fromLatin1(kKeyRequired)).toBool(field.required);
        field.multiline = object.value(QString::fromLatin1(kKeyMultiline)).toBool(field.multiline);
        tmpl.fields.append(field);
    }

    if (out) {
        *out = tmpl;
    }
    return true;
}

QByteArray serializeTemplate(const FormTemplate& tmpl) {
    QJsonArray fields;
    for (const FormTemplateField& field : tmpl.fields) {
        QJsonObject object;
        object.insert(QString::fromLatin1(kKeyName), field.name);
        object.insert(QString::fromLatin1(kKeyToolTip), field.toolTip);
        object.insert(QString::fromLatin1(kKeyKind), kindToText(field.kind));
        object.insert(QString::fromLatin1(kKeyRelX), field.relX);
        object.insert(QString::fromLatin1(kKeyRelY), field.relY);
        object.insert(QString::fromLatin1(kKeyRelWidth), field.relWidth);
        object.insert(QString::fromLatin1(kKeyRelHeight), field.relHeight);
        QJsonArray options;
        for (const QString& option : field.options) {
            options.append(option);
        }
        object.insert(QString::fromLatin1(kKeyOptions), options);
        object.insert(QString::fromLatin1(kKeyReadOnly), field.readOnly);
        object.insert(QString::fromLatin1(kKeyRequired), field.required);
        object.insert(QString::fromLatin1(kKeyMultiline), field.multiline);
        fields.append(object);
    }

    QJsonObject root;
    root.insert(QString::fromLatin1(kKeyVersion), kFormatVersion);
    root.insert(QString::fromLatin1(kKeyName), tmpl.name);
    root.insert(QString::fromLatin1(kKeyFields), fields);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

// 模板名是否可用：空名或带路径分隔符的名字都不行，否则会写到目录外面去。
bool isUsableName(const QString& name, QString* error) {
    if (name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("模板名不能为空"));
        return false;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        setError(error, QStringLiteral("模板名不能包含路径分隔符"));
        return false;
    }
    if (sanitizeStem(name).isEmpty()) {
        setError(error, QStringLiteral("模板名不能只包含文件名非法字符"));
        return false;
    }
    return true;
}

}  // namespace

// 内置模板库：常用表单模板。
//
// 域的位置用相对比例（relX / relY / relWidth / relHeight），
// 套到 A4 或 Letter 上都对齐 —— 模板不该绑死一种纸张。
namespace {

// 造一个文本域。
FormTemplateField textField(const QString& name, const QString& tip, double x, double y,
                            double width, double height, bool multiline = false) {
    FormTemplateField field;
    field.name = name;
    field.toolTip = tip;
    field.kind = FormFieldKind::Text;
    field.relX = x;
    field.relY = y;
    field.relWidth = width;
    field.relHeight = height;
    field.multiline = multiline;
    return field;
}

// 造一个下拉域。
FormTemplateField comboField(const QString& name, const QString& tip, double x, double y,
                              double width, double height, const QStringList& options) {
    FormTemplateField field = textField(name, tip, x, y, width, height);
    field.kind = FormFieldKind::ComboBox;
    field.options = options;
    return field;
}

// 造一个复选框域。
FormTemplateField checkField(const QString& name, const QString& tip, double x, double y,
                              double width = 0.03, double height = 0.025) {
    FormTemplateField field = textField(name, tip, x, y, width, height);
    field.kind = FormFieldKind::CheckBox;
    return field;
}

}  // namespace

QVector<FormTemplate> FormTemplateStore::builtInTemplates() {
    QVector<FormTemplate> templates;

    FormTemplate registration;
    registration.name = QStringLiteral("登记表");
    registration.fields << textField(QStringLiteral("name"), QStringLiteral("姓名"), 0.12, 0.16,
                                     0.3, 0.035)
                        << comboField(QStringLiteral("gender"), QStringLiteral("性别"), 0.55, 0.16,
                                      0.15, 0.035,
                                      {QStringLiteral("男"), QStringLiteral("女")})
                        << textField(QStringLiteral("birthday"), QStringLiteral("出生日期"), 0.12, 0.24,
                                     0.3, 0.035)
                        << textField(QStringLiteral("phone"), QStringLiteral("联系电话"), 0.55, 0.24,
                                     0.3, 0.035)
                        << textField(QStringLiteral("email"), QStringLiteral("电子邮箱"), 0.12, 0.32,
                                     0.4, 0.035)
                        << textField(QStringLiteral("remark"), QStringLiteral("备注"), 0.12, 0.42,
                                     0.7, 0.1, true);
    templates.append(registration);

    FormTemplate signIn;
    signIn.name = QStringLiteral("签到表");
    signIn.fields << textField(QStringLiteral("name"), QStringLiteral("姓名"), 0.1, 0.2, 0.25, 0.035)
                  << textField(QStringLiteral("time"), QStringLiteral("签到时间"), 0.45, 0.2, 0.3,
                               0.035)
                  << textField(QStringLiteral("remark"), QStringLiteral("备注"), 0.1, 0.28, 0.65, 0.08,
                               true);
    templates.append(signIn);

    FormTemplate feedback;
    feedback.name = QStringLiteral("意见反馈表");
    feedback.fields << textField(QStringLiteral("content"), QStringLiteral("反馈内容"), 0.1, 0.18,
                                 0.75, 0.2, true)
                    << textField(QStringLiteral("contact"), QStringLiteral("联系人"), 0.1, 0.42, 0.3,
                                 0.035)
                    << textField(QStringLiteral("phone"), QStringLiteral("联系方式"), 0.45, 0.42,
                                 0.3, 0.035)
                    << checkField(QStringLiteral("reply"), QStringLiteral("愿意回访"), 0.1, 0.5);
    templates.append(feedback);

    FormTemplate expense;
    expense.name = QStringLiteral("费用报销单");
    expense.fields << textField(QStringLiteral("name"), QStringLiteral("申报人"), 0.1, 0.16, 0.3,
                                0.035)
                   << textField(QStringLiteral("department"), QStringLiteral("部门"), 0.5, 0.16, 0.3,
                                0.035)
                   << textField(QStringLiteral("date"), QStringLiteral("报销日期"), 0.1, 0.24, 0.3,
                                0.035)
                   << textField(QStringLiteral("amount"), QStringLiteral("金额"), 0.5, 0.24, 0.3,
                                0.035)
                   << textField(QStringLiteral("reason"), QStringLiteral("事由"), 0.1, 0.32, 0.7,
                                0.12, true);
    templates.append(expense);

    return templates;
}

bool FormTemplateStore::builtInTemplate(const QString& name, FormTemplate* out) {
    if (out == nullptr) {
        return false;
    }
    const QString wanted = name.trimmed();
    const QVector<FormTemplate> templates = builtInTemplates();
    for (const FormTemplate& tmpl : templates) {
        if (tmpl.name == wanted) {
            *out = tmpl;
            return true;
        }
    }
    return false;
}

FormTemplateStore::FormTemplateStore(const QString& rootDir)
    : m_rootDir(rootDir.isEmpty() ? defaultRootDir() : rootDir) {}

QString FormTemplateStore::defaultRootDir() {
    // 模板是用户自己的数据，放在应用数据目录而不是安装目录：安装目录可能只读，
    // 而且换机器重装后用户的模板不该跟着丢。
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("form-templates"));
}

QString FormTemplateStore::primaryFilePath(const QString& name) const {
    return QDir(m_rootDir).filePath(sanitizeStem(name.trimmed()) + QStringLiteral(".json"));
}

QStringList FormTemplateStore::templateNames() const {
    QStringList names;
    QSet<QString> seen;
    for (const QString& fileName : jsonFilesIn(m_rootDir)) {
        FormTemplate tmpl;
        if (!parseTemplate(readWholeFile(QDir(m_rootDir).filePath(fileName)), &tmpl, nullptr)) {
            // 坏文件只影响它自己：跳过，继续列其它模板。
            continue;
        }
        if (seen.contains(tmpl.name)) {
            continue;
        }
        seen.insert(tmpl.name);
        names.append(tmpl.name);
    }
    std::sort(names.begin(), names.end(),
              [](const QString& left, const QString& right) {
                  return left.localeAwareCompare(right) < 0;
              });
    return names;
}

bool FormTemplateStore::save(const FormTemplate& tmpl, QString* error) {
    FormTemplate normalized = tmpl;
    normalized.name = tmpl.name.trimmed();
    if (!isUsableName(normalized.name, error)) {
        return false;
    }

    if (!QDir(m_rootDir).exists() && !QDir().mkpath(m_rootDir)) {
        setError(error, QStringLiteral("无法创建模板目录：%1").arg(m_rootDir));
        return false;
    }

    QString path = primaryFilePath(normalized.name);
    if (QFile::exists(path)) {
        // 目标位置已经有文件：同名模板直接覆盖；但如果那个文件属于另一个模板
        // （名字里的非法字符被替换后撞了车），另存一个带序号的文件，
        // 别默默覆盖掉用户的另一个模板。
        FormTemplate existing;
        const bool parsed = parseTemplate(readWholeFile(path), &existing, nullptr);
        if (parsed && existing.name != normalized.name) {
            const QString stem = sanitizeStem(normalized.name);
            bool found = false;
            for (int index = 1; index < 1000; ++index) {
                const QString candidate = QDir(m_rootDir).filePath(
                    QStringLiteral("%1-%2.json").arg(stem).arg(index));
                if (!QFile::exists(candidate)) {
                    path = candidate;
                    found = true;
                    break;
                }
            }
            if (!found) {
                setError(error, QStringLiteral("模板名冲突：找不到可用的文件名"));
                return false;
            }
        }
    }

    // 原子替换交给 QSaveFile：它写同目录临时文件，commit() 时才替换目标。
    //
    // 不能“先删旧文件再 rename”：删掉旧文件之后、rename 成功之前崩溃或被
    // 杀软占用，用户的模板就彻底丢了（注释里那句“旧的模板还在”是假的）。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入模板文件：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray data = serializeTemplate(normalized);
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        setError(error, QStringLiteral("模板文件写入不完整"));
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("无法保存模板文件：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool FormTemplateStore::load(const QString& name, FormTemplate* out, QString* error) const {
    const QString wanted = name.trimmed();
    if (!isUsableName(wanted, error)) {
        return false;
    }

    const QString path = primaryFilePath(wanted);
    if (QFile::exists(path)) {
        FormTemplate tmpl;
        if (!parseTemplate(readWholeFile(path), &tmpl, error)) {
            // 文件在，但内容坏了：明确告诉调用方是坏文件，而不是"不存在"。
            return false;
        }
        if (tmpl.name == wanted) {
            if (out) {
                *out = tmpl;
            }
            return true;
        }
        // 这个位置属于另一个模板（撞名后另存过），继续找带序号的文件。
    }

    for (const QString& fileName : jsonFilesIn(m_rootDir)) {
        const QString candidate = QDir(m_rootDir).filePath(fileName);
        if (candidate == path) {
            continue;
        }
        FormTemplate tmpl;
        if (!parseTemplate(readWholeFile(candidate), &tmpl, nullptr)) {
            continue;
        }
        if (tmpl.name != wanted) {
            continue;
        }
        if (out) {
            *out = tmpl;
        }
        return true;
    }

    setError(error, QStringLiteral("模板不存在：%1").arg(wanted));
    return false;
}

bool FormTemplateStore::remove(const QString& name, QString* error) {
    const QString wanted = name.trimmed();
    if (!isUsableName(wanted, error)) {
        return false;
    }

    const QString path = primaryFilePath(wanted);
    if (QFile::exists(path)) {
        FormTemplate existing;
        const bool parsed = parseTemplate(readWholeFile(path), &existing, nullptr);
        if (!parsed) {
            // 坏掉的模板也要能删掉：不然用户没法清理它。
            if (QFile::remove(path)) {
                return true;
            }
            setError(error, QStringLiteral("删除模板文件失败"));
            return false;
        }
        if (existing.name == wanted) {
            if (QFile::remove(path)) {
                return true;
            }
            setError(error, QStringLiteral("删除模板文件失败"));
            return false;
        }
        // 这个位置属于别的模板，不能删，接着找带序号的文件。
    }

    for (const QString& fileName : jsonFilesIn(m_rootDir)) {
        const QString candidate = QDir(m_rootDir).filePath(fileName);
        if (candidate == path) {
            continue;
        }
        FormTemplate tmpl;
        if (!parseTemplate(readWholeFile(candidate), &tmpl, nullptr)) {
            continue;
        }
        if (tmpl.name != wanted) {
            continue;
        }
        if (QFile::remove(candidate)) {
            return true;
        }
        setError(error, QStringLiteral("删除模板文件失败"));
        return false;
    }

    setError(error, QStringLiteral("模板不存在：%1").arg(wanted));
    return false;
}

}  // namespace aipdf::pdf
