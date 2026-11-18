// 密文（保护 > 密文）四种处理方式的单元测试。
//
// 为什么单独开一个文件：这四件事的差别只有一样——它们对**文字层**和**渲染层**
// 做了什么不一样。「函数返回 true」证明不了任何东西：
//   标记之后原文还得能搜到、复制到；
//   马赛克/擦除/应用之后原文必须搜不到；
//   而擦除与应用之间又必须分得开——擦除只删那一块，页面别处仍是矢量文字；
//   应用是整页位图化，整页的文字层都没了。
// 所以每条用例都对着**产物**断言：重新读出来的文字层 + 重新渲染出来的像素。
//
// 夹具：scripts/generate-test-pdfs.py 生成的 text.pdf。
// 遮盖区域不写死坐标——先 extractText() 拿到真实字符盒，再挑一行有字的矩形。

#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/security/Redaction.h"

#include <QDir>
#include <QImage>
#include <QRectF>
#include <QTest>

#include <algorithm>

using aipdf::pdf::MediaBox;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::RedactionMark;
using aipdf::pdf::TextChar;
using aipdf::pdf::TextPage;

namespace {

// 像素断言用的渲染分辨率：每点 2 像素，字形才有足够像素可数。
constexpr double kRenderScale = 2.0;

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 文本层里的一行：文字 + 它在 PDF user space（左下原点）里的盒子。
struct TextLine {
    QRectF box;
    QString text;
};

// 把字符按行归并：一行 = 一串基线高度几乎相同的字符。
//
// 坐标一律取自 extractText()，不写死任何数值：
// 夹具改了行数或字号，这里照样成立。
QVector<TextLine> collectLines(const TextPage& text) {
    QVector<TextChar> chars = text.chars;
    std::sort(chars.begin(), chars.end(), [](const TextChar& left, const TextChar& right) {
        if (left.box.top() != right.box.top()) {
            return left.box.top() > right.box.top();
        }
        return left.box.left() < right.box.left();
    });

    QVector<TextLine> lines;
    double lineTop = 0.0;
    for (const TextChar& character : chars) {
        if (character.box.width() <= 0.0 && character.box.height() <= 0.0) {
            continue;
        }
        if (lines.isEmpty() || qAbs(character.box.top() - lineTop) > 2.0) {
            TextLine line;
            line.box = character.box;
            line.text = character.text;
            lines.append(line);
            lineTop = character.box.top();
            continue;
        }
        lines.last().box = lines.last().box.united(character.box);
        lines.last().text += character.text;
    }
    return lines;
}

// 挑一行来遮：字符最多的那一行。
//
// 为什么不是第一行：夹具的其它行在它上下，遮掉中间这行才能验证
// 「页面其余部分的文字还在」——第一行没有「上面」可留。
TextLine pickLongestLine(const TextPage& text) {
    TextLine best;
    for (const TextLine& line : collectLines(text)) {
        if (line.text.size() > best.text.size()) {
            best = line;
        }
    }
    return best;
}

// 文字层里落在区域内的字符：区域里的字还在不在，全看这个。
QVector<TextChar> charsInRect(const TextPage& text, const QRectF& rect) {
    QVector<TextChar> result;
    for (const TextChar& character : text.chars) {
        if (character.box.isNull()) {
            continue;
        }
        if (character.box.intersects(rect)) {
            result.append(character);
        }
    }
    return result;
}

// PDF user space 的矩形 → 渲染图上的像素矩形（渲染图原点在左上，y 要翻过来）。
QRect pixelRectFor(const MediaBox& box, const QRectF& rect, double scale, const QSize& imageSize) {
    const int x = static_cast<int>((rect.left() - box.left) * scale);
    const int y = static_cast<int>((box.top - rect.bottom()) * scale);
    const int width = static_cast<int>(rect.width() * scale);
    const int height = static_cast<int>(rect.height() * scale);
    return QRect(x, y, width, height).intersected(QRect(QPoint(0, 0), imageSize));
}

// 非白像素：页面上有没有内容。
int countInkedPixels(const QImage& image, const QRect& rect) {
    int inked = 0;
    const QRect clipped = rect.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QRgb pixel = line[x];
            if (qRed(pixel) != 0xFF || qGreen(pixel) != 0xFF || qBlue(pixel) != 0xFF) {
                ++inked;
            }
        }
    }
    return inked;
}

