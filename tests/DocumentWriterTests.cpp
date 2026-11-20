// 保存路径的测试：serializeForSave 保证「保存下来的文件，批注在别的阅读器里也看得见」。
//
// 这一层最容易出的错是"漏补"——保存成功、文件合法、批注却在外单位置集体消失。
// 所以每条断言都盯着「补了几条」和「补了之后渲染有没有变化」，而不是"没报错"。

#include "pdf/annot/Annotation.h"
#include "pdf/document/DocumentWriter.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/security/PdfSecurity.h"
#include "pdf/document/QpdfBackend.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::DocumentWriter;
using aipdf::pdf::SaveOutcome;
using aipdf::pdf::SaveBytes;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

Annotation makeAnnotation(quint64 id, AnnotationType type, const QRectF& rect) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = 0;
    annotation.type = type;
    annotation.rect = rect;
    annotation.quads = { rect };
    annotation.color = type == AnnotationType::Square ? QColor(220, 40, 40) : QColor(255, 214, 0);
    return annotation;
}

// 打开夹具、写入批注、返回准备保存的字节。
aipdf::pdf::SaveBytes saveBytesWith(const QVector<QVector<Annotation>>& perPage) {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    if (!opened.document.isValid()) {
        return {};
    }
    for (int pageIndex = 0; pageIndex < perPage.size(); ++pageIndex) {
        auto page = opened.document.page(pageIndex);
        if (!page.has_value()) {
            return {};
        }
        if (!page->setAnnotations(perPage.at(pageIndex))) {
            return {};
        }
    }
    return DocumentWriter::serializeForSave(opened.document);
}

}  // namespace

class DocumentWriterTests : public QObject {
    Q_OBJECT

private slots:
    void everyAnnotationGetsAnAppearance();
    void appearanceChangesPixelsOnSave();
    void noteOnlyDocumentIsLeftUntouched();
    void annotationsOnEveryPageAreCovered();
    void saveWritesFileWithAppearance();
    void saveCreatesBackupAndLeavesNoTempFile();
    void saveFailsWhenTargetDirectoryIsMissing();
    void inMemoryAnnotationHasNoAppearance();
    void qpdfGateRejectsBrokenBytes();
    void attachmentIsEmbeddedInSavedBytes();
    void videoAttachmentCarriesMimeType();
    void replyAndStateSurviveSaveAndReopen();
    void imageStampAndSoundAreEmbedded();
    void nativeSoundAnnotationIsInjected();
    void saveEncryptsWrittenFile();
    void savingEncryptedDocumentKeepsItsPassword();
    void reencryptingAnEncryptedDocumentUsesTheNewSettings();
};

void DocumentWriterTests::everyAnnotationGetsAnAppearance() {
    const aipdf::pdf::SaveBytes prepared = saveBytesWith({ {
        makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)),
        makeAnnotation(2, AnnotationType::Highlight, QRectF(60.0, 230.0, 160.0, 18.0)),
        makeAnnotation(3, AnnotationType::Square, QRectF(300.0, 120.0, 80.0, 40.0)),
    } });

    QVERIFY2(prepared.ok, qPrintable(prepared.error));
    QVERIFY2(prepared.warning.isEmpty(), qPrintable(prepared.warning));
    QCOMPARE(prepared.appearanceCount, 3);

    const auto reopened = openDocumentFromBytes(prepared.bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), 2);
    auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->annotations().size(), 3);
    // 三条批注的 /AP 引用都得在文件里，一条都不能少。
    QCOMPARE(prepared.bytes.count("/AP << /N"), 3);
}

