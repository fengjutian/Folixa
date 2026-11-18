#pragma once

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一页里的一块内容：一个矩形区域 + 它在阅读顺序里的位置。
//
// rect 用的是 **PDF user space**（左下原点），和仓库里其它地方
// （pdf::MediaBox、PDFium 字符框、app/PageTransform）是同一套坐标。
// 写法是 QRectF(x, y, 宽, 高)：覆盖 [x, x+宽] × [y, y+高]，
// **y 是矩形的下边**（PDF 的 y 向上为正，所以 y 越大越靠上）。
// 页面自带的 /Rotate 没有被应用：调用方若按显示顺序排版，
// 先用 app/PageTransform 换算回用户空间再传进来。
//
// order 是**这一页内部**的阅读次序：小的先读。
// order 相同的区域按传入次序排（稳定、可预期）。
struct TaggedRegion {
    QRectF rect;    // PDF user space（左下原点），y 是下边
    int order = 0;  // 该区域在本页里的阅读顺序序号（小的先读）
};

// 给整份 PDF 写一棵真正的标签树：
//   * 内容流里每个显示操作符（Tj / TJ / ' / "）都被包进 marked content：
//     `/Tag <</MCID n>> BDC … EMC`，n 是该页内从 0 开始的 MCID；
//   * 每页页面字典加 /StructParents（键 = 页序号，从 0 连续）；
//   * catalog 加 /StructTreeRoot，文档加 /MarkInfo <</Marked true>>；
//   * /StructTreeRoot → Document → 每页一个 /P → 每行文本一个 /Span，
//     Span 的 /K 是该行包含的 MCID 整数数组。
//
// 语义要点：
//   * 区域按 order 升序排好后，写进标签树的 Span 顺序就是它——
//     **阅读顺序由标签树决定，与 MCID 的数字大小无关**；
//   * 标记是**渲染层**的：marked content 不改变页面外观，
//     写出来的 PDF 渲染结果与写之前逐像素一致；
//   * 区域与内容流的对应关系是「就近匹配」：把每个显示操作符按它算出来的
//     基线位置归到离它最近的那个区域。矩形与文字完全对不上时（一个都没落进
//     矩形里），退化成「按视觉顺序把整页的显示操作符均分成几段」，
//     一段对应一个区域——宁可粗一点，也不去内容流里做有风险的裁剪；
//   * 写之前先在内存里重开校验一次，写完再重开校验一次，两次都过才返回；
//   * 幂等：对**已经打过标签的 PDF** 再调用一次，会先摘掉上一轮的标记
//     （`/Tag <</MCID n>> BDC` 与配对的 EMC）再重新打，
//     不会把同一个显示操作符套两层 BDC，MCID 也就不会重复。
//
// 失败返回空 QByteArray，并通过 error 给出中文原因。
//
// 口径：pageRegions 为空（或者所有页都没有区域）时**原样返回输入字节**，
// 什么都不写。这不是错误：没有区域就没有可标记的东西，调用方不必特判。
// pageRegions 的下标是页序号；比页数少时缺的那些页不打标记（但仍然会有
// /StructParents，键要连续），比页数多时多出来的部分忽略。
QByteArray writeTaggedStructure(const QByteArray& pdfBytes,
                                 const QVector<QVector<TaggedRegion>>& pageRegions,
                                 QString* error = nullptr);

// 读回一棵标签树（用于测试与界面回显）：页序号 → 该页按树顺序排好的
// 区域序号列表。
//
// 「区域序号」是这一页的 Span 按**最小 MCID** 排序后的下标：
// 它给每个 Span 一个与内容流无关的稳定身份，于是返回的列表能看出
// 「树里的顺序」与「MCID 顺序」是否一致（一致时就是 0,1,2,…）。
// 没有标签树时返回空列表。
QVector<QVector<int>> readTaggedOrder(const QByteArray& pdfBytes, QString* error = nullptr);

// ---------------------------------------------------------------------------
// 语义标签树与行级标签树共用的那部分：扫描、分配 MCID、写标记内容、建
// /ParentTree。下面这些类型与函数是**共用件**，两个写手都走它。
// ---------------------------------------------------------------------------

// 内容流里扫描出来的一个文本显示操作符（Tj / TJ / ' / "）。
struct ScannedTextRun {
    // 基线起点与终点（PDF user space，左下原点）。
    QPointF origin;
    QPointF end;
    // 基线的 y 坐标。
    double baseline = 0.0;
    // 字号（用来判断行高、是不是标题）。
    double fontSize = 0.0;
    // 显示出来的文字（按字体的编码还原；还原不出来时为空）。
    QString text;
    // 字体名里带 Bold / Black / Heavy 之类（粗体启发式）。
    bool bold = false;
    // 属于本页的第几条内容流。
    int streamIndex = 0;
    // 内容流里的先后次序（稳定排序用）。
    int streamOrder = 0;
};

// 扫描出来的一行文字（按基线聚类得到）。
struct ScannedTextLine {
    // 行框（PDF user space）。y 是**下边**（PDF 的 y 向上为正）。
    QRectF bounds;
    // 这一行里最大的字号。
    double fontSize = 0.0;
    // 这一行是不是粗体。
    bool bold = false;
    // 这一行显示出来的文字（按显示次序拼起来）。
    QString text;
    // 这一行包含的文本运行下标（ScannedPage::runs 的下标）。
    QVector<int> runIndices;
};

