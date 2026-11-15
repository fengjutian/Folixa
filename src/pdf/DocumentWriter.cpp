#include "pdf/DocumentWriter.h"

#include "doc/SafeSave.h"
#include "i18n/Translate.h"
#include "pdf/Annotation.h"
#include "pdf/AnnotationDictionary.h"
#include "pdf/AppearanceStream.h"
#include "pdf/IncrementalWriter.h"
#include "pdf/PdfiumAdapter.h"
#include "pdf/QpdfBackend.h"

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

    QVector<AppearanceAttachment> attachments;
    const int pageCount = document.pageCount();
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        const auto page = document.page(pageIndex);
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
                                      const QVector<Annotation>& model) {
    SaveOutcome outcome;

    const SaveBytes prepared = serializeForSave(document, model);
    if (!prepared.ok) {
        outcome.error = prepared.error;
        return outcome;
    }
    outcome.warning = prepared.warning;
    outcome.appearanceCount = prepared.appearanceCount;

    // 落盘前的最后一道结构检查。qpdf 未编入时这是一次空操作（返回 true），
    // 所以调用方不需要 ifdef；编入时它能把"能渲染但结构坏了"的文件挡在磁盘之外。
    QString structureError;
    if (!verifyWithQpdf(prepared.bytes, &structureError)) {
        outcome.error = structureError;
        return outcome;
    }

    // 文件层的保证（临时文件、重开校验、备份、原子替换）仍在 SafeSave 里：
    // 那是经过缺陷修复的代码，重写一遍只会引入新问题。这里只是把它收进唯一的入口。
    const doc::SaveResult written = doc::SafeSave::writeAtomically(
        targetPath, prepared.bytes, document.pageCount(), createBackup);
    outcome.ok = written.ok;
    outcome.error = written.errorMessage;
    outcome.backupPath = written.backupPath;
    return outcome;
}

}  // namespace aipdf::pdf