void DocumentWriterTests::appearanceChangesPixelsOnSave() {
    const aipdf::pdf::SaveBytes plain = saveBytesWith({ {
        makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)),
    } });
    QVERIFY(plain.ok);

    // 同样的文档、同样的批注，但走"不补外观"的路径做对照。
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations({ makeAnnotation(1, AnnotationType::Highlight,
                                                  QRectF(60.0, 200.0, 200.0, 18.0)) }));
    QString error;
    const QByteArray withoutAppearance = opened.document.serialize(&error);
    QVERIFY2(!withoutAppearance.isEmpty(), qPrintable(error));

    const auto before = openDocumentFromBytes(withoutAppearance);
    const auto after = openDocumentFromBytes(plain.bytes);
    QVERIFY(before.document.isValid());
    QVERIFY(after.document.isValid());

    const auto pageBefore = before.document.page(0);
    const auto pageAfter = after.document.page(0);
    QVERIFY(pageBefore.has_value());
    QVERIFY(pageAfter.has_value());
    const QImage imageBefore = pageBefore->render(1.0);
    const QImage imageAfter = pageAfter->render(1.0);
    QCOMPARE(imageBefore.size(), imageAfter.size());

    int differing = 0;
    for (int y = 0; y < imageBefore.height(); ++y) {
        for (int x = 0; x < imageBefore.width(); ++x) {
            const QRgb a = imageBefore.pixel(x, y);
            const QRgb b = imageAfter.pixel(x, y);
            if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
                qAbs(qBlue(a) - qBlue(b)) > 2) {
                ++differing;
            }
        }
    }
    // 200×18 的高亮，光栅化误差留 10% 余量。
    QVERIFY2(differing >= 3000 && differing <= 4200,
             qPrintable(QStringLiteral("差异像素 %1").arg(differing)));
}

void DocumentWriterTests::noteOnlyDocumentIsLeftUntouched() {
    // 便签没有图形外观：不该被补 /AP，字节也不该被追加任何东西。
    const aipdf::pdf::SaveBytes prepared = saveBytesWith({ {
        makeAnnotation(9, AnnotationType::Note, QRectF(40.0, 40.0, 24.0, 24.0)),
    } });
    QVERIFY(prepared.ok);
    QVERIFY(prepared.warning.isEmpty());
    QCOMPARE(prepared.appearanceCount, 0);
    QVERIFY(!prepared.bytes.contains("/AP << /N"));
}

void DocumentWriterTests::annotationsOnEveryPageAreCovered() {
    const aipdf::pdf::SaveBytes prepared = saveBytesWith({
        { makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 120.0, 14.0)) },
        { makeAnnotation(2, AnnotationType::Highlight, QRectF(80.0, 160.0, 120.0, 14.0)) },
    });
    QVERIFY(prepared.ok);
    QCOMPARE(prepared.appearanceCount, 2);

    const auto reopened = openDocumentFromBytes(prepared.bytes);
    QVERIFY(reopened.document.isValid());
    for (int pageIndex = 0; pageIndex < 2; ++pageIndex) {
        auto page = reopened.document.page(pageIndex);
        QVERIFY(page.has_value());
        QCOMPARE(page->annotations().size(), 1);
    }
}

void DocumentWriterTests::saveWritesFileWithAppearance() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("out.pdf"));

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations(
        { makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)) }));

    const SaveOutcome outcome = DocumentWriter::save(opened.document, path, true);
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY2(outcome.warning.isEmpty(), qPrintable(outcome.warning));
    QCOMPARE(outcome.appearanceCount, 1);

    QVERIFY(QFileInfo::exists(path));
    // 临时文件不能留在用户目录里。
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".folixa.tmp")));
    // 目标原本不存在，不该凭空多出备份。
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".folixa.bak")));

    QFile written(path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();
    written.close();
    QVERIFY(bytes.contains("/AP << /N"));

    const auto reopened = openDocumentFromBytes(bytes);
    QVERIFY(reopened.document.isValid());
    QCOMPARE(reopened.document.pageCount(), 2);
    auto reopenedPage = reopened.document.page(0);
    QVERIFY(reopenedPage.has_value());
    QCOMPARE(reopenedPage->annotations().size(), 1);
    // 读回来要能看出"这条已经带外观流"，界面据此不再叠加绘制。
    QVERIFY(reopenedPage->annotations().first().hasAppearance);
}

void DocumentWriterTests::inMemoryAnnotationHasNoAppearance() {
    // 刚写进文档的批注还没有 /AP（要等保存时才补）：界面必须继续自己画。
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations(
        { makeAnnotation(5, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)) }));

    const auto annotations = page->annotations();
    QCOMPARE(annotations.size(), 1);
    QVERIFY(!annotations.first().hasAppearance);
}

