#include "pdf/ops/DocumentComparer.h"

#include "app/dialogs/DocumentCompareDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QSignalSpy>
#include <QStringList>
#include <QTableWidget>
#include <QTest>
#include <QUuid>

using aipdf::app::DocumentCompareDialog;
using aipdf::pdf::CompareDifference;
using aipdf::pdf::DiffKind;
using aipdf::pdf::DocumentCompareOptions;
using aipdf::pdf::DocumentCompareResult;
using aipdf::pdf::TextBlock;
using aipdf::pdf::VisualPageDifference;

namespace {

// 造一份最小的 PDF：每页给若干行文字（Helvetica 标准 14 号字）。
//
// 为什么在测试里自己造 PDF：仓库的夹具文字是固定的，
// 造不出「只改了一个词」这种精确场景；自己造能精确控制每一行，
// 比较算法的行为才验得准。
QByteArray escapePdfText(const QString& text) {
    QString result = text;
    result.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    result.replace(QLatin1Char('('), QStringLiteral("\\("));
    result.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    return result.toLatin1();
}

QByteArray buildPdf(const QVector<QVector<QString>>& pages) {
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
    for (int index = 0; index < pages.size(); ++index) {
        kids.append(QByteArray::number(4 + 2 * index) + " 0 R ");
    }
    addObject(2, QByteArray("<< /Type /Pages /Kids [") + kids + "] /Count "
                   + QByteArray::number(pages.size()) + " >>");
    addObject(3, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));

    for (int index = 0; index < pages.size(); ++index) {
        const int pageId = 4 + 2 * index;
        const int contentId = pageId + 1;
        QByteArray content;
        const QVector<QString>& lines = pages.at(index);
        for (int line = 0; line < lines.size(); ++line) {
            const int baseline = 350 - 20 * line;
            content.append("BT /F1 14 Tf 60 " + QByteArray::number(baseline) + " Td ("
                           + escapePdfText(lines.at(line)) + ") Tj ET\n");
        }
        addObject(pageId,
                  QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
                             "/Resources << /Font << /F1 3 0 R >> >> /Contents ")
                      + QByteArray::number(contentId) + " 0 R >>");
        addObject(contentId, QByteArray("<< /Length ") + QByteArray::number(content.size())
                                + " >>\nstream\n" + content + "endstream");
    }

    const int xrefOffset = out.size();
    const int count = 4 + 2 * pages.size();
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

// 一页的构造参数：文字行 + 图形指令 + 页面尺寸。
struct TestPage {
    QStringList lines;
    QStringList graphics;
    double width = 612.0;
    double height = 400.0;
};

// 一个填充矩形的图形指令（PDF user space，左下原点）。
//
// 为什么用颜色区分两份文件：颜色变了、位置没变，
// 差异区域就正好落在矩形上，包围盒可以按坐标验。
QString filledRect(double x, double y, double width, double height, const QString& color) {
    return QStringLiteral("%1 rg\n%2 %3 %4 %5 re\nf")
        .arg(color)
        .arg(QString::number(x, 'f', 1))
        .arg(QString::number(y, 'f', 1))
        .arg(QString::number(width, 'f', 1))
        .arg(QString::number(height, 'f', 1));
}

// 造一份可以带图形的 PDF。
//
// 为什么不能复用 buildPdf：它每页都挂了 /Font 与文字，
// 而「纯图形页」必须真的一个 BT/ET 都没有，
// 否则「文字层差异为 0」这条断言根本不成立。
QByteArray buildPdfWithGraphics(const QVector<TestPage>& pages) {
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
    for (int index = 0; index < pages.size(); ++index) {
        kids.append(QByteArray::number(4 + 2 * index) + " 0 R ");
    }
    addObject(2, QByteArray("<< /Type /Pages /Kids [") + kids + "] /Count "
                   + QByteArray::number(pages.size()) + " >>");
    addObject(3, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));

    for (int index = 0; index < pages.size(); ++index) {
        const TestPage& page = pages.at(index);
        const int pageId = 4 + 2 * index;
        const int contentId = pageId + 1;
        QByteArray content;
        for (const QString& graphic : page.graphics) {
            content.append(graphic.toLatin1());
            content.append("\n");
        }
        for (int line = 0; line < page.lines.size(); ++line) {
            const int baseline = qRound(page.height) - 40 - 20 * line;
            content.append("BT /F1 14 Tf 60 " + QByteArray::number(baseline) + " Td ("
                           + escapePdfText(page.lines.at(line)) + ") Tj ET\n");
        }
        // 没有文字的页不给 /Font：纯图形页必须真的没有文字层。
        const QByteArray resources = page.lines.isEmpty()
                                        ? QByteArray("<< >>")
                                        : QByteArray("<< /Font << /F1 3 0 R >> >>");
        addObject(pageId,
                  QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ")
                      + QByteArray::number(qRound(page.width)) + " "
                      + QByteArray::number(qRound(page.height)) + "] /Resources " + resources
                      + " /Contents " + QByteArray::number(contentId) + " 0 R >>");
        addObject(contentId, QByteArray("<< /Length ") + QByteArray::number(content.size())
                                + " >>\nstream\n" + content + "endstream");
    }

    const int xrefOffset = out.size();
    const int count = 4 + 2 * pages.size();
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

