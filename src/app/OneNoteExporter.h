#pragma once

#include <QByteArray>
#include <QImage>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace aipdf::app {

// ------------------------------------------------------------------
// 失败原因
// ------------------------------------------------------------------

// 发送到 OneNote 失败的三类原因。
//
// 为什么一定要分成三类：「本机没装 OneNote」「OneNote 起不来」「OneNote 起来了
// 但页面没写进去」是三件完全不同的事，用户该做的事也完全不同（装一个 / 手动打开
// 一次完成首次设置 / 重试或换台机器）。给同一句「发送失败」等于让人自己猜原因。
enum class OneNoteErrorKind {
    None,
    NotInstalled,    // 本机没装 OneNote（注册表里没有 OneNote.Application）
    ComUnavailable,  // 装了，但 COM 起不来（COM 初始化失败 / 连不上 OneNote 进程）
    ExportFailed,    // OneNote 起来了，但内容没写进去（XML 被拒、OneNote 正弹窗等）
};

// 导成图片的格式。OneNote 的 one:Image 只认 png/jpeg 这几种，别的要先转。
enum class OneNoteImageFormat {
    Png,
    Jpeg,
};

// ------------------------------------------------------------------
// 本机环境
// ------------------------------------------------------------------

// 本机的 OneNote 情况。
//
// 为什么做成数据而不是一个 bool：单测要能造出「没装」这种状态，
// 而真实的探测要读注册表、不能启动 OneNote。
struct OneNoteEnvironment {
    // 注册表里能不能找到 OneNote.Application 这个 COM 服务器。
    bool installed = false;
    // OneNote.exe 的路径（探测到了才有）。
    QString executablePath;
    // 技术细节（不出现在给用户看的主文案里）。
    QString detail;
};

// 探测本机有没有装 OneNote。
//
// 只读注册表，不启动 OneNote：探测本身不该在用户桌面上弹东西。
OneNoteEnvironment probeOneNoteEnvironment();

// ------------------------------------------------------------------
// 导出计划
// ------------------------------------------------------------------

// 导出参数。
struct OneNoteExportOptions {
    // 送进 OneNote 的页面标题前缀（一般是文件名）。
    QString documentTitle;
    // 要导出的页码（1 基）。空 = 全部页面。
    QVector<int> pageNumbers;
    // 导成什么格式。
    OneNoteImageFormat format = OneNoteImageFormat::Png;
    // 渲染分辨率（DPI）。
    int dpi = 144;
};

// 导出计划：纯数据，描述「导哪几页、导成什么、送到哪里」。
//
// 为什么要有这么一个「计划」：真正去动 OneNote 的那一步会启动 OneNote 进程、
// 往用户笔记本里写页面，单测绝对不能碰。把「算清楚要导什么」拆成纯函数，
// 页码与格式才能在单测里被一条条断言。
struct OneNoteExportPlan {
    // 文档名（页面标题前缀）。
    QString documentTitle;
    // 要导的页码（1 基，已归一化：去重、去越界、升序）。
    QVector<int> pageNumbers;
    // 导成什么格式。
    OneNoteImageFormat format = OneNoteImageFormat::Png;
    // 渲染分辨率（DPI）。
    int dpi = 144;
    // 送到的位置（探测之后才知道；探测不到时为空）。
    QString notebookName;
    QString sectionName;
    QString sectionId;

    int pageCount() const { return pageNumbers.size(); }
};

// 归一化页码：空选 = 全部页；越界页码丢掉；重复的只留一次；结果按升序。
//
// 为什么越界页码是「丢掉」而不是报错：页码是界面传过来的，
// 文档刚好被改短一页时不该让整个导出失败，少导一页比全失败好。
QVector<int> normalizeOneNotePageNumbers(const QVector<int>& selected, int totalPageCount);

// 生成导出计划（纯函数：不碰 COM、不碰磁盘、不碰 OneNote）。
OneNoteExportPlan buildOneNoteExportPlan(const QString& documentTitle, int totalPageCount,
                                          const OneNoteExportOptions& options);

// 页码列表 → 「第 1-3 页」这样的一段话。
QString formatOneNotePageRange(const QVector<int>& pageNumbers);

// 格式 → 「PNG 图片」这样的标签。
QString oneNoteImageFormatLabel(OneNoteImageFormat format);

// 一个 PDF 页在 OneNote 里叫什么（例：「合同.pdf - 第 3 页」）。
QString oneNotePageTitle(const OneNoteExportPlan& plan, int pageNumber);

// 把一页图编码成 OneNote 能吃的文件字节（PNG / JPEG）。失败返回空数组。
//
// dpi 大于 0 时把分辨率写进图片文件：OneNote 按图片自带的分辨率算显示尺寸，
// 不写的话一张 A4 页会被拉成巨大的图。
QByteArray encodeOneNoteImageBytes(const QImage& image, OneNoteImageFormat format, int dpi = 0);

