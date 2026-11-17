#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>

#include "doc/UniversalConverter.h"

namespace aipdf::doc {

// ============================================================ 输入来源 =====

// 这次排版用的是哪一路输入。
//
// 为什么用枚举而不是 bool：界面要跟用户说清「这次用的是纯文本还是富文本」，
// 而「不是文本类输入」（表单套模板那种）是第三种状态，bool 表达不了。
enum class ConversionSourceKind {
    Unknown,    // 不是「文本类输入 → PDF」
    PlainText,  // 纯文本
    RichText,   // 富文本（HTML）
};

// 失败的具体原因（结构化）。
//
// 为什么领域层不直接给中文句子：doc 层没有翻译上下文，给用户看的中文句子
// 一律由 app 层按这个枚举拼（那边才有 i18n::tr）。
// error 里仍然留一句说明，是给直接调用 doc 层的场合兜底用的。
enum class ConversionErrorKind {
    None,
    NoContent,    // 输入里没有内容可转
    NoHtmlBody,   // HTML 里没有正文
    LayoutFailed, // 排版本身失败（编码、字体等）
};

// ============================================================ 网页图片 ====

// 取图片字节的回调：按地址返回图片的原始字节。
//
// 为什么做成注入的：领域层不碰网络（doc 层不该有副作用，单测里也跑不了网络），
// 单测要能塞一张假图片进来。
//
// **空回调 = 这次不下载图片**（行为与从前完全一致）。
using ImageFetcher = std::function<QByteArray(const QUrl&)>;

// 一张图没带进来的原因（结构化）。
//
// 为什么是枚举而不是一句现成的中文：领域层只给事实，
// 给用户看的中文句子由 app 层拼（那边才有 i18n::tr）。
enum class HtmlImageIssueKind {
    DownloadFailed,     // 没拿到字节（地址不对 / 下载失败 / 超时）
    UnsupportedFormat,  // 格式不支持（TIFF / GIF / WebP 等）
    DecodeFailed,       // 解不开（数据坏了 / 被截断）
    TooLarge,           // 像素太大，一页放不下
    LayoutFailed,       // 排不进这份 PDF（合成失败）
    LimitReached,       // 页面里的图太多，超出单次上限
};

// 一张没带进来的图。
struct HtmlImageIssue {
    // 图片地址。
    QString url;
    // 原因（结构化）。
    HtmlImageIssueKind kind = HtmlImageIssueKind::DownloadFailed;
    // 技术细节（ASCII）：到底哪儿不对。
    QString detail;
};

// 这次带图的结果。
struct HtmlImageOutcome {
    // 正文里找到的图片张数（同一个地址只算一次）。
    int found = 0;
    // 成功写进 PDF 的张数。
    int embedded = 0;
    // 这次是不是真的去下载过（没有注入取字节回调时为 false）。
    bool attempted = false;
    // 没带进来的图（逐条带原因）。
    QVector<HtmlImageIssue> issues;
};

// 正文里的一张图片引用。
struct HtmlImageRef {
    // 绝对地址（按 baseUrl 解析；baseUrl 为空时保留原样）。
    QString url;
    // 在正文纯文本里的字符偏移（-1 = 位置未知）。
    int textOffset = -1;
};

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
    // 失败的具体原因（结构化）。界面层按它拼人话。
    ConversionErrorKind errorKind = ConversionErrorKind::None;
    // 这次排版用的是哪一路输入（剪贴板转换时才有意义）。
    ConversionSourceKind sourceKind = ConversionSourceKind::Unknown;
    // 这次带图的结果（网页转换时才有内容）。
    HtmlImageOutcome imageOutcome;
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

// 把剪贴板里的**富文本**（HTML）排成一份 PDF。
//
// 为什么要有这一条：剪贴板里从 Word / 网页复制过来的内容带 HTML，
// 只取纯文本会把标题层级、段落结构、列表全丢掉，出来的 PDF 是一坨。
// 剪贴板里有 HTML 时应当优先按 HTML 排版（调用方负责这个优先级）。
//
// 剪贴板里的**图片不支持**：本程序没有把图片排成 PDF 的能力
// （doc/UniversalConverter 只做文本类文件），硬做只能出一张没有文字层的图，
// 与其给一个半成品，不如明确说「图片不支持」。
//
// HTML 里没有正文（整段是脚本、样式、导航）时**直接失败**，不生成空白 PDF。
ConversionResult clipboardHtmlToPdf(
    const QString& html,
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
    // 正文里的图片：绝对地址，去重后按出现顺序。
    //
    // 只有 <img src> 算数：srcset 是一组候选（按屏幕宽度挑），
    // 本程序不渲染 CSS，也就无从判断该挑哪一张。
    QVector<HtmlImageRef> images;
};

// 抽 HTML 正文：去掉 script / style / 导航 / 页眉页脚，只留文章内容。
//
// 为什么自己写而不用 doc/UniversalConverter 的 HTML 解析：那份只做「去标签留文字」，
// 不认识 nav / header / footer 这些导航块——抓回来的网页会把整条菜单栏
// 也排进 PDF，用户要自己翻到正文。
//
// baseUrl 用来把正文里的相对链接解析成绝对地址；为空时按链接原样保留。
HtmlBody extractHtmlBody(const QString& html, const QUrl& baseUrl = QUrl());

// 把抽好的正文排成 PDF（网页与剪贴板富文本共用这一份）。
//
// title 是**生效的标题**：网页传自己的 <title>，剪贴板传调用方给的标题
// （剪贴板里的 <title> 是来源文档的标题，不是这一份的）。
//
// fetcher 非空时会把正文里的图片一并带进去：按地址取字节 → 解码 →
// 单独排成一页，插在正文里它出现的位置附近。**空 fetcher = 不下载图片**
// （与从前完全一致），结果里的 imageOutcome.attempted 会说明这一点。
ConversionResult htmlBodyToPdf(const HtmlBody& body, const QString& title,
                               const UniversalConverterOptions& options = UniversalConverterOptions(),
                               const ImageFetcher& fetcher = ImageFetcher());

// 把 HTML 正文转成 PDF。
//
// 网页里没有正文（整页都是脚本、样式、导航）时**直接失败**，不生成空白 PDF。
//
// fetcher 的含义与 htmlBodyToPdf 一样：非空 = 连图片一起带进去。
ConversionResult htmlToPdf(
    const QString& html,
    const QUrl& baseUrl = QUrl(),
    const UniversalConverterOptions& options = UniversalConverterOptions(),
    const ImageFetcher& fetcher = ImageFetcher());

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
