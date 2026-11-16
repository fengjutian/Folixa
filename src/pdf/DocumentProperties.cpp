#include "pdf/DocumentProperties.h"

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

#include <memory>
#include <string>

namespace aipdf::pdf {

namespace {

#ifdef AIPDF_WITH_QPDF

// 读 /Info 里的一个字符串字段。
//
// getUTF8Value 会把 PDF 的两种字符串编码（PDFDocEncoding 与 UTF-16BE）
// 统一转成 UTF-8：PDF 里同一个标题可能是 (Hello) 也可能是 <FEFF…>，
// 界面上不该因为编码不同而看到两个样子。
QString readInfoString(const QPDFObjectHandle& info, const char* key) {
    if (!info.isDictionary()) {
        return {};
    }
    const QPDFObjectHandle value = info.getKey(key);
    if (!value.isString()) {
        return {};
    }
    const std::string text = value.getUTF8Value();
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

// 写一个字符串字段。
//
// 空值直接跳过：空字符串表示"这次不想动这个字段"，
// 不是"把这个字段清空"——否则改一次标题会顺手抹掉作者与主题。
void writeInfoString(QPDFObjectHandle& info, const char* key, const QString& value) {
    if (value.isEmpty()) {
        return;
    }
    info.replaceKey(
        key, QPDFObjectHandle::newUnicodeString(std::string(value.toUtf8().constData())));
}

#else

// 没有 qpdf 时的统一说法：与 PdfSecurity 里的 missingQpdfError 同一个口径。
QString missingQpdfError() {
    return QStringLiteral("本次构建没有启用 qpdf，文档属性不可用");
}

#endif

}  // namespace

bool DocumentProperties::isEmpty() const {
    return title.isEmpty() && author.isEmpty() && subject.isEmpty() && keywords.isEmpty() &&
           creator.isEmpty() && producer.isEmpty() && creationDate.isEmpty() &&
           modificationDate.isEmpty();
}

bool readDocumentProperties(const QByteArray& pdfBytes,
                             DocumentProperties* out,
                             QString* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("没有提供文档属性的输出参数");
        }
        return false;
    }
    *out = DocumentProperties();
    if (pdfBytes.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("没有可读取的 PDF 内容");
        }
        return false;
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("info.pdf",
                              pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));

        // /Info 挂在 trailer 上；文件里没有这一项时 getKey 给回一个空句柄。
        const QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
        if (!info.isDictionary()) {
            // 没有 /Info 不是错误：很多 PDF 根本没写过属性，
            // 界面上应当显示"空"，而不是报错。
            return true;
        }
        out->title = readInfoString(info, "/Title");
        out->author = readInfoString(info, "/Author");
        out->subject = readInfoString(info, "/Subject");
        out->keywords = readInfoString(info, "/Keywords");
        out->creator = readInfoString(info, "/Creator");
        out->producer = readInfoString(info, "/Producer");
        out->creationDate = readInfoString(info, "/CreationDate");
        out->modificationDate = readInfoString(info, "/ModDate");
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = QStringLiteral("读取文档属性失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }
#else
    Q_UNUSED(pdfBytes);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return false;
#endif
}

QByteArray writeDocumentProperties(const QByteArray& pdfBytes,
                                    const DocumentProperties& properties,
                                    QString* error) {
    if (pdfBytes.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("没有可写入的 PDF 内容");
        }
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("info-write.pdf",
                              pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));

        QPDFObjectHandle trailer = pdf.getTrailer();
        QPDFObjectHandle info = trailer.getKey("/Info");
        if (!info.isDictionary()) {
            // 原文件没有 /Info：新建一个字典。
            // 按 PDF 规范，/Info 必须是指向字典的间接引用，
            // 所以要 makeIndirectObject 之后再挂到 trailer 上。
            info = QPDFObjectHandle::newDictionary();
            trailer.replaceKey("/Info", pdf.makeIndirectObject(info));
            info = trailer.getKey("/Info");
            if (!info.isDictionary()) {
                if (error != nullptr) {
                    *error = QStringLiteral("无法在文档里建立 /Info 字典");
                }
                return {};
            }
        }

        // 逐个键改写：/Info 里原有的其他键（比如 /Trapped）保持不动。
        writeInfoString(info, "/Title", properties.title);
        writeInfoString(info, "/Author", properties.author);
        writeInfoString(info, "/Subject", properties.subject);
        writeInfoString(info, "/Keywords", properties.keywords);
        writeInfoString(info, "/Creator", properties.creator);
        writeInfoString(info, "/Producer", properties.producer);
        writeInfoString(info, "/CreationDate", properties.creationDate);
        writeInfoString(info, "/ModDate", properties.modificationDate);

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();

        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) {
                *error = QStringLiteral("qpdf 没有写出文档属性结果");
            }
            return {};
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = QStringLiteral("写入文档属性失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }
#else
    Q_UNUSED(properties);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return {};
#endif
}

}  // namespace aipdf::pdf
