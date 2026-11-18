#include "pdf/convert/PortfolioWriter.h"

#include <QFile>
#include <QHash>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

using aipdf::pdf::PortfolioEntry;
using aipdf::pdf::PortfolioOptions;
using aipdf::pdf::PortfolioReadResult;

namespace {

#ifdef AIPDF_WITH_QPDF
#define REQUIRE_QPDF() \
    do {                \
    } while (false)
#else
#define REQUIRE_QPDF() QSKIP("当前构建没有启用 qpdf，跳过")
#endif

// 一份最小的合法 PDF（不是包）：用来验证「读一个不是包的 PDF 会失败」。
QByteArray plainPdf() {
    QVector<QPair<int, QByteArray>> objects;
    objects.append(qMakePair(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")));
    objects.append(qMakePair(2, QByteArray("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")));
    objects.append(qMakePair(3, QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] >>")));

    QByteArray out("%PDF-1.7\n");
    QHash<int, int> offsets;
    int maxId = 0;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        maxId = qMax(maxId, object.first);
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = maxId + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        if (offsets.contains(id)) {
            out += QStringLiteral("%1 00000 n \n")
                       .arg(offsets.value(id), 10, 10, QLatin1Char('0'))
                       .toLatin1();
        } else {
            out += QByteArray("0000000000 65535 f \n");
        }
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root %2 0 R >>\nstartxref\n%3\n%%EOF\n")
               .arg(count)
               .arg(1)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一个当附件用的 PDF 字节（内容随便，能当嵌入文件就行）。
QByteArray attachmentBytes(const QString& marker) {
    return QStringLiteral("%PDF-1.7\n% attachment %1\n").arg(marker).toUtf8();
}

}  // namespace

class PortfolioWriterTests : public QObject {
    Q_OBJECT

private slots:
    void emptyEntriesFail();
    void twoEntriesRoundTrip();
    void entryNameAndContentMatch();
    void titleAndAuthorRoundTrip();
    void writtenBytesAreValidPdf();
    void singleEntryWorks();
    void readingNonPortfolioFails();
    void readingEmptyBytesFails();
    void duplicateNamesFail();
    void qpdfParsesThePackage();
    void entriesKeepInputOrder();
    void descriptionRoundTrip();
    // 回归：嵌入文件流必须带 /Type /EmbeddedFile、/Subtype 与 /Params。
    //
    // 曾经的 bug：qpdf 12.4.2 的 replaceKey 只认字典对象，
    // 对流对象调用会被**静默忽略**（本文件开着 setSuppressWarnings，
    // 连警告都看不到）—— 别的阅读器因此认不出这是嵌入文件。
    void embeddedFileStreamCarriesTypeSubtypeAndParams();
};

// 1. 空条目 → 失败 + 中文原因。
void PortfolioWriterTests::emptyEntriesFail() {
    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(QVector<PortfolioEntry>(), PortfolioOptions(),
                                                        &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("没有")));
}

// 2. 两个条目写出来 → 读回有 2 个条目。
void PortfolioWriterTests::twoEntriesRoundTrip() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")), QString()});
    entries.append({QStringLiteral("乙.pdf"), attachmentBytes(QStringLiteral("b")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PortfolioReadResult read = aipdf::pdf::readPortfolio(bytes);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.entries.size(), 2);
}

// 3. 读回的条目名与内容一致。
void PortfolioWriterTests::entryNameAndContentMatch() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")), QString()});
    entries.append({QStringLiteral("乙.pdf"), attachmentBytes(QStringLiteral("b")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PortfolioReadResult read = aipdf::pdf::readPortfolio(bytes);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.entries.size(), entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        QCOMPARE(read.entries.at(i).name, entries.at(i).name);
        QCOMPARE(read.entries.at(i).bytes, entries.at(i).bytes);
    }
}

// 4. title / author 读回一致。
void PortfolioWriterTests::titleAndAuthorRoundTrip() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")), QString()});

    PortfolioOptions options;
    options.title = QStringLiteral("印前样张汇总");
    options.author = QStringLiteral("张工");
    options.subject = QStringLiteral("2026 春季印前检查");

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, options, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PortfolioReadResult read = aipdf::pdf::readPortfolio(bytes);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.title, options.title);
    QCOMPARE(read.author, options.author);
    QCOMPARE(read.subject, options.subject);
}

// 5. 写出的字节是合法 PDF：qpdf 能解析，页数与条目数一致。
void PortfolioWriterTests::writtenBytesAreValidPdf() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")), QString()});
    entries.append({QStringLiteral("乙.pdf"), attachmentBytes(QStringLiteral("b")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVERIFY(bytes.startsWith("%PDF-"));

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("portfolio.pdf", bytes.constData(),
                          static_cast<size_t>(bytes.size()));
    // 每个条目一页封面。
    QCOMPARE(static_cast<int>(pdf.getAllPages().size()), 2);
    QVERIFY(pdf.getRoot().getKey("/Collection").isDictionary());
    QVERIFY(pdf.getRoot().getKey("/Dests").isDictionary());
}

// 6. 单条目也可以。
void PortfolioWriterTests::singleEntryWorks() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("只有一个.pdf"), attachmentBytes(QStringLiteral("only")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PortfolioReadResult read = aipdf::pdf::readPortfolio(bytes);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.entries.size(), 1);
    QCOMPARE(read.entries.first().name, QStringLiteral("只有一个.pdf"));
}

// 7. 读一个不是包的 PDF → 失败 + 中文原因。
void PortfolioWriterTests::readingNonPortfolioFails() {
    REQUIRE_QPDF();
    const PortfolioReadResult read = aipdf::pdf::readPortfolio(plainPdf());
    QVERIFY(!read.ok);
    QVERIFY(!read.error.isEmpty());
    QVERIFY(read.error.contains(QStringLiteral("不是")));
}

// 8. 读空字节 → 失败。
void PortfolioWriterTests::readingEmptyBytesFails() {
    const PortfolioReadResult read = aipdf::pdf::readPortfolio(QByteArray());
    QVERIFY(!read.ok);
    QVERIFY(!read.error.isEmpty());
}

// 9. 条目名重复 → 失败（约定：不自动改名）。
void PortfolioWriterTests::duplicateNamesFail() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("同名.pdf"), attachmentBytes(QStringLiteral("a")), QString()});
    entries.append({QStringLiteral("同名.pdf"), attachmentBytes(QStringLiteral("b")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("重复")), qPrintable(error));
}

// 10. 写出的包能被 qpdf 命令行解析（--check）。
void PortfolioWriterTests::qpdfParsesThePackage() {
    REQUIRE_QPDF();
    if (QString::fromUtf8(AIPDF_QPDF_CLI).isEmpty()) QSKIP("没有 qpdf 命令行，跳过");

    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")), QString()});
    entries.append({QStringLiteral("乙.pdf"), attachmentBytes(QStringLiteral("b")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("portfolio.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();

    QProcess process;
    process.start(QString::fromUtf8(AIPDF_QPDF_CLI),
                  {QStringLiteral("--check"), path});
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
}

// 11. 条目顺序与写入顺序一致。
void PortfolioWriterTests::entriesKeepInputOrder() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("第一.pdf"), attachmentBytes(QStringLiteral("1")), QString()});
    entries.append({QStringLiteral("第二.pdf"), attachmentBytes(QStringLiteral("2")), QString()});
    entries.append({QStringLiteral("第三.pdf"), attachmentBytes(QStringLiteral("3")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PortfolioReadResult read = aipdf::pdf::readPortfolio(bytes);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.entries.size(), 3);
    QCOMPARE(read.entries.at(0).name, QStringLiteral("第一.pdf"));
    QCOMPARE(read.entries.at(1).name, QStringLiteral("第二.pdf"));
    QCOMPARE(read.entries.at(2).name, QStringLiteral("第三.pdf"));
}

// 12. 条目说明读得回来。
void PortfolioWriterTests::descriptionRoundTrip() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")),
                     QStringLiteral("第一版的封面样张")});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    const PortfolioReadResult read = aipdf::pdf::readPortfolio(bytes);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.entries.size(), 1);
    QCOMPARE(read.entries.first().description, QStringLiteral("第一版的封面样张"));
}


void PortfolioWriterTests::embeddedFileStreamCarriesTypeSubtypeAndParams() {
    REQUIRE_QPDF();
    QVector<PortfolioEntry> entries;
    entries.append({QStringLiteral("甲.pdf"), attachmentBytes(QStringLiteral("a")), QString()});

    QString error;
    const QByteArray bytes = aipdf::pdf::writePortfolio(entries, PortfolioOptions(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));

    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.processMemoryFile("portfolio.pdf", bytes.constData(),
                          static_cast<size_t>(bytes.size()));

    // 名字树 /Names /EmbeddedFiles：名字与 Filespec 交替。
    const QPDFObjectHandle namesArray =
        pdf.getRoot().getKey("/Names").getKey("/EmbeddedFiles").getKey("/Names");
    QVERIFY(namesArray.isArray());
    const std::vector<QPDFObjectHandle> items = namesArray.getArrayAsVector();
    QCOMPARE(static_cast<int>(items.size()), 2);

    const QPDFObjectHandle spec = items.at(1);
    QVERIFY(spec.isDictionary());
    const QPDFObjectHandle stream = spec.getKey("/EF").getKey("/F");
    QVERIFY2(stream.isStream(), "嵌入文件的内容必须是流");

    // 这些键曾经因为 replaceKey 作用在流对象上而全部丢失。
    const QPDFObjectHandle dict = stream.getDict();
    QCOMPARE(dict.getKey("/Type").getName(), std::string("/EmbeddedFile"));
    QVERIFY2(!dict.getKey("/Subtype").getName().empty(), "嵌入文件流必须有 /Subtype");
    QCOMPARE(dict.getKey("/Params").getKey("/Size").getIntValue(),
             static_cast<long long>(entries.first().bytes.size()));
}

QTEST_GUILESS_MAIN(PortfolioWriterTests)
#include "PortfolioWriterTests.moc"
