#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace aipdf::doc {

// 动作向导支持的动作类型。
//
// 为什么用枚举而不是纯字符串：向导的每一步都要被校验（动作类型、参数名、
// 参数类型、取值范围），枚举让「这一步是什么动作」在编译期就有唯一答案；
// 字符串只在 JSON 里露面，用来兼容外部导入的向导。
enum class WizardActionType {
    Unknown = 0,     // 未知动作（读进来的坏数据会落到这里）
    OpenFile,        // 打开文件
    MergeDocuments,  // 合并文档
    SplitDocument,   // 拆分文档
    AddBookmark,     // 加书签
    AddPageNumbers,  // 加页码
    AddWatermark,    // 加水印
    Encrypt,         // 加密
    ConvertToFormat, // 转换为另一格式
    Print,           // 打印
    End              // 结束
};

// 参数的值类型。
enum class WizardParamType {
    String = 0,
    StringList,
    Integer,
    Number,
    Boolean
};

// 一个步骤参数。
//
// 为什么参数带类型而不是一律存成字符串：类型本身就是校验的一部分。
// 「打开文件」的 path 必须是字符串；写成整数就是配置错了，
// 必须在跑之前报出来，而不是等到执行时才发现文件打不开。
struct WizardParam {
    // 参数名。同一动作里不能重名。
    QString name;
    // 参数类型。
    WizardParamType type = WizardParamType::String;
    // 各类型的值分别放在哪个字段里：只有与 type 对应的那个字段有意义。
    QString stringValue;
    QStringList listValue;
    qint64 integerValue = 0;
    double numberValue = 0.0;
    bool booleanValue = false;
};

// 造参数的几个小工具。
//
// 为什么提供而不是让调用方直接填结构体：调用方填错类型的机会很大
// （比如把 integerValue 填进 stringValue），校验时只会报「类型不对」，
// 而这里能保证「类型」与「放值的字段」永远一致。
WizardParam wizardStringParam(const QString& name, const QString& value);
WizardParam wizardStringListParam(const QString& name, const QStringList& value);
WizardParam wizardIntegerParam(const QString& name, qint64 value);
WizardParam wizardNumberParam(const QString& name, double value);
WizardParam wizardBooleanParam(const QString& name, bool value);

// 向导里的一步（一个结构化动作）。
struct WizardStep {
    // 动作类型。
    WizardActionType type = WizardActionType::Unknown;
    // 动作类型名（JSON 里用的英文名，例如 "openFile"）。
    //
    // 为什么单独存一份而不是只靠 type 反推：从 JSON 读进来的未知动作，
    // type 会落到 Unknown，但原始名字得留着——报错时必须说清楚
    // 「是哪种动作不对」，否则用户无从下手。
    QString typeName;
    // 这一步在界面上显示的名字。留空就显示动作类型的中文名。
    QString title;
    // 这一步的参数。
    QVector<WizardParam> params;
};

// 造一步：自动把 typeName 填成规范名。
WizardStep makeWizardStep(WizardActionType type, const QVector<WizardParam>& params = {});

// 一份向导：有序的一串步骤。
struct ActionWizard {
    // 向导 id。保存时留空会自动生成一个。
    QString id;
    // 向导名。不能为空。
    QString name;
    // 备注。界面上显示用。
    QString description;
    // 步骤，按执行顺序排列。
    QVector<WizardStep> steps;
};

// 动作类型 ↔ 英文名的互转。
//
// 英文名是存进 JSON 的稳定标识：界面上的中文名会改，英文名不会。
QString wizardActionTypeName(WizardActionType type);
// 动作类型的中文名（报错与摘要里给人看的）。
QString wizardActionTypeTitle(WizardActionType type);
// 英文名 → 动作类型。认不出来时 ok 置为 false，返回 Unknown。
WizardActionType wizardActionTypeFromName(const QString& name, bool* ok = nullptr);

// 校验一份向导。
//
// 校验不过就返回 false，并把中文原因写进 error（原因里会写明是第几步、
// 缺哪个参数、类型对不对）。绝不静默忽略坏步骤：向导是用户攒下来的编排，
// 悄悄跳过一步会让人以为整条流程都跑过了。
//
// 规则：
//   - 向导名不能为空；
//   - 步骤列表不能为空；
//   - 每一步的动作类型必须是受支持的；
//   - 每一步必须带齐该动作的必填参数；
//   - 参数名必须是该动作认识的参数（不认识的参数也报错，避免拼错字被吞掉）；
//   - 参数类型必须与动作要求的一致；
//   - 取值有约束的参数（页码位置、转换格式、打印份数……）取值必须合法。
bool validateActionWizard(const ActionWizard& wizard, QString* error = nullptr);

