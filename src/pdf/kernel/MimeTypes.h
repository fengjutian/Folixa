#pragma once

#include <QByteArray>
#include <QFileInfo>
#include <QString>

namespace aipdf::pdf {

// 按文件名后缀推断 MIME 类型。
//
// 为什么放在这里：这个表被三层都要用——
//   1. 页面上的附件批注（pdf/annot）写嵌入文件流时用它；
//   2. 文档级附件（pdf/document）补 /Subtype 时用它；
//   3. 适配层（pdf/kernel）读回附件时用它兜底。
// 放在 annot 层会让 kernel 反向依赖 annot，所以放在 kernel 这一层：
// annot 与 document 都是往 kernel 走的，方向不反。
//
// 为什么只收认得出的类型：表里没有的一律走 application/octet-stream——
// 宁可给一个诚实的通用类型，也不要编一个具体类型骗别的阅读器。
//
// 为什么是**头文件内联**而不是 .h/.cpp 配对：这张表被适配层（pdf/kernel）
// 用着，而全仓有五十多个测试目标各自编译 PdfiumAdapter.cpp——
// 放成 .cpp 就得往每一个目标里加一行登记，漏一个就是链接错误。
// 内联函数没有这个问题：一个头文件，谁 include 谁就能用。
inline QByteArray mimeTypeForFileName(const QString& fileName) {
    // 后缀一律小写再比对：Windows 上文件名大小写不固定，按原样比对会漏掉一半。
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    struct Entry {
        const char* suffix;
        const char* mime;
    };
    // 只收认得出的类型；表里没有的一律走 application/octet-stream。
    static const Entry kTable[] = {
        {"avi", "video/x-msvideo"},
        {"mp4", "video/mp4"},
        {"mov", "video/quicktime"},
        {"webm", "video/webm"},
        {"mkv", "video/x-matroska"},
        {"wav", "audio/wav"},
        {"mp3", "audio/mpeg"},
        {"m4a", "audio/mp4"},
        {"wma", "audio/x-ms-wma"},
        {"ogg", "audio/ogg"},
        {"flac", "audio/flac"},
        {"pdf", "application/pdf"},
        {"txt", "text/plain"},
        {"md", "text/markdown"},
        {"csv", "text/csv"},
        {"json", "application/json"},
        {"xml", "application/xml"},
        {"html", "text/html"},
        {"zip", "application/zip"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"bmp", "image/bmp"},
        {"svg", "image/svg+xml"},
    };
    for (const auto& entry : kTable) {
        if (suffix == QLatin1String(entry.suffix)) {
            return QByteArray(entry.mime);
        }
    }
    return QByteArray("application/octet-stream");
}

}  // namespace aipdf::pdf