// 近黑像素：标记刷黑之后该区域应该几乎全是黑的。
int countNearBlackPixels(const QImage& image, const QRect& rect) {
    int dark = 0;
    const QRect clipped = rect.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QRgb pixel = line[x];
            if (qRed(pixel) < 60 && qGreen(pixel) < 60 && qBlue(pixel) < 60) {
                ++dark;
            }
        }
    }
    return dark;
}

// 灰阶像素：既不是纯白也不是纯黑。马赛克就该长这样。
int countGrayPixels(const QImage& image, const QRect& rect) {
    int gray = 0;
    const QRect clipped = rect.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QRgb pixel = line[x];
            const bool white = qRed(pixel) > 200 && qGreen(pixel) > 200 && qBlue(pixel) > 200;
            const bool black = qRed(pixel) < 60 && qGreen(pixel) < 60 && qBlue(pixel) < 60;
            if (!white && !black) {
                ++gray;
            }
        }
    }
    return gray;
}

// 打开夹具并取出第一页的文本层。
bool openFirstPage(const QString& name, PdfDocument* document, TextPage* text) {
    const auto opened = aipdf::pdf::openDocument(fixturePath(name));
    if (!opened.document.isValid()) {
        return false;
    }
    *document = opened.document;
    const auto page = document->page(0);
    if (!page.has_value()) {
        return false;
    }
    const auto extracted = page->extractText();
    if (!extracted.has_value()) {
        return false;
    }
    *text = *extracted;
    return true;
}

}  // namespace

class RedactionTests : public QObject {
    Q_OBJECT

private slots:
    void markKeepsTextLayerAndDarkensRegion();
    void mosaicBlursRegionAndDropsText();
    void eraseDropsRegionTextAndKeepsRest();
    void applyRasterizesWholePage();
    void emptyRegionIsRejected();
    void outOfBoundsRegionIsClipped();
    void invalidDocumentIsRejected();
    void multipleMarksAreProcessed();
    void rotatedPageIsRejected();
    void eraseKeepsOtherPagesIntact();
    void markWithoutPageIndexCoversEveryPage();
};

// 标记：只在页面上盖黑块，原文一个字都不许少。
void RedactionTests::markKeepsTextLayerAndDarkensRegion() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    const TextLine line = pickLongestLine(text);
    QVERIFY(!line.text.trimmed().isEmpty());

    RedactionMark mark;
    mark.rect = line.box.adjusted(-1.0, -1.0, 1.0, 1.0);
    mark.pageIndex = 0;

    auto page = document.page(0);
    QVERIFY(page.has_value());
    const QImage before = page->render(kRenderScale);
    const QRect beforeRect =
        pixelRectFor(page->contentBox(), mark.rect, kRenderScale, before.size());

    QString error;
    QVERIFY2(aipdf::pdf::markRedactions(document, {mark}, &error), qPrintable(error));

    // 改动之后要**重新取一次页面句柄**：PdfDocument::page() 每次都 FPDF_LoadPage，
    // 拿到的页面对内容流改动是「旧视图」——不重取的话看到的是改动前的页面，
    // 断言会变成永远成立的空话。
    auto current = document.page(0);
    QVERIFY(current.has_value());

    // 标记只是视觉提示：文字层里原文还在，而且还在原来的位置。
    const auto after = current->extractText();
    QVERIFY(after.has_value());
    const QVector<TextChar> remaining = charsInRect(*after, mark.rect);
    QVERIFY2(!remaining.isEmpty(), "标记之后原文从文字层里消失了——标记不该动内容");

    // 渲染图上那块变黑了。
    const QImage marked = current->render(kRenderScale);
    const QRect markedRect =
        pixelRectFor(current->contentBox(), mark.rect, kRenderScale, marked.size());
    const int area = qMax(1, markedRect.width() * markedRect.height());
    QVERIFY2(countNearBlackPixels(marked, markedRect) > countNearBlackPixels(before, beforeRect),
             "标记区域没有变黑");
    QVERIFY2(countNearBlackPixels(marked, markedRect) * 10 > area * 8,
             "标记区域应该几乎整块是黑的");
}

