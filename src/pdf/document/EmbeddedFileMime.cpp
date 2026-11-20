#include "pdf/document/EmbeddedFileMime.h"

#include "pdf/kernel/MimeTypes.h"

#include <QFileInfo>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>
#endif

namespace aipdf::pdf {

namespace {

#ifdef AIPDF_WITH_QPDF

// MIME 类型转成 PDF 的名字对象形式。
//
// 为什么用「未转义」的形式：qpdf 内部表示的 PDF 名字是一串以 / 开头的字节，
// **# 转义是写出去的时候才做的事**（qpdf 头文件 QPDFObjectHandle.hh:482 讲得很
// 清楚：newName("/text/plain").getName() 返回 "/text/plain"，
// 而 unparse() 才是 "/text#2fplain"）。所以这里只把 MIME 前面加个斜杠，
// 转义交给 qpdf——自己先转一遍再交给 qpdf，会被转两次（# 变成 #23）。
//
// 为什么用名字对象而不是字符串：ISO 32000 里 /Subtype 写的是名字对象
// （Acrobat 写的是 /application#2Fpdf），仓库里作品集与 Rich Media 那两条
// 也是这么写的。
QByteArray pdfNameForMime(const QString& mime) {
    return "/" + mime.toLatin1();
}

// 字符串对象的值（统一按 UTF-8 取；qpdf 会把 UTF-16BE 的串转过来）。
QString stringValue(const QPDFObjectHandle& handle) {
    if (!handle.isString()) return QString();
    try {
        return QString::fromStdString(handle.getUTF8Value());
    } catch (...) {
        return QString();
    }
}

// 名字树是递归的：一层里可能有 /Names 数组，也可能有 /Kids 子节点。
void collectNameTree(const QPDFObjectHandle& node,
                      QVector<QPair<QString, QPDFObjectHandle>>* out,
                      int depth) {
    // 深度守卫：坏文件的 /Kids 可能自指，不设上限会递归到爆栈。
    if (depth > 32 || !node.isDictionary()) {
        return;
    }
    const QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        const std::vector<QPDFObjectHandle> items = names.getArrayAsVector();
        // 名字树的 /Names 数组是「键 值 键 值 …」成对出现的。
        for (size_t index = 0; index + 1 < items.size(); index += 2) {
            out->append({stringValue(items[index]), items[index + 1]});
        }
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        const std::vector<QPDFObjectHandle> children = kids.getArrayAsVector();
        for (const QPDFObjectHandle& child : children) {
            collectNameTree(child, out, depth + 1);
        }
    }
}

// 给一个嵌入文件流补 /Subtype。已经是流、已经有 /Subtype 的都跳过。
bool patchStream(const QPDFObjectHandle& stream, const QString& mime) {
    if (!stream.isStream()) {
        return false;
    }
    // 必须改流的字典，不能直接 stream.replaceKey(...)：
    // qpdf 12.4.2 的 replaceKey 只认字典对象，对流对象调用会被**静默忽略**
    // （作品集那条路已经踩过这个坑，注释写在 PortfolioWriter.cpp 里）。
    QPDFObjectHandle dict = stream.getDict();
    if (!dict.isDictionary()) {
        return false;
    }
    if (dict.hasKey("/Subtype")) {
        return false;
    }
    // /Type /EmbeddedFile：PDFium 写的字典里**没有这一项**（实测：只有
    // /DL /Filter /Length /Params）。规范说它是可选的，但别的阅读器靠它
    // 认出「这是嵌入文件流」——补上它跟补 /Subtype 是一起做的。
    if (!dict.hasKey("/Type")) {
        dict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
    }
    dict.replaceKey("/Subtype",
                    QPDFObjectHandle::newName(pdfNameForMime(mime).toStdString()));
    return true;
}

// 取文件说明里的文件名：/UF 是 Unicode 全名，/F 是老式的文件名。
QString fileSpecName(const QPDFObjectHandle& filespec) {
    if (!filespec.isDictionary()) {
        return QString();
    }
    QString name = stringValue(filespec.getKey("/UF"));
    if (name.isEmpty()) {
        name = stringValue(filespec.getKey("/F"));
    }
    return name;
}

// 取文件说明指向的嵌入文件流：/EF 字典里的 /F（回退 /UF）。
QPDFObjectHandle embeddedStream(const QPDFObjectHandle& filespec) {
    if (!filespec.isDictionary()) {
        return QPDFObjectHandle();
    }
    const QPDFObjectHandle ef = filespec.getKey("/EF");
    if (!ef.isDictionary()) {
        return QPDFObjectHandle();
    }
    QPDFObjectHandle stream = ef.getKey("/F");
    if (!stream.isStream()) {
        stream = ef.getKey("/UF");
    }
    return stream;
}

// 打开一份可能是加密的 PDF，并说清楚「为什么打不开」。
//
// 与 PdfSecurity.cpp 里的 openForInspection() 是同一套判定（那边查隐藏数据，
// 这边补 MIME）：带口令试一次，再不带口令试一次；两次都失败才报错，并把
// 「口令不对」与「文件坏了」分开说——两者的处理方式完全不同。
enum class OpenOutcome {
    Ok,
    NeedsPassword,  // 加密了，而且没给口令
    WrongPassword,  // 加密了，口令不对
    Damaged,        // 解析失败，与口令无关
};

OpenOutcome openForPatching(std::shared_ptr<QPDF>& pdf,
                             const QByteArray& bytes,
                             const QString& password,
                             bool* encrypted,
                             std::string* detail) {
    const QByteArray utf8 = password.toUtf8();
    const char* passwordArg = utf8.isEmpty() ? nullptr : utf8.constData();

    const auto attempt = [&bytes, detail](const char* pwd) -> std::shared_ptr<QPDF> {
        auto candidate = QPDF::create();
        try {
            candidate->processMemoryFile("embedded-file-mime.pdf", bytes.constData(),
                                         static_cast<size_t>(bytes.size()), pwd);
        } catch (const std::exception& exception) {
            if (detail != nullptr && detail->empty()) {
                *detail = exception.what();
            }
            return nullptr;
        }
        return candidate;
    };

    // 第一次：带口令试。对明文文档无害（qpdf 会忽略口令）。
    if (std::shared_ptr<QPDF> candidate = attempt(passwordArg)) {
        pdf = candidate;
        *encrypted = candidate->isEncrypted();
        return OpenOutcome::Ok;
    }
    // 第二次：不带口令试（明文文档、用户口令为空的加密文档走这条路）。
    if (std::shared_ptr<QPDF> candidate = attempt(nullptr)) {
        pdf = candidate;
        *encrypted = candidate->isEncrypted();
        return OpenOutcome::Ok;
    }

    // 两次都失败：分清「口令不对」与「文件坏了」。
    // 判据是 qpdf 的错误文本：解密失败时报的是 invalid password。
    if (detail != nullptr && detail->find("password") != std::string::npos) {
        return password.isEmpty() ? OpenOutcome::NeedsPassword : OpenOutcome::WrongPassword;
    }
    return OpenOutcome::Damaged;
}

// 打开失败时给用户看的中文原因。
QString describeOpenFailure(OpenOutcome outcome, const std::string& detail) {
    switch (outcome) {
    case OpenOutcome::NeedsPassword:
        return QStringLiteral("文档已加密，需要打开密码才能补 MIME 类型");
    case OpenOutcome::WrongPassword:
        return QStringLiteral("文档已加密，打开密码不正确");
    case OpenOutcome::Damaged:
    default:
        return QStringLiteral("无法解析文档：%1").arg(QString::fromUtf8(detail.c_str()));
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QByteArray applyEmbeddedFileMimeTypes(const QByteArray& bytes,
                                       const QString& password,
                                       EmbeddedFileMimeReport* report,
                                       QString* error) {
    if (report != nullptr) {
        *report = EmbeddedFileMimeReport();
    }
    if (error != nullptr) {
        error->clear();
    }
    if (bytes.isEmpty()) {
        return bytes;
    }

#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForPatching(pdf, bytes, password, &encrypted, &detail);
    if (outcome != OpenOutcome::Ok) {
        // 打不开就明确失败：**原样返回输入字节**，绝不产出半改的文件。
        if (error != nullptr) {
            *error = describeOpenFailure(outcome, detail);
        }
        return bytes;
    }
    if (report != nullptr) {
        report->encrypted = encrypted;
    }

    try {
        const QPDFObjectHandle root = pdf->getRoot();
        const QPDFObjectHandle names = root.getKey("/Names");
        if (!names.isDictionary()) {
            return bytes;  // 文档里没有名字树：没有文档级附件，不用补。
        }
        const QPDFObjectHandle embeddedFiles = names.getKey("/EmbeddedFiles");
        if (!embeddedFiles.isDictionary()) {
            return bytes;
        }

        QVector<QPair<QString, QPDFObjectHandle>> entries;
        collectNameTree(embeddedFiles, &entries, 0);
        if (entries.isEmpty()) {
            return bytes;
        }

        int patched = 0;
        int skipped = 0;
        for (const auto& entry : entries) {
            if (report != nullptr) {
                ++report->scanned;
            }
            const QString name = fileSpecName(entry.second);
            if (name.isEmpty()) {
                ++skipped;
                continue;
            }
            const QString mime = QString::fromLatin1(mimeTypeForFileName(name));
            if (patchStream(embeddedStream(entry.second), mime)) {
                ++patched;
            } else {
                ++skipped;
            }
        }

        if (report != nullptr) {
            report->patched = patched;
            report->skipped = skipped;
        }
        if (patched == 0) {
            // 没有要补的：原样返回，**不做整份重写**。
            return bytes;
        }

        QPDFWriter writer(*pdf);
        if (encrypted) {
            // 保留原加密：原所有者口令、原 /Perms 权限位、原打开口令一律不动。
            //
            // 为什么显式写出来：qpdf 12.4.2 的 setPreserveEncryption 默认就是 true，
            // 但那是「默认值」，不是「承诺」——哪天的默认值变了，这里就静默改口径。
            // copyEncryptionParameters 把加密字典从被写的那个 QPDF 原样搬过来，
            // 免得依赖「写出去的那份就是来源」这个隐含前提。
            //
            // 为什么不能走「解密 -> 用用户口令当所有者口令重新加密」：那会改掉
            // 用户的权限位与所有者口令，是用户数据损失（实测确认）。
            writer.setPreserveEncryption(true);
            writer.copyEncryptionParameters(*pdf);
        }
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) {
                *error = QStringLiteral("qpdf 没有写出补过 MIME 的结果");
            }
            return bytes;
        }
        if (report != nullptr) {
            report->rewritten = true;
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = QString::fromUtf8(e.what());
        }
        return bytes;
    }
#else
    // 没编 qpdf 的构建：补不了 MIME，但**不能因此拦住保存**。
    return bytes;
#endif
}

// 不带口令的入口（既有调用方用这个）。
//
// 对未加密文档的行为与以前完全一样；加密文档走「需要打开密码」的失败分支，
// 原因写进 error，不再是一句没人看的 qpdf 原文。
QByteArray applyEmbeddedFileMimeTypes(const QByteArray& bytes,
                                       EmbeddedFileMimeReport* report,
                                       QString* error) {
    return applyEmbeddedFileMimeTypes(bytes, QString(), report, error);
}

}  // namespace aipdf::pdf