// 扫描出来的一处图像（内容流里用 Do 画出来的图像 XObject）。
struct ScannedImage {
    // 图像在页面上的位置（PDF user space）。
    QRectF bounds;
    // 页资源字典里的资源名（例如 /Im1）。
    QString name;
    // 图像 XObject 在文件里的对象号（写 /OBJR 时用；-1 表示没有）。
    int objectId = -1;
    int generation = 0;
};

// 一页的扫描结果。
struct ScannedPage {
    // 全部文本显示操作符（按内容流里的次序）。
    QVector<ScannedTextRun> runs;
    // 按基线聚出来的行（从上到下）。
    QVector<ScannedTextLine> lines;
    // 画出来的图像。
    QVector<ScannedImage> images;
};

// 整份文档的扫描结果。
struct ScannedDocument {
    QVector<ScannedPage> pages;
    int pageCount() const { return pages.size(); }
};

// 扫一份 PDF 的内容流（**只读**，不改文件）。
//
// 扫出来的东西：每一页的文本显示操作符（带位置、字号、文字）、
// 按基线聚成的行、以及画出来的图像。
// 行级标签树按「区域」归类，语义标签树按「行 / 图 / 域」归类——
// 两边都要先有这份扫描结果，所以扫描放在共用件里。
bool scanDocumentContent(const QByteArray& pdfBytes, ScannedDocument* out, QString* error = nullptr);

// 一个对象（表单域的 widget 注解、页面上的图像 XObject）在文件里的位置。
//
// 为什么用对象号而不是 qpdf 的对象句柄：共用写手拿到的是纯数据计划，
// qpdf 的对象由写手自己按对象号找回来——纯数据计划才能被测试读懂、被打印。
struct ObjectRef {
    // 所在页（0 基）。
    int pageIndex = -1;
    // 对象在文件里的对象号（间接对象）。
    int objectId = -1;
    // 世代号。
    int generation = 0;
};

// 写标签树时的一个结构元素（纯数据描述）。
struct StructElementPlan {
    // 结构类型名（不带斜杠），例如 "Part"、"H1"。
    QString type;
    // /T（给人看的名字）。
    QString title;
    // /Alt（替代文本）。
    QString alt;
    // /ActualText（实际文字）。
    QString actualText;
    // /Lang（元素自身的语言）。
    QString language;
    // 所属页（0 基）；-1 表示不限定。
    int pageIndex = -1;
    // 继承父元素的页（不写 /Pg）：true 表示这一层不写 /Pg，由父元素决定。
    bool inheritPage = false;
    // 这个元素的内容来自哪些「块」（块下标 = 页内块序号）。
    QVector<int> blockIndices;
    // 这个元素的内容是这些注解（表单域的 widget），不是 MCID。
    QVector<ObjectRef> annotations;
    // 这个元素的内容是页面上的图像对象（/K 写成 /OBJR 引用）。
    QVector<ObjectRef> objectRefs;
    // 表头格的适用范围（/Scope）："Column" / "Row" / "Both"。空表示不写。
    QString scope;
    // 子元素在 StructWritePlan::elements 里的下标。
    QVector<int> childIndices;
    // 父元素在 StructWritePlan::elements 里的下标；-1 表示顶层。
    int parentIndex = -1;
};

// 一次写标签树的完整计划。
struct StructWritePlan {
    // 结构元素。习惯上 elements[0] 是根（/Document）。
    QVector<StructElementPlan> elements;
    // 根元素在 elements 里的下标。
    int rootIndex = 0;
    // 每页的「块 → 文本运行下标」。
    //
    // 「块」是调用方自己的编号：行级标签树里是「区域」，
    // 语义标签树里是「段落 / 表格 / 图 / 域」。
    // 一个块可以跨多个文本运行；一个文本运行只属于一个块。
    QVector<QVector<QVector<int>>> pageBlockRuns;
    // 页的 /StructParents 键。留空就按页序从 0 连续分配。
    QVector<int> pageStructParentKeys;
    // 文档级补充（PDF/UA 需要的那些）。
    QString documentLanguage;      // /Lang
    QString documentTitle;         // /Info /Title
    bool displayDocTitle = false;  // /DisplayDocTitle
    bool writeTabsStructureOrder = false;  // 给每页写 /Tabs /S
    QByteArray xmpMetadata;        // /Metadata（XMP）
};

// 按计划写标签树。
//
// 这就是那个**唯一的写手**：标记内容、MCID 分配、/ParentTree、结构树本身
// 都在这里做，行级标签树与语义标签树都走它——不写两套。
//
// 做的四件事：
//   * 每一页的内容流里，块里的每个文本显示操作符被包进
//     `/Tag <</MCID n>> BDC … EMC`，n 按视觉顺序从 0 连续发号；
//   * 计划里的每个结构元素写成 /StructElem：/S 是类型，/T /Alt /ActualText
//     /Lang 按计划写，/K 是它的块的 MCID（表单域则是 widget 注解）；
//   * /ParentTree 按页建：键是页的 /StructParents，值是那一页的数组，
//     数组下标是 MCID（表单域的 widget 接在 MCID 之后）；
//   * catalog 写 /StructTreeRoot 与 /MarkInfo，按需要写 /Lang /DisplayDocTitle
//     /Metadata，/Info 写 /Title，页上按需要写 /Tabs /S。
//
// 写完会重新打开校验一次（结构有问题、有警告都算失败）。
// 失败返回空 QByteArray，并通过 error 给出中文原因。
QByteArray writeStructPlan(const QByteArray& pdfBytes, const StructWritePlan& plan,
                           QString* error = nullptr);

}  // namespace aipdf::pdf