// 马赛克：肉眼看不清，文字层里也读不到，但页面还是矢量的。
void RedactionTests::mosaicBlursRegionAndDropsText() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    const TextLine line = pickLongestLine(text);
    RedactionMark mark;
    mark.rect = line.box.adjusted(-1.0, -1.0, 1.0, 1.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY2(aipdf::pdf::mosaicRedactions(document, {mark}, &error), qPrintable(error));

    // 重新取页面句柄：改动前的句柄看到的是旧页面（见 markKeepsTextLayerAndDarkensRegion）。
    auto page = document.page(0);
    QVERIFY(page.has_value());
    const auto after = page->extractText();
    QVERIFY(after.has_value());
    QVERIFY2(charsInRect(*after, mark.rect).isEmpty(), "马赛克之后文字层里还能读到原文");

    // 渲染图上这块是灰块：既不是纯黑，也不是纯白。
    const QImage image = page->render(kRenderScale);
    const QRect rect = pixelRectFor(page->contentBox(), mark.rect, kRenderScale, image.size());
    const int area = qMax(1, rect.width() * rect.height());
    QVERIFY2(countGrayPixels(image, rect) > 0, "马赛克区域没有灰色块");
    QVERIFY2(countNearBlackPixels(image, rect) * 10 < area * 6,
             "马赛克区域几乎全是黑的——那不是马赛克，是刷黑");
}

// 擦除：这一块的文字读不到了，页面别处的文字还活着（没整页位图化）。
void RedactionTests::eraseDropsRegionTextAndKeepsRest() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    const TextLine line = pickLongestLine(text);
    RedactionMark mark;
    mark.rect = line.box.adjusted(-1.0, -1.0, 1.0, 1.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY2(aipdf::pdf::eraseRedactions(document, {mark}, &error), qPrintable(error));

    auto page = document.page(0);
    QVERIFY(page.has_value());
    const auto after = page->extractText();
    QVERIFY(after.has_value());
    QVERIFY2(charsInRect(*after, mark.rect).isEmpty(), "擦除之后文字层里还能读到原文");

    // 其余的行还在：擦除只删这一块。
    bool keptOtherLine = false;
    for (const TextLine& other : collectLines(*after)) {
        if (!other.text.trimmed().isEmpty() && !other.box.intersects(mark.rect)) {
            keptOtherLine = true;
            break;
        }
    }
    QVERIFY2(keptOtherLine, "擦除把整页都弄成位图了——它应该只删目标区域");
}

// 应用密文：整页位图化，页面上一行文字都不剩。
void RedactionTests::applyRasterizesWholePage() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    const TextLine line = pickLongestLine(text);
    RedactionMark mark;
    mark.rect = line.box.adjusted(-1.0, -1.0, 1.0, 1.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY2(aipdf::pdf::applyRedactions(document, {mark}, &error), qPrintable(error));

    auto page = document.page(0);
    QVERIFY(page.has_value());
    const auto after = page->extractText();
    QVERIFY2(!after.has_value() || after->chars.isEmpty(),
             "应用密文之后整页仍有文字层——它应该整页位图化");

    // 页面还是画得出东西：位图化不是把页面清空。
    const QImage image = page->render(kRenderScale);
    QVERIFY(!image.isNull());
    QVERIFY2(countInkedPixels(image, image.rect()) > 0, "位图化之后页面一片空白");
}

