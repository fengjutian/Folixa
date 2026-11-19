#pragma once

#include "doc/batch/ActionWizard.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::doc {

// =============================================================================
//  动作向导的「参数规格 + 强类型解析」层
//
//  为什么要有这一层：
//  doc/batch/ActionWizard 回答的是「这份向导文件本身合不合法」；
//  界面层要回答的是「这一步的参数怎么解释、本程序能不能照这些参数做出来」。
//  后者以前是每个动作各写一遍 lambda 取参（stringParam / numberParam / …），
//  同一份取参与校验抄了十遍，抄错了只有跑起来才知道。
//
//  这一层把三件事收进一张表：
//    1. 每个动作要哪些参数、什么类型、是否必填、取值约束；
//    2. 强类型取值：分清「参数不存在」「类型不对」「取值非法」三种失败；
//    3. 每个动作一份解析结果结构体 + 一个总入口。
//
//  与 ActionWizard 的分工（两层问的不是同一个问题）：
//    - ActionWizard 管**文件格式**：这份向导存得下吗、字段齐不齐、类型对不对。
//    - 这一层管**能不能做出来**：按这些参数，本程序做得到吗（值域更窄）。
//    所以同一个向导可能「格式合法但这一步做不了」，那不是矛盾，是两回事。
//
//  这一层不碰界面、不碰文件系统：路径存不存在、文件能不能打开由上层判。
// =============================================================================

// 一个参数的规格：叫什么、什么类型、是否必填、取值约束。
struct WizardParamSpec {
    // 参数名（与 WizardParam::name 对应）。
    QString name;
    // 参数类型。
    WizardParamType type = WizardParamType::String;
    // 是否必填。必填参数缺了就报错，**不拿默认值顶**。
    bool required = false;
    // 字符串参数的取值白名单。非空表示只能取这些值之一。
    QStringList allowedValues;
    // 整数参数的取值白名单。非空表示只能取这些值之一（例如页码起始号只能是 1）。
    QVector<qint64> allowedNumbers;
    // 取值范围（整数与小数参数用）。hasRange 为 true 时才看最小值/最大值。
    bool hasRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
    // 可选参数的默认值（界面显示用）。必填参数这一项留空。
    QString defaultValue;
    // 这个参数是什么意思（中文，界面提示用）。
    QString description;
    // 字符串参数的**前缀**白名单。
    //
    // 为什么需要它：有的参数是「取值带一个数」的形式（例如拆分口径的
    // "count:5" 表示每 5 页一份）。allowedValues 是精确匹配，光靠它
    // 放不开这种写法，只能加一条前缀规则。
    //
    // 判定：取值以其中一个前缀开头就算通过（后面的部分由执行层解析与校验，
    // 这里不重复解析一遍）。
    //
    // 为什么放在结构体末尾：规格表用聚合初始化逐个构造，
    // 字段插在中间会让所有构造点错位；放末尾则默认值兜住，老构造点不受影响。
    QStringList allowedPrefixes;
};

// 一个动作的规格：能力边界 + 参数表。
struct WizardActionSpec {
    // 动作类型。
    WizardActionType type = WizardActionType::Unknown;
    // 英文名（与 wizardActionTypeName 一致）。
    QString name;
    // 中文名（与 wizardActionTypeTitle 一致）。
    QString title;
    // 本程序在动作向导里**已经接了**这一步吗？
    //
    // 界面层靠它区分两种不同的实话：
    //   - false：这一步本程序还没接（请用户用对应的功能入口单独做）；
    //   - true：这一步接了，但参数可能超出本程序能做的范围。
    bool wired = false;
    // 能力边界说明：中文一句话，说清这一步能做什么、不能做什么。
    QString capability;
    // 这一步的参数规格。
    QVector<WizardParamSpec> params;
};

// =============================================================================
//  解析结果：每种动作一份，字段就是这一步真正要用的东西。
// =============================================================================

// 「打开文件」的解析结果。
struct WizardOpenFileAction {
    // 要打开的 PDF 路径（存不存在由上层判）。
    QString path;
};

// 「合并文档」的解析结果。
struct WizardMergeAction {
    // 要合并的源文件路径，按合并顺序排列。
    QStringList inputPaths;
    // 合并结果写到哪儿。
    QString outputPath;
};

// 「拆分文档」的解析结果。
struct WizardSplitAction {
    // 拆出来的文件写到哪个目录。
    QString outputDirectory;
    // 拆分口径："range"（按区间）或 "each"（逐页）。
    QString mode = QStringLiteral("range");
    // 页区间列表（mode 为 "range" 时用）。
    QStringList ranges;
};

// 「添加书签」的解析结果。
struct WizardBookmarkAction {
    // 书签标题。
    QString title;
    // 书签指向第几页（1 基）。
    qint64 pageIndex = 1;
};

// 「添加页码」的解析结果。
struct WizardPageNumbersAction {
    // 页码位置（本程序只支持 "bottomRight"）。
    QString position = QStringLiteral("bottomRight");
    // 页码起始号（本程序只支持 1）。
    qint64 startNumber = 1;
};

// 「添加水印」的解析结果。
struct WizardWatermarkAction {
    // 水印文字。
    QString text;
    // 透明度，0 到 1。
    double opacity = 0.15;
    // 水印位置（本程序只支持 "center"）。
    QString position = QStringLiteral("center");
};

