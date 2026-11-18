#include "doc/batch/BatchProcessor.h"

#include "pdf/security/PdfSecurity.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUuid>

using aipdf::doc::BatchItemResult;
using aipdf::doc::BatchReport;
using aipdf::doc::BatchStep;
using aipdf::doc::BatchStepBytes;
using aipdf::doc::runBatch;
using aipdf::pdf::PdfEncryptionOptions;
using aipdf::pdf::PdfOpenResult;
using aipdf::pdf::openDocumentFromBytes;

namespace {

// 造一份带文字层的最小 PDF：批处理要拿真 PDF 去加密，
// 拿几字节假数据去只会测到「qpdf 报错」这一条路径。
QByteArray buildPdf(int pageCount) {
    QByteArray out;
    out.append("%PDF-1.7\n%\xe2\xe3\xcf\xd3\n");
    QHash<int, int> offsets;
    const auto addObject = [&out, &offsets](int id, const QByteArray& body) {
        offsets.insert(id, out.size());
        out.append(QByteArray::number(id) + " 0 obj\n");
        out.append(body);
        out.append("\nendobj\n");
    };

    addObject(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>"));
    QByteArray kids;
    for (int index = 0; index < pageCount; ++index) {
        kids.append(QByteArray::number(4 + 2 * index) + " 0 R ");
    }
    addObject(2, QByteArray("<< /Type /Pages /Kids [") + kids + "] /Count "
                   + QByteArray::number(pageCount) + " >>");
    addObject(3, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));
    for (int index = 0; index < pageCount; ++index) {
        const int pageId = 4 + 2 * index;
        const int contentId = pageId + 1;
        const QByteArray content = "BT /F1 14 Tf 60 350 Td (Batch encryption sample) Tj ET\n";
        addObject(pageId,
                  QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
                             "/Resources << /Font << /F1 3 0 R >> >> /Contents ")
                      + QByteArray::number(contentId) + " 0 R >>");
        addObject(contentId, QByteArray("<< /Length ") + QByteArray::number(content.size())
                                + " >>\nstream\n" + content + "endstream");
    }

    // 交叉引用表必须稠密：留空位会让 qpdf 报一串「object has offset 0」警告。
    const int xrefOffset = out.size();
    const int count = 4 + 2 * pageCount;
    out.append("xref\n0 " + QByteArray::number(count) + "\n");
    out.append("0000000000 65535 f \n");
    for (int id = 1; id < count; ++id) {
        out.append(QByteArray::number(offsets.value(id, 0)).rightJustified(10, '0')
                   + " 00000 n \n");
    }
    out.append("trailer\n<< /Size " + QByteArray::number(count) + " /Root 1 0 R >>\n");
    out.append("startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n");
    return out;
}

QByteArray writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(contents);
    file.close();
    return contents;
}

// 读一个文件的内容（Qt6 没有 QByteArray::fromFile）。
QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

}  // namespace

class BatchEncryptTests final : public QObject {
    Q_OBJECT

private:
    QString m_tempDir;

private slots:
    void init();
    void cleanup();

    void encryptedFileRequiresPasswordAndOpensWithIt();
    void batchEncryptsEveryFileAndWritesThem();
    void oneBadFileDoesNotStopTheBatch();
    void failureReasonIsWrittenIntoTheJsonReport();
    void everyStepByteCountIsRecorded();
    void emptyPasswordIsRejectedWithClearReason();
    void nonPdfInputFailsWithClearReason();
    void permissionBitsFollowDialogDefaults();
    void reportRoundTripKeepsStepBytes();
    void defaultOptionsUseDialogPermissions();
    void encryptedSourceIsReencryptedWithTheNewPassword();
};

void BatchEncryptTests::init() {
    const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_tempDir = QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-encrypt-") + unique);
    QDir().mkpath(m_tempDir);
}

void BatchEncryptTests::cleanup() {
    if (!m_tempDir.isEmpty()) {
        QDir(m_tempDir).removeRecursively();
    }
    m_tempDir.clear();
}

