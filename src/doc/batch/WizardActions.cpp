#include "doc/batch/WizardActions.h"

#include <QStringList>
#include <QVector>

#include <cmath>

namespace aipdf::doc {
namespace {

// 失败统一走这里：把中文原因写进 error，返回 false。
void setError(QString* error, const QString& reason) {
    if (error != nullptr) *error = reason;
}

// 参数类型的中文名（报错里说清楚「应该是哪种、实际是哪种」）。
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

// 整数白名单拼成「1、2」这样的中文串。
QString joinNumbers(const QVector<qint64>& numbers) {
    QStringList parts;
    for (const qint64 value : numbers) parts.append(QString::number(value));
    return parts.join(QStringLiteral("、"));
}

// 取值范围的边界怎么写：整数不带小数点，小数保留原样。
QString formatBound(double value, bool integer) {
    if (integer) return QString::number(static_cast<qint64>(value));
    return QString::number(value);
}

// ---------------------------------------------------------------------------
//  参数规格与动作规格表
//
//  为什么把十种动作的参数表集中写在这一处：
//  向导的价值就在于「这一步需要什么参数」是确定的。散着写，界面层就会
//  各自抄一份，取值约束迟早抄歪；集中在这里，改一处全体一致。
//
//  取值约束比 ActionWizard 的那份**更窄**，因为这里问的是另一个问题：
//  「本程序照这些参数能不能做出来」。
//    - 水印位置只支持 center（页码位置只支持 bottomRight）；
//    - 页码起始号只支持 1；
//    - 打印份数 1 到 99；
//  ActionWizard 管的是「文件存得下吗」，这一层管的是「做得到吗」。
// ---------------------------------------------------------------------------

QVector<WizardActionSpec> buildCatalog() {
    QVector<WizardActionSpec> catalog;

    // ---- 打开文件 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::OpenFile;
        spec.name = QStringLiteral("openFile");
        spec.title = QStringLiteral("打开文件");
        spec.wired = true;
        spec.capability =
            QStringLiteral("打开指定的 PDF 文件并作为当前文档；只支持 PDF 与单个文件，"
                           "路径存不存在由界面层检查。");
        spec.params.append(WizardParamSpec{QStringLiteral("path"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("要打开的 PDF 文件路径。")});
        catalog.append(spec);
    }

    // ---- 合并文档 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::MergeDocuments;
        spec.name = QStringLiteral("mergeDocuments");
        spec.title = QStringLiteral("合并文档");
        spec.wired = true;
        spec.capability =
            QStringLiteral("把多份 PDF 按给定顺序合并成一份并写到指定路径，源文件里的书签会一起搬过去；"
                           "当前文档作为第一份参与合并，合并结果要能重新打开才算数。");
        spec.params.append(WizardParamSpec{QStringLiteral("inputPaths"), WizardParamType::StringList, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("要合并的源文件路径，按合并顺序排列。")});
        spec.params.append(WizardParamSpec{QStringLiteral("outputPath"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("合并结果写到哪个文件。")});
        catalog.append(spec);
    }

    // ---- 拆分文档 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::SplitDocument;
        spec.name = QStringLiteral("splitDocument");
        spec.title = QStringLiteral("拆分文档");
        spec.wired = true;
        spec.capability =
            QStringLiteral("按输出目录把当前文档拆成多份，拆分口径有四种：range 按 ranges 给的页区间、"
                           "count 或 count:N 每 N 页一份、each 逐页、outline 按顶层书签。");
        spec.params.append(WizardParamSpec{QStringLiteral("outputDirectory"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("拆出来的文件写到哪个目录。")});
        spec.params.append(WizardParamSpec{QStringLiteral("mode"), WizardParamType::String, false,
                                           QStringList{QStringLiteral("range"), QStringLiteral("count"),
                                                       QStringLiteral("each"), QStringLiteral("outline")},
                                           QVector<qint64>(), false, 0.0, 0.0,
                                           QStringLiteral("range"),
                                           QStringLiteral("拆分口径：range 按区间，count 或 count:N 每 N 页一份，"
                                                          "each 逐页，outline 按顶层书签。")});
        spec.params.back().allowedPrefixes.append(QStringLiteral("count:"));
        spec.params.append(WizardParamSpec{QStringLiteral("ranges"), WizardParamType::StringList, false,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("页区间列表（mode 为 range 时用）。")});
        catalog.append(spec);
    }

    // ---- 添加书签 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::AddBookmark;
        spec.name = QStringLiteral("addBookmark");
        spec.title = QStringLiteral("添加书签");
        spec.wired = true;
        spec.capability =
            QStringLiteral("在当前文档里加一条指向指定页码的书签；只加顶层书签、不改已有层级，"
                           "页码从 1 开始数，超出文档页数会当场报错。");
        spec.params.append(WizardParamSpec{QStringLiteral("title"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("书签标题。")});
        spec.params.append(WizardParamSpec{QStringLiteral("pageIndex"), WizardParamType::Integer, false,
                                           QStringList(), QVector<qint64>(), true, 1.0, 1000000.0,
                                           QStringLiteral("1"),
                                           QStringLiteral("书签指向第几页（从 1 开始数）。")});
        catalog.append(spec);
    }

    // ---- 添加页码 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::AddPageNumbers;
        spec.name = QStringLiteral("addPageNumbers");
        spec.title = QStringLiteral("添加页码");
        spec.wired = true;
        spec.capability =
            QStringLiteral("给当前文档每页右下角加页码，页码从 1 开始编；"
                           "只支持右下角一个位置，页码起始号只能是 1。");
        spec.params.append(WizardParamSpec{
            QStringLiteral("position"), WizardParamType::String, true,
            QStringList{QStringLiteral("bottomRight")}, QVector<qint64>(), false, 0.0, 0.0,
            QString(),
            QStringLiteral("页码位置，本程序只支持 bottomRight（右下角）。")});
        spec.params.append(WizardParamSpec{
            QStringLiteral("startNumber"), WizardParamType::Integer, false,
            QStringList(), QVector<qint64>{1}, false, 0.0, 0.0,
            QStringLiteral("1"),
            QStringLiteral("页码起始号，本程序只支持 1。")});
        catalog.append(spec);
    }

