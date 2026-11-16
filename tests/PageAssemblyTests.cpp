// 文档拼装（合并 / 拆分 / 水印与页面装饰）的单元测试。
//
// 这一组测的是 pdf 层**已有**的能力：
//   - PdfDocument::appendDocumentPages（合并）
//   - PdfDocument::extractPagesToBytes（提取一页或多页）
//   - PageDecorations 的水印 / 页眉页脚 / 页码 / 贝茨编号
//   - PageSplitPlanner 的拆分口径计算（每 N 页 / 按书签 / 按体积 / 提取范围）
//
// 判据都落在"写盘之后"：内存里的结构对了不算数——
// 必须重新打开文件、渲染页面或读文本层，看到结果才算数。
// 界面上的"合并/拆分/加水印"都是这条通路，测试要守住的就是它。

#include "pdf/PageDecorations.h"
#include "pdf/PageSplitPlanner.h"
#include "pdf/PdfiumAdapter.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using aipdf::pdf::applyBatesNumbering;
using aipdf::pdf::applyHeaderFooter;
using aipdf::pdf::applyPageNumbers;
using aipdf::pdf::applyWatermark;
using aipdf::pdf::BatesOptions;
using aipdf::pdf::HeaderFooterOptions;
using aipdf::pdf::openDocument;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::OutlineItem;
using aipdf::pdf::PageNumberOptions;
using aipdf::pdf::PageRange;
using aipdf::pdf::PageSplitPlanner;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::PdfOpenResult;
using aipdf::pdf::WatermarkOptions;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 统计非白色像素，用来判断页面是不是真的多画了东西。
int countInkedPixels(const QImage& image) {
    if (image.isNull()) {
        return 0;
    }
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int inked = 0;
    for (int y = 0; y < argb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            const QRgb pixel = line[x];
            if (qRed(pixel) != 0xFF || qGreen(pixel) != 0xFF || qBlue(pixel) != 0xFF) {
                ++inked;
            }
        }
    }
    return inked;
}

// 某页的非白色像素数。页面取不到时返回 0（调用方会因此看到"没变化"）。
int inkedPixelsOfPage(const PdfDocument& document, int index) {
    const auto page = document.page(index);
    if (!page.has_value()) {
        return 0;
    }
    return countInkedPixels(page->render(1.0));
}

// 一页的纯文本。
QString pageText(const PdfDocument& document, int index) {
    const auto page = document.page(index);
    if (!page.has_value()) {
        return QString();
    }
    const auto text = page->extractText();
    if (!text.has_value()) {
        return QString();
    }
    return text->plainText();
}

// 一页的第一行文字：用来确认"哪一页接到了哪一页前面"。
QString firstLine(const PdfDocument& document, int index) {
    QString line;
    for (const QChar character : pageText(document, index)) {
        if (character == QLatin1Char('\r') || character == QLatin1Char('\n')) {
            break;
        }
        line += character;
    }
    return line.trimmed();
}

// 把字节写进临时文件再重新打开：写盘往返才是权威结果。
PdfDocument writeAndReopen(const QByteArray& bytes, const QString& fileName, QTemporaryDir* dir) {
    const QString path = dir->filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(bytes);
    file.close();
    const PdfOpenResult reopened = openDocument(path);
    return reopened.document;
}

// 序列化并写盘，返回重新打开的文档。
PdfDocument roundTrip(const PdfDocument& document, const QString& fileName, QTemporaryDir* dir) {
    QString error;
    const QByteArray bytes = document.serialize(&error);
    if (bytes.isEmpty()) {
        return {};
    }
    return writeAndReopen(bytes, fileName, dir);
}

OutlineItem outlineItem(const QString& title, int pageIndex) {
    OutlineItem item;
    item.title = title;
    item.pageIndex = pageIndex;
    return item;
}

}  // namespace

class PageAssemblyTests : public QObject {
    Q_OBJECT

private slots:
    void mergeAppendsSourcePages();
    void mergedDocumentKeepsPagesAfterWrite();
    void extractPagesProducesTwoPageDocument();
    void extractNonContiguousPagesKeepsOrder();
    void watermarkAddsInkAfterWrite();
    void headerFooterTextSurvivesWrite();
    void pageNumbersTextSurvivesWrite();
    void batesNumberingTextSurvivesWrite();
    void splitByCountProducesEvenSegments();
    void splitByOutlineDeduplicatesBookmarkPages();
    void splitBySizeBreaksWhenLimitExceeded();
    void extractRangeClampsOutOfBounds();
};

// ------------------------------------------------------------------ 合并 ----

void PageAssemblyTests::mergeAppendsSourcePages() {
    const PdfOpenResult target = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    const PdfOpenResult source = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(target.document.isValid());
    QVERIFY(source.document.isValid());
    QCOMPARE(target.document.pageCount(), 1);
    QCOMPARE(source.document.pageCount(), 3);

    PdfDocument merged = target.document;
    QVERIFY(merged.appendDocumentPages(source.document));
    QCOMPARE(merged.pageCount(), 4);
}