// 读回验证：加密后的文件不给密码打不开，给了密码能打开，页数不变。
void BatchEncryptTests::encryptedFileRequiresPasswordAndOpensWithIt() {
    const QByteArray source = buildPdf(2);
    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");

    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(source, options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));
    QVERIFY(encrypted.size() > 0);

    // 不给密码：必须打不开，而且要明确报「要密码」。
    const PdfOpenResult withoutPassword = openDocumentFromBytes(encrypted);
    QVERIFY(!withoutPassword.document.isValid());
    QVERIFY(withoutPassword.passwordRequired);

    // 给密码：能打开，页数与原文一致。
    const PdfOpenResult withPassword =
        openDocumentFromBytes(encrypted, QString(), options.userPassword);
    QVERIFY2(withPassword.document.isValid(), qPrintable(withPassword.errorMessage));
    QCOMPARE(withPassword.document.pageCount(), 2);
}

// 批处理：一份密一份明都能加密并落盘。
void BatchEncryptTests::batchEncryptsEveryFileAndWritesThem() {
    const QString first = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    const QString second = QDir(m_tempDir).filePath(QStringLiteral("b.pdf"));
    writeFile(first, buildPdf(1));
    writeFile(second, buildPdf(3));
    const QString outputDirectory = QDir(m_tempDir).filePath(QStringLiteral("out"));

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");

    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport report =
        runBatch({first, second}, {}, steps, true, outputDirectory);

    QCOMPARE(report.items.size(), 2);
    QCOMPARE(report.succeededCount, 2);
    QCOMPARE(report.failedCount, 0);
    QVERIFY(!report.aborted);

    for (const BatchItemResult& item : report.items) {
        QVERIFY2(item.succeeded, qPrintable(item.error));
        QVERIFY(item.error.isEmpty());
        QVERIFY(QFileInfo::exists(item.outputPath));
        QVERIFY(item.outputBytes > 0);

        // 读回：落盘的文件必须真的要求密码。
        const QByteArray bytes = readFile(item.outputPath);
        QVERIFY(!bytes.isEmpty());
        const PdfOpenResult withoutPassword = openDocumentFromBytes(bytes);
        QVERIFY(!withoutPassword.document.isValid());
        QVERIFY(withoutPassword.passwordRequired);
    }
}

// 负例：一个文件坏掉不能拖垮整批。
void BatchEncryptTests::oneBadFileDoesNotStopTheBatch() {
    const QString good = QDir(m_tempDir).filePath(QStringLiteral("good.pdf"));
    const QString bad = QDir(m_tempDir).filePath(QStringLiteral("bad.pdf"));
    const QString alsoGood = QDir(m_tempDir).filePath(QStringLiteral("also-good.pdf"));
    writeFile(good, buildPdf(1));
    writeFile(bad, QByteArrayLiteral("this is not a pdf at all"));
    writeFile(alsoGood, buildPdf(1));

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");

    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport report = runBatch({good, bad, alsoGood}, {}, steps, true);

    QCOMPARE(report.items.size(), 3);
    QCOMPARE(report.succeededCount, 2);
    QCOMPARE(report.failedCount, 1);
    QVERIFY(!report.aborted);

    // 坏掉的那个要说清为什么，不能只是一句「失败」。
    const BatchItemResult& failed = report.items.at(1);
    QVERIFY(!failed.succeeded);
    QVERIFY2(failed.error.contains(QString::fromUtf8("加密")), qPrintable(failed.error));
    QVERIFY(!failed.error.isEmpty());

    // 前后的文件照样处理完了。
    QVERIFY(report.items.at(0).succeeded);
    QVERIFY(report.items.at(2).succeeded);
}

// 负例：失败原因要逐个写进 JSON 报告。
void BatchEncryptTests::failureReasonIsWrittenIntoTheJsonReport() {
    const QString bad = QDir(m_tempDir).filePath(QStringLiteral("bad.pdf"));
    writeFile(bad, QByteArrayLiteral("not a pdf"));

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");
    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport report = runBatch({bad}, {}, steps, true);
    const QByteArray json = report.toJson();

    QVERIFY2(json.contains(bad.toUtf8()), json.constData());
    QVERIFY2(json.contains(QString::fromUtf8("步骤").toUtf8()), json.constData());
    QVERIFY2(json.contains(QString::fromUtf8("失败").toUtf8()), json.constData());
    // 中文不转义：报告是给人看的。
    QVERIFY2(json.contains(QString::fromUtf8("加密失败").toUtf8()), json.constData());

    const QJsonDocument document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());
    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);
    const QJsonObject item = items.at(0).toObject();
    QCOMPARE(item.value(QStringLiteral("succeeded")).toBool(), false);
    QCOMPARE(document.object().value(QStringLiteral("failedCount")).toInt(), 1);
    // 失败的文件没有产出路径。
    QVERIFY(item.value(QStringLiteral("outputPath")).toString().isEmpty());
}