    // ---- 添加水印 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::AddWatermark;
        spec.name = QStringLiteral("addWatermark");
        spec.title = QStringLiteral("添加水印");
        spec.wired = true;
        spec.capability =
            QStringLiteral("给当前文档每页正中加一段文字水印，透明度可在 0 到 1 之间调；"
                           "只支持页面正中一个位置。");
        spec.params.append(WizardParamSpec{QStringLiteral("text"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("水印上的文字。")});
        spec.params.append(WizardParamSpec{QStringLiteral("opacity"), WizardParamType::Number, false,
                                           QStringList(), QVector<qint64>(), true, 0.0, 1.0,
                                           QStringLiteral("0.15"),
                                           QStringLiteral("水印透明度，0（看不见）到 1（完全不透明）。")});
        spec.params.append(WizardParamSpec{
            QStringLiteral("position"), WizardParamType::String, false,
            QStringList{QStringLiteral("center")}, QVector<qint64>(), false, 0.0, 0.0,
            QStringLiteral("center"),
            QStringLiteral("水印位置，本程序只支持 center（页面正中）。")});
        catalog.append(spec);
    }

    // ---- 加密 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::Encrypt;
        spec.name = QStringLiteral("encrypt");
        spec.title = QStringLiteral("加密");
        spec.wired = true;
        spec.capability =
            QStringLiteral("给当前文档设用户口令与所有者口令，并决定要不要允许打印；"
                           "加密动作发生在保存时，需要带 qpdf 的构建。");
        spec.params.append(WizardParamSpec{QStringLiteral("userPassword"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("打开文档要用的用户口令。")});
        spec.params.append(WizardParamSpec{QStringLiteral("ownerPassword"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("所有者口令（用来改权限）。")});
        spec.params.append(WizardParamSpec{QStringLiteral("allowPrinting"), WizardParamType::Boolean, false,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QStringLiteral("true"),
                                           QStringLiteral("允许打印吗。")});
        catalog.append(spec);
    }

    // ---- 转换格式 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::ConvertToFormat;
        spec.name = QStringLiteral("convertToFormat");
        spec.title = QStringLiteral("转换格式");
        spec.wired = true;
        spec.capability =
            QStringLiteral("把当前文档转成另一种格式并写到指定路径，目标格式只能是 "
                           "docx、xlsx、pptx、rtf、txt 之一；"
                           "这五种都是整篇导出，图片与表格带得过去。");
        spec.params.append(WizardParamSpec{
            QStringLiteral("format"), WizardParamType::String, true,
            QStringList{QStringLiteral("docx"), QStringLiteral("xlsx"),
                        QStringLiteral("pptx"), QStringLiteral("rtf"),
                        QStringLiteral("txt")},
            QVector<qint64>(), false, 0.0, 0.0, QString(),
            QStringLiteral("目标格式。")});
        spec.params.append(WizardParamSpec{QStringLiteral("outputPath"), WizardParamType::String, true,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("转换结果写到哪个文件。")});
        catalog.append(spec);
    }

