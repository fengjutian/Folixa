#include "pdf/forms/FormRuntime.h"

#include "pdf/forms/FormJsEvaluator.h"
#include "pdf/forms/QuickJsFormScript.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <algorithm>
#include <memory>

#include <QHash>

namespace aipdf::pdf {
namespace {

#ifdef AIPDF_WITH_QPDF

QString objectName(const QPDFObjectHandle& object) {
    if (!object.isName()) return QString();
    std::string name = object.getName();
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    return QString::fromStdString(name);
}

QString objectText(QPDFObjectHandle object) {
    if (object.isString()) return QString::fromStdString(object.getStringValue());
    if (object.isName()) return objectName(object);
    if (object.isNumber()) return QString::number(object.getNumericValue());
    return QString();
}

QStringList optionValues(QPDFObjectHandle options) {
    QStringList values;
    if (!options.isArray()) return values;
    const int count = options.getArrayNItems();
    for (int i = 0; i < count; ++i) {
        const QPDFObjectHandle item = options.getArrayItem(i);
        if (item.isArray() && item.getArrayNItems() >= 2) {
            // [导出值 显示值]：值取第一个，与 Acrobat 一致。
            values.append(objectText(item.getArrayItem(0)));
        } else {
            values.append(objectText(item));
        }
    }
    return values;
}

QString scriptText(QPDFObjectHandle actionOrAa) {
    if (actionOrAa.isString()) return QString::fromStdString(actionOrAa.getStringValue());
    if (actionOrAa.isStream()) {
        const std::shared_ptr<Buffer> data = actionOrAa.getStreamData(qpdf_dl_generalized);
        if (!data) return QString();
        return QString::fromUtf8(reinterpret_cast<const char*>(data->getBuffer()),
                                 static_cast<int>(data->getSize()));
    }
    if (!actionOrAa.isDictionary()) return QString();
    QPDFObjectHandle js = actionOrAa.getKey("/JS");
    if (js.isString()) return QString::fromStdString(js.getStringValue());
    if (js.isStream()) {
        const std::shared_ptr<Buffer> data = js.getStreamData(qpdf_dl_generalized);
        if (!data) return QString();
        return QString::fromUtf8(reinterpret_cast<const char*>(data->getBuffer()),
                                 static_cast<int>(data->getSize()));
    }
    return QString();
}

void readFieldScripts(const QPDFObjectHandle& node, FormFieldScript* script) {
    const QPDFObjectHandle aa = node.getKey("/AA");
    if (aa.isDictionary()) {
        script->keystroke = scriptText(aa.getKey("/K"));
        script->format = scriptText(aa.getKey("/F"));
        script->validate = scriptText(aa.getKey("/V"));
        script->calculate = scriptText(aa.getKey("/C"));
    }
    script->action = scriptText(node.getKey("/A"));
}

FormFieldType typeFromName(const QString& name) {
    if (name == QLatin1String("Tx")) return FormFieldType::Text;
    if (name == QLatin1String("Btn")) return FormFieldType::Button;
    if (name == QLatin1String("Ch")) return FormFieldType::Choice;
    if (name == QLatin1String("Sig")) return FormFieldType::Signature;
    return FormFieldType::Unknown;
}

qint64 objectKey(const QPDFObjectHandle& object) {
    const QPDFObjGen id = object.getObjGen();
    if (id.getObj() <= 0) return 0;
    return (static_cast<qint64>(id.getObj()) << 32) | static_cast<quint32>(id.getGen());
}

// 从字段对象沿 /Parent 往上拼全名。/CO 里存的是字段对象引用，不是字段名。
QString fullyQualifiedName(QPDFObjectHandle node) {
    QStringList parts;
    for (int depth = 0; depth < 64 && node.isDictionary(); ++depth) {
        if (node.hasKey("/T")) {
            const QString part = objectText(node.getKey("/T"));
            if (!part.isEmpty()) parts.prepend(part);
        }
        node = node.getKey("/Parent");
    }
    return parts.join(QLatin1Char('.'));
}

// 继承下来的域属性：/FT、/Ff、/V、/DV、/Opt、/DA、/TU 都可以挂在祖先上。
struct Inherited {
    QString fieldType;
    int flags = 0;
    QPDFObjectHandle value;
    QPDFObjectHandle defaultValue;
    QPDFObjectHandle options;
    QString appearance;
    QString tooltip;
};

struct ParseContext {
    QHash<qint64, int> widgetPage;
    QVector<FormFieldState>* fields = nullptr;
};

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

bool hasFieldChild(const QVector<QPDFObjectHandle>& kids) {
    for (const QPDFObjectHandle& kid : kids) {
        if (kid.isDictionary() && kid.hasKey("/T")) return true;
    }
    return false;
}

QPDFObjectHandle firstWidget(const QVector<QPDFObjectHandle>& kids) {
    for (const QPDFObjectHandle& kid : kids) {
        if (isWidget(kid)) return kid;
    }
    return {};
}

QRectF widgetRect(const QPDFObjectHandle& widget) {
    const QPDFObjectHandle rect = widget.getKey("/Rect");
    if (!rect.isArray() || rect.getArrayNItems() < 4) return {};
    double values[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        const QPDFObjectHandle item = rect.getArrayItem(i);
        if (!item.isNumber()) return {};
        values[i] = item.getNumericValue();
    }
    const double left = qMin(values[0], values[2]);
    const double bottom = qMin(values[1], values[3]);
    return QRectF(left, bottom, qAbs(values[2] - values[0]), qAbs(values[3] - values[1]));
}

int pageOfWidget(const QPDFObjectHandle& widget, const ParseContext& context) {
    const qint64 key = objectKey(widget);
    if (key != 0) {
        const auto found = context.widgetPage.constFind(key);
        if (found != context.widgetPage.constEnd()) return found.value();
    }
    const QPDFObjectHandle page = widget.getKey("/P");
    const qint64 pageKey = objectKey(page);
    if (pageKey != 0) {
        const auto found = context.widgetPage.constFind(pageKey);
        if (found != context.widgetPage.constEnd()) return found.value();
    }
    return -1;
}

void collectField(const QPDFObjectHandle& node, const QString& prefix, const Inherited& parent,
                  ParseContext* context) {
    if (!node.isDictionary()) return;
    Inherited inherited = parent;
    if (node.hasKey("/FT")) inherited.fieldType = objectName(node.getKey("/FT"));
    if (node.hasKey("/Ff")) inherited.flags = static_cast<int>(node.getKey("/Ff").getIntValue());
    if (node.hasKey("/V")) inherited.value = node.getKey("/V");
    if (node.hasKey("/DV")) inherited.defaultValue = node.getKey("/DV");
    if (node.hasKey("/Opt")) inherited.options = node.getKey("/Opt");
    if (node.hasKey("/DA")) inherited.appearance = objectText(node.getKey("/DA"));
    if (node.hasKey("/TU")) inherited.tooltip = objectText(node.getKey("/TU"));

    QString name = prefix;
    if (node.hasKey("/T")) {
        const QString part = objectText(node.getKey("/T"));
        if (!part.isEmpty()) name = prefix.isEmpty() ? part : prefix + QLatin1Char('.') + part;
    }

    const QVector<QPDFObjectHandle> kids = childObjects(node);
    const bool terminal = !inherited.fieldType.isEmpty() &&
                          (kids.isEmpty() || !hasFieldChild(kids));
    if (terminal && !name.isEmpty()) {
        FormFieldState field;
        field.name = name;
        field.type = typeFromName(inherited.fieldType);
        field.flags = inherited.flags;
        field.readOnly = (field.flags & 1) != 0;
        field.required = (field.flags & 2) != 0;
        field.multiline = (field.flags & (1 << 12)) != 0;
        field.value = objectText(inherited.value);
        field.defaultValue = objectText(inherited.defaultValue);
        field.formattedValue = field.value;
        field.committedValue = field.value;
        field.options = optionValues(inherited.options);
        field.tooltip = inherited.tooltip;
        field.appearance = inherited.appearance;
        readFieldScripts(node, &field.script);

        const QPDFObjectHandle widget = isWidget(node) ? node : firstWidget(kids);
        if (widget.isDictionary()) {
            field.rect = widgetRect(widget);
            field.pageIndex = pageOfWidget(widget, *context);
        }
        context->fields->append(field);
        return;
    }

    for (const QPDFObjectHandle& kid : kids) {
        collectField(kid, name, inherited, context);
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QString formEventName(FormEventType type) {
    switch (type) {
    case FormEventType::Focus: return QStringLiteral("Focus");
    case FormEventType::Blur: return QStringLiteral("Blur");
    case FormEventType::Keystroke: return QStringLiteral("Keystroke");
    case FormEventType::Validate: return QStringLiteral("Validate");
    case FormEventType::Calculate: return QStringLiteral("Calculate");
    case FormEventType::Format: return QStringLiteral("Format");
    case FormEventType::MouseDown: return QStringLiteral("MouseDown");
    case FormEventType::MouseUp: return QStringLiteral("MouseUp");
    case FormEventType::PageOpen: return QStringLiteral("PageOpen");
    case FormEventType::PageClose: return QStringLiteral("PageClose");
    case FormEventType::DocOpen: return QStringLiteral("DocOpen");
    case FormEventType::DocClose: return QStringLiteral("DocClose");
    case FormEventType::WillSave: return QStringLiteral("WillSave");
    case FormEventType::DidSave: return QStringLiteral("DidSave");
    }
    return QString();
}

bool FormFieldState::isRadio() const {
    return type == FormFieldType::Button && (flags & (1 << 15)) != 0;
}

bool FormFieldState::isPushButton() const {
    return type == FormFieldType::Button && (flags & (1 << 16)) != 0;
}

bool FormFieldState::isCheckBox() const {
    return type == FormFieldType::Button && !isRadio() && !isPushButton();
}

bool FormFieldState::isCombo() const {
    return type == FormFieldType::Choice && (flags & (1 << 17)) != 0;
}

FormRuntime FormRuntime::load(const QByteArray& pdfBytes, QString* error) {
    FormRuntime runtime;
    if (error) error->clear();
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("form-runtime.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        runtime.valid_ = true;

        // 先建「widget → 页码」表，字段的 /Rect 落在哪一页由它决定。
        ParseContext context;
        context.fields = &runtime.fields_;
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        for (size_t index = 0; index < pages.size(); ++index) {
            QPDFObjectHandle page = pages.at(index);
            const QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isArray()) continue;
            const int count = annots.getArrayNItems();
            for (int i = 0; i < count; ++i) {
                const qint64 key = objectKey(annots.getArrayItem(i));
                if (key != 0) context.widgetPage.insert(key, static_cast<int>(index));
            }
        }

        QPDFObjectHandle acroForm = pdf.getRoot().getKey("/AcroForm");
        if (!acroForm.isDictionary()) return runtime;

        if (const QPDFObjectHandle order = acroForm.getKey("/CO"); order.isArray()) {
            const int count = order.getArrayNItems();
            for (int i = 0; i < count; ++i) {
                const QPDFObjectHandle item = order.getArrayItem(i);
                // 规范写的是字段对象引用；也接受个别实现写的字符串名字。
                const QString name = item.isString() ? objectText(item) : fullyQualifiedName(item);
                if (!name.isEmpty()) runtime.calculationOrder_.append(name);
            }
        }

        const QPDFObjectHandle fields = acroForm.getKey("/Fields");
        if (fields.isArray()) {
            const int count = fields.getArrayNItems();
            for (int i = 0; i < count; ++i) {
                collectField(fields.getArrayItem(i), QString(), Inherited(), &context);
            }
        }
    } catch (const std::exception& exception) {
        runtime.valid_ = false;
        runtime.fields_.clear();
        runtime.error_ = QString::fromUtf8(exception.what());
    }
#else
    Q_UNUSED(pdfBytes)
    runtime.error_ = QStringLiteral("qpdf is unavailable");
#endif
    if (!runtime.valid_ && error) *error = runtime.error_;
    return runtime;
}

const FormFieldState* FormRuntime::field(const QString& name) const {
    for (const FormFieldState& field : fields_) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

FormFieldState* FormRuntime::mutableField(const QString& name) {
    for (FormFieldState& field : fields_) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

QString FormRuntime::value(const QString& name) const {
    const FormFieldState* found = field(name);
    return found != nullptr ? found->value : QString();
}

QString FormRuntime::formattedValue(const QString& name) const {
    const FormFieldState* found = field(name);
    return found != nullptr ? found->formattedValue : QString();
}

QVector<FormFieldValue> FormRuntime::fieldValues() const {
    QVector<FormFieldValue> values;
    values.reserve(fields_.size());
    for (const FormFieldState& field : fields_) {
        values.append(FormFieldValue{field.name, field.value});
    }
    return values;
}

void FormRuntime::record(FormEventType type, const QString& fieldName, bool ok,
                         const QString& detail) {
    trace_.append(FormEventTrace{type, fieldName, ok, detail});
    if (trace_.size() > 2048) trace_.remove(0, trace_.size() - 2048);
}

FormEventResult FormRuntime::focus(const QString& fieldName) {
    FormEventResult result;
    if (field(fieldName) == nullptr) {
        result.ok = false;
        result.error = QStringLiteral("字段不存在：%1").arg(fieldName);
        return result;
    }
    record(FormEventType::Focus, fieldName, true, QString());
    result.trace = trace_;
    return result;
}

FormEventResult FormRuntime::applyFormat(const QString& fieldName) {
    FormEventResult result;
    FormFieldState* target = mutableField(fieldName);
    if (target == nullptr) {
        result.ok = false;
        result.error = QStringLiteral("字段不存在：%1").arg(fieldName);
        return result;
    }
    if (target->script.format.isEmpty()) {
        target->formattedValue = target->value;
        result.value = target->value;
        result.formatted = target->value;
        return result;
    }
    const FormJsResult formatted = QuickJsFormScript::evaluate(target->script.format, fieldValues(),
                                                     fieldName, target->value);
    if (!formatted.ok) {
        record(FormEventType::Format, fieldName, false, formatted.error);
        target->formattedValue = target->value;
        result.ok = false;
        result.error = formatted.error;
        return result;
    }
    target->formattedValue = formatted.value;
    record(FormEventType::Format, fieldName, true, formatted.value);
    result.value = target->value;
    result.formatted = target->formattedValue;
    return result;
}

bool FormRuntime::runCalculatePass(bool* changed) {
    QStringList order = calculationOrder_;
    if (order.isEmpty()) {
        for (const FormFieldState& field : fields_) order.append(field.name);
    }
    for (const QString& name : order) {
        FormFieldState* target = mutableField(name);
        if (target == nullptr || target->script.calculate.isEmpty()) continue;
        const FormJsResult computed = QuickJsFormScript::evaluate(target->script.calculate, fieldValues(),
                                                        name, target->value);
        if (!computed.ok) {
            record(FormEventType::Calculate, name, false, computed.error);
            continue;
        }
        if (!computed.valueAssigned) continue;
        if (computed.value != target->value) {
            target->value = computed.value;
            target->committedValue = computed.value;
            *changed = true;
            dirty_ = true;
            if (!changedFields_.contains(name)) changedFields_.append(name);
            record(FormEventType::Calculate, name, true, computed.value);
            applyFormat(name);
        } else {
            record(FormEventType::Calculate, name, true, computed.value);
        }
    }
    return true;
}

bool FormRuntime::recalculate() {
    bool anyChange = false;
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        runCalculatePass(&changed);
        if (!changed) return anyChange;
        anyChange = true;
    }
    record(FormEventType::Calculate, QString(), false,
           QStringLiteral("计算脚本 8 轮内没有收敛"));
    return anyChange;
}

FormEventResult FormRuntime::setValue(const QString& fieldName, const QString& newValue) {
    FormEventResult result;
    FormFieldState* target = mutableField(fieldName);
    if (target == nullptr) {
        result.ok = false;
        result.error = QStringLiteral("字段不存在：%1").arg(fieldName);
        return result;
    }
    if (target->readOnly) {
        record(FormEventType::Validate, fieldName, false, QStringLiteral("只读域"));
        result.ok = false;
        result.error = QStringLiteral("只读域不能修改：%1").arg(fieldName);
        return result;
    }

    if (!target->script.validate.isEmpty()) {
        const FormJsResult checked = QuickJsFormScript::evaluate(target->script.validate, fieldValues(),
                                                        fieldName, newValue);
        if (!checked.ok) {
            record(FormEventType::Validate, fieldName, false, checked.error);
            result.ok = false;
            result.error = checked.error;
            return result;
        }
        if (!checked.valid || !checked.rc) {
            record(FormEventType::Validate, fieldName, false, QStringLiteral("校验未通过"));
            result.ok = false;
            result.accepted = false;
            result.error = QStringLiteral("校验未通过：%1").arg(fieldName);
            return result;
        }
        record(FormEventType::Validate, fieldName, true, QString());
    }

    if (target->value != newValue) {
        target->value = newValue;
        dirty_ = true;
        if (!changedFields_.contains(fieldName)) changedFields_.append(fieldName);
    }
    recalculate();
    const FormEventResult formatted = applyFormat(fieldName);
    target->committedValue = target->value;

    result.ok = formatted.ok;
    result.error = formatted.error;
    result.value = target->value;
    result.formatted = target->formattedValue;
    result.trace = trace_;
    return result;
}

FormEventResult FormRuntime::typeText(const QString& fieldName, const QString& text) {
    FormEventResult result;
    FormFieldState* target = mutableField(fieldName);
    if (target == nullptr) {
        result.ok = false;
        result.error = QStringLiteral("字段不存在：%1").arg(fieldName);
        return result;
    }
    if (target->readOnly) {
        result.ok = false;
        result.accepted = false;
        result.error = QStringLiteral("只读域不能修改：%1").arg(fieldName);
        return result;
    }

    const QString beforeText = target->value;
    for (const QChar character : text) {
        const QString proposed = target->value + character;
        if (!target->script.keystroke.isEmpty()) {
            const FormJsResult checked =
                QuickJsFormScript::evaluate(target->script.keystroke, fieldValues(), fieldName,
                                   target->value, QString(character), target->value.size(),
                                   target->value.size());
            if (!checked.ok) {
                record(FormEventType::Keystroke, fieldName, false, checked.error);
                result.ok = false;
                result.accepted = false;
                result.error = checked.error;
                break;
            }
            if (!checked.rc) {
                record(FormEventType::Keystroke, fieldName, false,
                       QStringLiteral("击键脚本拒绝：%1").arg(character));
                result.ok = false;
                result.accepted = false;
                result.error = QStringLiteral("这次输入被击键脚本拒绝：%1").arg(character);
                break;
            }
            record(FormEventType::Keystroke, fieldName, true, QString(character));
            if (checked.valueAssigned) {
                target->value = checked.value;
                dirty_ = true;
                continue;
            }
        } else {
            record(FormEventType::Keystroke, fieldName, true, QString(character));
        }
        target->value = proposed;
        dirty_ = true;
    }

    if (target->value != beforeText && !changedFields_.contains(fieldName)) {
        changedFields_.append(fieldName);
    }
    const FormEventResult formatted = applyFormat(fieldName);
    result.value = target->value;
    result.formatted = target->formattedValue;
    result.trace = trace_;
    if (result.ok) result.ok = formatted.ok;
    if (result.error.isEmpty()) result.error = formatted.error;
    return result;
}

FormEventResult FormRuntime::blur(const QString& fieldName) {
    FormEventResult result;
    FormFieldState* target = mutableField(fieldName);
    if (target == nullptr) {
        result.ok = false;
        result.error = QStringLiteral("字段不存在：%1").arg(fieldName);
        return result;
    }

    if (!target->script.validate.isEmpty()) {
        const FormJsResult checked = QuickJsFormScript::evaluate(target->script.validate, fieldValues(),
                                                        fieldName, target->value);
        if (!checked.ok || !checked.valid || !checked.rc) {
            const QString reason = !checked.ok
                                       ? checked.error
                                       : QStringLiteral("校验未通过，已回滚到上一次有效值");
            record(FormEventType::Validate, fieldName, false, reason);
            target->value = target->committedValue;
            applyFormat(fieldName);
            result.ok = false;
            result.accepted = false;
            result.error = reason;
            result.value = target->value;
            result.formatted = target->formattedValue;
            result.trace = trace_;
            return result;
        }
        record(FormEventType::Validate, fieldName, true, QString());
    }

    recalculate();
    applyFormat(fieldName);
    target->committedValue = target->value;
    record(FormEventType::Blur, fieldName, true, QString());

    result.value = target->value;
    result.formatted = target->formattedValue;
    result.trace = trace_;
    return result;
}

FormAppearanceResult FormRuntime::applyTo(const QByteArray& pdfBytes) const {
    QVector<FormValueUpdate> updates;
    for (const QString& name : changedFields_) {
        const FormFieldState* target = field(name);
        if (target != nullptr) updates.append(FormValueUpdate{name, target->value});
    }
    if (updates.isEmpty()) {
        FormAppearanceResult result;
        result.report.error = QStringLiteral("没有改过的字段，不需要写回");
        return result;
    }
    return writeFormValues(pdfBytes, updates);
}

FormEventResult FormRuntime::dispatchDocumentEvent(FormEventType type) {
    FormEventResult result;
    record(type, QString(), true, QString());
    result.trace = trace_;
    return result;
}

}  // namespace aipdf::pdf
