#include "pdf/forms/QuickJsFormScript.h"

extern "C" {
#include "quickjs.h"
}

#include <chrono>
#include <cstring>

namespace aipdf::pdf {
namespace {

struct FieldBridge {
    const QVector<FormFieldValue>* fields = nullptr;
};

struct Deadline {
    std::chrono::steady_clock::time_point start;
    int timeoutMs = 50;
    bool timedOut = false;
};

int interruptHandler(JSRuntime*, void* opaque) {
    auto* deadline = static_cast<Deadline*>(opaque);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - deadline->start)
                             .count();
    if (elapsed > deadline->timeoutMs) {
        deadline->timedOut = true;
        return 1;
    }
    return 0;
}

QString toQString(JSContext* ctx, JSValueConst value) {
    const char* text = JS_ToCString(ctx, value);
    if (text == nullptr) return QString();
    const QString result = QString::fromUtf8(text);
    JS_FreeCString(ctx, text);
    return result;
}

void setString(JSContext* ctx, JSValueConst object, const char* name, const QString& value) {
    JS_SetPropertyStr(ctx, object, name, JS_NewString(ctx, value.toUtf8().constData()));
}

JSValue makeFieldObject(JSContext* ctx, const QString& value) {
    JSValue object = JS_NewObject(ctx);
    const QByteArray utf8 = value.toUtf8();
    JS_SetPropertyStr(ctx, object, "value", JS_NewString(ctx, utf8.constData()));
    JS_SetPropertyStr(ctx, object, "valueAsString", JS_NewString(ctx, utf8.constData()));
    return object;
}

JSValue jsGetField(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* bridge = static_cast<FieldBridge*>(JS_GetContextOpaque(ctx));
    if (bridge == nullptr || bridge->fields == nullptr || argc < 1) return JS_UNDEFINED;
    const QString name = toQString(ctx, argv[0]);
    QString value;
    for (const FormFieldValue& field : *bridge->fields) {
        if (field.name == name) {
            value = field.value;
            break;
        }
    }
    return makeFieldObject(ctx, value);
}

QString exceptionText(JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    QString text;
    if (JS_IsObject(exception)) {
        JSValue message = JS_GetPropertyStr(ctx, exception, "message");
        text = toQString(ctx, message);
        JS_FreeValue(ctx, message);
    }
    if (text.isEmpty()) text = toQString(ctx, exception);
    JS_FreeValue(ctx, exception);
    return text;
}

// Acrobat 表单脚本最常用的内置函数。
//
// 不是「全部 Acrobat JS」，而是真实表单里出现频率最高的那一批：
// 数字格式化 / 击键过滤 / 求和求平均。函数体用 JS 写，避免在 C++ 里
// 再实现一遍字符串与正则。
const char* const kAcrobatPrelude = R"JS(
function AFMakeNumber(value) {
    var n = parseFloat(value);
    return isNaN(n) ? 0 : n;
}

function AFNumber_Format(decimals, sepStyle, negStyle, currStyle, strCurrency, bCurrencyPrepend) {
    var n = parseFloat(event.value);
    if (isNaN(n)) return;
    var negative = n < 0;
    var text = Math.abs(n).toFixed(decimals);
    var parts = text.split(".");
    if (sepStyle === 0) {
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    } else if (sepStyle === 2) {
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ".");
    } else if (sepStyle === 3) {
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, " ");
    }
    text = parts.join(decimals > 0 ? "." : "");
    if (currStyle !== 0 && strCurrency) {
        text = bCurrencyPrepend === false ? text + strCurrency : strCurrency + text;
    }
    if (negative) text = "-" + text;
    event.value = text;
}

function AFNumber_Keystroke(decimals, sepStyle, negStyle, currStyle, strCurrency, bCurrencyPrepend) {
    var change = event.change;
    if (change === undefined || change === null) return;
    var candidate = String(event.value) + String(change);
    if (!/^-?\d*\.?\d*$/.test(candidate)) event.rc = false;
}

function AFNumber_Parse(decimals, sepStyle, negStyle, currStyle, strCurrency, bCurrencyPrepend) {
    event.value = AFMakeNumber(String(event.value).replace(/,/g, ""));
}

function AFPercent_Format(decimals, sepStyle) {
    var n = parseFloat(event.value);
    if (isNaN(n)) return;
    event.value = (n * 100).toFixed(decimals) + "%";
}

function AFSimple_Calculate(op, fieldNames) {
    var names = (typeof fieldNames === "string") ? [fieldNames] : fieldNames;
    var values = [];
    for (var i = 0; i < names.length; i++) {
        var field = getField(names[i]);
        values.push(field === undefined ? 0 : AFMakeNumber(field.value));
    }
    var result = 0;
    if (op === "SUM") {
        for (var a = 0; a < values.length; a++) result += values[a];
    } else if (op === "AVG") {
        for (var b = 0; b < values.length; b++) result += values[b];
        result = values.length > 0 ? result / values.length : 0;
    } else if (op === "PRD") {
        result = 1;
        for (var c = 0; c < values.length; c++) result *= values[c];
    } else if (op === "MIN") {
        result = values.length > 0 ? Math.min.apply(null, values) : 0;
    } else if (op === "MAX") {
        result = values.length > 0 ? Math.max.apply(null, values) : 0;
    }
    event.value = result;
}
)JS";

}  // namespace