QByteArray writeTempFile(const QString& directory, const QString& name, const QByteArray& bytes) {
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(bytes);
    file.close();
    return bytes;
}

// 读一个文件的内容（Qt6 没有 QByteArray::fromFile）。
QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 找出第一条指定种类的差异。
const CompareDifference* findDifference(const DocumentCompareResult& result, DiffKind kind) {
    for (const CompareDifference& difference : result.differences) {
        if (difference.kind == kind) {
            return &difference;
        }
    }
    return nullptr;
}

}  // namespace

class DocumentComparerTests final : public QObject {
    Q_OBJECT

private:
    QString m_tempDir;

private slots:
    void init();
    void cleanup();

    void identicalDocumentsHaveNoDifferences();
    void modifiedBlockIsReportedWithBothTexts();
    void insertedBlockIsReported();
    void deletedBlockIsReported();
    void addedPageIsReported();
    void removedPageIsReported();
    void everyDifferenceCarriesPageAndSnippet();
    void unreadableDocumentFailsWithChineseReason();
    void documentsWithoutTextLayerFailWithChineseReason();
    void encryptedDocumentNeedsPassword();
    void missingFileIsReportedAsFailure();
    void textSimilarityHandlesEdgeCases();
    void compareDialogListsDifferencesAndEmitsNavigation();
    void compareDialogWithoutFilesFailsClearly();
    void pixelComparisonIsOffByDefault();
    void graphicsOnlyChangeIsDetectedByPixelComparison();
    void identicalDocumentsHaveNoVisualDifference();
    void graphicsChangedButTextUnchangedShowsOnlyVisualDifference();
    void differentPageSizesAreComparedOnCommonArea();
    void compareDialogShowsVisualDifferencesSeparately();
    void insertedPageKeepsLaterPagesAligned();
    void deletedPageKeepsLaterPagesAligned();
    void movedPageIsMatchedByFingerprint();
    void completelyDifferentDocumentsFallBackToPositionPairing();
    void alignmentFallsBackWhenPageLimitExceeded();
    void alignmentCanBeDisabledByOption();
};

void DocumentComparerTests::init() {
    const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_tempDir = QDir(QDir::tempPath()).filePath(QStringLiteral("aipdf-compare-") + unique);
    QDir().mkpath(m_tempDir);
}

void DocumentComparerTests::cleanup() {
    if (!m_tempDir.isEmpty()) {
        QDir(m_tempDir).removeRecursively();
    }
    m_tempDir.clear();
}

// 两份一模一样的文档：不该报任何差异，且整体相似度是 1。
void DocumentComparerTests::identicalDocumentsHaveNoDifferences() {
    const QByteArray bytes = buildPdf({{QStringLiteral("Contract title"),
                                         QStringLiteral("Party A")},
                                        {QStringLiteral("Second page text")}});
    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(bytes, bytes, DocumentCompareOptions(), &result, &error),
             qPrintable(error));
    QVERIFY(result.succeeded);
    QVERIFY(result.differences.isEmpty());
    QCOMPARE(result.error, QString());
    QCOMPARE(result.oldPageCount, 2);
    QCOMPARE(result.newPageCount, 2);
    QCOMPARE(result.comparedPageCount, 2);
    QVERIFY(result.oldBlockCount > 0);
    QVERIFY(result.unchangedBlockCount > 0);
    QVERIFY(qAbs(result.overallSimilarity - 1.0) < 1e-6);
}

// 改了一个词：要报成「修改」，并给出原文、新文与相似度。
void DocumentComparerTests::modifiedBlockIsReportedWithBothTexts() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Contract title"),
                                           QStringLiteral("Total amount is 100 USD")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Contract title"),
                                           QStringLiteral("Total amount is 200 USD")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));
    QCOMPARE(result.modifiedBlockCount, 1);
    QCOMPARE(result.insertedBlockCount, 0);
    QCOMPARE(result.deletedBlockCount, 0);

    const CompareDifference* modified = findDifference(result, DiffKind::Modified);
    QVERIFY(modified != nullptr);
    QCOMPARE(modified->oldText, QStringLiteral("Total amount is 100 USD"));
    QCOMPARE(modified->newText, QStringLiteral("Total amount is 200 USD"));
    QCOMPARE(modified->oldPageIndex, 0);
    QCOMPARE(modified->newPageIndex, 0);
    // 相似度要落在 (0,1)：完全一样是 1.0，完全不同是 0.0。
    QVERIFY(modified->similarity > 0.0);
    QVERIFY(modified->similarity < 1.0);
    QVERIFY(result.overallSimilarity < 1.0);
}