// 空区域：四种做法都必须明确失败，而不是悄悄什么都不做。
void RedactionTests::emptyRegionIsRejected() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    RedactionMark mark;
    mark.rect = QRectF(100.0, 100.0, 0.0, 0.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY(!aipdf::pdf::markRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::mosaicRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::eraseRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::applyRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
}

// 越界区域：裁剪到页面内继续处理，不崩溃。
void RedactionTests::outOfBoundsRegionIsClipped() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    const TextLine line = pickLongestLine(text);
    // 左右都伸出页面：裁剪之后仍然盖住那一行。
    RedactionMark mark;
    mark.rect = QRectF(line.box.left() - 500.0, line.box.top() - 2.0,
                       line.box.width() + 1000.0, line.box.height() + 4.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY2(aipdf::pdf::eraseRedactions(document, {mark}, &error), qPrintable(error));

    auto page = document.page(0);
    QVERIFY(page.has_value());
    const auto after = page->extractText();
    QVERIFY(after.has_value());
    QVERIFY2(charsInRect(*after, line.box).isEmpty(), "裁剪后的区域没有生效");
}

// 无效文档：必须返回 false 并给出原因。
void RedactionTests::invalidDocumentIsRejected() {
    PdfDocument document;
    RedactionMark mark;
    mark.rect = QRectF(10.0, 10.0, 40.0, 20.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY(!aipdf::pdf::markRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::mosaicRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::eraseRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::applyRedactions(document, {mark}, &error));
    QVERIFY(!error.isEmpty());
}

// 多处标记：一次处理完，互不影响。
void RedactionTests::multipleMarksAreProcessed() {
    PdfDocument document;
    TextPage text;
    QVERIFY2(openFirstPage(QStringLiteral("text.pdf"), &document, &text), "打不开 text.pdf 夹具");

    const QVector<TextLine> lines = collectLines(text);
    QVERIFY(lines.size() >= 3);

    QVector<RedactionMark> marks;
    for (const TextLine& line : {lines.at(0), lines.at(1)}) {
        RedactionMark mark;
        mark.rect = line.box.adjusted(-1.0, -1.0, 1.0, 1.0);
        mark.pageIndex = 0;
        marks.append(mark);
    }

    QString error;
    QVERIFY2(aipdf::pdf::eraseRedactions(document, marks, &error), qPrintable(error));

    auto page = document.page(0);
    QVERIFY(page.has_value());
    const auto after = page->extractText();
    QVERIFY(after.has_value());
    for (const RedactionMark& mark : marks) {
        QVERIFY2(charsInRect(*after, mark.rect).isEmpty(), "有一处标记没有生效");
    }
    // 没被标的那一行还在。
    bool keptOtherLine = false;
    for (const TextLine& other : collectLines(*after)) {
        if (!other.text.trimmed().isEmpty() && !other.box.intersects(marks.at(0).rect) &&
            !other.box.intersects(marks.at(1).rect)) {
            keptOtherLine = true;
            break;
        }
    }
    QVERIFY(keptOtherLine);
}

// 带 /Rotate 的页面：明确失败并说明原因，不许默默画歪。
void RedactionTests::rotatedPageIsRejected() {
    auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("rotated.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    auto page = opened.document.page(0);
    QVERIFY(page.has_value());
    QCOMPARE(page->rotationDegrees(), 90);

    RedactionMark mark;
    mark.rect = QRectF(10.0, 10.0, 60.0, 20.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY(!aipdf::pdf::markRedactions(opened.document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("旋转")), "错误信息应当说明是页面旋转导致的");
    error.clear();
    QVERIFY(!aipdf::pdf::mosaicRedactions(opened.document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::eraseRedactions(opened.document, {mark}, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!aipdf::pdf::applyRedactions(opened.document, {mark}, &error));
    QVERIFY(!error.isEmpty());
}

// 擦除只影响目标页：别的页一个字都不许少。
void RedactionTests::eraseKeepsOtherPagesIntact() {
    auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QVERIFY(opened.document.pageCount() >= 2);

    auto first = opened.document.page(0);
    QVERIFY(first.has_value());
    const auto text = first->extractText();
    QVERIFY(text.has_value());
    const TextLine line = pickLongestLine(*text);

    RedactionMark mark;
    mark.rect = line.box.adjusted(-1.0, -1.0, 1.0, 1.0);
    mark.pageIndex = 0;

    QString error;
    QVERIFY2(aipdf::pdf::eraseRedactions(opened.document, {mark}, &error), qPrintable(error));

    auto second = opened.document.page(1);
    QVERIFY(second.has_value());
    const auto secondText = second->extractText();
    QVERIFY(secondText.has_value());
    QVERIFY2(!secondText->chars.isEmpty(), "擦除第 1 页把第 2 页的文字也弄没了");
}

// 不指定页号：每一页都放这个标记。
void RedactionTests::markWithoutPageIndexCoversEveryPage() {
    auto opened = aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf")));
    QVERIFY2(opened.document.isValid(), qPrintable(opened.errorMessage));
    QVERIFY(opened.document.pageCount() >= 2);

    RedactionMark mark;
    mark.rect = QRectF(10.0, 10.0, 60.0, 20.0);
    mark.pageIndex = -1;

    QString error;
    QVERIFY2(aipdf::pdf::markRedactions(opened.document, {mark}, &error), qPrintable(error));

    for (int index = 0; index < opened.document.pageCount(); ++index) {
        auto page = opened.document.page(index);
        QVERIFY(page.has_value());
        const QImage image = page->render(kRenderScale);
        const QRect rect = pixelRectFor(page->contentBox(), mark.rect, kRenderScale, image.size());
        QVERIFY2(countNearBlackPixels(image, rect) > 0,
                 qPrintable(QStringLiteral("第 %1 页没有画上标记").arg(index + 1)));
    }
}

QTEST_MAIN(RedactionTests)
#include "RedactionTests.moc"