// 图片在 OneNote 页面上应该占多大（单位：点，1/72 英寸）。
QSizeF oneNoteImageDisplaySizePoints(const QImage& image, int dpi);

// 生成写进 OneNote 的页面 XML（纯函数：单测直接断言里面的 base64 与命名空间）。
//
// 结构照微软官方 UpdatePageContent 示例来：one:Page → one:Title / one:Outline，
// 图片挂在 one:Outline → one:OEChildren → one:OE → one:Image → one:Data。
QString buildOneNotePageXml(const QString& pageId, const QString& pageTitle,
                             const QByteArray& imageFile, OneNoteImageFormat format,
                             const QSizeF& displaySizePoints);

// 失败原因的中文说明。
//
// detail 是「为什么」的具体内容：ExportFailed 时它是给用户看的原因
// （例如「第 3 页渲染成图片失败」）；另外两类时它是技术细节，挂在括号里。
QString oneNoteErrorMessage(OneNoteErrorKind kind, const QString& detail = QString());

// 成功之后给用户看的一句话：导哪几页、导成什么、送到 OneNote 的哪一页。
QString oneNoteSuccessMessage(const OneNoteExportPlan& plan, int createdPageCount);

// ------------------------------------------------------------------
// 后端
// ------------------------------------------------------------------

// OneNote 里的目标位置。
struct OneNoteTarget {
    QString notebookName;  // 笔记本名（探测不到时为空）
    QString sectionName;   // 分区名
    QString sectionId;     // 分区 ID（建页要用）
};

// OneNote 后端：真正去碰 COM 的那一层。
//
// 为什么抽成接口：COM 调用会把 OneNote 进程拉起来，还会往用户的笔记本里写页面，
// 单测不能碰它。抽成接口之后，单测塞一个假后端，就能把「没装」「COM 起不来」
// 「写页失败」三条路径都真实地走一遍。
class OneNoteBackend {
public:
    virtual ~OneNoteBackend() = default;

    // 找到要把内容送进去的分区。失败时 errorKind 说明是哪一类失败。
    virtual bool findTargetSection(OneNoteTarget* target, OneNoteErrorKind* errorKind,
                                   QString* detail) = 0;

    // 在目标分区里新建一页，把图片写进去。
    virtual bool createPageWithImage(const QString& sectionId, const QString& pageTitle,
                                     const QByteArray& imageFile, OneNoteImageFormat format,
                                     const QSizeF& displaySizePoints, OneNoteErrorKind* errorKind,
                                     QString* detail) = 0;
};

// ------------------------------------------------------------------
// 导出器
// ------------------------------------------------------------------

// 一次发送的结果。
struct OneNoteExportResult {
    bool ok = false;
    // 失败原因种类（成功时是 None）。
    OneNoteErrorKind errorKind = OneNoteErrorKind::None;
    // 给用户看的中文（成功时说清导了什么、送到哪）。
    QString message;
    // 技术细节。
    QString detail;
    // 送到了哪个笔记本 / 分区。
    QString notebookName;
    QString sectionName;
    // 送出去的每一页的标题。
    QStringList createdPageTitles;

    int createdPageCount() const { return createdPageTitles.size(); }
};

// 取页图的方式：给 0 基页号与 DPI，返回该页图像；取不到返回空图。
//
// 为什么要回调而不是直接收 PDF 文档：这个类只管「送」，
// 「怎么把 PDF 页变成图」是渲染层的事，两者分开才测得了。
using OneNotePageRenderer = std::function<QImage(int pageIndex, int dpi)>;

// 把 PDF 页面送到 OneNote。
class OneNoteExporter {
public:
    // 用真实的本机环境 + 真实的 COM 后端。
    OneNoteExporter();
    // 注入本机环境（单测用：模拟「没装 OneNote」）。
    explicit OneNoteExporter(const OneNoteEnvironment& environment);
    // 注入环境与后端（单测用：模拟 COM 起不来、写页失败）。
    OneNoteExporter(const OneNoteEnvironment& environment, std::unique_ptr<OneNoteBackend> backend);

    // 按计划把页面送到 OneNote。
    //
    // renderer 负责把 PDF 页渲染成图；这个类只负责「送」和「说清楚送了什么」。
    OneNoteExportResult exportToOneNote(const OneNoteExportPlan& plan,
                                         const OneNotePageRenderer& renderer);

    const OneNoteEnvironment& environment() const { return environment_; }

private:
    OneNoteEnvironment environment_;
    std::unique_ptr<OneNoteBackend> backend_;
};

}  // namespace aipdf::app
