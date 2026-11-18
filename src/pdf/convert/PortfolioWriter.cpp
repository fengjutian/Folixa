#include "pdf/convert/PortfolioWriter.h"

#include <QSet>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <vector>
#endif

namespace aipdf::pdf {
namespace {

// 封面页的纸张大小（A4，单位：点）。
constexpr double kCoverWidth = 595.0;
constexpr double kCoverHeight = 842.0;

// 把文字做成内容流里能直接用的字符串操作数。
//
// 为什么分两支：PDF 的字面量字符串只能放 Latin-1 字符，中文必须走
// UTF-16BE 的十六进制串（以 <FEFF...> 开头）。两支写出来的都是合法操作数。
QString pdfTextOperand(const QString& text) {
    bool ascii = true;
    for (QChar ch : text) {
        if (ch.unicode() > 0x7F) {
            ascii = false;
            break;
        }
    }
    if (ascii) {
        QString escaped;
        escaped.reserve(text.size() + 8);
        for (QChar ch : text) {
            if (ch == QLatin1Char('(') || ch == QLatin1Char(')') || ch == QLatin1Char('\\')) {
                escaped += QLatin1Char('\\');
            }
            escaped += ch;
        }
        return QLatin1Char('(') + escaped + QLatin1Char(')');
    }
    QString hex = QStringLiteral("<FEFF");
    for (QChar ch : text) {
        hex += QStringLiteral("%1").arg(static_cast<uint>(ch.unicode()), 4, 16, QLatin1Char('0'))
                   .toUpper();
    }
    hex += QLatin1Char('>');
    return hex;
}

// 按文件名与内容猜嵌入文件的 MIME 类型。
//
// 为什么猜：PDF 的嵌入文件规范里 /Subtype 是 MIME 类型，
// 调用方不传类型时总得给一个，总不能留空。
QString embeddedSubtype(const QString& name, const QByteArray& bytes) {
    if (bytes.startsWith("%PDF-")) return QStringLiteral("/application#2Fpdf");
    const QString lower = name.toLower();
    if (lower.endsWith(QStringLiteral(".txt")) || lower.endsWith(QStringLiteral(".md"))) {
        return QStringLiteral("/text#2Fplain");
    }
    if (lower.endsWith(QStringLiteral(".csv"))) return QStringLiteral("/text#2Fcsv");
    if (lower.endsWith(QStringLiteral(".xml"))) return QStringLiteral("/application#2Fxml");
    if (lower.endsWith(QStringLiteral(".png"))) return QStringLiteral("/image#2Fpng");
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg"))) {
        return QStringLiteral("/image#2Fjpeg");
    }
    return QStringLiteral("/application#2Foctet-stream");
}

// /Dests 字典的键：必须是 PDF 的「名字」token，只能是普通字符。
//
// 为什么条目名要过一道：条目名可以是中文，直接当字典键写出去，
// 别的阅读器解析这一页时会报「unknown token」——整份包就废了。
// 名字树 /Names /EmbeddedFiles 里存的是文本串，中文名在那里是安全的。
QString destKey(const QString& name, int index) {
    for (const QChar ch : name) {
        const ushort code = ch.unicode();
        if (code <= 0x20 || code > 0x7E) return QStringLiteral("entry-%1").arg(index + 1);
        if (QStringLiteral("()<>[]{}/%#").contains(ch)) {
            return QStringLiteral("entry-%1").arg(index + 1);
        }
    }
    return name;
}

#ifdef AIPDF_WITH_QPDF
// 字符串对象的值（统一按 UTF-8 取，UTF-16BE 的串 qpdf 会转过来）。
QString stringValue(const QPDFObjectHandle& handle) {
    if (!handle.isString()) return QString();
    return QString::fromUtf8(handle.getUTF8Value().c_str());
}
#endif  // AIPDF_WITH_QPDF

}  // namespace

