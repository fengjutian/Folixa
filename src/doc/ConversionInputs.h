#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include "doc/UniversalConverter.h"

namespace aipdf::doc {

// ============================================================ 转换结果 =====
//
// 三个输入（剪贴板 / 网页 / 表单文件）里，前两个都是「文本 → PDF」，
// 失败方式也一样（没有内容 → 明确报错；字体缺字形 → 成功但有警告），
// 界面上要显示的话术也就一样。合成一个结构体，界面就只写一遍分支。
struct ConversionResult {
    // 成功为真；失败时 bytes 为空、error 写明原因（中文）。
    bool ok = false;
    // 生成的 PDF 字节。
    QByteArray bytes;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 警告（中文）。成功但有损时非空（例如有字符显示不出来）。
    QString warning;
    // 文档标题（网页取 <title>；剪贴板由调用方给）。
    QString title;
    // 生成的页数。
    int pageCount = 0;
    // 被替换成替代字形的字符数（中文等标准 14 字体显示不了的字符）。
    int replacedCharacterCount = 0;
};

// ---------------------------------------------------------- 剪贴板 ----

// 把剪贴板里的文本排成一份 PDF。
//
// 文本为空（或者只有空白）时**直接失败**：静默生成一份空白 PDF 会让人
// 以为「转换成功了」，回头打开才发现里面什么都没有。
//
// title 为空时不加标题行；给了标题就排在正文前面。
//
// 为什么参数直接用 UniversalConverterOptions：纸张、边距、字号、字体这几项
// 在仓库里只有一张表（doc/UniversalConverter），再开一张必然和它漂移。
ConversionResult clipboardTextToPdf(
    const QString& text,
    const QString& title = QString(),
    const UniversalConverterOptions& options = UniversalConverterOptions());

// ---------------------------------------------------------- 网页 ----

// 从 HTML 里抽出来的正文。
struct HtmlBody {
    // 标题：<title> 优先，正文里的第一个 <h1> 次之。
    QString title;
    // 正文纯文本。段落之间用空行分开。
    QString text;
    // 正文里的链接：已按 baseUrl 解析成绝对地址，去重后按出现顺序。
    QStringList links;
};

// 抽 HTML 正文：去掉 script / style / 导航 / 页眉页脚，只留文章内容。
//
// 为什么自己写而不用 doc/UniversalConverter 的 HTML 解析：那份只做「去标签留文字」，
// 不认识 nav / header / footer 这些导航块——抓回来的网页会把整条菜单栏
// 也排进 PDF，用户要自己翻到正文。
//
// baseUrl 用来把正文里的相对链接解析成绝对地址；为空时按链接原样保留。
HtmlBody extractHtmlBody(const QString& html, const QUrl& baseUrl = QUrl());

// 把 HTML 正文转成 PDF。
//
// 网页里没有正文（整页都是脚本、样式、导航）时**直接失败**，不生成空白 PDF。
ConversionResult htmlToPdf(
    const QString& html,
    const QUrl& baseUrl = QUrl(),
    const UniversalConverterOptions& options = UniversalConverterOptions());

// ---------------------------------------------------------- 表单文件 ----

// 表单数据文件的格式。
enum class FormDataFormat {
    Unknown,
    // %FDF-1.2（Acrobat 认的那种）
    Fdf,
    // <xfdf><fields><field name="…"><value>…</value></field></fields></xfdf>
    Xfdf,
    // <form-data><field name="…">值</field></form-data>
    Xml,
};

// 三种格式共用的中间结构：**字段名 → 值**。
//
// 为什么一定要有这一层：FDF、XFDF、XML 三种格式的写法完全不同，
// 但「往模板里填」只关心名字和值。解析归解析，填表归填表。
struct FormFieldValue {
    QString name;
    QString value;
};

// 解析结果。
struct FormDataParseResult {
    bool ok = false;
    // 认出来的格式。
    FormDataFormat format = FormDataFormat::Unknown;
    // 读到的字段（按文件里的顺序）。
    QVector<FormFieldValue> values;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 按扩展名判断格式（.fdf / .xfdf / .xml）；认不出来是 Unknown。
FormDataFormat formDataFormatForFileName(const QString& fileName);

// 解析表单数据文件。格式先按扩展名判断，扩展名不认识时按内容再猜一次。
//
// 失败（文件是空的 / 里面没有字段 / 格式认不出）时 ok 为假、error 写明原因，
// 绝不返回一份「看起来成功但一个字段都没有」的结果。
FormDataParseResult parseFormDataFile(const QByteArray& bytes, const QString& fileName);

// 填不上的字段：逐个列出，不静默跳过。
struct FormFieldIssue {
    // 字段名。
    QString name;
    // 原因（中文）。
    QString reason;
};

// 套模板的结果。
struct FormFillReport {
    bool ok = false;
    // 填好的 PDF 字节（**仍然是可填写的表单**，不是拍平的）。
    QByteArray bytes;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 填上的字段名。
    QVector<QString> applied;
    // 填不上的字段（逐个带原因）。
    QVector<FormFieldIssue> issues;
    // 模板里有、但表单数据没覆盖的字段（保持原样）。
    QVector<QString> untouched;
    // 结果的页数。
    int pageCount = 0;
    // 模板里一共有多少个表单域。
    int templateFieldCount = 0;
};

// 把表单数据套进模板 PDF，填出一份**仍然可以继续填写**的 PDF。
//
// 模板里的字段按名字匹配；同名的一组字段（跨页的单选框）会一起填。
// 模板没有的字段、只读域、写入失败的字段都进 issues，逐个说明原因。
FormFillReport applyFormDataToTemplate(const QByteArray& templatePdf,
                                       const QVector<FormFieldValue>& values);

// 解析表单数据文件 + 套进模板，一步到位。
//
// 解析失败时 report.error 是解析的原因；模板为空时是「没有模板就没法填」。
FormFillReport applyFormDataFileToTemplate(const QByteArray& templatePdf,
                                           const QByteArray& dataBytes,
                                           const QString& fileName);

}  // namespace aipdf::doc
