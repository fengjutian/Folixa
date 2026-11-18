// 嵌入文件本体清除的单元测试。
//
// 为什么单独开一个文件：sanitizeHiddenData() 只删了 /Names 里的 /EmbeddedFiles
// **索引**，文件本体（/Filespec + EmbeddedFile 流）还会被页面上的
// /FileAttachment 批注拉活，于是照样写进输出文件。
// 「声称清掉了、字节里还在」是最坏的一种失败——所以这里的断言不是
// 「返回 true」，而是**payload 真的不在输出字节里**。
//
// 夹具是现造的：拿 normal.pdf 当底，用 qpdf 自己写一个 /Filespec +
// /EmbeddedFile 流 + /Names 树，再在页面上挂一个 /FileAttachment 批注。
// payload 用一段可识别的 ASCII 串，好比对了。

#include "pdf/PdfSecurity.h"
#include "pdf/PdfiumAdapter.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTest>

#include <memory>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

using aipdf::pdf::removeEmbeddedFiles;
using aipdf::pdf::sanitizeHiddenData;
using aipdf::pdf::sanitizeHiddenDataFully;

namespace {

// 好认的 payload：输出字节里搜到它，就说明文件本体还在。
const QByteArray kPayload = QByteArrayLiteral("EMBEDDED-PAYLOAD-MARKER-12345");

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

QByteArray readFixture(const QString& name) {
    QFile file(fixturePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

#ifdef AIPDF_WITH_QPDF

// 造一份带文档级嵌入文件的 PDF。
//
// 两条引用都挂上，是故意的：
//   1) catalog 的 /Names → /EmbeddedFiles 名称树（文档级嵌入文件的标准入口）；
//   2) 页面上的 /FileAttachment 批注 → /FS（附件本体也是嵌入文件）。
// 只删第 1 条时，第 2 条会让整条链路继续可达——那就是 sanitizeHiddenData
// 留下的缺口，本测试要盯住的就是它。
QByteArray buildPdfWithEmbeddedFile(const QByteArray& payload, bool withAnnotation = true) {
    const QByteArray source = readFixture(QStringLiteral("normal.pdf"));
    if (source.isEmpty()) {
        return {};
    }

    QPDF pdf;
    pdf.processMemoryFile("embedded-source.pdf", source.constData(),
                          static_cast<size_t>(source.size()));

    // 用 qpdf 官方的嵌入文件接口造：
    //   EmbeddedFile 流 → /Filespec → 名称树（/Names /EmbeddedFiles）。
    const QPDFEFStreamObjectHelper stream =
        QPDFEFStreamObjectHelper::createEFStream(pdf, payload.toStdString());
    const QPDFFileSpecObjectHelper spec =
        QPDFFileSpecObjectHelper::createFileSpec(pdf, "payload.txt", stream);
    QPDFEmbeddedFileDocumentHelper::get(pdf).replaceEmbeddedFile("payload.txt", spec);

    // 页面批注：第二条引用路径。
    if (withAnnotation && !pdf.getAllPages().empty()) {
        QPDFObjectHandle page = pdf.getAllPages().at(0);
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newInteger(0));
        rect.appendItem(QPDFObjectHandle::newInteger(0));
        rect.appendItem(QPDFObjectHandle::newInteger(0));
        rect.appendItem(QPDFObjectHandle::newInteger(0));
        QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
        annot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        annot.replaceKey("/Subtype", QPDFObjectHandle::newName("/FileAttachment"));
        annot.replaceKey("/Rect", rect);
        annot.replaceKey("/FS", spec.getObjectHandle());

        QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) {
            annots = QPDFObjectHandle::newArray();
            page.replaceKey("/Annots", annots);
        }
        annots.appendItem(annot);
    }

    QPDFWriter writer(pdf);
    writer.setOutputMemory();
    // 不压缩流：payload 以明文落在夹具字节里，
    // 「输入里有、输出里没有」这个断言才不是空转。
    writer.setCompressStreams(false);
    writer.write();
    std::unique_ptr<Buffer> buffer(writer.getBuffer());
    if (!buffer || buffer->getSize() == 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                      static_cast<int>(buffer->getSize()));
}

// 文件里（不管可不可达）还有没有这个 payload。
//
// 只在原始字节里搜是不够的：qpdf 写出会把流压成 Flate，
// 压过之后明文就搜不到了——那样「搜不到」证明不了任何事。
// 所以这里把文件里所有对象（含不可达对象）都翻一遍，
// 取每个流的**解码后**数据再搜。
bool payloadSurvives(const QByteArray& bytes,
                      const QByteArray& payload,
                      const QString& password = QString()) {
    QPDF pdf;
    const QByteArray utf8 = password.toUtf8();
    pdf.processMemoryFile("scan.pdf", bytes.constData(), static_cast<size_t>(bytes.size()),
                          utf8.isEmpty() ? nullptr : utf8.constData());
    for (QPDFObjectHandle object : pdf.getAllObjects()) {
        if (!object.isStream()) {
            continue;
        }
        const std::shared_ptr<Buffer> data = object.getStreamData();
        if (!data || data->getSize() == 0) {
            continue;
        }
        const QByteArray content(reinterpret_cast<const char*>(data->getBuffer()),
                                  static_cast<int>(data->getSize()));
        if (content.contains(payload)) {
            return true;
        }
    }
    return false;
}

// 文件里还有几个 /FileAttachment 批注（附件载体）。
int fileAttachmentCount(const QByteArray& bytes, const QString& password = QString()) {
    QPDF pdf;
    const QByteArray utf8 = password.toUtf8();
    pdf.processMemoryFile("scan.pdf", bytes.constData(), static_cast<size_t>(bytes.size()),
                          utf8.isEmpty() ? nullptr : utf8.constData());
    int count = 0;
    for (const QPDFObjectHandle& page : pdf.getAllPages()) {
        const QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) {
            continue;
        }
        for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
            const QPDFObjectHandle subtype = annot.getKey("/Subtype");
            if (subtype.isName() && subtype.getName() == "/FileAttachment") {
                ++count;
            }
        }
    }
    return count;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

class EmbeddedFileTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void removeEmbeddedFilesDropsPayload();
    void removedDocumentStillOpensInPdfium();
    void documentWithoutEmbeddedFilesIsAccepted();
    void sanitizeHiddenDataStillKeepsTheBody();
    void sanitizeHiddenDataDropsBodyWithoutOtherReferences();
    void removeThenSanitizeKeepsItClean();
    void sanitizeHiddenDataFullyRemovesTheBody();
    void sanitizeHiddenDataFullyKeepsVisibleContent();
    void emptyInputIsRejected();
    void brokenInputIsRejected();
    void missingQpdfIsReported();
    void encryptedDocumentIsCleanedWithItsPassword();

private:
    bool qpdfAvailable_ = false;
};

void EmbeddedFileTests::initTestCase() {
    qpdfAvailable_ = aipdf::pdf::encryptionAvailable();
    if (!qpdfAvailable_) {
        QSKIP("本次构建没有启用 qpdf，嵌入文件清理不可测");
    }
    if (readFixture(QStringLiteral("normal.pdf")).isEmpty()) {
        QSKIP("缺少 normal.pdf 夹具");
    }
}

// 核心断言：跑完 removeEmbeddedFiles，payload 真的不在字节里了。
void EmbeddedFileTests::removeEmbeddedFilesDropsPayload() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY2(!source.isEmpty(), "造不出带嵌入文件的夹具");
    QVERIFY2(source.contains(kPayload), "夹具里本来就该有 payload");
    QVERIFY2(payloadSurvives(source, kPayload), "夹具里的嵌入文件流读不出来");

    QString error;
    const QByteArray cleaned = removeEmbeddedFiles(source, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    // 两层断言：原始字节里没有，解码后的流内容里也没有。
    QVERIFY2(!cleaned.contains(kPayload), "输出字节里还能搜到 payload");
    QVERIFY2(!payloadSurvives(cleaned, kPayload), "输出文件里还留着嵌入文件的流本体");
    // 附件载体（/FileAttachment 批注）也要清掉：留着它就是留着一个坏图标。
    QCOMPARE(fileAttachmentCount(cleaned), 0);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 清理之后文件还得是能打开的 PDF，页数不变。
void EmbeddedFileTests::removedDocumentStillOpensInPdfium() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY(!source.isEmpty());

    QString error;
    const QByteArray cleaned = removeEmbeddedFiles(source, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    const auto opened = aipdf::pdf::openDocumentFromBytes(cleaned, QStringLiteral("cleaned.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 本来就没有嵌入文件的文档：不该报错，也不该把文件弄坏。
void EmbeddedFileTests::documentWithoutEmbeddedFilesIsAccepted() {
#ifdef AIPDF_WITH_QPDF
    QString error;
    const QByteArray cleaned =
        removeEmbeddedFiles(readFixture(QStringLiteral("normal.pdf")), &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto opened = aipdf::pdf::openDocumentFromBytes(cleaned, QStringLiteral("plain.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 反面对照：sanitizeHiddenData 只删索引，文件本体还在。
//
// 这一条是整个改动的理由：没有它，新函数就没有「补上的缺口」可言。
void EmbeddedFileTests::sanitizeHiddenDataStillKeepsTheBody() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY(!source.isEmpty());

    QString error;
    const QByteArray sanitized = sanitizeHiddenData(source, &error);
    QVERIFY2(!sanitized.isEmpty(), qPrintable(error));

    // 明文可能被压缩掉了，所以要在解码后的流内容里找。
    QVERIFY2(payloadSurvives(sanitized, kPayload),
             "sanitizeHiddenData 之后文件本体已经没了——那就没有缺口可补，测试前提失效");
    // 附件载体还在：它把文件规格拉在文档里，索引删了也没用。
    QCOMPARE(fileAttachmentCount(sanitized), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 边界情况：只有名称树一条引用时，qpdf 的写出器本来就会丢弃不可达对象，
// 所以 sanitizeHiddenData 已经把本体清掉了。
//
// 这一条是为了把「缺口到底在哪」说清楚：缺口不在名称树那条路上，
// 而在**还有别的引用点**（批注 /AF）把文件规格拉活的时候。
// 写清楚这件事，才不至于把「qpdf 默认丢不可达对象」当成新函数的功劳。
void EmbeddedFileTests::sanitizeHiddenDataDropsBodyWithoutOtherReferences() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload, false);
    QVERIFY(!source.isEmpty());
    QVERIFY(payloadSurvives(source, kPayload));

    QString error;
    const QByteArray sanitized = sanitizeHiddenData(source, &error);
    QVERIFY2(!sanitized.isEmpty(), qPrintable(error));
    QVERIFY2(!payloadSurvives(sanitized, kPayload),
             "只有名称树一条引用时，qpdf 写出器本来就该丢弃不可达的文件本体");
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 两个函数要能串起来用：先清嵌入文件，再清隐藏数据，结果仍然干净。
void EmbeddedFileTests::removeThenSanitizeKeepsItClean() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY(!source.isEmpty());

    QString error;
    const QByteArray cleaned = removeEmbeddedFiles(source, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    const QByteArray sanitized = sanitizeHiddenData(cleaned, &error);
    QVERIFY2(!sanitized.isEmpty(), qPrintable(error));
    QVERIFY2(!sanitized.contains(kPayload), "串起来用之后 payload 又回来了");
    QVERIFY2(!payloadSurvives(sanitized, kPayload), "串起来用之后文件本体还在");

    const auto opened = aipdf::pdf::openDocumentFromBytes(sanitized, QStringLiteral("both.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 空输入：返回空 + 错误信息，不许静默「成功」。
void EmbeddedFileTests::emptyInputIsRejected() {
    QString error;
    QVERIFY(removeEmbeddedFiles(QByteArray(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

// 坏输入：不能崩，也不能假装成功。
void EmbeddedFileTests::brokenInputIsRejected() {
    QString error;
    QVERIFY(removeEmbeddedFiles(QByteArrayLiteral("this is not a pdf at all"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

// 没有 qpdf 的构建：必须给出中文原因，而不是空返回。
//
// 本机构建启用了 qpdf，走的是可用路径；这一条在没启用 qpdf 的构建上
// 才是硬断言，所以这里做成弱断言：有错误路径就检查错误信息非空。
void EmbeddedFileTests::missingQpdfIsReported() {
    if (qpdfAvailable_) {
        QSKIP("本次构建启用了 qpdf，走的是可用路径");
    }
    QString error;
    QVERIFY(removeEmbeddedFiles(readFixture(QStringLiteral("normal.pdf")), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}


// 界面上的「清理隐藏信息」走的就是这条组合。
//
// 为什么要有这条测试：removeEmbeddedFiles() 早就实现好了，却没有任何界面入口调用它——
// 用户点那个按钮，文件字节照样留在输出里（「声称清掉了、字节里还在」是最坏的失败）。
// 这条断言盯的是**用户点按钮之后**的结果，不是某个函数的中间产物。
void EmbeddedFileTests::sanitizeHiddenDataFullyRemovesTheBody() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY(!source.isEmpty());
    QVERIFY(payloadSurvives(source, kPayload));

    QString error;
    const QByteArray cleaned = sanitizeHiddenDataFully(source, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    QVERIFY2(!cleaned.contains(kPayload), "组合清理之后 payload 又回来了");
    QVERIFY2(!payloadSurvives(cleaned, kPayload), "组合清理之后文件本体还在");
    // 挂附件的批注要连附件一起删：留着它就是留着一个坏图标。
    QCOMPARE(fileAttachmentCount(cleaned), 0);

    const auto opened = aipdf::pdf::openDocumentFromBytes(cleaned, QStringLiteral("fully-cleaned.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QCOMPARE(opened.document.pageCount(), 1);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 清隐藏信息不能把正文一起清掉：清完还得能读、能搜。
void EmbeddedFileTests::sanitizeHiddenDataFullyKeepsVisibleContent() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY(!source.isEmpty());

    QString error;
    const QByteArray cleaned = sanitizeHiddenDataFully(source, &error);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    const auto opened = aipdf::pdf::openDocumentFromBytes(cleaned, QStringLiteral("fully-cleaned.pdf"));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    const auto text = page->extractText();
    QVERIFY(text.has_value());
    QVERIFY2(text->plainText().contains(QStringLiteral("Hello PDFium")),
             "清理隐藏信息之后正文没了");
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

// 加密文档：给密码就能清嵌入文件；清完**仍然加密**。
//
// 这一条盯的是两件事：
//   - 不给密码时必须明确失败（不能给一份「什么都没清」的假成功）；
//   - 给了密码就要清得干净，而且**不能把用户的锁拆了**——
//     把加密文档清成明文，等于替用户拆了锁。
void EmbeddedFileTests::encryptedDocumentIsCleanedWithItsPassword() {
#ifdef AIPDF_WITH_QPDF
    const QByteArray source = buildPdfWithEmbeddedFile(kPayload);
    QVERIFY(!source.isEmpty());
    QVERIFY(payloadSurvives(source, kPayload));

    aipdf::pdf::PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("secret");
    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(source, options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));
    QVERIFY(aipdf::pdf::requiresPasswordToOpen(encrypted, nullptr));

    // 不给密码：清不动，而且要说清是密码的事。
    QString deniedError;
    QVERIFY(removeEmbeddedFiles(encrypted, &deniedError).isEmpty());
    QVERIFY2(deniedError.contains(QString::fromUtf8("密码")), qPrintable(deniedError));

    // 给密码：清得掉。
    const QByteArray cleaned = removeEmbeddedFiles(encrypted, &error, options.userPassword);
    QVERIFY2(!cleaned.isEmpty(), qPrintable(error));

    // 清完仍然是加密的：不能把锁拆了。
    QVERIFY2(aipdf::pdf::requiresPasswordToOpen(cleaned, nullptr),
             "清理加密文档后不能把它清成明文");

    // 同一个密码能打开，嵌入文件真的没了。
    const auto opened =
        aipdf::pdf::openDocumentFromBytes(cleaned, QString(), options.userPassword);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QVERIFY2(!payloadSurvives(cleaned, kPayload, options.userPassword),
             "加密文档的嵌入文件本体还在");
    QCOMPARE(fileAttachmentCount(cleaned, options.userPassword), 0);
#else
    QSKIP("本次构建没有启用 qpdf");
#endif
}

QTEST_GUILESS_MAIN(EmbeddedFileTests)
#include "EmbeddedFileTests.moc"