// 新文多出一行：报成「插入」，并带上新文原文。
void DocumentComparerTests::insertedBlockIsReported() {
    const QByteArray oldBytes =
        buildPdf({{QStringLiteral("Contract title"), QStringLiteral("Party A")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Contract title"),
                                           QStringLiteral("Party A"),
                                           QStringLiteral("New appendix clause")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    QCOMPARE(result.insertedBlockCount, 1);
    QCOMPARE(result.modifiedBlockCount, 0);
    const CompareDifference* inserted = findDifference(result, DiffKind::Inserted);
    QVERIFY(inserted != nullptr);
    QCOMPARE(inserted->newText, QStringLiteral("New appendix clause"));
    QCOMPARE(inserted->oldText, QString());
    QCOMPARE(inserted->newPageIndex, 0);
}

// 原文多出一行：报成「删除」，并带上原文原文。
void DocumentComparerTests::deletedBlockIsReported() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Contract title"),
                                           QStringLiteral("Party A"),
                                           QStringLiteral("Removed clause")}});
    const QByteArray newBytes =
        buildPdf({{QStringLiteral("Contract title"), QStringLiteral("Party A")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    QCOMPARE(result.deletedBlockCount, 1);
    QCOMPARE(result.insertedBlockCount, 0);
    const CompareDifference* deleted = findDifference(result, DiffKind::Deleted);
    QVERIFY(deleted != nullptr);
    QCOMPARE(deleted->oldText, QStringLiteral("Removed clause"));
    QCOMPARE(deleted->newText, QString());
    QCOMPARE(deleted->oldPageIndex, 0);
}

// 中间插了一页：要报成「新增页」，而不是把后面的页全说成改过。
void DocumentComparerTests::addedPageIsReported() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha page")},
                                          {QStringLiteral("Beta page")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha page")},
                                          {QStringLiteral("Gamma page")},
                                          {QStringLiteral("Beta page")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    QCOMPARE(result.oldPageCount, 2);
    QCOMPARE(result.newPageCount, 3);
    QCOMPARE(result.addedPageCount, 1);
    QCOMPARE(result.removedPageCount, 0);

    const CompareDifference* added = findDifference(result, DiffKind::PageAdded);
    QVERIFY(added != nullptr);
    QCOMPARE(added->newPageIndex, 1);
    QCOMPARE(added->oldPageIndex, -1);
    QVERIFY2(added->newText.contains(QStringLiteral("Gamma page")), qPrintable(added->newText));
}

// 原文多一页：报成「删除页」，并带上那一页的原文片段。
void DocumentComparerTests::removedPageIsReported() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha page")},
                                          {QStringLiteral("Beta page")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha page")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    QCOMPARE(result.removedPageCount, 1);
    QCOMPARE(result.addedPageCount, 0);
    const CompareDifference* removed = findDifference(result, DiffKind::PageRemoved);
    QVERIFY(removed != nullptr);
    QCOMPARE(removed->oldPageIndex, 1);
    QCOMPARE(removed->newPageIndex, -1);
    QVERIFY2(removed->oldText.contains(QStringLiteral("Beta page")), qPrintable(removed->oldText));
}

// 「引用定位」：每条差异都要带页码与可核对的文字片段。
void DocumentComparerTests::everyDifferenceCarriesPageAndSnippet() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Clause one"),
                                           QStringLiteral("Amount 100 USD")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Clause one"),
                                           QStringLiteral("Amount 200 USD")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));
    QVERIFY(!result.differences.isEmpty());
    for (const CompareDifference& difference : result.differences) {
        const int page = difference.newPageIndex >= 0 ? difference.newPageIndex
                                                      : difference.oldPageIndex;
        QVERIFY2(page >= 0, "差异必须带页码");
        QVERIFY2(!difference.oldText.isEmpty() || !difference.newText.isEmpty(),
                 "差异必须带可核对的原文或新文");
        QVERIFY(difference.similarity >= 0.0);
        QVERIFY(difference.similarity <= 1.0);
    }
}

// 负例：打不开的文件必须明确失败，不能崩、也不能报「没有差异」。
void DocumentComparerTests::unreadableDocumentFailsWithChineseReason() {
    DocumentCompareResult result;
    QString error;
    const QByteArray garbage = QByteArrayLiteral("%PDF-1.7\nthis is not a pdf body at all\n");
    QVERIFY(!aipdf::pdf::compareDocuments(garbage, garbage, DocumentCompareOptions(), &result,
                                          &error));
    QVERIFY(!result.succeeded);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!error.isEmpty());
    // 原因必须是中文：界面上要直接显示给用户看。
    QVERIFY2(result.error.contains(QString::fromUtf8("打不开")),
             qPrintable(result.error));
}