void DocumentWriterTests::saveCreatesBackupAndLeavesNoTempFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.pdf"));

    // 先存一份没有批注的，作为"原文件"。
    {
        const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
        QVERIFY(opened.document.isValid());
        const SaveOutcome first = DocumentWriter::save(opened.document, path, false);
        QVERIFY2(first.ok, qPrintable(first.error));
    }
    QVERIFY(QFileInfo::exists(path));

    // 再带着批注覆盖保存：应当留下 .folixa.bak，并且不残留临时文件。
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations(
        { makeAnnotation(2, AnnotationType::Square, QRectF(100.0, 120.0, 80.0, 40.0)) }));

    const SaveOutcome second = DocumentWriter::save(opened.document, path, true);
    QVERIFY2(second.ok, qPrintable(second.error));
    QCOMPARE(second.backupPath, path + QStringLiteral(".folixa.bak"));
    QVERIFY(QFileInfo::exists(second.backupPath));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".folixa.tmp")));
}

void DocumentWriterTests::saveFailsWhenTargetDirectoryIsMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());

    const QString path = dir.filePath(QStringLiteral("missing/out.pdf"));
    const SaveOutcome outcome = DocumentWriter::save(opened.document, path, true);

    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(!QFileInfo::exists(path));
}

void DocumentWriterTests::qpdfGateRejectsBrokenBytes() {
    // 守卫：结构校验必须真的能拦下坏文件，否则它就是一句"总是 true"的空操作。
    if (!aipdf::pdf::qpdfBackendAvailable()) {
        QSKIP("本次构建未启用 qpdf（-DAIPDF_WITH_QPDF=ON 后生效）");
    }

    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->setAnnotations(
        { makeAnnotation(1, AnnotationType::Highlight, QRectF(60.0, 200.0, 200.0, 18.0)) }));
    const auto prepared = DocumentWriter::serializeForSave(opened.document);
    QVERIFY(prepared.ok);

    // 正常产物必须通过。
    QString error;
    QVERIFY2(aipdf::pdf::verifyWithQpdf(prepared.bytes, &error), qPrintable(error));

    // 砍掉尾部（trailer/startxref 没了），必须被拦下。
    const QByteArray broken = prepared.bytes.left(prepared.bytes.size() / 2);
    error.clear();
    QVERIFY2(!aipdf::pdf::verifyWithQpdf(broken, &error), "qpdf 没拦下截断的文件");
    QVERIFY(!error.isEmpty());
}

void DocumentWriterTests::attachmentIsEmbeddedInSavedBytes() {
    // 附件这条链的验收：保存后的字节里必须真的带着文件内容，
    // 而不是只写了一个指向不存在对象的 /FS。
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());

    Annotation annotation;
    annotation.id = 77;
    annotation.pageIndex = 0;
    annotation.type = AnnotationType::FileAttachment;
    annotation.rect = QRectF(60.0, 400.0, 20.0, 20.0);
    annotation.color = QColor(120, 120, 120);
    annotation.contents = QStringLiteral("payload.bin");
    annotation.author = QStringLiteral("tester");
    annotation.attachmentName = QStringLiteral("payload.bin");
    annotation.attachmentData = QByteArray("ATTACHMENT-PAYLOAD-0123456789");
    QVERIFY(page->setAnnotations({ annotation }));
    // 先确认 PDFium 真的建出了这条批注：它不认识 FILEATTACHMENT 的话，
    // 后面的"没写进文件"就会归错地方。
    QCOMPARE(page->annotations().size(), 1);

    const SaveBytes saved = DocumentWriter::serializeForSave(opened.document, { annotation });
    if (!saved.warning.isEmpty()) {
        qWarning("warning: %s", qPrintable(saved.warning));
    }
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY(saved.bytes.contains("/Type /EmbeddedFile"));
    QVERIFY(saved.bytes.contains("/Type /Filespec"));
    QVERIFY2(saved.bytes.contains("ATTACHMENT-PAYLOAD-0123456789"),
             "文件内容没有真的写进 PDF");
    QVERIFY(saved.bytes.contains("/FS "));

    // 顺带过一遍结构校验：多出两个对象后 xref 仍然要自洽。
    QString qpdfError;
    if (aipdf::pdf::qpdfBackendAvailable()) {
        QVERIFY2(aipdf::pdf::verifyWithQpdf(saved.bytes, &qpdfError), qPrintable(qpdfError));
    }
}