// 「加密」的解析结果。
struct WizardEncryptAction {
    // 用户口令。
    QString userPassword;
    // 所有者口令。
    QString ownerPassword;
    // 允许打印吗。
    bool allowPrinting = true;
};

// 「转换格式」的解析结果。
struct WizardConvertAction {
    // 目标格式（pdf / docx / xlsx / pptx / ofd / txt / md / html 之一）。
    QString format;
    // 转换结果写到哪儿。
    QString outputPath;
};

// 「打印」的解析结果。
struct WizardPrintAction {
    // 打印机名。空表示用默认打印机。
    QString printerName;
    // 打印几份，1 到 99。
    int copies = 1;
    // 纸张规格。空表示用打印机默认。
    QString paperSize;
    // 页码范围，形如 "1-3,5"。空表示全部页。
    //
    // 原样保留：这一层不解析页码范围（那是执行打印那一层的事），
    // 只保证它是个字符串。
    QString pageRange;
};

// 「结束」的解析结果。
struct WizardEndAction {
    // 「结束」不带参数：它只是向导自己声明的收尾点。
};

// 一个动作的解析结果。
//
// 为什么用一个大结构体而不是变体：十种动作的字段互不相同，变体在 Qt 里
// 只能靠 qvariant 加人工判别，反而更容易用错；这里用 type 指明哪一份
// 子结构体有效，读起来一目了然。
struct WizardActionData {
    // 这次解析的是哪一种动作（与下面哪一份子结构体有效对应）。
    WizardActionType type = WizardActionType::Unknown;
    WizardOpenFileAction openFile;
    WizardMergeAction merge;
    WizardSplitAction split;
    WizardBookmarkAction bookmark;
    WizardPageNumbersAction pageNumbers;
    WizardWatermarkAction watermark;
    WizardEncryptAction encrypt;
    WizardConvertAction convert;
    WizardPrintAction print;
    WizardEndAction end;
};

// =============================================================================
//  参数查找
// =============================================================================

// 这个参数在不在参数表里。
bool hasWizardParam(const QVector<WizardParam>& params, const QString& name);

// 找到参数。找不到返回 nullptr。
const WizardParam* findWizardParam(const QVector<WizardParam>& params, const QString& name);

// =============================================================================
//  强类型取值
//
//  两种用法：
//    - 通用取值（不带动作类型）：只判「参数在不在」「类型对不对」。
//    - 按动作规格取值（带动作类型）：再多判「取值合不合法」
//      （白名单、取值范围、必填不能是空值）。
//
//  失败时把中文原因写进 error，三种情况分开说：
//    - 参数不存在：「动作参数「path」不存在」
//    - 类型不对：「动作参数「path」的类型应为字符串，实际为整数」
//    - 取值非法：「动作「打印」的参数「copies」取值必须在 1 到 99 之间，当前为 0」
//
//  这几个函数**不做默认值兜底**：可选参数先用 hasWizardParam 问一句，
//  没给就当没填（由调用方用规格里的默认值），**不拿默认值把「没给」盖掉**。
// =============================================================================

// 取一个字符串参数。
bool takeString(const QVector<WizardParam>& params, const QString& name, QString* out, QString* error);
// 取一个整数参数。
bool takeInteger(const QVector<WizardParam>& params, const QString& name, qint64* out, QString* error);
// 取一个小数参数（值必须有限，NaN 与无穷大算取值非法）。
bool takeNumber(const QVector<WizardParam>& params, const QString& name, double* out, QString* error);
// 取一个布尔参数。
bool takeBoolean(const QVector<WizardParam>& params, const QString& name, bool* out, QString* error);
// 取一个字符串列表参数（列表里不许有空项，空项算取值非法）。
bool takeStringList(const QVector<WizardParam>& params, const QString& name, QStringList* out, QString* error);

// 按动作规格取一个字符串参数（额外校验白名单与必填非空）。
bool takeString(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                QString* out, QString* error);
// 按动作规格取一个整数参数（额外校验取值白名单与取值范围）。
bool takeInteger(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                 qint64* out, QString* error);
// 按动作规格取一个小数参数（额外校验取值范围）。
bool takeNumber(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                double* out, QString* error);
// 按动作规格取一个布尔参数。
bool takeBoolean(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                 bool* out, QString* error);
// 按动作规格取一个字符串列表参数（额外校验必填不能是空列表）。
bool takeStringList(WizardActionType actionType, const QVector<WizardParam>& params, const QString& name,
                    QStringList* out, QString* error);

// =============================================================================
//  总入口与动作清单
// =============================================================================

// 把一步动作的参数解析成该动作可直接消费的结构。
//
//  失败时把中文原因写进 error：缺哪个参数、类型对不对、取值合不合法。
//  动作类型不认识（WizardActionType::Unknown）时明确报错，不猜。
bool parseWizardAction(WizardActionType type, const QVector<WizardParam>& params,
                        WizardActionData* out, QString* error);

// 本程序在动作向导里支持的动作清单（10 类，按面板顺序排列）。
//
// 每一项都带能力边界说明，界面层可以直接拿去回答
// 「这一步本程序能不能做、做到什么程度」。
QVector<WizardActionSpec> wizardActionCatalog();

// 某个动作的规格。找不到（Unknown）返回 nullptr。
const WizardActionSpec* wizardActionSpec(WizardActionType type);

// 某个动作的能力边界说明。找不到返回空串。
QString wizardActionCapability(WizardActionType type);

}  // namespace aipdf::doc