QByteArray writePortfolio(const QVector<PortfolioEntry>& entries,
                           const PortfolioOptions& options,
                           QString* error) {
    const auto fail = [error](const QString& message) {
        if (error != nullptr) *error = message;
        return QByteArray();
    };

    if (entries.isEmpty()) {
        return fail(QStringLiteral("没有可打包的条目"));
    }
    QSet<QString> usedNames;
    for (int i = 0; i < entries.size(); ++i) {
        const PortfolioEntry& entry = entries.at(i);
        if (entry.name.trimmed().isEmpty()) {
            return fail(QStringLiteral("第 %1 个条目没有名字").arg(i + 1));
        }
        if (entry.bytes.isEmpty()) {
            return fail(QStringLiteral("第 %1 个条目（%2）内容为空").arg(i + 1).arg(entry.name));
        }
        if (usedNames.contains(entry.name)) {
            // 重名直接判失败，理由写在 PortfolioWriter.h 里。
            return fail(QStringLiteral("条目名重复：%1").arg(entry.name));
        }
        usedNames.insert(entry.name);
    }

#ifndef AIPDF_WITH_QPDF
    return fail(QStringLiteral("本次构建没有启用 qpdf，写不了 PDF 包"));
#else
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        // 从空 PDF 起手：包的内容全部由我们自己写，不继承任何源文件的结构。
        pdf.emptyPDF();

        QPDFObjectHandle root = pdf.getRoot();
        QPDFObjectHandle pages = root.getKey("/Pages");
        if (!pages.isDictionary()) {
            return fail(QStringLiteral("qpdf 建出来的空 PDF 没有页树"));
        }
        if (!pages.isIndirect()) {
            // 页树必须是间接对象：每个页的 /Parent 都要指回它。
            pages = pdf.makeIndirectObject(pages);
            root.replaceKey("/Pages", pages);
        }

        // 封面用到的标准字体（不内嵌：封面只是索引页，不参与印刷内容）。
        QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
        font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
        font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
        const QPDFObjectHandle fontRef = pdf.makeIndirectObject(font);

        // qpdf 的数组接口收 std::vector，这几个容器跟着它走。
        std::vector<QPDFObjectHandle> pageRefs;
        std::vector<QPDFObjectHandle> specRefs;
        QVector<QString> names;
        for (const PortfolioEntry& entry : entries) {
            // 1. 嵌入文件流。
            const std::string rawData(entry.bytes.constData(),
                                      static_cast<size_t>(entry.bytes.size()));
            QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf, rawData);
            // 必须改流的字典，不能直接 stream.replaceKey(...)：
            // qpdf 12.4.2 的 replaceKey 只认字典对象，对流对象调用会被**静默忽略**
            // （本文件开着 setSuppressWarnings，连警告都看不到），
            // 后果是嵌入文件流没有 /Type /EmbeddedFile 与 /Subtype，
            // 别的阅读器认不出这是嵌入文件。
            QPDFObjectHandle streamDict = stream.getDict();
            streamDict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            streamDict.replaceKey("/Subtype",
                                  QPDFObjectHandle::newName(
                                      embeddedSubtype(entry.name, entry.bytes).toStdString()));
            QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
            params.replaceKey("/Size",
                              QPDFObjectHandle::newInteger(static_cast<long long>(entry.bytes.size())));
            streamDict.replaceKey("/Params", params);
            const QPDFObjectHandle streamRef = pdf.makeIndirectObject(stream);

            // 2. 文件说明（Filespec）：名字与内容流的对应关系写在这里。
            QPDFObjectHandle embeddedFiles = QPDFObjectHandle::newDictionary();
            embeddedFiles.replaceKey("/F", streamRef);
            embeddedFiles.replaceKey("/UF", streamRef);
            QPDFObjectHandle spec = QPDFObjectHandle::newDictionary();
            spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
            spec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(entry.name.toUtf8().constData()));
            spec.replaceKey("/UF",
                            QPDFObjectHandle::newUnicodeString(entry.name.toUtf8().constData()));
            spec.replaceKey("/EF", embeddedFiles);
            if (!entry.description.isEmpty()) {
                spec.replaceKey("/Desc",
                                QPDFObjectHandle::newUnicodeString(
                                    entry.description.toUtf8().constData()));
            }
            specRefs.push_back(pdf.makeIndirectObject(spec));

            // 3. 封面页：写上条目名与说明，作为包里的索引页。
            QString content = QStringLiteral("BT /F1 18 Tf 56 760 Td ") +
                              pdfTextOperand(entry.name) + QStringLiteral(" Tj ET\n");
            if (!entry.description.isEmpty()) {
                content += QStringLiteral("BT /F1 11 Tf 56 736 Td ") +
                           pdfTextOperand(entry.description) + QStringLiteral(" Tj ET\n");
            }
            const QByteArray contentBytes = content.toUtf8();
            QPDFObjectHandle contentStream = QPDFObjectHandle::newStream(
                &pdf, std::string(contentBytes.constData(),
                                  static_cast<size_t>(contentBytes.size())));
            const QPDFObjectHandle contentRef = pdf.makeIndirectObject(contentStream);

            QPDFObjectHandle fontDict = QPDFObjectHandle::newDictionary();
            fontDict.replaceKey("/F1", fontRef);
            QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
            resources.replaceKey("/Font", fontDict);

            QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
            page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
            page.replaceKey("/Parent", pages);
            page.replaceKey("/MediaBox",
                            QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{
                                QPDFObjectHandle::newInteger(0),
                                QPDFObjectHandle::newInteger(0),
                                QPDFObjectHandle::newInteger(static_cast<long long>(kCoverWidth)),
                                QPDFObjectHandle::newInteger(static_cast<long long>(kCoverHeight)),
                            }));
            page.replaceKey("/Resources", resources);
            page.replaceKey("/Contents", contentRef);
            pageRefs.push_back(pdf.makeIndirectObject(page));

            names.append(entry.name);
        }

        // 4. 页树。
        pages.replaceKey("/Kids", QPDFObjectHandle::newArray(pageRefs));
        pages.replaceKey("/Count",
                         QPDFObjectHandle::newInteger(static_cast<long long>(pageRefs.size())));

        // 5. 名字树：/Names /EmbeddedFiles。键要求升序，这里按名字排一遍。
        QVector<int> order;
        order.reserve(entries.size());
        for (int i = 0; i < entries.size(); ++i) order.append(i);
        std::stable_sort(order.begin(), order.end(), [&names](int left, int right) {
            return names.at(left) < names.at(right);
        });
        std::vector<QPDFObjectHandle> nameArray;
        nameArray.reserve(order.size() * 2);
        for (int index : order) {
            nameArray.push_back(
                QPDFObjectHandle::newUnicodeString(names.at(index).toUtf8().constData()));
            nameArray.push_back(specRefs.at(index));
        }
        QPDFObjectHandle embeddedNames = QPDFObjectHandle::newDictionary();
        embeddedNames.replaceKey("/Names", QPDFObjectHandle::newArray(nameArray));
        QPDFObjectHandle namesDict = QPDFObjectHandle::newDictionary();
        namesDict.replaceKey("/EmbeddedFiles", embeddedNames);
        root.replaceKey("/Names", namesDict);

        // 6. /Collection：这就是「活页夹」本体，/D 里按展示顺序列各条目。
        QPDFObjectHandle details = QPDFObjectHandle::newArray(specRefs);
        QPDFObjectHandle collection = QPDFObjectHandle::newDictionary();
        collection.replaceKey("/Type", QPDFObjectHandle::newName("/Collection"));
        collection.replaceKey("/View", QPDFObjectHandle::newName("/D"));
        collection.replaceKey("/D", details);
        root.replaceKey("/Collection", collection);

        // 7. /Dests：按条目名指向各自的封面页。
        //
        // 注意字典键要带斜杠（qpdf 的 replaceKey 要的是 "/Name" 这种形式）：
        // 不带斜杠写出去的是裸字符串，别的阅读器解析这一页会直接报错。
        QPDFObjectHandle dests = QPDFObjectHandle::newDictionary();
        for (int i = 0; i < names.size(); ++i) {
            QPDFObjectHandle destination = QPDFObjectHandle::newArray(
                std::vector<QPDFObjectHandle>{pageRefs.at(i), QPDFObjectHandle::newName("/Fit")});
            const QByteArray key = QByteArray("/") + destKey(names.at(i), i).toUtf8();
            dests.replaceKey(key.constData(), destination);
        }
        root.replaceKey("/Dests", dests);

        // 8. 文档属性（/Info）。
        QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
        if (!options.title.isEmpty()) {
            info.replaceKey("/Title",
                            QPDFObjectHandle::newUnicodeString(options.title.toUtf8().constData()));
        }
        if (!options.author.isEmpty()) {
            info.replaceKey("/Author",
                            QPDFObjectHandle::newUnicodeString(options.author.toUtf8().constData()));
        }
        if (!options.subject.isEmpty()) {
            info.replaceKey("/Subject",
                            QPDFObjectHandle::newUnicodeString(options.subject.toUtf8().constData()));
        }
        pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            return fail(QStringLiteral("qpdf 没有写出 PDF 包"));
        }
        const QByteArray bytes(reinterpret_cast<const char*>(buffer->getBuffer()),
                               static_cast<int>(buffer->getSize()));

        // 自检：写出来的字节必须能被重新解析，而且 /Collection 得在。
        // 「写完才发现结构不对」比直接失败糟糕得多。
        QPDF check;
        check.setSuppressWarnings(true);
        check.processMemoryFile("portfolio.pdf", bytes.constData(),
                                static_cast<size_t>(bytes.size()));
        if (!check.getRoot().getKey("/Collection").isDictionary()) {
            return fail(QStringLiteral("写出的 PDF 包缺少 /Collection"));
        }

        if (error != nullptr) error->clear();
        return bytes;
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("写 PDF 包失败：%1")
                        .arg(QString::fromUtf8(exception.what())));
    }