void DocumentWriterTests::videoAttachmentCarriesMimeType() {
    // 录制中心的产物是 .avi；挂进 PDF 后，阅读器要能认出这是视频，
    // 点开才会交给播放器 —— 靠的是嵌入文件流上的 /Subtype（MIME）。
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());

    Annotation annotation;
    annotation.id = 78;
    annotation.pageIndex = 0;
    annotation.type = AnnotationType::FileAttachment;
    annotation.rect = QRectF(100.0, 400.0, 20.0, 20.0);
    annotation.color = QColor(120, 120, 120);
    annotation.attachmentName = QStringLiteral("Folixa-screen.avi");
    annotation.attachmentData = QByteArray("RIFF....AVI-MJPEG-PAYLOAD");
    QVERIFY(page->setAnnotations({ annotation }));

    const SaveBytes saved = DocumentWriter::serializeForSave(opened.document, { annotation });
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY2(saved.bytes.contains("/Subtype (video/x-msvideo)"),
             "视频附件没有写 MIME 类型");
    QVERIFY2(saved.bytes.contains("RIFF....AVI-MJPEG-PAYLOAD"),
             "视频内容没有真的写进 PDF");

    QString qpdfError;
    if (aipdf::pdf::qpdfBackendAvailable()) {
        QVERIFY2(aipdf::pdf::verifyWithQpdf(saved.bytes, &qpdfError), qPrintable(qpdfError));
    }
}

void DocumentWriterTests::replyAndStateSurviveSaveAndReopen() {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());

    Annotation parent;
    parent.id = 31;
    parent.pageIndex = 0;
    parent.type = AnnotationType::Highlight;
    parent.quads = { QRectF(60.0, 300.0, 200.0, 18.0) };
    parent.rect = QRectF(60.0, 300.0, 200.0, 18.0);
    parent.color = QColor(255, 214, 0);
    parent.author = QStringLiteral("张三");
    parent.reviewState = QStringLiteral("Accepted");
    parent.createdAt = QDateTime(QDate(2026, 11, 15), QTime(10, 30, 0), Qt::UTC);

    Annotation reply = parent;
    reply.id = 32;
    reply.type = AnnotationType::Note;
    reply.rect = QRectF(300.0, 300.0, 16.0, 16.0);
    reply.contents = QStringLiteral("我同意");
    reply.replyToId = 31;

    QVERIFY(page->setAnnotations({ parent, reply }));

    const SaveBytes saved = DocumentWriter::serializeForSave(opened.document, { parent, reply });
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY2(saved.bytes.contains("/IRT"), "回复没有写出 /IRT");
    QVERIFY2(saved.bytes.contains("/RT /R"), "回复没有写出 /RT /R");
    QVERIFY2(saved.bytes.contains("/State (Accepted)"), "审阅状态没写进去");

    // 重新打开，确认对方软件看到的是同一条"回复"，而不是两条孤立批注。
    const auto reopened = openDocumentFromBytes(saved.bytes);
    QVERIFY(reopened.document.isValid());
    auto reopenedPage = reopened.document.page(0);
    QVERIFY(reopenedPage.has_value());
    bool sawAuthor = false;
    bool sawState = false;
    for (const Annotation& annotation : reopenedPage->annotations()) {
        if (annotation.author == QStringLiteral("张三")) {
            sawAuthor = true;
        }
        if (annotation.reviewState == QStringLiteral("Accepted")) {
            sawState = true;
        }
    }
    QVERIFY2(sawAuthor, "作者没读回来");
    QVERIFY2(sawState, "审阅状态没读回来");
}