// 负例：两份都没有文字层（扫描件）不能说成「一样」，必须说清比不了。
void DocumentComparerTests::documentsWithoutTextLayerFailWithChineseReason() {
    const QByteArray scanned = buildPdf({{}});
    DocumentCompareResult result;
    QString error;
    QVERIFY(!aipdf::pdf::compareDocuments(scanned, scanned, DocumentCompareOptions(), &result,
                                          &error));
    QVERIFY(!result.succeeded);
    QVERIFY(!result.error.isEmpty());
    QVERIFY2(result.error.contains(QString::fromUtf8("文字层")),
             qPrintable(result.error));
}

// 负例：加密文件没给密码就打不开；给了密码才能比。
void DocumentComparerTests::encryptedDocumentNeedsPassword() {
    const QByteArray encrypted = readFile(fixturePath(QStringLiteral("encrypted.pdf")));
    QVERIFY(!encrypted.isEmpty());

    DocumentCompareResult result;
    QString error;
    QVERIFY(!aipdf::pdf::compareDocuments(encrypted, encrypted, DocumentCompareOptions(), &result,
                                          &error));
    QVERIFY(!result.succeeded);
    QVERIFY2(result.error.contains(QString::fromUtf8("加密")), qPrintable(result.error));

    DocumentCompareOptions options;
    options.oldPassword = QStringLiteral("secret");
    options.newPassword = QStringLiteral("secret");
    DocumentCompareResult opened;
    QVERIFY2(aipdf::pdf::compareDocuments(encrypted, encrypted, options, &opened, &error),
             qPrintable(error));
    QVERIFY(opened.succeeded);
}

// 负例：文件不存在要说清是哪个文件。
void DocumentComparerTests::missingFileIsReportedAsFailure() {
    const QString missing = QDir(m_tempDir).filePath(QStringLiteral("nope.pdf"));
    DocumentCompareResult result;
    QString error;
    QVERIFY(!aipdf::pdf::compareDocumentFiles(missing, missing, DocumentCompareOptions(), &result,
                                              &error));
    QVERIFY(!result.succeeded);
    QVERIFY2(result.error.contains(QStringLiteral("nope.pdf")), qPrintable(result.error));
}

// 负例与边界：相似度函数的边界值必须明确，不能把空串算成「完全一样」。
void DocumentComparerTests::textSimilarityHandlesEdgeCases() {
    QCOMPARE(aipdf::pdf::textSimilarity(QString(), QString()), 1.0);
    QCOMPARE(aipdf::pdf::textSimilarity(QString(), QStringLiteral("abc")), 0.0);
    QCOMPARE(aipdf::pdf::textSimilarity(QStringLiteral("abc"), QString()), 0.0);
    QCOMPARE(aipdf::pdf::textSimilarity(QStringLiteral("abc"), QStringLiteral("abc")), 1.0);
    // 只差空白：归一化之后应当视为相同。
    QCOMPARE(aipdf::pdf::textSimilarity(QStringLiteral("a b"), QStringLiteral("a  b")), 1.0);

    const double similar = aipdf::pdf::textSimilarity(QStringLiteral("Total amount is 100 USD"),
                                                       QStringLiteral("Total amount is 200 USD"));
    QVERIFY(similar > 0.5);
    QVERIFY(similar < 1.0);

    const double different = aipdf::pdf::textSimilarity(QStringLiteral("alpha"),
                                                         QStringLiteral("zzzzzzzz"));
    QVERIFY(different < 0.3);

    // 中文同样要能算出相似度（中文没有空格，靠字符二元组）。
    const double chinese = aipdf::pdf::textSimilarity(QString::fromUtf8("合同金额为一千元"),
                                                       QString::fromUtf8("合同金额为两千元"));
    QVERIFY(chinese > 0.5);
    QVERIFY(chinese < 1.0);
}

