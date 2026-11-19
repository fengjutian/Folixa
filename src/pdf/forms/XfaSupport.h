#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 一份 XFA 表单的处理结果。
//
// 为什么分这么细：XFA 有「静态」与「动态」两种完全不同的东西。
// 静态 XFA 的字段结构就写在模板里，读出来导出成 CSV 是有意义的；
// 动态 XFA 的行为取决于脚本，本程序不执行脚本，硬把字段抽出来会给出
// 一份「看着能用、其实不对」的数据。所以必须让调用方一眼看出是哪一种。
enum class XfaStatus {
    NotXfa,   // 这份 PDF 不是 XFA 表单（没有 /XFA 数据）。
    Static,   // 静态 XFA：字段已解析出来。
    Dynamic,  // 含动态逻辑：字段仍会解析，但动态部分不执行。
    Error,    // 是 XFA 表单，但解析失败（原因在 error 里）。
};

// XFA 模板里的一个字段。
struct XfaField {
    // 字段名（模板里的 name 属性）。没有 name 的字段不会进这份清单。
    QString name;
    // 字段标签：模板里的说明文字（ui/text/value 或 caption/value/text）。
    // 取不到就退回 name。
    QString label;
    // 字段类型：textField / checkBox / radioButton / choiceList / dateField /
    // numberField / signatureField / imageField / unknown。
    QString type;
    // 字段当前值（模板里的 value/text）。
    QString value;
    // 是否必填（模板里 required="true"）。
    bool required = false;
};

// XFA 表单的检查与提取结果。
struct XfaReport {
    // 整体状态。
    XfaStatus status = XfaStatus::NotXfa;
    // 这份 PDF 里有没有 /XFA 数据。
    bool isXfa = false;
    // 有没有动态逻辑（脚本 / 校验 / 计算 / 绑定等）。
    //
    // 注意：动态不等于「读不了」。静态字段照样抽得出来，
    // 但表单在阅读器里的实际行为取决于脚本，本程序不执行。
    bool hasDynamicLogic = false;
    // 命中了哪些动态节点名（script / validation / calculate / bind…）。
    QStringList dynamicMarkers;
    // 解析出来的字段。
    QVector<XfaField> fields;
    // 因为没有 name 而被跳过的字段数（跳过的字段没法导出成 CSV 的行）。
    int skippedWithoutName = 0;
    // 解析失败时的中文原因；成功时为空。
    QString error;
    // 一行中文摘要。
    QString summary() const;
};

// 读一份 PDF 里的 XFA 表单，抽出字段清单。
//
// 做什么：
//   * 找 /XFA 数据（catalog 上或 /AcroForm 里都认——
//     规范把 /XFA 放在 /AcroForm 字典里，但确实有生成器写在 catalog 上，
//     两处都找才不至于漏掉）；
//   * 解析 XDP 包里的 <template>，抽出字段名 / 标签 / 类型 / 当前值 / 是否必填；
//   * 检测动态逻辑（<script> / <validation> / <calculate> / <bind> 等）并如实标出。
//
// 不做什么（边界）：
//   * **不执行任何脚本**。带动态逻辑的表单，本程序只做静态字段提取；
//   * 不做 XFA 表单渲染，也不填值——PDFium 没有 XFA 引擎，
//     渲染出来的是空白页，交给本程序只会得到一份看起来能用的假数据；
//   * 不做 XFA → AcroForm 的转换。
//
// 不是 XFA 的 PDF 不是错误：status 会是 NotXfa，error 为空。
// 是 XFA 但 XML 损坏时 status 是 Error，error 里写中文原因。
//
// 需要 qpdf：/XFA 是对象级的间接引用，只有真正的 PDF 解析器才能按对象找到它。
// 本次构建没启用 qpdf（AIPDF_WITH_QPDF 默认关）时，status 是 Error，
// error 里写明「本次构建没有启用 qpdf」——明确说做不到，不假装能读。
XfaReport inspectXfa(const QByteArray& pdfBytes, QString* error = nullptr);

// 把字段清单导出成 CSV。
//
// 格式约定：
//   * UTF-8 **带 BOM**（Windows 上的 Excel 靠 BOM 认编码，不然中文表头是乱码）；
//   * 行尾 CRLF（Excel 的原生行尾）；
//   * 表头是中文：名称,标签,类型,当前值,是否必填；
//   * 字段里的逗号、引号、换行按 RFC 4180 用双引号包起来。
//
// 没有字段时返回的也是一份只有表头的 CSV（表头始终在，调用方不必特判）。
QByteArray exportXfaFieldsToCsv(const XfaReport& report);