#endif
}

PortfolioReadResult readPortfolio(const QByteArray& bytes) {
    PortfolioReadResult result;
    if (bytes.isEmpty()) {
        result.error = QStringLiteral("没有可读的内容");
        return result;
    }

#ifndef AIPDF_WITH_QPDF
    result.error = QStringLiteral("本次构建没有启用 qpdf，读不了 PDF 包");
    return result;
#else
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("portfolio.pdf", bytes.constData(),
                              static_cast<size_t>(bytes.size()));

        const QPDFObjectHandle root = pdf.getRoot();
        const QPDFObjectHandle collection = root.getKey("/Collection");
        if (!collection.isDictionary()) {
            result.error = QStringLiteral("这份 PDF 不是 PDF 包（没有 /Collection）");
            return result;
        }

        // 条目来源：优先 /Collection /D（那是包里的展示顺序），没有再退回名字树。
        QVector<QPDFObjectHandle> specs;
        const QPDFObjectHandle details = collection.getKey("/D");
        if (details.isArray()) {
            for (const QPDFObjectHandle& item : details.getArrayAsVector()) {
                if (item.isDictionary()) specs.append(item);
            }
        }
        if (specs.isEmpty()) {
            // 名字树：/Names /EmbeddedFiles。
            std::function<void(const QPDFObjectHandle&, int)> walk =
                [&walk, &specs](const QPDFObjectHandle& node, int depth) {
                if (depth > 32 || !node.isDictionary()) return;
                const QPDFObjectHandle names = node.getKey("/Names");
                if (names.isArray()) {
                    const std::vector<QPDFObjectHandle> items = names.getArrayAsVector();
                    for (size_t i = 0; i + 1 < items.size(); i += 2) {
                        if (items[i + 1].isDictionary()) specs.append(items[i + 1]);
                    }
                }
                const QPDFObjectHandle kids = node.getKey("/Kids");
                if (kids.isArray()) {
                    for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
                        walk(kid, depth + 1);
                    }
                }
            };
            const QPDFObjectHandle namesDict = root.getKey("/Names");
            if (namesDict.isDictionary()) {
                walk(namesDict.getKey("/EmbeddedFiles"), 0);
            }
        }

        for (const QPDFObjectHandle& spec : specs) {
            PortfolioEntry entry;
            entry.name = stringValue(spec.getKey("/UF"));
            if (entry.name.isEmpty()) entry.name = stringValue(spec.getKey("/F"));
            entry.description = stringValue(spec.getKey("/Desc"));
            const QPDFObjectHandle embedded = spec.getKey("/EF");
            if (embedded.isDictionary()) {
                QPDFObjectHandle file = embedded.getKey("/F");
                if (!file.isStream()) file = embedded.getKey("/UF");
                if (file.isStream()) {
                    const std::shared_ptr<Buffer> data = file.getStreamData(qpdf_dl_generalized);
                    if (data) {
                        entry.bytes = QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                                 static_cast<int>(data->getSize()));
                    }
                }
            }
            result.entries.append(entry);
        }

        // 文档属性（/Info）。
        const QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
        if (info.isDictionary()) {
            result.title = stringValue(info.getKey("/Title"));
            result.author = stringValue(info.getKey("/Author"));
            result.subject = stringValue(info.getKey("/Subject"));
        }

        result.ok = true;
        result.error.clear();
        return result;
    } catch (const std::exception& exception) {
        result.ok = false;
        result.entries.clear();
        result.error = QStringLiteral("解析这份 PDF 失败：%1")
                           .arg(QString::fromUtf8(exception.what()));
        return result;
    }
#endif
}

}  // namespace aipdf::pdf
