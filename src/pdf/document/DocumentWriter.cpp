#include "pdf/document/DocumentWriter.h"

#include "pdf/kernel/SafeSave.h"
#include "i18n/Translate.h"
#include "pdf/annot/Annotation.h"
#include "pdf/annot/AnnotationDictionary.h"
#include "pdf/annot/AppearanceStream.h"
#include "pdf/document/IncrementalWriter.h"
#include "pdf/annot/PageAnnotsInjector.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/security/PdfSecurity.h"
#include "pdf/document/QpdfBackend.h"
#include "pdf/document/EmbeddedFileMime.h"

namespace aipdf::pdf {

SaveBytes DocumentWriter::serializeForSave(const PdfDocument& document,
                                                        const QVector<Annotation>& model) {
    SaveBytes result;

    QString serializeError;
    result.bytes = document.serialize(&serializeError);
    if (result.bytes.isEmpty()) {
        result.error = serializeError.isEmpty()
                           ? i18n::tr("aipdf::pdf::DocumentWriter", "文档序列化失败")
                           : serializeError;
        return result;
    }
    result.ok = true;

    // 原生 /Sound 注释：PDFium 建不出来，只能我们自己把字典插进页面 /Annots。
    bool injectedApplied = false;
    {
        QVector<InlineAnnotationInsert> inserts;
        const int modelPageCount = document.pageCount();
        for (const Annotation& annotation : model) {
            if (annotation.type != AnnotationType::Sound || annotation.soundData.isEmpty() ||
                annotation.pageIndex < 0 || annotation.pageIndex >= modelPageCount) {
                continue;
            }
            QByteArray sound = "<< /Type /Sound /R ";
            sound += QByteArray::number(annotation.soundRate);
            sound += " /C ";
            sound += QByteArray::number(annotation.soundChannels);
            sound += " /B ";
            sound += QByteArray::number(annotation.soundBits);
            sound += " /Length ";
            sound += QByteArray::number(annotation.soundData.size());
            sound += " >>\nstream\n";
            sound += annotation.soundData;
            sound += "\nendstream";

            QByteArray dict = "<< /Type /Annot /Subtype /Sound /Rect [";
            dict += QByteArray::number(annotation.rect.left(), 'f', 2);
            dict += ' ';
            dict += QByteArray::number(annotation.rect.top(), 'f', 2);
            dict += ' ';
            dict += QByteArray::number(annotation.rect.right(), 'f', 2);
            dict += ' ';
            dict += QByteArray::number(annotation.rect.bottom(), 'f', 2);
            dict += "] /NM (Folixa:";
            dict += QByteArray::number(annotation.id);
            dict += ") /Sound @EXTRA0 0 R";
            if (!annotation.contents.isEmpty()) {
                dict += " /Contents ";
                dict += annotationTextString(annotation.contents);
            }
            if (!annotation.author.isEmpty()) {
                dict += " /T ";
                dict += annotationTextString(annotation.author);
            }
            dict += " >>";

            InlineAnnotationInsert insert;
            insert.pageIndex = annotation.pageIndex;
            insert.dictionary = dict;
            insert.extraObjects.append(sound);
            inserts.append(insert);
        }
        if (!inserts.isEmpty()) {
            const InlineWriteResult injected = injectInlineAnnotations(result.bytes, inserts);
            if (!injected.ok) {
                result.warning = injected.error;
            } else {
                result.bytes = injected.bytes;
                injectedApplied = true;
            }
        }
    }

    // 注入之后必须换一份句柄：后续补 /AP 的流程要能看到刚插进去的注释。
    PdfDocument working;
    const PdfDocument* effective = &document;
    if (injectedApplied) {
        working = openDocumentFromBytes(result.bytes).document;
        if (working.isValid()) {
            effective = &working;
        }
    }

    QVector<AppearanceAttachment> attachments;
    const int pageCount = effective->pageCount();
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        const auto page = effective->page(pageIndex);
        if (!page.has_value()) {
            continue;
        }
        QVector<Annotation> candidates;
        const QVector<Annotation> pageAnnotations = page->annotations();
        if (model.isEmpty()) {
            candidates = pageAnnotations;
        } else {
            // 以页面里真实存在的批注为准，用模型里的版本覆盖同 id 的字段。
            // 反过来（只信模型）会把"模型里有、文档里没建出来"的批注也当成待写对象，
            // 写入器找不到标记就会让整次保存失败。
            QHash<quint64, Annotation> byId;
            for (const Annotation& annotation : model) {
                byId.insert(annotation.id, annotation);
            }
            for (const Annotation& existing : pageAnnotations) {
                const auto it = byId.constFind(existing.id);
                candidates.append(it != byId.constEnd() ? it.value() : existing);
            }
        }
        for (Annotation annotation : candidates) {
            // id 为 0 表示这条批注不是我们写的（没有 Folixa 标记）：
            // 别的软件写的批注通常自带外观流，重写既没必要也不安全。
            if (annotation.id == 0) {
                continue;
            }
            const Appearance appearance = buildAnnotationAppearance(annotation);
            AnnotationDictionaryParts parts = annotationDictionaryParts(annotation);

            for (const QByteArray& body : appearance.extraObjectBodies) {
                // 图片图章的画面在独立对象里，外观流用 @EXTRA0 / @EXTRA1 引用。
                parts.objects.append(body);
            }
            // 附件这类批注可能没有可画的外观，但有额外对象要建；
            // 回复（/IRT）与审阅状态也可能挂在没有外观的便签上。
            const bool hasMetadata = annotation.replyToId != 0 || !annotation.reviewState.isEmpty();
            if (appearance.isEmpty() && parts.objects.isEmpty() && !hasMetadata) {
                continue;  // 便签这类既无外观、无对象、也无元数据的批注，原样不动
            }
            attachments.append({ QByteArray("Folixa:") + QByteArray::number(annotation.id),
                                 appearance,
                                 parts.entries,
                                 parts.objects });
        }
    }

