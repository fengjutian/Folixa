#pragma once

#include <QByteArray>
#include <QSizeF>
#include <QString>

namespace aipdf::doc {

// 常用纸张。
//
// 为什么用枚举而不是字符串：纸张尺寸是几何事实，用字符串就等于把
// "A4 是多大" 的解析散落到每个调用点；枚举让"纸张 → 尺寸"只有一张表。
enum class PageSize { A4, A3, A5, Letter, Legal, Custom };

// 页面方向。
enum class PageOrientation { Portrait, Landscape };

// 新建空白文档的参数。
struct BlankDocumentOptions {
    // 纸张种类。Custom 时改用 widthPt / heightPt。
    PageSize size = PageSize::A4;
    // 方向。
    //
    // Custom 时**不生效**：自定义宽高按调用方给的原样使用，
    // 方向由调用方自己用宽高表达。这里偷偷换一次宽高是最难排查的一类问题。
    PageOrientation orientation = PageOrientation::Portrait;
    // 页数。0 或负数直接失败；超过上限会被夹到上限。
    int pageCount = 1;
    // 自定义页面宽高，单位是 PDF 点（1/72 英寸）。
    double widthPt = 0.0;
    double heightPt = 0.0;
};

// 常用纸张的尺寸（PDF 点）。
//
// 横向 = 把纵向的宽高对调。
// Custom 返回**无效尺寸**（QSizeF 的默认值，isValid() 为假）：
// 自定义宽高是随调用而定的，不在这张表里。
QSizeF pageSizeInPoints(PageSize size, PageOrientation orientation);

// 新建一份全新的空 PDF。
//
// 返回可用的 PDF 字节；失败返回空字节，并把原因（中文）写进 error。
//
// 这一层自己拼 PDF 字节，不借 PDFium：PDFium 的接口不是线程安全的
// （见 src/pdf/PdfiumAdapter.cpp 开头的说明），本程序所有 PDFium 调用
// 都串在同一把进程级锁上，而那把锁没有对外接口；doc 层再开一个
// PDFium 调用点就等于绕过它，可能踩坏 PDFium 的共享状态。
// 空白页的结构本来就极简，自己拼能把依赖留在 Qt Core 这一层。
QByteArray createBlankDocument(const BlankDocumentOptions& options, QString* error = nullptr);

}  // namespace aipdf::doc
