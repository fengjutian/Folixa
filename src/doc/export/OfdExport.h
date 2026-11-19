#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "pdf/content/PageGraphics.h"
#include "pdf/kernel/TextPage.h"

namespace aipdf::doc {

// OFD 导出的元数据。
struct OfdExportOptions {
    // 文档标题（写进 OFD.xml 的 DocInfo）。
    QString title;
    // 文档作者。
    QString author;
};

// 一页 OFD 的内容：文字层 + 图形层。
//
// 为什么合成一个结构体，而不是「文字数组 + 图形数组」两个平行参数：
//   图形与文字是按页一一对应的。两个平行数组一旦长度对不上，
//   就会把第 2 页的图配到第 1 页的文字上，而且**不报错**——
//   导出的文件看起来正常，只有逐页比对才发现内容错位。
//   合成一个结构体之后，这种错在构造时就不可能发生。
struct OfdPageContent {
    // 文字层（PDFium 给的）。
    pdf::TextPage text;
    // 矢量路径与内嵌位图（pdf::extractPageGraphics 给的）。
    //
    // ok=false 表示没取到（没启用 qpdf，或者这一页读取失败）：
    // 按「这一页没有图形」处理，不让整次导出失败——文字还是能导的。
    // 界面层若要知道「图形为什么没导」，自己看 graphics.error 即可。
    pdf::PageGraphics graphics;
};

// OFD 导出结果。
struct OfdExportResult {
    bool ok = false;
    // 完整的 OFD 包（ZIP 字节）。失败时为空。
    QByteArray bytes;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 写进包里的路径对象数、图像对象数（界面用来汇报）。
    int pathCount = 0;
    int imageCount = 0;
};

// 说明：OFD 的文本对象要按行排版，所以这里复用了 OfficeExport 的行重建
// （buildTextLines，见 doc/export/OfficeExport.h）。两处必须用同一套行划分规则，
// 否则 Word 导出的行与 OFD 导出的行会对不上。

// 导出一页为 OFD 包。
//
// OFD 是中国的版式标准（GB/T 33190-2016），包结构是：
//   OFD.xml                        包级描述（DocRoot 指向文档）
//   Doc_0/Document.xml              文档（公共资源、公共页列表）
//   Doc_0/Res/PublicRes.xml         公共资源（这里只放字体声明）
//   Doc_0/Pages/Page_0/Content.xml  页面内容（文本对象）
//   Doc_0/Pages/Page_0/Res/…        页面资源（有图片时才有：图片文件与资源表）
//
// 坐标单位是毫米（1 点 = 25.4/72 毫米），原点是页面左上角、y 向下。
//
// 这个重载只导**文本对象**：图形与图片要走下面的 OfdPageContent 重载。
OfdExportResult exportPageToOfd(const pdf::TextPage& text, const QSizeF& pageSizePt,
                                const OfdExportOptions& options = OfdExportOptions{});

// 批量：多页 → 一个 OFD 包（每页一个 Content.xml）。
OfdExportResult exportPagesToOfd(const QVector<pdf::TextPage>& pages, const QSizeF& pageSizePt,
                                 const OfdExportOptions& options = OfdExportOptions{});

// 带图形与图片的重载：矢量路径写成 OFD 的路径对象（PathObject），
// 内嵌位图写成 OFD 的图像对象（ImageObject），文字照旧。
//
// 图形由调用方给（pdf::extractPageGraphics / extractDocumentGraphics）：
// 导出这一层不该自己再去打开一遍 PDF——领域层的导出函数只做格式映射。
OfdExportResult exportPageToOfd(const OfdPageContent& content, const QSizeF& pageSizePt,
                                const OfdExportOptions& options = OfdExportOptions{});

// 批量：多页 → 一个 OFD 包（每页一个 Content.xml）。
OfdExportResult exportPagesToOfd(const QVector<OfdPageContent>& pages, const QSizeF& pageSizePt,
                                 const OfdExportOptions& options = OfdExportOptions{});

}  // namespace aipdf::doc