// ---------------------------------------------------------------- 写值 ----

// 一个字段的写值结果。
//
// 为什么逐项给结果而不是只给一个总结果：一次可能要改好几个字段，
// 其中一个字段只读、另一个字段名写错了，都得让人看得见；
// 只回一句「失败」等于让用户自己猜是哪一处。
struct XfaFieldUpdate {
    // 字段名。
    QString field;
    // 实际写进去的值。
    QString value;
    // 非空表示这一项没改成，里面是中文原因。
    QString error;
    // 这一项是不是成功了。
    bool ok() const { return error.isEmpty(); }
};

// 写值 / 执行脚本的结果。
struct XfaWriteReport {
    // 整体是否成功：所有请求项都成功才是 true。
    bool ok = false;
    // 整体失败的原因（中文）。逐项失败的原因在 updates 里。
    QString error;
    // 逐项结果。
    QVector<XfaFieldUpdate> updates;
    // 非致命提示（脚本引用了不存在的字段、某些脚本没有执行之类的）。
    QStringList warnings;
    // 一行中文摘要。
    QString summary() const;
};

// 改一个 XFA 字段的值，并把改过的 PDF 写进 outPdf。
//
// 做什么：
//   * 在 XFA 的数据集里找到该字段的数据节点（<xfa:Field> 里的 <value><text>）；
//   * 只改值的那几个字节，XML 结构一个节点都不动（重新序列化会把
//     前缀、缩进、空元素写法全改掉，阅读器未必还认）；
//   * 把改过的 XFA 流写回 PDF，outPdf 里是改完的整份 PDF。
//
// 逐项失败会给出中文原因：字段不存在 / 字段只读 / 值与字段类型对不上。
// 只要有一项成功，outPdf 里就是改过的 PDF；全部失败时 outPdf 不动。
//
// 需要 qpdf：/XFA 是对象级的间接引用，只有真正的 PDF 解析器才能按对象定位并写回。
// 本次构建没启用 qpdf（AIPDF_WITH_QPDF 默认关）时，报告里写明原因，
// 不假装能改。
XfaWriteReport writeXfaFieldValue(const QByteArray& pdfBytes, const QString& fieldName,
                                  const QString& value, QByteArray* outPdf, QString* error = nullptr);

// 一次改多个字段：values 是「字段名 → 新值」的列表。
//
// 语义与 writeXfaFieldValue 相同，只是省得反复解析同一份 PDF。
XfaWriteReport writeXfaFieldValues(const QByteArray& pdfBytes,
                                  const QVector<QPair<QString, QString>>& values,
                                  QByteArray* outPdf, QString* error = nullptr);

// 执行 XFA 里的计算脚本（白名单子集），把算出来的值写进字段并写回 PDF。
//
// 声明在 XfaScriptRunner.h：求值脚本要用 FormJsEvaluator，
// 这一层（字段提取、写值）不该被它拖住。

// ---------------------------------------------------------------- 脚本 ----

// XFA 里的一段脚本。
struct XfaScriptUnit {
    // 脚本名（<exObject name="..."> 的 name 属性）；没有名字就空。
    QString name;
    // 脚本代码。
    QString code;
    // 脚本属于哪个字段（脚本里的 this 指向谁）；数据集里的脚本没有归属字段。
    QString owner;
    // 脚本的角色：calculate（计算）/ event（事件）/ validation（校验）/ script（其它）。
    QString role;
};

// 一份 XFA 表单里的脚本清单。
struct XfaScriptListing {
    bool ok = false;
    // 读不出脚本时的中文原因。
    QString error;
    // 找到的脚本（模板里的、数据集里的都算）。
    QVector<XfaScriptUnit> scripts;
};

// 列出这份 PDF 里的 XFA 脚本。
//
// 为什么单独给一个接口：求值脚本需要 FormJsEvaluator，
// 而 XfaSupport 这一层（字段提取、写值）不该被它拖住——
// 只看字段的人不必先装上脚本求值器。
XfaScriptListing listXfaScripts(const QByteArray& pdfBytes);

}  // namespace aipdf::pdf