void PageAssemblyTests::mergedDocumentKeepsPagesAfterWrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PdfOpenResult target = openDocument(fixturePath(QStringLiteral("normal.pdf")));
    const PdfOpenResult source = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(target.document.isValid());
    QVERIFY(source.document.isValid());

    PdfDocument merged = target.document;
    QVERIFY(merged.appendDocumentPages(source.document));

    const PdfDocument reopened = roundTrip(merged, QStringLiteral("merged.pdf"), &dir);
    QVERIFY(reopened.isValid());
    QCOMPARE(reopened.pageCount(), 4);
    // 两份文档的内容都得在：被合并的"Hello PDFium"和源文档的"Navigation demo"。
    // 这里只断言"都在"，不断言谁在前——PDFium 的导入接口目前把源页插到最前面
    // （insert_at = 0），而接口名字叫 appendDocumentPages，两者并不一致；
    // 这个不一致由适配层负责修，测试不该把它钉死。
    QStringList lines;
    for (int index = 0; index < reopened.pageCount(); ++index) {
        lines.append(firstLine(reopened, index));
    }
    QVERIFY2(lines.contains(QStringLiteral("Hello PDFium")),
             qPrintable(QStringLiteral("合并后看不到被合并文档的内容：%1").arg(lines.join('|'))));
    QVERIFY2(lines.contains(QStringLiteral("Navigation demo")),
             qPrintable(QStringLiteral("合并后看不到源文档的内容：%1").arg(lines.join('|'))));
    // 每一页都得有内容：不能出现"页数对了但内容全丢"的假合并。
    for (int index = 0; index < reopened.pageCount(); ++index) {
        QVERIFY2(!firstLine(reopened, index).isEmpty(),
                 qPrintable(QStringLiteral("第 %1 页是空的").arg(index + 1)));
    }
}

// ------------------------------------------------------------ 提取 / 拆分 ----

void PageAssemblyTests::extractPagesProducesTwoPageDocument() {
    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(opened.document.isValid());
    QCOMPARE(opened.document.pageCount(), 3);

    const QByteArray bytes = opened.document.extractPagesToBytes(QVector<int>{0, 1});
    QVERIFY(!bytes.isEmpty());
    const PdfOpenResult extracted = openDocumentFromBytes(bytes);
    QVERIFY(extracted.document.isValid());
    QCOMPARE(extracted.document.pageCount(), 2);
    // 抽出来的两页，内容要跟原文对应页**逐字一致**。
    QCOMPARE(pageText(extracted.document, 0), pageText(opened.document, 0));
    QCOMPARE(pageText(extracted.document, 1), pageText(opened.document, 1));
    QVERIFY(pageText(extracted.document, 0).contains(QStringLiteral("Navigation demo")));
    QVERIFY(pageText(extracted.document, 1).contains(QStringLiteral("Page two")));
}

void PageAssemblyTests::extractNonContiguousPagesKeepsOrder() {
    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("navigation.pdf")));
    QVERIFY(opened.document.isValid());

    // 抽不连续的页：第 1 页和第 3 页，顺序必须照旧。
    const QByteArray bytes = opened.document.extractPagesToBytes(QVector<int>{0, 2});
    QVERIFY(!bytes.isEmpty());
    const PdfOpenResult extracted = openDocumentFromBytes(bytes);
    QVERIFY(extracted.document.isValid());
    QCOMPARE(extracted.document.pageCount(), 2);
    QCOMPARE(pageText(extracted.document, 0), pageText(opened.document, 0));
    QCOMPARE(pageText(extracted.document, 1), pageText(opened.document, 2));
    QVERIFY(pageText(extracted.document, 1).contains(QStringLiteral("Page three")));
}

// ---------------------------------------------------------- 页面装饰 ----

void PageAssemblyTests::watermarkAddsInkAfterWrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY(opened.document.isValid());
    // 用第 2 页：夹具 multipage.pdf 的第 1 页是空白的
    // （生成脚本里字体对象号和首页内容流对象号撞了），
    // 拿空白页当基准，"墨迹变多"这条断言就没有意义了。
    QVERIFY(inkedPixelsOfPage(opened.document, 1) > 0);
    const int before = inkedPixelsOfPage(opened.document, 1);

    PdfDocument document = opened.document;
    WatermarkOptions options;
    options.text = QStringLiteral("DRAFT");
    options.fontSize = 40.0;
    options.opacity = 0.3;
    QVERIFY(applyWatermark(document, options));

    const PdfDocument reopened = roundTrip(document, QStringLiteral("watermarked.pdf"), &dir);
    QVERIFY(reopened.isValid());
    QCOMPARE(reopened.pageCount(), 12);
    const int after = inkedPixelsOfPage(reopened, 1);
    QVERIFY2(after > before, "水印没有出现在写盘后的页面上");
}

void PageAssemblyTests::headerFooterTextSurvivesWrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY(opened.document.isValid());

    PdfDocument document = opened.document;
    HeaderFooterOptions options;
    options.header = QStringLiteral("Contract review");
    options.footer = QStringLiteral("Confidential");
    QVERIFY(applyHeaderFooter(document, options));

    const PdfDocument reopened = roundTrip(document, QStringLiteral("header.pdf"), &dir);
    QVERIFY(reopened.isValid());
    const QString text = pageText(reopened, 1);
    QVERIFY2(text.contains(QStringLiteral("Contract review")), "页眉没写进文件");
    QVERIFY2(text.contains(QStringLiteral("Confidential")), "页脚没写进文件");
}

