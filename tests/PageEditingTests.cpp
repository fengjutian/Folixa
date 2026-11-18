// 页面级编辑的专项单测：替换页、裁剪、贝茨编号、页码格式、表格检测。
//
// 为什么单独开一个文件：这五条此前只有界面路径，没有断言。
// "代码存在"和"结果正确"是两回事——
// 裁剪写进去的盒子有没有真的生效、贝茨编号是不是逐页递增、
// 表格检测出来的格子对不对，都得对着产物验，不能靠"函数返回 true"就算过。
//
// 全部用真 PDFium：页面几何与文本层都是 PDFium 文档的一部分，模拟不出来。

#include "app/controllers/AnnotationController.h"
#include "app/editor/PdfPageOperations.h"
#include "pdf/forms/FormFieldWriter.h"
#include "pdf/forms/FormScriptStore.h"
#include "pdf/content/PageDecorations.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QColor>
#include <QRectF>
#include <memory>
#include <QTest>

using aipdf::app::AnnotationController;
using aipdf::app::PdfPageOperations;
using aipdf::pdf::BatesOptions;
using aipdf::pdf::PageNumberOptions;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::openDocument;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

// 整页文本拼起来：装饰类功能都要往文本层里写字，验的是"写进去了没有"。
QString pageText(const PdfDocument& document, int index) {
    const auto page = document.page(index);
    if (!page.has_value()) {
        return QString();
    }
    const auto text = page->extractText();
    if (!text.has_value()) {
        return QString();
    }
    QString joined;
    for (const auto& character : text->chars) {
        joined += character.text;
    }
    return joined;
}

// 在页面上画一个 2 行 3 列的文字网格，用来喂表格检测。
//
// 坐标是 PDF 用户空间（左下原点）。列间距 100、行间距 40，
// 足够让检测器把它们分成两行三列。
bool drawGrid(PdfDocument& document, int pageIndex) {
    auto page = document.page(pageIndex);
    if (!page.has_value()) {
        return false;
    }
    static const char* const kCells[2][3] = {{"A1", "B1", "C1"}, {"A2", "B2", "C2"}};
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (!page->addTextLayer(QString::fromLatin1(kCells[row][column]),
                                    50.0 + column * 100.0, 500.0 - row * 40.0, 12.0,
                                    QColor(0, 0, 0))) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

class PageEditingTests : public QObject {
    Q_OBJECT

private slots:
    void cropBoxNarrowsContentBox();
    void batesNumbersIncreasePerPage();
    void pageNumberUsesCustomFormat();
    void replacePageSwapsContent();
    void detectTableFindsGrid();
    void tableCellTextCanBeRewritten();
    void formScriptsSurvivePdfiumRoundTrip();
};

// 裁剪：写进去的 CropBox 必须真的变成页面的有效盒子。
void PageEditingTests::cropBoxNarrowsContentBox() {
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());

    auto page = result.document.page(0);
    QVERIFY(page.has_value());
    const auto original = page->contentBox();
    auto cropped = original;
    cropped.left += 20.0;
    cropped.bottom += 30.0;
    cropped.right -= 20.0;
    cropped.top -= 30.0;
    QVERIFY(page->setCropBox(cropped));

    // 重新取一次页面句柄：写进去的盒子是页面自己的状态。
    const auto after = result.document.page(0);
    QVERIFY(after.has_value());
    const auto now = after->contentBox();
    QVERIFY(qAbs(now.left - (original.left + 20.0)) < 0.01);
    QVERIFY(qAbs(now.bottom - (original.bottom + 30.0)) < 0.01);
    QVERIFY(qAbs(now.right - (original.right - 20.0)) < 0.01);
    QVERIFY(qAbs(now.top - (original.top - 30.0)) < 0.01);
}

// 贝茨编号：逐页递增，页数不够就少写，不够就早退。
void PageEditingTests::batesNumbersIncreasePerPage() {
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const int count = result.document.pageCount();
    QVERIFY(count >= 2);

    BatesOptions options;
    options.prefix = QStringLiteral("BATES");
    options.start = 1;
    options.digits = 4;
    QVERIFY(aipdf::pdf::applyBatesNumbering(result.document, options));

    // 第一页是 0001，第二页是 0002——不是每页都写同一个。
    QVERIFY(pageText(result.document, 0).contains(QStringLiteral("BATES0001")));
    QVERIFY(pageText(result.document, 1).contains(QStringLiteral("BATES0002")));
    QVERIFY(!pageText(result.document, 0).contains(QStringLiteral("BATES0002")));
}

// 页码格式：界面给了自定义格式，引擎必须按它拼，而不是写死 "%1 / %2"。
void PageEditingTests::pageNumberUsesCustomFormat() {
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    const int count = result.document.pageCount();

    PageNumberOptions options;
    options.format = QStringLiteral("- %1 of %2 -");
    QVERIFY(aipdf::pdf::applyPageNumbers(result.document, options));

    QVERIFY(pageText(result.document, 0)
                .contains(QStringLiteral("- 1 of %1 -").arg(count)));
    QVERIFY(!pageText(result.document, 0).contains(QStringLiteral("%1")));
}