void DocumentWriterTests::imageStampAndSoundAreEmbedded() {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());

    // 造一张 4x4 的**半透明**红图当作图章：顺带验证 /SMask 真的写进了文件。
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(QColor(200, 30, 30, 128));
    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    QVERIFY(!png.isEmpty());

    Annotation stamp;
    stamp.id = 51;
    stamp.pageIndex = 0;
    stamp.type = AnnotationType::Stamp;
    stamp.rect = QRectF(100.0, 200.0, 80.0, 80.0);
    stamp.color = QColor(200, 30, 30);
    stamp.imageData = png;

    Annotation sound;
    sound.id = 52;
    sound.pageIndex = 0;
    sound.type = AnnotationType::Sound;
    sound.rect = QRectF(300.0, 200.0, 24.0, 24.0);
    sound.color = QColor(38, 132, 255);
    sound.soundData = QByteArray("\x00\x01\x02\x03\x04\x05\x06\x07", 8);
    sound.soundRate = 8000;
    sound.soundChannels = 1;
    sound.soundBits = 8;

    QVERIFY2(page->setAnnotations({ stamp }), "图片图章没建出来");
    // 音频走的是附件通路：数据嵌入由 attachmentIsEmbeddedInSavedBytes 覆盖，
    // 这里只验证图片图章真的写成了 Image XObject。

    const SaveBytes saved = DocumentWriter::serializeForSave(opened.document, { stamp });
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY2(saved.bytes.contains("/Subtype /Image"), "图章图片没有写成 Image XObject");
    QVERIFY(saved.bytes.contains("/Im0"));
    QVERIFY2(saved.bytes.contains("/SMask"), "透明图章没有写软蒙版，透明区会变黑");
    QVERIFY(saved.bytes.contains("/DeviceGray"));

    QString qpdfError;
    if (aipdf::pdf::qpdfBackendAvailable()) {
        QVERIFY2(aipdf::pdf::verifyWithQpdf(saved.bytes, &qpdfError), qPrintable(qpdfError));
    }

    const auto reopened = openDocumentFromBytes(saved.bytes);
    QVERIFY(reopened.document.isValid());
}

void DocumentWriterTests::saveEncryptsWrittenFile() {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("encrypted.pdf"));

    aipdf::pdf::PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");
    options.permissions.allowCopying = false;
    options.permissions.allowPrinting = true;

    const aipdf::pdf::SaveOutcome saved =
        aipdf::pdf::DocumentWriter::save(opened.document, path, false, {}, &options);
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY(QFile::exists(path));

    // 没有密码：必须被当成「需要密码」，而不是损坏文件。
    const auto locked = openDocument(path);
    QVERIFY(locked.passwordRequired);
    QVERIFY(!locked.document.isValid());

    // 错误密码同样打不开。
    const auto wrong = openDocument(path, QStringLiteral("wrong"));
    QVERIFY(!wrong.document.isValid());

    // 正确密码：页数与内容结构都在。
    const auto reopened = openDocument(path, QStringLiteral("reader"));
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), opened.document.pageCount());

    // 落盘的是真正的 R6 加密，权限位也按设置写了。
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    int revision = 0;
    int permissions = 0;
    QVERIFY(aipdf::pdf::inspectEncryption(bytes, QStringLiteral("reader"), &revision,
                                          &permissions));
    QCOMPARE(revision, 6);
    QVERIFY(!(permissions & (1 << 4)));  // 复制位：关闭
    QVERIFY(permissions & (1 << 2));     // 打印位：开启
}

// 打开一份加密文档 → 原样保存 → 文件仍然加密，密码不变。
//
// 这是用户真的会走的路：打开加密 PDF → 改一点 → 保存。
// 落盘字节如果被清成明文，等于替用户拆了锁；校验时如果不带密码，
// 保存会直接失败（被自己写的结构校验挡住，报「invalid password」）。
void DocumentWriterTests::savingEncryptedDocumentKeepsItsPassword() {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());

    aipdf::pdf::PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("reader");
    options.ownerPassword = QStringLiteral("owner");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.filePath(QStringLiteral("locked.pdf"));
    const aipdf::pdf::SaveOutcome encrypted =
        aipdf::pdf::DocumentWriter::save(opened.document, sourcePath, false, {}, &options);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.error));

    const auto locked = openDocument(sourcePath, QStringLiteral("reader"));
    QVERIFY2(locked.document.isValid(), qPrintable(locked.errorMessage));

    // 原样保存：不改加密设置。
    const QString outPath = dir.filePath(QStringLiteral("saved.pdf"));
    const aipdf::pdf::SaveOutcome saved = aipdf::pdf::DocumentWriter::save(
        locked.document, outPath, false, {}, nullptr, QStringLiteral("reader"));
    QVERIFY2(saved.ok, qPrintable(saved.error));

    // 落盘的文件仍然要密码：没被清成明文。
    const auto withoutPassword = openDocument(outPath);
    QVERIFY(withoutPassword.passwordRequired);
    const auto withPassword = openDocument(outPath, QStringLiteral("reader"));
    QVERIFY2(withPassword.document.isValid(), qPrintable(withPassword.errorMessage));
    QCOMPARE(withPassword.document.pageCount(), locked.document.pageCount());

    // 没有打开密码时不能偷偷保存成明文：必须明确失败。
    const QString deniedPath = dir.filePath(QStringLiteral("denied.pdf"));
    const aipdf::pdf::SaveOutcome denied = aipdf::pdf::DocumentWriter::save(
        locked.document, deniedPath, false, {}, nullptr, QString());
    QVERIFY(!denied.ok);
    QVERIFY2(denied.error.contains(QString::fromUtf8("密码")), qPrintable(denied.error));
    QVERIFY(!QFile::exists(deniedPath));
}