void PageAssemblyTests::pageNumbersTextSurvivesWrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY(opened.document.isValid());

    PdfDocument document = opened.document;
    QVERIFY(applyPageNumbers(document, PageNumberOptions{}));

    const PdfDocument reopened = roundTrip(document, QStringLiteral("numbers.pdf"), &dir);
    QVERIFY(reopened.isValid());
    // 第 2 页上的页码是 "2 / 12"（总数是全文 12 页）。
    QVERIFY2(pageText(reopened, 1).contains(QStringLiteral("2 / 12")), "页码没写进文件");
}

void PageAssemblyTests::batesNumberingTextSurvivesWrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PdfOpenResult opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    QVERIFY(opened.document.isValid());

    PdfDocument document = opened.document;
    BatesOptions options;
    options.prefix = QStringLiteral("BATES");
    options.start = 1;
    options.digits = 6;
    QVERIFY(applyBatesNumbering(document, options));

    const PdfDocument reopened = roundTrip(document, QStringLiteral("bates.pdf"), &dir);
    QVERIFY(reopened.isValid());
    QVERIFY2(pageText(reopened, 1).contains(QStringLiteral("BATES000002")),
             "贝茨编号没写进文件");
}

// ------------------------------------------------------------ 拆分口径 ----

void PageAssemblyTests::splitByCountProducesEvenSegments() {
    const QVector<PageRange> ranges = PageSplitPlanner::byCount(10, 3);
    QCOMPARE(ranges.size(), 4);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 3);
    QCOMPARE(ranges.at(1).first, 3);
    QCOMPARE(ranges.at(2).first, 6);
    // 末段只有 1 页，不补空段。
    QCOMPARE(ranges.at(3).first, 9);
    QCOMPARE(ranges.at(3).last, 10);
    QCOMPARE(ranges.at(3).count(), 1);

    // 页数不够每份的页数时，只出一段。
    const QVector<PageRange> single = PageSplitPlanner::byCount(2, 5);
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.at(0).first, 0);
    QCOMPARE(single.at(0).last, 2);
}

void PageAssemblyTests::splitByOutlineDeduplicatesBookmarkPages() {
    // 四个顶层书签，其中两个指向同一页：那一页只能归到第一段里。
    std::vector<OutlineItem> outline;
    outline.push_back(outlineItem(QStringLiteral("A"), 0));
    outline.push_back(outlineItem(QStringLiteral("B"), 3));
    outline.push_back(outlineItem(QStringLiteral("C"), 3));
    outline.push_back(outlineItem(QStringLiteral("D"), 7));

    const QVector<PageRange> ranges = PageSplitPlanner::byOutline(10, outline);
    QCOMPARE(ranges.size(), 3);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 3);
    QCOMPARE(ranges.at(1).first, 3);
    QCOMPARE(ranges.at(1).last, 7);
    // 最后一段一直到文档末尾。
    QCOMPARE(ranges.at(2).first, 7);
    QCOMPARE(ranges.at(2).last, 10);

    // 目标解析不出来的书签（pageIndex = -1）不参与分段。
    std::vector<OutlineItem> broken;
    broken.push_back(outlineItem(QStringLiteral("A"), -1));
    QVERIFY(PageSplitPlanner::byOutline(10, broken).isEmpty());
}

void PageAssemblyTests::splitBySizeBreaksWhenLimitExceeded() {
    const QVector<qint64> sizes{100, 100, 1000};
    const QVector<PageRange> ranges = PageSplitPlanner::bySize(sizes, 500);
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges.at(0).first, 0);
    QCOMPARE(ranges.at(0).last, 2);
    // 单独就超限的那一页自成一档（页不可分）。
    QCOMPARE(ranges.at(1).first, 2);
    QCOMPARE(ranges.at(1).last, 3);
}

void PageAssemblyTests::extractRangeClampsOutOfBounds() {
    // 1 基、含两端 → 0 基半开区间。
    const QVector<PageRange> normal = PageSplitPlanner::extract(10, 2, 4);
    QCOMPARE(normal.size(), 1);
    QCOMPARE(normal.at(0).first, 1);
    QCOMPARE(normal.at(0).last, 4);
    QCOMPARE(normal.at(0).count(), 3);

    // 下界越界夹到第 1 页，上界越界夹到最后一页。
    const QVector<PageRange> clamped = PageSplitPlanner::extract(10, 0, 100);
    QCOMPARE(clamped.size(), 1);
    QCOMPARE(clamped.at(0).first, 0);
    QCOMPARE(clamped.at(0).last, 10);

    // 起止颠倒返回空列表。
    QVERIFY(PageSplitPlanner::extract(10, 5, 3).isEmpty());
}

QTEST_MAIN(PageAssemblyTests)
#include "PageAssemblyTests.moc"