// 界面这一层：差异要摆进表格，点一行要能发出「跳到这一页」的信号。
void DocumentComparerTests::compareDialogListsDifferencesAndEmitsNavigation() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Clause one"),
                                           QStringLiteral("Amount 100 USD")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Clause one"),
                                           QStringLiteral("Amount 200 USD")}});
    const QString oldPath = QDir(m_tempDir).filePath(QStringLiteral("old.pdf"));
    const QString newPath = QDir(m_tempDir).filePath(QStringLiteral("new.pdf"));
    writeTempFile(m_tempDir, QStringLiteral("old.pdf"), oldBytes);
    writeTempFile(m_tempDir, QStringLiteral("new.pdf"), newBytes);

    DocumentCompareDialog dialog;
    dialog.setFiles(oldPath, newPath);
    QCOMPARE(dialog.oldFilePath(), oldPath);
    QCOMPARE(dialog.newFilePath(), newPath);

    QSignalSpy navigationSpy(&dialog, &DocumentCompareDialog::navigateToPage);
    QVERIFY(dialog.runComparison());
    QVERIFY(dialog.lastResult().succeeded);
    QVERIFY(dialog.lastError().isEmpty());
    QCOMPARE(dialog.differenceRowCount(), 1);

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("compare.table"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->columnCount(), 5);
    // 页码那一列要能看出是第几页。
    QVERIFY2(table->item(0, 1)->text().contains(QString::fromUtf8("1")),
             qPrintable(table->item(0, 1)->text()));

    // 点一行 → 发出跳转信号（带页号与片段）。
    dialog.activateDifference(0);
    QCOMPARE(navigationSpy.count(), 1);
    const QList<QVariant> arguments = navigationSpy.takeFirst();
    QCOMPARE(arguments.at(0).toInt(), 0);
    QVERIFY(!arguments.at(1).toString().isEmpty());
}

// 负例：没选文件就说清要选文件，不许静默跑出「没有差异」。
void DocumentComparerTests::compareDialogWithoutFilesFailsClearly() {
    DocumentCompareDialog dialog;
    QVERIFY(!dialog.runComparison());
    QVERIFY2(dialog.lastError().contains(QString::fromUtf8("请先选两份 PDF")),
             qPrintable(dialog.lastError()));
}

// 像素比对默认是关的：渲染很慢，不能默认让用户等。
void DocumentComparerTests::pixelComparisonIsOffByDefault() {
    QVERIFY(!DocumentCompareOptions().comparePixels);

    const QByteArray oldBytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one")}, {filledRect(100, 100, 200, 150, "1 0 0")}}});
    const QByteArray newBytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one")}, {filledRect(100, 100, 200, 150, "0 0 1")}}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));
    QVERIFY(result.succeeded);
    // 默认没开：不做像素比对，也就没有任何视觉差异。
    QVERIFY(!result.pixelsCompared);
    QCOMPARE(result.pixelComparedPageCount, 0);
    QCOMPARE(result.visualDifferencePageCount, 0);
    QVERIFY(result.visualDifferences.isEmpty());
}

// 纯图形改动：文字层一个字符都没动，视觉上却变了。
//
// 这是这一轮要闭合的边界：「只比文字层」看不见的改动。
void DocumentComparerTests::graphicsOnlyChangeIsDetectedByPixelComparison() {
    // 两份都是纯图形页：整份 PDF 里没有任何文字层。
    const QByteArray oldBytes = buildPdfWithGraphics(
        {TestPage{{}, {filledRect(100, 100, 200, 150, "1 0 0")}}});
    const QByteArray newBytes = buildPdfWithGraphics(
        {TestPage{{}, {filledRect(100, 100, 200, 150, "0 0 1")}}});

    DocumentCompareOptions options;
    options.comparePixels = true;
    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));
    QVERIFY(result.succeeded);

    // 文字层：两边都没有文字，一个字都没变。
    QCOMPARE(result.oldBlockCount, 0);
    QCOMPARE(result.newBlockCount, 0);
    QVERIFY(result.differences.isEmpty());
    QCOMPARE(result.insertedBlockCount, 0);
    QCOMPARE(result.deletedBlockCount, 0);
    QCOMPARE(result.modifiedBlockCount, 0);

    // 视觉：图形改了，必须被检出。
    QVERIFY(result.pixelsCompared);
    QCOMPARE(result.pixelComparedPageCount, 1);
    QCOMPARE(result.pixelIncomparablePageCount, 0);
    QCOMPARE(result.visualDifferencePageCount, 1);
    QCOMPARE(result.visualDifferences.size(), 1);

    const VisualPageDifference& visual = result.visualDifferences.at(0);
    QVERIFY(visual.comparable);
    QCOMPARE(visual.oldPageIndex, 0);
    QCOMPARE(visual.newPageIndex, 0);
    QVERIFY(visual.differingPixelCount > 0);
    QVERIFY(visual.differenceRatio > 0.05);
    QVERIFY(visual.differenceRatio < 0.5);
    QVERIFY(qAbs(result.averageVisualDifferenceRatio - visual.differenceRatio) < 1e-9);
    QVERIFY(qAbs(result.maxVisualDifferenceRatio - visual.differenceRatio) < 1e-9);

    // 差异区域要落在那个矩形上（用户空间：x 100..300，y 100..250）。
    // 留两点的余量：边缘可能有抗锯齿。
    // 注意 QRectF 在这个坐标里的 top() 是较小的那个 y（PDF 的下沿），
    // 与 TextBlock::bounds 同一约定。
    QVERIFY2(visual.oldBounds.left() >= 98.0 && visual.oldBounds.left() <= 102.0,
             qPrintable(QString::number(visual.oldBounds.left())));
    QVERIFY2(visual.oldBounds.right() >= 298.0 && visual.oldBounds.right() <= 302.0,
             qPrintable(QString::number(visual.oldBounds.right())));
    QVERIFY2(visual.oldBounds.top() >= 98.0 && visual.oldBounds.top() <= 102.0,
             qPrintable(QString::number(visual.oldBounds.top())));
    QVERIFY2(visual.oldBounds.bottom() >= 248.0 && visual.oldBounds.bottom() <= 252.0,
             qPrintable(QString::number(visual.oldBounds.bottom())));
}

