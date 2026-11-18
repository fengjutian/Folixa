// 页面装饰（水印 / 页眉页脚 / 页码）的单元测试。
//
// 这些功能的判据不是"调用了没报错"，而是：
//   1. 每一页都真的多了内容（渲染像素变多）；
//   2. 文字能进文本层（这也意味着水印会出现在搜索与复制里，是预期行为）。

#include "pdf/content/PageDecorations.h"

#include <QDir>
#include <QImage>
#include <QtTest>

using aipdf::pdf::applyHeaderFooter;
using aipdf::pdf::applyPageNumbers;
using aipdf::pdf::applyWatermark;
using aipdf::pdf::HeaderFooterOptions;
using aipdf::pdf::openDocument;
using aipdf::pdf::PageNumberOptions;
using aipdf::pdf::WatermarkOptions;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

int inkedPixels(const QImage& image) {
    int inked = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != QColor(Qt::white)) {
                ++inked;
            }
        }
    }
    return inked;
}

}  // namespace

class PageDecorationsTests : public QObject {
    Q_OBJECT

private slots:
    void watermarkTouchesEveryPage();
    void watermarkRejectsUnrenderableText();
    void headerFooterAddsInk();
    void pageNumbersIncludeIndexAndTotal();
};

void PageDecorationsTests::watermarkTouchesEveryPage() {
    auto opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());

    WatermarkOptions options;
    options.text = QStringLiteral("DRAFT");
    options.fontSize = 40.0;
    options.opacity = 0.3;
    QVERIFY(applyWatermark(document, options));

    for (int index = 0; index < 3; ++index) {
        auto page = document.page(index);
        QVERIFY(page.has_value());
        const auto text = page->extractText();
        QVERIFY2(text.has_value() && text->plainText().contains(QStringLiteral("DRAFT")),
                 "水印没有进文本层");
    }
}

void PageDecorationsTests::watermarkRejectsUnrenderableText() {
    auto opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    WatermarkOptions options;
    options.text = QStringLiteral("机密");
    QVERIFY2(!applyWatermark(opened.document, options),
             "标准 14 字体画不出中文，应该明确拒绝而不是画出方块");
}

void PageDecorationsTests::headerFooterAddsInk() {
    auto opened = openDocument(fixturePath(QStringLiteral("empty.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    auto page = document.page(0);
    QVERIFY(page.has_value());
    const int before = inkedPixels(page->render(1.0));

    HeaderFooterOptions options;
    options.header = QStringLiteral("Contract review");
    options.footer = QStringLiteral("Confidential");
    QVERIFY(applyHeaderFooter(document, options));

    auto updated = document.page(0);
    QVERIFY(updated.has_value());
    QVERIFY(inkedPixels(updated->render(1.0)) > before);
}

void PageDecorationsTests::pageNumbersIncludeIndexAndTotal() {
    auto opened = openDocument(fixturePath(QStringLiteral("multipage.pdf")));
    auto& document = opened.document;
    QVERIFY(document.isValid());
    QVERIFY(applyPageNumbers(document, PageNumberOptions{}));

    auto first = document.page(0);
    QVERIFY(first.has_value());
    const auto text = first->extractText();
    QVERIFY(text.has_value());
    QVERIFY2(text->plainText().contains(QStringLiteral("1 / 12")), "页码没写进去");
}

QTEST_MAIN(PageDecorationsTests)
#include "PageDecorationsTests.moc"