// 报告里每一步的字节数都要照记。
void BatchEncryptTests::everyStepByteCountIsRecorded() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    const QByteArray source = buildPdf(1);
    writeFile(input, source);

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");

    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport report = runBatch({input}, {}, steps, true);
    QCOMPARE(report.items.size(), 1);
    const BatchItemResult& item = report.items.at(0);
    QVERIFY(item.succeeded);
    QCOMPARE(item.stepBytes.size(), 1);
    const BatchStepBytes& stepBytes = item.stepBytes.at(0);
    QCOMPARE(stepBytes.name, QStringLiteral("加密"));
    QCOMPARE(stepBytes.inputBytes, static_cast<qint64>(source.size()));
    QCOMPARE(stepBytes.outputBytes, item.outputBytes);
    // 加密后体积通常变大（多了 /Encrypt 字典与加密流）。
    QVERIFY(stepBytes.outputBytes > 0);
}

// 负例：没有打开密码必须明确失败——那样的文件谁都能直接打开。
void BatchEncryptTests::emptyPasswordIsRejectedWithClearReason() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    writeFile(input, buildPdf(1));

    PdfEncryptionOptions options;  // 打开密码留空
    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport report = runBatch({input}, {}, steps, true);
    QCOMPARE(report.items.size(), 1);
    QVERIFY(!report.items.at(0).succeeded);
    QVERIFY2(report.items.at(0).error.contains(QString::fromUtf8("打开密码")),
             qPrintable(report.items.at(0).error));
    QCOMPARE(report.failedCount, 1);
}

// 负例：输入不是 PDF 时要给中文原因，不能崩、也不能静默通过。
void BatchEncryptTests::nonPdfInputFailsWithClearReason() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    writeFile(input, QByteArrayLiteral("hello, this is not a PDF"));

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");
    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport report = runBatch({input}, {}, steps, true);
    QCOMPARE(report.failedCount, 1);
    QVERIFY(!report.items.at(0).error.isEmpty());
    QVERIFY2(report.items.at(0).error.contains(QString::fromUtf8("加密")),
             qPrintable(report.items.at(0).error));
}

// 权限位沿用「加密与权限」对话框的默认值：能打印、不能复制正文、不能组装。
void BatchEncryptTests::permissionBitsFollowDialogDefaults() {
    const QByteArray source = buildPdf(1);
    const PdfEncryptionOptions options =
        aipdf::doc::defaultEncryptOptions(QStringLiteral("batch-secret"));

    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(source, options, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));

    int revision = 0;
    int permissionBits = 0;
    QVERIFY2(aipdf::pdf::inspectEncryption(encrypted, options.userPassword, &revision,
                                           &permissionBits, &error),
             qPrintable(error));
    // R6 = AES-256。
    QCOMPARE(revision, 6);

    // 权限位按**阅读器看到的**来判（PDFium 的读法，位号从 0 起）：
    // 这是用户真正会遇到的语义，比自己猜 qpdf 的 /P 位序可靠。
    const PdfOpenResult opened =
        openDocumentFromBytes(encrypted, QString(), options.userPassword);
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    const aipdf::pdf::DocumentPermissions permissions = opened.document.permissions();
    QVERIFY(permissions.encrypted);
    // 与「加密与权限」对话框的默认值一致：能打印、不能复制正文、不能组装、不能改写。
    QVERIFY(permissions.allowPrinting);
    QVERIFY(permissions.allowHighQualityPrinting);
    QVERIFY(!permissions.allowCopying);
    QVERIFY(!permissions.allowAssembly);
    QVERIFY(!permissions.allowModifying);
    QVERIFY(permissions.allowAccessibility);
    QVERIFY(permissions.allowAnnotations);
    QVERIFY(permissions.allowFormFilling);
}