// 两份完全相同：像素层面也必须没有差异。
//
// 这条同时验了「同一份 PDF 渲染两次是逐位一致的」这个前提：
// 抗锯齿要是飘了，这条就会红。
void DocumentComparerTests::identicalDocumentsHaveNoVisualDifference() {
    const QByteArray bytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one")}, {filledRect(100, 100, 200, 150, "1 0 0")}}});

    DocumentCompareOptions options;
    options.comparePixels = true;
    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(bytes, bytes, options, &result, &error),
             qPrintable(error));
    QVERIFY(result.pixelsCompared);
    QCOMPARE(result.pixelComparedPageCount, 1);
    QCOMPARE(result.pixelIncomparablePageCount, 0);
    QCOMPARE(result.visualDifferencePageCount, 0);
    QVERIFY(result.visualDifferences.isEmpty());
    QCOMPARE(result.maxVisualDifferenceRatio, 0.0);
    QCOMPARE(result.averageVisualDifferenceRatio, 0.0);
}

// 文字没变、图形变了：文字层要报 0 差异，视觉要报有差异。
void DocumentComparerTests::graphicsChangedButTextUnchangedShowsOnlyVisualDifference() {
    const QByteArray oldBytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one"), QStringLiteral("Amount 100 USD")},
                  {filledRect(100, 100, 200, 150, "1 0 0")}}});
    const QByteArray newBytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one"), QStringLiteral("Amount 100 USD")},
                  {filledRect(100, 100, 200, 150, "0 0 1")}}});

    DocumentCompareOptions options;
    options.comparePixels = true;
    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));
    QVERIFY(result.succeeded);

    // 文字层：一个字都没改。
    QVERIFY(result.oldBlockCount > 0);
    QVERIFY(result.differences.isEmpty());
    QCOMPARE(result.modifiedBlockCount, 0);
    QCOMPARE(result.insertedBlockCount, 0);
    QCOMPARE(result.deletedBlockCount, 0);

    // 视觉：图形变了。
    QCOMPARE(result.visualDifferencePageCount, 1);
    QCOMPARE(result.visualDifferences.size(), 1);
    QVERIFY(result.visualDifferences.at(0).differenceRatio > 0.0);
}

// 两页尺寸不同：不能直接比整张位图，只比左上角对齐的共同区域。
//
// 这条把共同区域的语义钉死：共同区域里像素一样 → 差异比例 0，
// 并且参与比较的像素数正好是共同区域的面积。
void DocumentComparerTests::differentPageSizesAreComparedOnCommonArea() {
    // 同一块图形，在两张不同高度的页上落到同一片图像区域里：
    // 旧页高 400，矩形在用户空间 y 300..350 → 图像行 50..100；
    // 新页高 792，矩形要放在 y 692..742 才落到同样的图像行 50..100。
    const QByteArray oldBytes = buildPdfWithGraphics(
        {TestPage{{}, {filledRect(100, 300, 200, 50, "1 0 0")}, 612.0, 400.0}});
    const QByteArray newBytes = buildPdfWithGraphics(
        {TestPage{{}, {filledRect(100, 692, 200, 50, "1 0 0")}, 612.0, 792.0}});

    DocumentCompareOptions options;
    options.comparePixels = true;
    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));
    QVERIFY(result.succeeded);

    // 比得了，不能因为尺寸不同就崩掉或整页跳过。
    QCOMPARE(result.pixelComparedPageCount, 1);
    QCOMPARE(result.pixelIncomparablePageCount, 0);

    QCOMPARE(result.visualDifferences.size(), 1);
    const VisualPageDifference& visual = result.visualDifferences.at(0);
    QVERIFY(visual.comparable);
    QVERIFY(visual.sizeMismatch);
    // 共同区域 = 612 × 400 = 244800 像素。
    QCOMPARE(visual.comparedPixelCount, 612LL * 400LL);
    QCOMPARE(visual.differenceRatio, 0.0);
    QCOMPARE(result.visualDifferencePageCount, 0);
}