bool QuickJsFormScript::available() { return true; }

FormJsResult QuickJsFormScript::evaluate(const QString& script,
                                         const QVector<FormFieldValue>& fields,
                                         const QString& eventField, const QString& eventValue,
                                         const QString& eventChange, int selectionStart,
                                         int selectionEnd, const QuickJsLimits& limits) {
    FormJsResult result;
    if (script.trimmed().isEmpty()) {
        result.ok = true;
        result.valid = true;
        return result;
    }

    JSRuntime* runtime = JS_NewRuntime();
    if (runtime == nullptr) {
        result.error = QStringLiteral("QuickJS 运行时创建失败");
        return result;
    }
    JS_SetMemoryLimit(runtime, static_cast<size_t>(limits.memoryMb) * 1024 * 1024);
    JS_SetMaxStackSize(runtime, 1024 * 1024);
    Deadline deadline;
    deadline.start = std::chrono::steady_clock::now();
    deadline.timeoutMs = limits.timeoutMs;
    JS_SetInterruptHandler(runtime, interruptHandler, &deadline);

    JSContext* ctx = JS_NewContext(runtime);
    if (ctx == nullptr) {
        JS_FreeRuntime(runtime);
        result.error = QStringLiteral("QuickJS 上下文创建失败");
        return result;
    }

    FieldBridge bridge;
    bridge.fields = &fields;
    JS_SetContextOpaque(ctx, &bridge);

    // Acrobat 内置函数（AF*）先装进上下文；装不上说明预置脚本本身有问题。
    JSValue prelude = JS_Eval(ctx, kAcrobatPrelude, std::strlen(kAcrobatPrelude),
                              "aipdf-prelude.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(prelude)) {
        result.error = QStringLiteral("Acrobat 预置脚本执行失败：%1").arg(exceptionText(ctx));
        JS_FreeValue(ctx, prelude);
        JS_FreeContext(ctx);
        JS_FreeRuntime(runtime);
        return result;
    }
    JS_FreeValue(ctx, prelude);

    JSValue global = JS_GetGlobalObject(ctx);

    JSValue self = JS_NewObject(ctx);
    setString(ctx, self, "name", eventField);
    setString(ctx, self, "value", eventValue);
    JS_SetPropertyStr(ctx, self, "getField", JS_NewCFunction(ctx, jsGetField, "getField", 1));

    JSValue event = JS_NewObject(ctx);
    setString(ctx, event, "value", eventValue);
    setString(ctx, event, "change", eventChange);
    setString(ctx, event, "name", eventField);
    JS_SetPropertyStr(ctx, event, "target", JS_DupValue(ctx, self));
    JS_FreeValue(ctx, self);
    JS_SetPropertyStr(ctx, event, "rc", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, event, "selStart", JS_NewInt32(ctx, selectionStart));
    JS_SetPropertyStr(ctx, event, "selEnd", JS_NewInt32(ctx, selectionEnd));
    JS_SetPropertyStr(ctx, global, "event", event);

    // 脚本里的 this 在全局代码里就是 globalThis，所以把字段的 name / value
    // 直接挂到全局对象上：this.value、this.getField(...) 都能用，
    // 而且脚本最后一条表达式的值仍然是求值结果（校验脚本靠它返回布尔）。
    setString(ctx, global, "name", eventField);
    setString(ctx, global, "value", eventValue);
    JS_SetPropertyStr(ctx, global, "getField", JS_NewCFunction(ctx, jsGetField, "getField", 1));

    const QByteArray utf8 = script.toUtf8();
    JSValue outcome = JS_Eval(ctx, utf8.constData(), static_cast<size_t>(utf8.size()),
                              "form-script.js", JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(outcome)) {
        if (deadline.timedOut) {
            result.error = QStringLiteral("脚本执行超时（超过 %1 ms），已中断")
                               .arg(limits.timeoutMs);
        } else {
            result.error = exceptionText(ctx);
            if (result.error.isEmpty()) result.error = QStringLiteral("脚本执行失败");
        }
        result.ok = false;
        result.valid = false;
        JS_FreeValue(ctx, outcome);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(runtime);
        return result;
    }

    // 读回 event.value / event.rc。脚本没赋值时 value 保持入口值。
    JSValue eventAfter = JS_GetPropertyStr(ctx, global, "event");
    JSValue valueAfter = JS_GetPropertyStr(ctx, eventAfter, "value");
    JSValue rcAfter = JS_GetPropertyStr(ctx, eventAfter, "rc");
    const QString valueText = toQString(ctx, valueAfter);
    result.value = valueText;
    result.rc = JS_ToBool(ctx, rcAfter) != 0;
    result.valueAssigned = (valueText != eventValue);
    // 校验脚本用返回值表达通过与否；其它脚本按 true 处理。
    result.valid = JS_IsBool(outcome) ? (JS_ToBool(ctx, outcome) != 0) : true;
    result.ok = true;

    JS_FreeValue(ctx, rcAfter);
    JS_FreeValue(ctx, valueAfter);
    JS_FreeValue(ctx, eventAfter);
    JS_FreeValue(ctx, outcome);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    return result;
}

}  // namespace aipdf::pdf