// 报告存盘再读回来：每一步的字节数不能丢。
void BatchEncryptTests::reportRoundTripKeepsStepBytes() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("a.pdf"));
    const QByteArray source = buildPdf(1);
    writeFile(input, source);

    PdfEncryptionOptions options;
    options.userPassword = QStringLiteral("batch-secret");
    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), options));

    const BatchReport original = runBatch({input}, {}, steps, true);
    const QByteArray json = original.toJson();

    BatchReport restored;
    QString error;
    QVERIFY2(BatchReport::fromJson(json, &restored, &error), qPrintable(error));
    QCOMPARE(restored.items.size(), original.items.size());
    QCOMPARE(restored.items.at(0).stepBytes.size(), 1);
    QCOMPARE(restored.items.at(0).stepBytes.at(0).name, QStringLiteral("加密"));
    QCOMPARE(restored.items.at(0).stepBytes.at(0).inputBytes,
             original.items.at(0).stepBytes.at(0).inputBytes);
    QCOMPARE(restored.items.at(0).stepBytes.at(0).outputBytes,
             original.items.at(0).stepBytes.at(0).outputBytes);
}

// 默认设置的权限位必须与「加密与权限」对话框一致（不另发明一套语义）。
void BatchEncryptTests::defaultOptionsUseDialogPermissions() {
    const PdfEncryptionOptions options =
        aipdf::doc::defaultEncryptOptions(QStringLiteral("secret"));
    QCOMPARE(options.userPassword, QStringLiteral("secret"));
    // 与 pdf::PdfPermissions 的默认值逐项一致。
    const PdfEncryptionOptions plain;
    QCOMPARE(options.permissions.allowPrinting, plain.permissions.allowPrinting);
    QCOMPARE(options.permissions.allowHighQualityPrinting, plain.permissions.allowHighQualityPrinting);
    QCOMPARE(options.permissions.allowCopying, plain.permissions.allowCopying);
    QCOMPARE(options.permissions.allowAccessibility, plain.permissions.allowAccessibility);
    QCOMPARE(options.permissions.allowAnnotations, plain.permissions.allowAnnotations);
    QCOMPARE(options.permissions.allowFormFilling, plain.permissions.allowFormFilling);
    QCOMPARE(options.permissions.allowAssembly, plain.permissions.allowAssembly);
    QCOMPARE(options.permissions.allowModifying, plain.permissions.allowModifying);
}

// 源文件自己就是加密的：用源文件密码解开旧的，再按新设置加密。
//
// 这条盯的是「对着加密字节调用 encryptPdf()」这个坑：
// qpdf 连打开都打不开，只会说一句 invalid password。
// 批处理里源文件加密是很常见的（上一轮批处理加密的产物就是），
// 不支持的话等于「批量加密只能跑一遍」。
void BatchEncryptTests::encryptedSourceIsReencryptedWithTheNewPassword() {
    const QString input = QDir(m_tempDir).filePath(QStringLiteral("locked.pdf"));
    const QByteArray source = buildPdf(1);

    PdfEncryptionOptions first;
    first.userPassword = QStringLiteral("old-secret");
    QString error;
    const QByteArray encrypted = aipdf::pdf::encryptPdf(source, first, &error);
    QVERIFY2(!encrypted.isEmpty(), qPrintable(error));
    writeFile(input, encrypted);

    PdfEncryptionOptions second;
    second.userPassword = QStringLiteral("new-secret");
    const QString outputDirectory = QDir(m_tempDir).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(outputDirectory));

    // 没有源文件密码：必须明确失败，而不是产出谁也读不了的文件。
    QVector<BatchStep> blindSteps;
    blindSteps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), second));
    const BatchReport blind = runBatch({input}, {}, blindSteps, true, outputDirectory);
    QCOMPARE(blind.failedCount, 1);
    QVERIFY(!blind.items.at(0).error.isEmpty());

    // 给了源文件密码：解开旧的，按新设置加密，产出用新密码能打开。
    QVector<BatchStep> steps;
    steps.append(aipdf::doc::makeEncryptStep(QStringLiteral("加密"), second,
                                             QStringLiteral("old-secret")));
    const BatchReport report = runBatch({input}, {}, steps, true, outputDirectory);
    QCOMPARE(report.failedCount, 0);
    QVERIFY2(report.items.at(0).succeeded, qPrintable(report.items.at(0).error));

    const QByteArray produced = readFile(report.items.at(0).outputPath);
    QVERIFY(!produced.isEmpty());
    const PdfOpenResult reopened = openDocumentFromBytes(produced, QString(), second.userPassword);
    QVERIFY2(reopened.document.isValid(), qPrintable(reopened.errorMessage));
    QCOMPARE(reopened.document.pageCount(), 1);
}

QTEST_GUILESS_MAIN(BatchEncryptTests)
#include "BatchEncryptTests.moc"