    // ---- 打印 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::Print;
        spec.name = QStringLiteral("print");
        spec.title = QStringLiteral("打印");
        spec.wired = true;
        spec.capability =
            QStringLiteral("把当前文档送到指定打印机打印，份数 1 到 99，"
                           "页码范围形如「1-3,5」（留空表示全部页）；"
                           "打印机名留空就用默认打印机，填了但系统里没有这台打印机会当场报错。");
        spec.params.append(WizardParamSpec{QStringLiteral("printerName"), WizardParamType::String, false,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("打印机名，留空用默认打印机。")});
        spec.params.append(WizardParamSpec{QStringLiteral("copies"), WizardParamType::Integer, false,
                                           QStringList(), QVector<qint64>(), true, 1.0, 99.0,
                                           QStringLiteral("1"),
                                           QStringLiteral("打印几份，1 到 99。")});
        spec.params.append(WizardParamSpec{QStringLiteral("paperSize"), WizardParamType::String, false,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("纸张规格，留空用打印机默认。")});
        spec.params.append(WizardParamSpec{QStringLiteral("pageRange"), WizardParamType::String, false,
                                           QStringList(), QVector<qint64>(), false, 0.0, 0.0,
                                           QString(),
                                           QStringLiteral("页码范围，形如「1-3,5」，留空表示全部页。")});
        catalog.append(spec);
    }

    // ---- 结束 ----
    {
        WizardActionSpec spec;
        spec.type = WizardActionType::End;
        spec.name = QStringLiteral("end");
        spec.title = QStringLiteral("结束");
        spec.wired = true;
        spec.capability =
            QStringLiteral("标记向导的收尾点：这一步之后的步骤不再执行；它本身不改动文档。");
        catalog.append(spec);
    }

    return catalog;
}

// 找一份参数规格。找不到返回 nullptr。
const WizardParamSpec* findParamSpec(const WizardActionSpec& spec, const QString& name) {
    for (const WizardParamSpec& candidate : spec.params) {
        if (candidate.name == name) return &candidate;
    }
    return nullptr;
}

// 按动作规格找参数并核对类型。
//
// 参数名不在该动作的规格里 → 报错（拼错字不许被吞掉）。
// 参数不存在 → 必填的说「缺少必填参数」，可选的说「不存在」。
// 类型不对 → 说清楚应该是哪种类型、实际是哪种。
const WizardParam* findSpecParam(const WizardActionSpec& spec, const QVector<WizardParam>& params,
                                 const QString& name, WizardParamType expected, QString* error) {
    const WizardParamSpec* paramSpec = findParamSpec(spec, name);
    if (paramSpec == nullptr) {
        setError(error, QStringLiteral("动作「%1」不认识参数「%2」").arg(spec.title, name));
        return nullptr;
    }
    const WizardParam* param = findWizardParam(params, name);
    if (param == nullptr) {
        if (paramSpec->required) {
            setError(error, QStringLiteral("动作「%1」缺少必填参数「%2」").arg(spec.title, name));
        } else {
            setError(error, QStringLiteral("动作参数「%1」不存在").arg(name));
        }
        return nullptr;
    }
    if (param->type != expected) {
        setError(error, QStringLiteral("动作「%1」的参数「%2」类型应为%3，实际为%4")
                          .arg(spec.title, name, paramTypeTitle(expected),
                               paramTypeTitle(param->type)));
        return nullptr;
    }
    return param;
}

}  // namespace