// 替换页：页数不变，内容换成源文件那一页的。
void PageEditingTests::replacePageSwapsContent() {
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    auto source = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(source.document.isValid());
    QVERIFY(source.document.pageCount() >= 2);

    const QString before = pageText(result.document, 0);
    QVERIFY(!before.isEmpty());

    PdfPageOperations operations(source.document);
    std::shared_ptr<aipdf::doc::PageBackup> replacement;
    QVERIFY(operations.backupPage(1, &replacement));
    QVERIFY(replacement != nullptr);

    AnnotationController controller(result.document);
    const int count = result.document.pageCount();
    QVERIFY(controller.replacePageWithBackup(0, replacement));

    // 页数不变（替换不是插入）。
    QCOMPARE(result.document.pageCount(), count);
    // 内容换成了源文件的第 2 页。
    QVERIFY(pageText(result.document, 0).contains(QStringLiteral("Page two")));
}

// 表格检测：画好的 2×3 网格要被认成 2 行 3 列，格子文字要对得上。
void PageEditingTests::detectTableFindsGrid() {
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    QVERIFY(drawGrid(result.document, 0));

    auto page = result.document.page(0);
    QVERIFY(page.has_value());
    const auto table = page->detectTableInRect(QRectF(40.0, 440.0, 300.0, 90.0));
    QVERIFY2(table.isValid(), "画好的网格没有被检测成表格");
    QCOMPARE(table.rowCount, 2);
    QCOMPARE(table.columnCount, 3);

    const auto* first = table.cell(0, 0);
    QVERIFY(first != nullptr);
    // 文本对象带尾随空格是内容流里的常态，比较时按去空白后的值。
    QCOMPARE(first->text.trimmed(), QStringLiteral("A1"));
    const auto* last = table.cell(1, 2);
    QVERIFY(last != nullptr);
    QCOMPARE(last->text.trimmed(), QStringLiteral("C2"));
}

// 表格单元格改写：改完要能在文本层里看到新文字。
void PageEditingTests::tableCellTextCanBeRewritten() {
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    QVERIFY(drawGrid(result.document, 0));

    auto page = result.document.page(0);
    QVERIFY(page.has_value());
    const auto table = page->detectTableInRect(QRectF(40.0, 440.0, 300.0, 90.0));
    QVERIFY(table.isValid());

    QVERIFY(page->setTableCellText(table, 0, 1, "Z9"));

    QVERIFY(pageText(result.document, 0).contains(QStringLiteral("Z9")));
    // 原来的文字被换掉了，不是多写了一份。
    QVERIFY(!pageText(result.document, 0).contains(QStringLiteral("B1")));
}

// 表单脚本必须能活过 PDFium 的重新序列化。
//
// 为什么单独测这一条：计算脚本住在字段字典的 /AA 里，计算次序住在
// AcroForm 的 /CO 里。这两处都不是"值"，PDFium 保存时完全可能把它们丢掉——
// 一旦丢了，界面上"计算次序"看着能跑，实际每次都是空跑。
void PageEditingTests::formScriptsSurvivePdfiumRoundTrip() {
#ifndef AIPDF_WITH_QPDF
    QSKIP("本次构建未启用 qpdf（写脚本走 qpdf 的对象级接口）");
#else
    auto result = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(result.document.isValid());
    QString error;
    const QByteArray base = result.document.serialize(&error);
    QVERIFY2(!base.isEmpty(), qPrintable(error));

    aipdf::pdf::FormFieldInsert insert;
    insert.pageIndex = 0;
    insert.name = QStringLiteral("total");
    insert.toolTip = QStringLiteral("合计");
    insert.rect = QRectF(50.0, 700.0, 160.0, 24.0);
    insert.kind = aipdf::pdf::FormFieldKind::Text;
    const aipdf::pdf::FormFieldWriteResult written = aipdf::pdf::appendFormField(base, insert);
    QVERIFY2(written.ok, qPrintable(written.error));

    aipdf::pdf::FormFieldScript script;
    script.fieldName = QStringLiteral("total");
    script.calculate = QStringLiteral("event.value * 2");
    const aipdf::pdf::FormScriptWriteResult scripted =
        aipdf::pdf::writeFormScripts(written.bytes, {script}, {QStringLiteral("total")});
    QVERIFY2(scripted.ok, qPrintable(scripted.error));

    // 走一遍 PDFium：重新打开、重新保存，再读一遍脚本。
    auto reopened = aipdf::pdf::openDocumentFromBytes(scripted.bytes, QString());
    QVERIFY(reopened.document.isValid());
    QString serializeError;
    const QByteArray resaved = reopened.document.serialize(&serializeError);
    QVERIFY2(!resaved.isEmpty(), qPrintable(serializeError));

    const QVector<aipdf::pdf::FormFieldScript> scripts =
        aipdf::pdf::readFormFieldScripts(resaved);
    QCOMPARE(scripts.size(), 1);
    QCOMPARE(scripts.first().fieldName, QStringLiteral("total"));
    QCOMPARE(scripts.first().calculate, QStringLiteral("event.value * 2"));
    // 计算次序也要活下来：AcroForm 的 /CO 是数组，不是值。
    const QStringList order = aipdf::pdf::readCalculationOrder(resaved);
    QCOMPARE(order.size(), 1);
    QCOMPARE(order.first(), QStringLiteral("total"));
#endif
}

QTEST_MAIN(PageEditingTests)
#include "PageEditingTests.moc"