    if (attachments.isEmpty()) {
        return result;  // 没有需要补的批注，原样返回
    }

    const IncrementalWrite write = appendAnnotationAppearances(result.bytes, attachments);
    if (!write.ok) {
        // 文件本身是好的，只是批注在外单位置看不见：不阻断保存，但必须让调用方知道。
        result.warning = write.error;
        return result;
    }

    result.bytes = write.bytes;
    result.appearanceCount = write.appearanceCount;
    return result;
}

SaveOutcome DocumentWriter::save(const PdfDocument& document, const QString& targetPath,
                                      bool createBackup,
                                      const QVector<Annotation>& model,
                                      const PdfEncryptionOptions* encryption,
                                      const QString& documentPassword) {
    SaveOutcome outcome;

    const SaveBytes prepared = serializeForSave(document, model);
    if (!prepared.ok) {
        outcome.error = prepared.error;
        return outcome;
    }
    outcome.warning = prepared.warning;
    outcome.appearanceCount = prepared.appearanceCount;

    // 保存是整份重写，已有数字签名的覆盖范围必然改变。这里先数出来，
    // 由界面明确告诉用户“签名已失效”，而不是静默毁掉。
    //
    // 不用 qpdf 解析：DocumentWriter 被多个测试目标单独编译，依赖 qpdf
    // 解析器会把它们全部拖下水。每个签名字典都有且只有一个 /ByteRange，
    // 按它计数足够做“有没有签名、有几个”的提示（宁可多提示，也不要静默）。
    outcome.signatureCount = prepared.bytes.count("/ByteRange");

    // 加密放在结构校验之前：要校验的是最终会落盘的那份字节。
    QByteArray bytes = prepared.bytes;

    // 最终落盘字节的密码。空 = 落盘的是明文。
    QString password;

    if (encryption != nullptr) {
        // 用户重新设置了加密。源字节可能还带着原加密（PDFium 序列化会保留它），
        // 必须先解开旧的，才能按新设置加密——对着加密字节调用 encryptPdf()
        // 只会得到一句「invalid password」。
        if (requiresPasswordToOpen(bytes, nullptr)) {
            QString decryptError;
            const QByteArray plain = decryptPdf(bytes, documentPassword, &decryptError);
            if (plain.isEmpty()) {
                outcome.error = decryptError.isEmpty()
                                    ? i18n::tr("aipdf::pdf::DocumentWriter",
                                               "文档已加密，但没有它的打开密码，无法改加密设置")
                                    : decryptError;
                return outcome;
            }
            bytes = plain;
        }
    }

    // 文档级附件的 MIME 类型：PDFium 的嵌入文件接口写不进 /Subtype（实测限制），
    // 这里用 qpdf 补上。**必须在加密之前做**——补完再加密，加密才盖得住它；
    // 反过来的话，加密后的字节 qpdf 读不了，类型就永远补不上。
    //
    // 补不上不是保存失败：这是「锦上添花」，不能因为补个类型就拦住用户存盘。
    // 原来加密、且用户没改加密设置的那条路这里跳过——那种情况下字节仍是加密的，
    // 读它要密码，写回还要保住原加密设置（本仓库拿不到原所有者口令）。
    if (!requiresPasswordToOpen(bytes, nullptr)) {
        EmbeddedFileMimeReport mimeReport;
        QString mimeError;
        const QByteArray withMime = applyEmbeddedFileMimeTypes(bytes, &mimeReport, &mimeError);
        if (!withMime.isEmpty()) {
            bytes = withMime;
        }
        if (mimeReport.patched > 0) {
            outcome.warning = outcome.warning.isEmpty()
                                  ? i18n::tr("aipdf::pdf::DocumentWriter",
                                             "已给 %1 个文档级附件补上 MIME 类型")
                                             .arg(mimeReport.patched)
                                  : outcome.warning + QStringLiteral("；") +
                                        i18n::tr("aipdf::pdf::DocumentWriter",
                                                 "已给 %1 个文档级附件补上 MIME 类型")
                                             .arg(mimeReport.patched);
        }
    }

    if (encryption != nullptr) {
        QString encryptionError;
        bytes = encryptPdf(bytes, *encryption, &encryptionError);
        if (bytes.isEmpty()) {
            outcome.error = encryptionError;
            return outcome;
        }
        password = encryption->userPassword;
    } else if (requiresPasswordToOpen(bytes, nullptr)) {
        // 保持原来的加密（没改加密设置）：落盘字节仍是加密的，
        // 重开校验必须带着同一个密码，否则会被自己的校验挡下来。
        if (documentPassword.isEmpty()) {
            outcome.error = i18n::tr("aipdf::pdf::DocumentWriter",
                                     "文档已加密，但没有它的打开密码，无法保存");
            return outcome;
        }
        password = documentPassword;
    }

    // 落盘前的最后一道结构检查。qpdf 未编入时这是一次空操作（返回 true），
    // 所以调用方不需要 ifdef；编入时它能把"能渲染但结构坏了"的文件挡在磁盘之外。
    QString structureError;
    if (!verifyWithQpdf(bytes, &structureError, password)) {
        outcome.error = structureError;
        return outcome;
    }

    // 文件层的保证（临时文件、重开校验、备份、原子替换）仍在 SafeSave 里：
    // 那是经过缺陷修复的代码，重写一遍只会引入新问题。这里只是把它收进唯一的入口。
    const doc::SaveResult written = doc::SafeSave::writeAtomically(
        targetPath, bytes, document.pageCount(), createBackup, password);
    outcome.ok = written.ok;
    outcome.error = written.errorMessage;
    outcome.backupPath = written.backupPath;
    return outcome;
}

}  // namespace aipdf::pdf