// 界面这一层：视觉差异要单独摆一张表，不能和文字差异混在一起。
void DocumentComparerTests::compareDialogShowsVisualDifferencesSeparately() {
    const QByteArray oldBytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one")}, {filledRect(100, 100, 200, 150, "1 0 0")}}});
    const QByteArray newBytes = buildPdfWithGraphics(
        {TestPage{{QStringLiteral("Clause one")}, {filledRect(100, 100, 200, 150, "0 0 1")}}});
    const QString oldPath = QDir(m_tempDir).filePath(QStringLiteral("old.pdf"));
    const QString newPath = QDir(m_tempDir).filePath(QStringLiteral("new.pdf"));
    writeTempFile(m_tempDir, QStringLiteral("old.pdf"), oldBytes);
    writeTempFile(m_tempDir, QStringLiteral("new.pdf"), newBytes);

    DocumentCompareDialog dialog;
    dialog.setFiles(oldPath, newPath);

    // 开关默认关：用户不点就不做慢的像素比对。
    auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("compare.pixelCompare"));
    QVERIFY(check != nullptr);
    QVERIFY(!check->isChecked());
    QVERIFY(!dialog.options().comparePixels);

    // 用户勾上之后，选项要跟着变。
    check->setChecked(true);
    QVERIFY(dialog.options().comparePixels);

    QVERIFY(dialog.runComparison());

    // 文字差异表：空的（文字一个字都没改）。
    QCOMPARE(dialog.differenceRowCount(), 0);
    auto* textTable = dialog.findChild<QTableWidget*>(QStringLiteral("compare.table"));
    QVERIFY(textTable != nullptr);
    QCOMPARE(textTable->rowCount(), 0);

    // 视觉差异表：有一条，带差异比例。
    QCOMPARE(dialog.visualDifferenceRowCount(), 1);
    const VisualPageDifference* visual = dialog.visualDifferenceAt(0);
    QVERIFY(visual != nullptr);
    QVERIFY(visual->differenceRatio > 0.0);
    auto* visualTable = dialog.findChild<QTableWidget*>(QStringLiteral("compare.visualTable"));
    QVERIFY(visualTable != nullptr);
    QCOMPARE(visualTable->rowCount(), 1);
    QVERIFY2(visualTable->item(0, 1)->text().contains(QStringLiteral("%")),
             qPrintable(visualTable->item(0, 1)->text()));
    QVERIFY2(visualTable->item(0, 0)->text().contains(QString::fromUtf8("1")),
             qPrintable(visualTable->item(0, 0)->text()));
}

// 中间插一页：插进去之后的页仍然要配对正确，不能整体后移。
//
// 这条是这次要闭的欠账的核心：「中间插页会让配对整体后移」。
// 按页码位置配对的话，第 2 页会跟插进来的页配成一对，
// 后面每一页都被报成「被改过」，结果等于没用。
void DocumentComparerTests::insertedPageKeepsLaterPagesAligned() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Delta inserted appendix")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));
    QVERIFY(result.succeeded);

    // 对齐必须真的用上了。
    QVERIFY(result.alignmentApplied);
    QCOMPARE(result.alignmentMode, QStringLiteral("sequence"));
    QVERIFY(!result.alignmentNote.isEmpty());

    // 三页原页都配上了，插进来的那一页是新文独有的。
    QCOMPARE(result.alignedPairCount, 3);
    QCOMPARE(result.comparedPageCount, 3);
    QCOMPARE(result.newOnlyPageCount, 1);
    QCOMPARE(result.newOnlyPageIndices.size(), 1);
    QCOMPARE(result.newOnlyPageIndices.at(0), 1);
    QCOMPARE(result.oldOnlyPageCount, 0);
    QVERIFY(result.oldOnlyPageIndices.isEmpty());

    // 页数口径按对齐结果算：多一页就是多一页。
    QCOMPARE(result.addedPageCount, 1);
    QCOMPARE(result.removedPageCount, 0);

    // 插页之后的页一个字都没动：不能报成「被改过」。
    QCOMPARE(result.modifiedBlockCount, 0);
    QCOMPARE(result.insertedBlockCount, 0);
    QCOMPARE(result.deletedBlockCount, 0);
    QCOMPARE(result.differences.size(), 1);
    QCOMPARE(result.differences.at(0).kind, DiffKind::PageAdded);
    QCOMPARE(result.differences.at(0).newPageIndex, 1);
    QVERIFY2(result.differences.at(0).newText.contains(QStringLiteral("Delta inserted appendix")),
             qPrintable(result.differences.at(0).newText));
}