// ---------------------------------------------------------------------------
//  参数查找
// ---------------------------------------------------------------------------

bool hasWizardParam(const QVector<WizardParam>& params, const QString& name) {
    return findWizardParam(params, name) != nullptr;
}

const WizardParam* findWizardParam(const QVector<WizardParam>& params, const QString& name) {
    for (const WizardParam& param : params) {
        if (param.name == name) return &param;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  通用取值：只判「在不在」「类型对不对」
// ---------------------------------------------------------------------------

bool takeString(const QVector<WizardParam>& params, const QString& name, QString* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardParam* param = findWizardParam(params, name);
    if (param == nullptr) {
        setError(error, QStringLiteral("动作参数「%1」不存在").arg(name));
        return false;
    }
    if (param->type != WizardParamType::String) {
        setError(error, QStringLiteral("动作参数「%1」的类型应为%2，实际为%3")
                          .arg(name, paramTypeTitle(WizardParamType::String),
                               paramTypeTitle(param->type)));
        return false;
    }
    *out = param->stringValue;
    return true;
}

bool takeInteger(const QVector<WizardParam>& params, const QString& name, qint64* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardParam* param = findWizardParam(params, name);
    if (param == nullptr) {
        setError(error, QStringLiteral("动作参数「%1」不存在").arg(name));
        return false;
    }
    if (param->type != WizardParamType::Integer) {
        setError(error, QStringLiteral("动作参数「%1」的类型应为%2，实际为%3")
                          .arg(name, paramTypeTitle(WizardParamType::Integer),
                               paramTypeTitle(param->type)));
        return false;
    }
    *out = param->integerValue;
    return true;
}

bool takeNumber(const QVector<WizardParam>& params, const QString& name, double* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardParam* param = findWizardParam(params, name);
    if (param == nullptr) {
        setError(error, QStringLiteral("动作参数「%1」不存在").arg(name));
        return false;
    }
    if (param->type != WizardParamType::Number) {
        setError(error, QStringLiteral("动作参数「%1」的类型应为%2，实际为%3")
                          .arg(name, paramTypeTitle(WizardParamType::Number),
                               paramTypeTitle(param->type)));
        return false;
    }
    // NaN 与无穷大不是能用的取值：报成取值非法，不让它流到下游。
    if (!std::isfinite(param->numberValue)) {
        setError(error, QStringLiteral("动作参数「%1」的取值必须是有限数").arg(name));
        return false;
    }
    *out = param->numberValue;
    return true;
}

bool takeBoolean(const QVector<WizardParam>& params, const QString& name, bool* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardParam* param = findWizardParam(params, name);
    if (param == nullptr) {
        setError(error, QStringLiteral("动作参数「%1」不存在").arg(name));
        return false;
    }
    if (param->type != WizardParamType::Boolean) {
        setError(error, QStringLiteral("动作参数「%1」的类型应为%2，实际为%3")
                          .arg(name, paramTypeTitle(WizardParamType::Boolean),
                               paramTypeTitle(param->type)));
        return false;
    }
    *out = param->booleanValue;
    return true;
}

bool takeStringList(const QVector<WizardParam>& params, const QString& name, QStringList* out,
                    QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardParam* param = findWizardParam(params, name);
    if (param == nullptr) {
        setError(error, QStringLiteral("动作参数「%1」不存在").arg(name));
        return false;
    }
    if (param->type != WizardParamType::StringList) {
        setError(error, QStringLiteral("动作参数「%1」的类型应为%2，实际为%3")
                          .arg(name, paramTypeTitle(WizardParamType::StringList),
                               paramTypeTitle(param->type)));
        return false;
    }
    // 列表里的空项一定是填错了：空路径、空区间都做不了事。
    for (const QString& item : param->listValue) {
        if (item.trimmed().isEmpty()) {
            setError(error, QStringLiteral("动作参数「%1」的取值里有空项").arg(name));
            return false;
        }
    }
    *out = param->listValue;
    return true;
}

// ---------------------------------------------------------------------------
//  按动作规格取值：再多判「取值合不合法」
// ---------------------------------------------------------------------------

bool takeString(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                QString* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardActionSpec* spec = wizardActionSpec(actionType);
    if (spec == nullptr) {
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }
    const WizardParam* param = findSpecParam(*spec, params, name, WizardParamType::String, error);
    if (param == nullptr) return false;
    const WizardParamSpec* paramSpec = findParamSpec(*spec, name);
    const QString value = param->stringValue;
    // 必填参数不能是空值：「给了个空值」与「没给这个参数」必须分开报。
    if (paramSpec->required && value.trimmed().isEmpty()) {
        setError(error, QStringLiteral("动作「%1」的参数「%2」取值不能为空").arg(spec->title, name));
        return false;
    }
    // 可选参数留空 = 没填，交给调用方用默认值，不按非法取值报。
    if (!paramSpec->required && value.trimmed().isEmpty()) {
        *out = value;
        return true;
    }
    if (!paramSpec->allowedValues.isEmpty() && !paramSpec->allowedValues.contains(value)) {
        // 前缀白名单放行：有的取值是「前缀 + 一个数」的形式
        // （例如拆分口径的 "count:5" 表示每 5 页一份）。
        // 精确白名单对这种写法一律拒绝，只能靠前缀规则放行。
        bool prefixAllowed = false;
        for (const QString& prefix : paramSpec->allowedPrefixes) {
            if (!prefix.isEmpty() && value.startsWith(prefix)) {
                prefixAllowed = true;
                break;
            }
        }
        if (!prefixAllowed) {
            setError(error, QStringLiteral("动作「%1」的参数「%2」取值必须是 %3 之一，当前为「%4」")
                              .arg(spec->title, name, paramSpec->allowedValues.join(QStringLiteral("、")),
                                   value));
            return false;
        }
    }
    *out = value;
    return true;
}

bool takeInteger(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                 qint64* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardActionSpec* spec = wizardActionSpec(actionType);
    if (spec == nullptr) {
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }
    const WizardParam* param = findSpecParam(*spec, params, name, WizardParamType::Integer, error);
    if (param == nullptr) return false;
    const WizardParamSpec* paramSpec = findParamSpec(*spec, name);
    const qint64 value = param->integerValue;
    if (!paramSpec->allowedNumbers.isEmpty() && !paramSpec->allowedNumbers.contains(value)) {
        setError(error, QStringLiteral("动作「%1」的参数「%2」取值必须是 %3 之一，当前为 %4")
                          .arg(spec->title)
                          .arg(name)
                          .arg(joinNumbers(paramSpec->allowedNumbers))
                          .arg(value));
        return false;
    }
    if (paramSpec->hasRange &&
        (static_cast<double>(value) < paramSpec->minimum ||
         static_cast<double>(value) > paramSpec->maximum)) {
        setError(error, QStringLiteral("动作「%1」的参数「%2」取值必须在 %3 到 %4 之间，当前为 %5")
                          .arg(spec->title, name)
                          .arg(formatBound(paramSpec->minimum, true))
                          .arg(formatBound(paramSpec->maximum, true))
                          .arg(value));
        return false;
    }
    *out = value;
    return true;
}

bool takeNumber(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                double* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardActionSpec* spec = wizardActionSpec(actionType);
    if (spec == nullptr) {
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }
    const WizardParam* param = findSpecParam(*spec, params, name, WizardParamType::Number, error);
    if (param == nullptr) return false;
    const WizardParamSpec* paramSpec = findParamSpec(*spec, name);
    const double value = param->numberValue;
    if (paramSpec->hasRange && (value < paramSpec->minimum || value > paramSpec->maximum)) {
        setError(error, QStringLiteral("动作「%1」的参数「%2」取值必须在 %3 到 %4 之间，当前为 %5")
                          .arg(spec->title, name)
                          .arg(formatBound(paramSpec->minimum, false))
                          .arg(formatBound(paramSpec->maximum, false))
                          .arg(value));
        return false;
    }
    *out = value;
    return true;
}

bool takeBoolean(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                 bool* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardActionSpec* spec = wizardActionSpec(actionType);
    if (spec == nullptr) {
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }
    const WizardParam* param = findSpecParam(*spec, params, name, WizardParamType::Boolean, error);
    if (param == nullptr) return false;
    *out = param->booleanValue;
    return true;
}

bool takeStringList(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                    QStringList* out, QString* error) {
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardActionSpec* spec = wizardActionSpec(actionType);
    if (spec == nullptr) {
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }
    const WizardParam* param = findSpecParam(*spec, params, name, WizardParamType::StringList, error);
    if (param == nullptr) return false;
    const WizardParamSpec* paramSpec = findParamSpec(*spec, name);
    // 必填参数不能是空列表。
    if (paramSpec->required && param->listValue.isEmpty()) {
        setError(error, QStringLiteral("动作「%1」的参数「%2」取值不能是空列表").arg(spec->title, name));
        return false;
    }
    QStringList values;
    if (!takeStringList(params, name, &values, error)) return false;
    *out = values;
    return true;
}

// ---------------------------------------------------------------------------
//  动作清单
// ---------------------------------------------------------------------------

QVector<WizardActionSpec> wizardActionCatalog() {
    // 规格表是常量：建一次就够，别每次查都重建一遍。
    static const QVector<WizardActionSpec> catalog = buildCatalog();
    return catalog;
}

const WizardActionSpec* wizardActionSpec(WizardActionType type) {
    static const QVector<WizardActionSpec> catalog = buildCatalog();
    for (const WizardActionSpec& spec : catalog) {
        if (spec.type == type) return &spec;
    }
    return nullptr;
}

QString wizardActionCapability(WizardActionType type) {
    const WizardActionSpec* spec = wizardActionSpec(type);
    if (spec == nullptr) return QString();
    return spec->capability;
}

// ---------------------------------------------------------------------------
//  总入口
// ---------------------------------------------------------------------------

bool parseWizardAction(WizardActionType type, const QVector<WizardParam>& params,
                        WizardActionData* out, QString* error) {
    if (error != nullptr) error->clear();
    if (out == nullptr) {
        setError(error, QStringLiteral("输出参数为空"));
        return false;
    }
    const WizardActionSpec* spec = wizardActionSpec(type);
    if (spec == nullptr) {
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }
    // 不认识的参数直接报出来：拼错字不许被吞掉。
    for (const WizardParam& param : params) {
        if (findParamSpec(*spec, param.name) == nullptr) {
            setError(error, QStringLiteral("动作「%1」不认识参数「%2」").arg(spec->title, param.name));
            return false;
        }
    }

    WizardActionData data;
    data.type = type;

    // 可选的字符串参数：没给、或给了个空值，都算「没填」，沿用结构体里的默认值。
    // （面板会把没填的可选参数也写进来，值是空串；那不是「非法取值」。）
    const auto takeOptionalString = [&params, type, error](const QString& name, QString* target) -> bool {
        if (!hasWizardParam(params, name)) return true;
        QString value;
        if (!takeString(type, params, name, &value, error)) return false;
        if (!value.trimmed().isEmpty()) *target = value;
        return true;
    };

    switch (type) {
    case WizardActionType::OpenFile: {
        if (!takeString(type, params, QStringLiteral("path"), &data.openFile.path, error)) return false;
        break;
    }
    case WizardActionType::MergeDocuments: {
        if (!takeStringList(type, params, QStringLiteral("inputPaths"), &data.merge.inputPaths,
                            error)) {
            return false;
        }
        if (!takeString(type, params, QStringLiteral("outputPath"), &data.merge.outputPath, error)) {
            return false;
        }
        break;
    }
    case WizardActionType::SplitDocument: {
        if (!takeString(type, params, QStringLiteral("outputDirectory"), &data.split.outputDirectory,
                        error)) {
            return false;
        }
        // 可选参数：没给就沿用结构体里的默认值（与规格表里的 defaultValue 一致）。
        if (!takeOptionalString(QStringLiteral("mode"), &data.split.mode)) return false;
        if (hasWizardParam(params, QStringLiteral("ranges"))) {
            if (!takeStringList(type, params, QStringLiteral("ranges"), &data.split.ranges, error)) {
                return false;
            }
        }
        break;
    }
    case WizardActionType::AddBookmark: {
        if (!takeString(type, params, QStringLiteral("title"), &data.bookmark.title, error)) return false;
        if (hasWizardParam(params, QStringLiteral("pageIndex"))) {
            if (!takeInteger(type, params, QStringLiteral("pageIndex"), &data.bookmark.pageIndex,
                             error)) {
                return false;
            }
        }
        break;
    }
    case WizardActionType::AddPageNumbers: {
        if (!takeString(type, params, QStringLiteral("position"), &data.pageNumbers.position, error)) {
            return false;
        }
        if (hasWizardParam(params, QStringLiteral("startNumber"))) {
            if (!takeInteger(type, params, QStringLiteral("startNumber"), &data.pageNumbers.startNumber,
                             error)) {
                return false;
            }
        }
        break;
    }
    case WizardActionType::AddWatermark: {
        if (!takeString(type, params, QStringLiteral("text"), &data.watermark.text, error)) return false;
        if (hasWizardParam(params, QStringLiteral("opacity"))) {
            if (!takeNumber(type, params, QStringLiteral("opacity"), &data.watermark.opacity, error)) {
                return false;
            }
        }
        if (!takeOptionalString(QStringLiteral("position"), &data.watermark.position)) return false;
        break;
    }
    case WizardActionType::Encrypt: {
        if (!takeString(type, params, QStringLiteral("userPassword"), &data.encrypt.userPassword,
                        error)) {
            return false;
        }
        if (!takeString(type, params, QStringLiteral("ownerPassword"), &data.encrypt.ownerPassword,
                        error)) {
            return false;
        }
        if (hasWizardParam(params, QStringLiteral("allowPrinting"))) {
            if (!takeBoolean(type, params, QStringLiteral("allowPrinting"), &data.encrypt.allowPrinting,
                             error)) {
                return false;
            }
        }
        break;
    }
    case WizardActionType::ConvertToFormat: {
        if (!takeString(type, params, QStringLiteral("format"), &data.convert.format, error)) {
            return false;
        }
        if (!takeString(type, params, QStringLiteral("outputPath"), &data.convert.outputPath, error)) {
            return false;
        }
        break;
    }
    case WizardActionType::Print: {
        if (!takeOptionalString(QStringLiteral("printerName"), &data.print.printerName)) return false;
        if (hasWizardParam(params, QStringLiteral("copies"))) {
            qint64 copies = 0;
            if (!takeInteger(type, params, QStringLiteral("copies"), &copies, error)) return false;
            data.print.copies = static_cast<int>(copies);
        }
        if (!takeOptionalString(QStringLiteral("paperSize"), &data.print.paperSize)) return false;
        if (!takeOptionalString(QStringLiteral("pageRange"), &data.print.pageRange)) return false;
        break;
    }
    case WizardActionType::End: {
        // 「结束」不带参数：走到这里说明参数表是空的（上面已经查过）。
        break;
    }
    case WizardActionType::Unknown:
        setError(error, QStringLiteral("动作类型未知，本程序不认识这一步"));
        return false;
    }

    *out = data;
    return true;
}

}  // namespace aipdf::doc