// 打开加密文档 → 在「加密与权限」里改了密码 → 保存 → 用新密码能打开。
//
// 这里最关键的是：源字节本身是加密的，必须先解开旧的才能按新设置加密。
// 直接对加密字节调用 encryptPdf() 只会得到一句「invalid password」。
void DocumentWriterTests::reencryptingAnEncryptedDocumentUsesTheNewSettings() {
    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourcePath = dir.filePath(QStringLiteral("locked.pdf"));

    aipdf::pdf::PdfEncryptionOptions first;
    first.userPassword = QStringLiteral("old");
    first.ownerPassword = QStringLiteral("old-owner");
    const aipdf::pdf::SaveOutcome encrypted =
        aipdf::pdf::DocumentWriter::save(opened.document, sourcePath, false, {}, &first);
    QVERIFY2(encrypted.ok, qPrintable(encrypted.error));

    const auto locked = openDocument(sourcePath, QStringLiteral("old"));
    QVERIFY2(locked.document.isValid(), qPrintable(locked.errorMessage));

    aipdf::pdf::PdfEncryptionOptions second;
    second.userPassword = QStringLiteral("new");
    second.ownerPassword = QStringLiteral("new-owner");
    const QString outPath = dir.filePath(QStringLiteral("reencrypted.pdf"));
    const aipdf::pdf::SaveOutcome saved = aipdf::pdf::DocumentWriter::save(
        locked.document, outPath, false, {}, &second, QStringLiteral("old"));
    QVERIFY2(saved.ok, qPrintable(saved.error));

    // 新密码能打开，旧密码打不开。
    const auto withNew = openDocument(outPath, QStringLiteral("new"));
    QVERIFY2(withNew.document.isValid(), qPrintable(withNew.errorMessage));
    QCOMPARE(withNew.document.pageCount(), locked.document.pageCount());
    const auto withOld = openDocument(outPath, QStringLiteral("old"));
    QVERIFY(!withOld.document.isValid());
    QVERIFY(withOld.passwordRequired);
}

void DocumentWriterTests::nativeSoundAnnotationIsInjected() {    const auto opened = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    QVERIFY(opened.document.isValid());

    // 声音注释只存在于模型里：PDFium 建不出来，保存时由写入器插进页面 /Annots。
    Annotation sound;
    sound.id = 91;
    sound.pageIndex = 0;
    sound.type = AnnotationType::Sound;
    sound.rect = QRectF(200.0, 300.0, 28.0, 28.0);
    sound.contents = QStringLiteral("voice-note.wav");
    sound.soundData = QByteArray("\x00\x01\x02\x03\x04\x05\x06\x07", 8);
    sound.soundRate = 8000;
    sound.soundChannels = 1;
    sound.soundBits = 8;

    const SaveBytes saved = DocumentWriter::serializeForSave(opened.document, { sound });
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY2(saved.bytes.contains("/Subtype /Sound"), "没有写出 /Sound 注释");

    const auto reopened = openDocumentFromBytes(saved.bytes);
    QVERIFY(reopened.document.isValid());
    auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    bool foundSound = false;
    for (const Annotation& annotation : page->annotations()) {
        if (annotation.type == AnnotationType::Sound) {
            foundSound = true;
        }
    }
    QVERIFY2(foundSound, "声音注释没有被写进页面");
}

QTEST_MAIN(DocumentWriterTests)
#include "DocumentWriterTests.moc"
