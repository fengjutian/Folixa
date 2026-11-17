#include "app/LastWaveWriter.h"

#include "i18n/Translate.h"

#include <memory>
#include <string>
#include <vector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <exception>
#endif

namespace aipdf::app {

namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

QByteArray appendContentStreamToPage(const QByteArray& pdfBytes, int pageIndex,
                                      const QByteArray& content,
                                      const QString& fontResourceName,
                                      QString* error) {
#ifndef AIPDF_WITH_QPDF
    Q_UNUSED(pdfBytes);
    Q_UNUSED(pageIndex);
    Q_UNUSED(content);
    Q_UNUSED(fontResourceName);
    setError(error, i18n::tr("aipdf::app::LastWaveWriter",
                              "本次构建没有启用 qpdf，往页面追加内容流不可用。"));
    return QByteArray();
#else
    if (pdfBytes.isEmpty()) {
        setError(error, i18n::tr("aipdf::app::LastWaveWriter", "没有可写入的 PDF 内容。"));
        return QByteArray();
    }
    if (content.isEmpty()) {
        setError(error, i18n::tr("aipdf::app::LastWaveWriter", "内容流是空的，没有东西可写。"));
        return QByteArray();
    }
    // 资源名里不能有空白：PDF 的名字对象遇到空白就断了。
    QString resource = fontResourceName.trimmed();
    resource.remove(QLatin1Char(' '));
    if (resource.isEmpty()) {
        resource = QStringLiteral("AipdfPara");
    }
    const std::string resourceName = std::string("/") + resource.toLatin1().constData();

    try {
        QPDF pdf;
        pdf.processMemoryFile("last-wave-append.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
            setError(error, i18n::tr("aipdf::app::LastWaveWriter", "页码超出范围：%1")
                                .arg(pageIndex + 1));
            return QByteArray();
        }
        QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));

        // 内容流：原来是一个流就改成 [原流, 新流]，原来是数组就追加一项。
        const QPDFObjectHandle stream =
            pdf.newStream(std::string(content.constData(), static_cast<size_t>(content.size())));
        std::vector<QPDFObjectHandle> items;
        const QPDFObjectHandle existing = page.getKey("/Contents");
        if (existing.isArray()) {
            items = existing.getArrayAsVector();
        } else if (existing.isStream()) {
            items.push_back(existing);
        }
        items.push_back(pdf.makeIndirectObject(stream));
        page.replaceKey("/Contents", QPDFObjectHandle::newArray(items));

        // 字体资源：内容流里写的是 /<资源名>，页面资源里必须有它，
        // 否则渲染器会把这页当成没有字体的页面（或者干脆报错）。
        QPDFObjectHandle resources = page.getKey("/Resources");
        if (!resources.isDictionary()) {
            resources = QPDFObjectHandle::newDictionary();
            page.replaceKey("/Resources", resources);
        }
        QPDFObjectHandle fonts = resources.getKey("/Font");
        if (!fonts.isDictionary()) {
            fonts = QPDFObjectHandle::newDictionary();
            resources.replaceKey("/Font", fonts);
        }
        if (!fonts.hasKey(resourceName)) {
            QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
            font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
            font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
            font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
            font.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
            fonts.replaceKey(resourceName, pdf.makeIndirectObject(font));
        }

        QPDFWriter writer(pdf);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setOutputMemory();
        writer.write();
        const std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            setError(error, i18n::tr("aipdf::app::LastWaveWriter", "qpdf 没有写出内容。"));
            return QByteArray();
        }
        if (error != nullptr) {
            error->clear();
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<qsizetype>(buffer->getSize()));
    } catch (const std::exception& exception) {
        setError(error, i18n::tr("aipdf::app::LastWaveWriter", "qpdf 写入失败：%1")
                            .arg(QString::fromUtf8(exception.what())));
        return QByteArray();
    }
#endif
}

}  // namespace aipdf::app