// 序列化成 JSON / 从 JSON 读回来。
//
// 读回来之后**不自动校验**：调用方可能想先看看坏数据长什么样再报错，
// 校验交给 validateActionWizard。
QByteArray serializeActionWizard(const ActionWizard& wizard, QString* error = nullptr);
bool deserializeActionWizard(const QByteArray& json, ActionWizard* out, QString* error = nullptr);

// 存一份向导到设置文件。
//
// 存的位置由 settingsKey 决定：它是设置文件里的分组名，不同的 settingsKey
// 存到不同的分组，互不干扰。
//
// **同名策略**：同名向导既不覆盖也不报错，两份都留着。
// 理由：向导名是用户随手起的标签，真正的身份是 id；覆盖会悄悄丢掉用户
// 另一份编排（比报错更糟），报错又会逼用户为了保存而改名字（同样别扭）。
// 需要按名字找向导时用 findActionWizardByName，需要区分两份时用 id。
//
// id 留空时自动生成，并**写回 wizard**（调用方需要它来加载/删除这一份）。
// 存之前先校验向导，不合法直接返回 false。
bool saveActionWizard(ActionWizard* wizard, const QString& settingsKey, QString* error = nullptr);

// 按 id 读一份向导。读不到（id 为空或不存在）时返回 false 并写明原因。
bool loadActionWizard(const QString& id, const QString& settingsKey, ActionWizard* out,
                       QString* error = nullptr);

// 列出某个分组下的全部向导（按 id 排序，保证顺序稳定）。
QVector<ActionWizard> listActionWizards(const QString& settingsKey);

// 按名字找向导：返回分组里**第一份**同名向导。
// 同名可能有多份（见 saveActionWizard 的同名策略），所以找不到时返回 false。
bool findActionWizardByName(const QString& settingsKey, const QString& name, ActionWizard* out);

// 删一份向导。不存在时返回 false（不崩）。
bool deleteActionWizard(const QString& id, const QString& settingsKey);

// 上层注入的「执行一步」回调。
//
// 返回 true 表示这一步成功；失败时把中文原因写进 reason。
// 这一层不碰文件系统、不碰界面：真正干活的是上层给进来的这个回调。
using WizardStepExecutor = std::function<bool(const WizardStep& step, QString* reason)>;

// 某一步失败之后怎么办。策略必须显式，不能靠默认值蒙混过去。
enum class WizardFailurePolicy {
    StopOnFirstFailure = 0, // 一遇失败就停（默认：后面的步骤多半依赖前面的产物）
    ContinueOnFailure        // 失败也继续往下跑（适合互不依赖的一批动作）
};

// 单步的执行结果。
struct WizardStepResult {
    // 步骤下标（0 基）。
    int index = -1;
    // 动作类型与英文名。
    WizardActionType type = WizardActionType::Unknown;
    QString typeName;
    // 这一步是否真的调用了执行回调。
    bool executed = false;
    // 这一步是否成功（没执行时为 false）。
    bool succeeded = false;
    // 中文原因：失败时是失败原因；没执行时是没执行的原因。
    QString reason;
};

// 整份向导跑完之后的报告。
struct WizardRunReport {
    // 是否全部成功。
    bool ok = false;
    // 总步数、真正执行的步数、成功步数、失败步数、因策略而没执行的步数。
    int totalSteps = 0;
    int executedSteps = 0;
    int succeededSteps = 0;
    int failedSteps = 0;
    int skippedSteps = 0;
    // 每一步的结果，顺序与向导里的步骤顺序一致。
    QVector<WizardStepResult> results;
    // 纯文本摘要（给人看的）。
    QString summary;
};

// 跑一份向导。
//
// 流程：先校验（不通过就一步都不跑，报告里如实写明没跑的原因），
// 然后按顺序把每一步交给上层给的回调执行，逐步记录结果。
//
// 「结束」动作（WizardActionType::End）执行成功之后，后面的步骤不再执行，
// 记为未执行——「结束」就是向导自己声明的收尾点。
//
// policy 决定某一步失败之后是否继续往下跑。
WizardRunReport runActionWizard(const ActionWizard& wizard,
                                const WizardStepExecutor& executor,
                                WizardFailurePolicy policy = WizardFailurePolicy::StopOnFirstFailure,
                                QString* error = nullptr);

}  // namespace aipdf::doc
