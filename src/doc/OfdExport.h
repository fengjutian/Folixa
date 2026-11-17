#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "pdf/TextPage.h"

namespace aipdf::doc {

// OFD 导出的元数据。
struct OfdExportOptions {
    // 文档标题（写进 OFD.xml 的 DocInfo）。
    QString title;
    // 文档作者。
    QString author;
};

// OFD 导出结果。
struct OfdExportResult {
    bool ok = false;
    // 完整的 OFD 包（ZIP 字节）。失败时为空。
    QByteArray bytes;
    // 失败原因（中文）。成功时为空。
    QString error;
};

// 说明：OFD 的文本对象要按行排版，所以这里复用了 OfficeExport 的行重建
// （buildTextLines，见 doc/OfficeExport.h）。两处必须用同一套行划分规则，
// 否则 Word 导出的行与 OFD 导出的行会对不上。

// 导出一页为 OFD 包。
//
// OFD 是中国的版式标准（GB/T 33190-2016），包结构是：
//   OFD.xml                        包级描述（DocRoot 指向文档）
//   Doc_0/Document.xml              文档（公共资源、公共页列表）
//   Doc_0/Res/PublicRes.xml         公共资源（这里只放字体声明）
//   Doc_0/Pages/Page_0/Content.xml  页面内容（文本对象）
//
// 只导出**文本对象**：OFD 里还有图片、图形、签章这些对象类型，
// 这一版没有实现。坐标单位是毫米（1 点 = 25.4/72 毫米），
// 原点是页面左上角、y 向下。
OfdExportResult exportPageToOfd(const pdf::TextPage& text, const QSizeF& pageSizePt,
                                const OfdExportOptions& options = OfdExportOptions{});

// 批量：多页 → 一个 OFD 包（每页一个 Content.xml）。
OfdExportResult exportPagesToOfd(const QVector<pdf::TextPage>& pages, const QSizeF& pageSizePt,
                                 const OfdExportOptions& options = OfdExportOptions{});

}  // namespace aipdf::doc