// 中间删一页：后面的页同样要配对正确。
void DocumentComparerTests::deletedPageKeepsLaterPagesAligned() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Gamma chapter three")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    QVERIFY(result.alignmentApplied);
    QCOMPARE(result.alignmentMode, QStringLiteral("sequence"));
    QCOMPARE(result.alignedPairCount, 2);
    QCOMPARE(result.comparedPageCount, 2);

    // 被删掉的那一页是原文独有的。
    QCOMPARE(result.oldOnlyPageCount, 1);
    QCOMPARE(result.oldOnlyPageIndices.size(), 1);
    QCOMPARE(result.oldOnlyPageIndices.at(0), 1);
    QCOMPARE(result.newOnlyPageCount, 0);
    QVERIFY(result.newOnlyPageIndices.isEmpty());

    QCOMPARE(result.removedPageCount, 1);
    QCOMPARE(result.addedPageCount, 0);
    QCOMPARE(result.differences.size(), 1);
    QCOMPARE(result.differences.at(0).kind, DiffKind::PageRemoved);
    QCOMPARE(result.differences.at(0).oldPageIndex, 1);
}

// 页移动：把第 2 页挪到最后，这一页仍然是「同一页」。
//
// 这条只能靠「不看顺序的指纹配对」兜住：保序的动态规划（LCS/NW）
// 在页被挪动时最多配上一半，挪走的那页会被说成「删了一页又加了一页」。
void DocumentComparerTests::movedPageIsMatchedByFingerprint() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Gamma chapter three")},
                                          {QStringLiteral("Beta chapter two")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    QVERIFY(result.alignmentApplied);
    QCOMPARE(result.alignmentMode, QStringLiteral("sequence"));
    QCOMPARE(result.alignedPairCount, 3);
    QCOMPARE(result.comparedPageCount, 3);

    // 页只是换了位置：没有页被加，也没有页被删。
    QCOMPARE(result.addedPageCount, 0);
    QCOMPARE(result.removedPageCount, 0);
    QVERIFY(result.differences.isEmpty());
}

// 对照样本：两份完全不同的文档不能硬凑成「互相改过」。
//
// 对齐不比按位置配对更好时必须退回按位置，并在 alignmentNote 里说明。
void DocumentComparerTests::completelyDifferentDocumentsFallBackToPositionPairing() {
    const QByteArray oldBytes =
        buildPdf({{QStringLiteral("Supplier invoice total 4800 euro")},
                  {QStringLiteral("Delivery note warehouse receipt")}});
    const QByteArray newBytes =
        buildPdf({{QStringLiteral("Landlord lease agreement rent")},
                  {QStringLiteral("Tenant notice renewal premises")}});

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, DocumentCompareOptions(), &result,
                                          &error),
             qPrintable(error));

    // 没有硬凑：退回按位置配对，并写明为什么。
    QVERIFY(!result.alignmentApplied);
    QCOMPARE(result.alignmentMode, QStringLiteral("position"));
    QVERIFY2(!result.alignmentNote.isEmpty(), "退回按位置配对必须写明原因");
    QCOMPARE(result.alignedPairCount, 0);

    // 两份文档的词完全不相干：每一页都算「一边独有」。
    QCOMPARE(result.addedPageCount, 2);
    QCOMPARE(result.removedPageCount, 2);
    QCOMPARE(result.oldOnlyPageCount, 2);
    QCOMPARE(result.newOnlyPageCount, 2);
}

// 页数超过上限：退回按位置配对，并写明是哪个上限挡住了。
void DocumentComparerTests::alignmentFallsBackWhenPageLimitExceeded() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Delta inserted appendix")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});

    DocumentCompareOptions options;
    options.maxAlignmentPages = 2;  // 故意把上限压到比页数小

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    QVERIFY(!result.alignmentApplied);
    QCOMPARE(result.alignmentMode, QStringLiteral("position"));
    QVERIFY2(result.alignmentNote.contains(QStringLiteral("limit")),
             qPrintable(result.alignmentNote));
}

// 关掉自动对齐：就是「第 i 页对第 i 页」的旧行为。
//
// 这条同时是负例对照的基准：插页样本在关掉对齐之后，
// 插页之后的页会全部配错（多报两页新增、一页删除）。
void DocumentComparerTests::alignmentCanBeDisabledByOption() {
    const QByteArray oldBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});
    const QByteArray newBytes = buildPdf({{QStringLiteral("Alpha chapter one")},
                                          {QStringLiteral("Delta inserted appendix")},
                                          {QStringLiteral("Beta chapter two")},
                                          {QStringLiteral("Gamma chapter three")}});

    DocumentCompareOptions options;
    options.autoAlign = false;

    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    QVERIFY(!result.alignmentApplied);
    QCOMPARE(result.alignmentMode, QStringLiteral("position"));
    QVERIFY2(result.alignmentNote.contains(QStringLiteral("disabled")),
             qPrintable(result.alignmentNote));

    // 旧行为的具体后果：按位置配对之后插页之后的页全错位。
    QCOMPARE(result.comparedPageCount, 1);
    QCOMPARE(result.addedPageCount, 3);
    QCOMPARE(result.removedPageCount, 2);
}

QTEST_MAIN(DocumentComparerTests)
#include "DocumentComparerTests.moc"
