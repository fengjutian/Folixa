#include "pdf/kernel/LayoutBlocks.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/ops/DocumentComparer.h"
#include "pdf/ops/LayoutDiff.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QTest>
#include <QVector>

using aipdf::pdf::DocumentCompareOptions;
using aipdf::pdf::DocumentCompareResult;
using aipdf::pdf::LayoutBlock;
using aipdf::pdf::LayoutBlockKind;
using aipdf::pdf::LayoutBlockOptions;
using aipdf::pdf::LayoutDiffKind;
using aipdf::pdf::LayoutDiffOptions;
using aipdf::pdf::LayoutDiffResult;
using aipdf::pdf::LayoutDifference;
using aipdf::pdf::layoutDiffKindName;

namespace {

// 一行文字（位置是基线起点，PDF user space）。
struct FixtureText {
    double x = 0.0;
    double y = 0.0;
    QString text;
    double fontSize = 14.0;
};

// 一张图片（放置矩形 + 像素内容）。
struct FixtureImage {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    // 8×8 的 RGB 像素（一行一行，R G B 排列）。
    QByteArray pixels;
};

// 一页夹具。
struct FixturePage {
    QVector<FixtureText> texts;
    QVector<FixtureImage> images;
    double width = 612.0;
    double height = 400.0;
    // 页面自带的 /Rotate（顺时针度数）。
    int rotation = 0;
};

QByteArray escapePdfText(const QString& text) {
    QString result = text;
    result.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    result.replace(QLatin1Char('('), QStringLiteral("\\("));
    result.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    return result.toLatin1();
}

QByteArray number(double value) {
    return QByteArray::number(value, 'f', 2);
}

// 造一份 PDF：文字位置、图片位置、页面尺寸都能精确控制。
//
// 为什么要自己造：仓库的夹具内容是固定的，造不出
// 「只把一行挪到别处」这种精确场景；自己造能精确控制每一块的位置，
// 块级归因的行为才验得准。
QByteArray buildFixturePdf(const QVector<FixturePage>& pages) {
    QByteArray out;
    out.append("%PDF-1.7\n%\xe2\xe3\xcf\xd3\n");
    QHash<int, int> offsets;

    const auto addObject = [&out, &offsets](int id, const QByteArray& body) {
        offsets.insert(id, out.size());
        out.append(QByteArray::number(id) + " 0 obj\n");
        out.append(body);
        out.append("\nendobj\n");
    };

    // 图片对象的编号：排在所有页面之后。
    QVector<QVector<int>> imageIds(pages.size());
    int nextId = 4 + 2 * pages.size();
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        for (int imageIndex = 0; imageIndex < pages.at(pageIndex).images.size(); ++imageIndex) {
            imageIds[pageIndex].append(nextId);
            ++nextId;
        }
    }

    addObject(1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>"));
    QByteArray kids;
    for (int index = 0; index < pages.size(); ++index) {
        kids.append(QByteArray::number(4 + 2 * index) + " 0 R ");
    }
    addObject(2, QByteArray("<< /Type /Pages /Kids [") + kids + "] /Count "
                   + QByteArray::number(pages.size()) + " >>");
    addObject(3, QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"));

    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const FixturePage& page = pages.at(pageIndex);
        const int pageId = 4 + 2 * pageIndex;
        const int contentId = pageId + 1;
        QByteArray content;

        for (int imageIndex = 0; imageIndex < page.images.size(); ++imageIndex) {
            const FixtureImage& image = page.images.at(imageIndex);
            content.append("q " + number(image.width) + " 0 0 " + number(image.height) + " "
                           + number(image.x) + " " + number(image.y) + " cm /Im"
                           + QByteArray::number(imageIndex) + " Do Q\n");
        }
        for (const FixtureText& text : page.texts) {
            content.append("BT /F1 " + number(text.fontSize) + " Tf " + number(text.x) + " "
                           + number(text.y) + " Td (" + escapePdfText(text.text)
                           + ") Tj ET\n");
        }

        QByteArray xobjects;
        for (int imageIndex = 0; imageIndex < page.images.size(); ++imageIndex) {
            xobjects.append("/Im" + QByteArray::number(imageIndex) + " "
                            + QByteArray::number(imageIds.at(pageIndex).at(imageIndex))
                            + " 0 R ");
        }
        QByteArray resources = "<< /Font << /F1 3 0 R >>";
        if (!xobjects.isEmpty()) {
            resources += " /XObject << " + xobjects + ">>";
        }
        resources += " >>";

        addObject(pageId,
                  QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ")
                      + number(page.width) + " " + number(page.height) + "] /Resources "
                      + resources + " /Contents " + QByteArray::number(contentId) + " 0 R "
                      + (page.rotation != 0
                             ? "/Rotate " + QByteArray::number(page.rotation) + " "
                             : QByteArray())
                      + ">>");
        addObject(contentId, QByteArray("<< /Length ") + QByteArray::number(content.size())
                                + " >>\nstream\n" + content + "endstream");
    }

    // 图片对象。
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const QVector<FixtureImage>& images = pages.at(pageIndex).images;
        for (int imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
            const FixtureImage& image = images.at(imageIndex);
            const int pixelSize = 8;
            addObject(imageIds.at(pageIndex).at(imageIndex),
                      QByteArray("<< /Type /XObject /Subtype /Image /Width ")
                          + QByteArray::number(pixelSize) + " /Height "
                          + QByteArray::number(pixelSize)
                          + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length "
                          + QByteArray::number(image.pixels.size())
                          + " >>\nstream\n" + image.pixels + "endstream");
        }
    }

    const int xrefOffset = out.size();
    const int count = nextId;
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

// 一张纯色的小图（8×8）。
QByteArray solidImagePixels(char red, char green, char blue) {
    QByteArray pixels;
    pixels.reserve(8 * 8 * 3);
    for (int index = 0; index < 8 * 8; ++index) {
        pixels.append(red);
        pixels.append(green);
        pixels.append(blue);
    }
    return pixels;
}

bool containsPoint(const QRectF& rect, double x, double y, double tolerance) {
    return rect.left() - tolerance <= x && x <= rect.right() + tolerance
        && rect.top() - tolerance <= y && y <= rect.bottom() + tolerance;
}

// 找出第一条指定种类的差异；找不到返回 nullptr。
const LayoutDifference* findDifference(const LayoutDiffResult& result, LayoutDiffKind kind) {
    for (const LayoutDifference& difference : result.differences) {
        if (difference.kind == kind) {
            return &difference;
        }
    }
    return nullptr;
}

// 找出提到某段文字的差异；找不到返回 nullptr。
const LayoutDifference* findDifferenceByText(const LayoutDiffResult& result, const QString& text) {
    for (const LayoutDifference& difference : result.differences) {
        if (difference.oldText.contains(text) || difference.newText.contains(text)) {
            return &difference;
        }
    }
    return nullptr;
}

int countKind(const LayoutDiffResult& result, LayoutDiffKind kind) {
    int count = 0;
    for (const LayoutDifference& difference : result.differences) {
        if (difference.kind == kind) {
            ++count;
        }
    }
    return count;
}

}  // namespace

class LayoutDiffTests : public QObject {
    Q_OBJECT

private slots:
    void movedBlockIsReportedWithSourceAndTarget();
    void unchangedBlocksAreNotReportedAsDeletedAndInserted();
    void replacedImageIsReportedWhileTextLayerSeesNothing();
    void deletedTableColumnIsReportedWithoutCellRewrites();
    void addedTableRowIsReportedWithoutCellRewrites();
    void crossPageMoveIsAttributed();
    void blocksMatchAcrossDifferentPageSizes();
    void insertedAndDeletedBlocksAreReported();
    void rewrittenTextIsReported();
    void compareDocumentsExposesLayoutDifferences();
    void layoutAnalysisCanBeTurnedOff();
    void rotatedPageKeepsObjectBoundsInUserSpace();
    void documentsWithoutAnalyzableContentAreNotCalledEqual();
};

// 一段文字挪了位置：必须报「移动」，并给出源页、源矩形、目标页、目标矩形。
void LayoutDiffTests::movedBlockIsReportedWithSourceAndTarget() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Bravo")},
                      {60.0, 260.0, QStringLiteral("Charlie")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {240.0, 160.0, QStringLiteral("Bravo")},
                      {60.0, 260.0, QStringLiteral("Charlie")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    QCOMPARE(countKind(result, LayoutDiffKind::BlockMoved), 1);
    const LayoutDifference* moved = findDifference(result, LayoutDiffKind::BlockMoved);
    QVERIFY(moved != nullptr);
    QCOMPARE(moved->oldPageIndex, 0);
    QCOMPARE(moved->newPageIndex, 0);
    QCOMPARE(moved->oldText, QStringLiteral("Bravo"));
    QCOMPARE(moved->newText, QStringLiteral("Bravo"));
    // 源矩形：还在老位置 (60, 300)。
    QVERIFY2(containsPoint(moved->oldBounds, 60.0, 300.0, 2.0),
             qPrintable(QStringLiteral("source bounds: %1,%2 %3x%4")
                            .arg(moved->oldBounds.left())
                            .arg(moved->oldBounds.top())
                            .arg(moved->oldBounds.width())
                            .arg(moved->oldBounds.height())));
    // 目标矩形：挪到了 (240, 160)。
    QVERIFY2(containsPoint(moved->newBounds, 240.0, 160.0, 2.0),
             qPrintable(QStringLiteral("target bounds: %1,%2 %3x%4")
                            .arg(moved->newBounds.left())
                            .arg(moved->newBounds.top())
                            .arg(moved->newBounds.width())
                            .arg(moved->newBounds.height())));
    QVERIFY(moved->oldBounds != moved->newBounds);
}

// 没动的块不能被卷成「删掉 + 新增」。
//
// 这一条要单独钉住：按顺序做动态规划配对时，
// 「A B C」换成「A C B」会把没动的块也配成一对不相干的块，
// 结果是没动的块全被报成删除 + 新增。
void LayoutDiffTests::unchangedBlocksAreNotReportedAsDeletedAndInserted() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Bravo")},
                      {60.0, 260.0, QStringLiteral("Charlie")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {240.0, 160.0, QStringLiteral("Bravo")},
                      {60.0, 260.0, QStringLiteral("Charlie")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error));

    QCOMPARE(countKind(result, LayoutDiffKind::BlockDeleted), 0);
    QCOMPARE(countKind(result, LayoutDiffKind::BlockInserted), 0);
    QCOMPARE(result.deletedBlockCount, 0);
    QCOMPARE(result.insertedBlockCount, 0);
    // 没动的两块也不该出现在任何一条差异里。
    QVERIFY(findDifferenceByText(result, QStringLiteral("Alpha")) == nullptr);
    QVERIFY(findDifferenceByText(result, QStringLiteral("Charlie")) == nullptr);
    QCOMPARE(result.unchangedBlockCount, 2);
}

// 同一个位置换了一张图：文字层看不出任何差异，版面分析要报「换图」。
void LayoutDiffTests::replacedImageIsReportedWhileTextLayerSeesNothing() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 120.0, QStringLiteral("Caption")}};
    oldPage.images = {{60.0, 300.0, 100.0, 50.0, solidImagePixels('\xff', '\x00', '\x00')}};
    FixturePage newPage;
    newPage.texts = {{60.0, 120.0, QStringLiteral("Caption")}};
    newPage.images = {{60.0, 300.0, 100.0, 50.0, solidImagePixels('\x00', '\x00', '\xff')}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    // 文字层：一个字符都没变。
    DocumentCompareOptions compareOptions;
    DocumentCompareResult compareResult;
    QString compareError;
    QVERIFY(aipdf::pdf::compareDocuments(oldBytes, newBytes, compareOptions,
                                         &compareResult, &compareError));
    QVERIFY2(compareResult.differences.isEmpty(),
             qPrintable(QStringLiteral("text layer reported %1 differences")
                            .arg(compareResult.differences.size())));

    // 版面分析：要报换图。
    QVERIFY(compareResult.layoutDiff.analyzed);
    QCOMPARE(countKind(compareResult.layoutDiff, LayoutDiffKind::ImageReplaced), 1);
    const LayoutDifference* replaced =
        findDifference(compareResult.layoutDiff, LayoutDiffKind::ImageReplaced);
    QVERIFY(replaced != nullptr);
    QCOMPARE(replaced->oldPageIndex, 0);
    QCOMPARE(replaced->newPageIndex, 0);
    QVERIFY2(containsPoint(replaced->oldBounds, 60.0, 300.0, 2.0),
             qPrintable(QStringLiteral("image bounds: %1,%2 %3x%4")
                            .arg(replaced->oldBounds.left())
                            .arg(replaced->oldBounds.top())
                            .arg(replaced->oldBounds.width())
                            .arg(replaced->oldBounds.height())));
}

// 表格少了一列：报「表格列变化」，没变的格子不能报「改写」。
void LayoutDiffTests::deletedTableColumnIsReportedWithoutCellRewrites() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("A1")},
                      {160.0, 340.0, QStringLiteral("B1")},
                      {260.0, 340.0, QStringLiteral("C1")},
                      {60.0, 300.0, QStringLiteral("A2")},
                      {160.0, 300.0, QStringLiteral("B2")},
                      {260.0, 300.0, QStringLiteral("C2")}};
    FixturePage newPage;
    // 删掉中间那一列（B），C 列留在原位。
    newPage.texts = {{60.0, 340.0, QStringLiteral("A1")},
                      {260.0, 340.0, QStringLiteral("C1")},
                      {60.0, 300.0, QStringLiteral("A2")},
                      {260.0, 300.0, QStringLiteral("C2")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    QCOMPARE(countKind(result, LayoutDiffKind::TableColumnsChanged), 1);
    QCOMPARE(countKind(result, LayoutDiffKind::TableRowsChanged), 0);
    const LayoutDifference* columns =
        findDifference(result, LayoutDiffKind::TableColumnsChanged);
    QVERIFY(columns != nullptr);
    QCOMPARE(columns->oldColumnCount, 3);
    QCOMPARE(columns->newColumnCount, 2);
    // 没变的格子不能被报成改写。
    QCOMPARE(countKind(result, LayoutDiffKind::TableCellRewritten), 0);
}

// 表格多了一行：报「表格行变化」，没变的格子不能报「改写」。
void LayoutDiffTests::addedTableRowIsReportedWithoutCellRewrites() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("A1")},
                      {160.0, 340.0, QStringLiteral("B1")},
                      {60.0, 300.0, QStringLiteral("A2")},
                      {160.0, 300.0, QStringLiteral("B2")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("A1")},
                      {160.0, 340.0, QStringLiteral("B1")},
                      {60.0, 300.0, QStringLiteral("A2")},
                      {160.0, 300.0, QStringLiteral("B2")},
                      {60.0, 260.0, QStringLiteral("A3")},
                      {160.0, 260.0, QStringLiteral("B3")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    QCOMPARE(countKind(result, LayoutDiffKind::TableRowsChanged), 1);
    QCOMPARE(countKind(result, LayoutDiffKind::TableColumnsChanged), 0);
    const LayoutDifference* rows = findDifference(result, LayoutDiffKind::TableRowsChanged);
    QVERIFY(rows != nullptr);
    QCOMPARE(rows->oldRowCount, 2);
    QCOMPARE(rows->newRowCount, 3);
    QCOMPARE(countKind(result, LayoutDiffKind::TableCellRewritten), 0);
}

// 跨页移动：块从第 1 页挪到第 2 页，必须报「移动」而不是「删掉 + 新增」。
void LayoutDiffTests::crossPageMoveIsAttributed() {
    FixturePage oldFirst;
    oldFirst.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Bravo")},
                      {60.0, 260.0, QStringLiteral("Charlie")},
                      {60.0, 220.0, QStringLiteral("Delta")},
                      {60.0, 180.0, QStringLiteral("Echo")},
                      {60.0, 140.0, QStringLiteral("Foxtrot")}};
    FixturePage oldSecond;
    oldSecond.texts = {{60.0, 340.0, QStringLiteral("Golf")}};

    FixturePage newFirst;
    newFirst.texts = {{60.0, 340.0, QStringLiteral("Alpha")}};
    FixturePage newSecond;
    newSecond.texts = {{60.0, 340.0, QStringLiteral("Bravo")},
                       {60.0, 300.0, QStringLiteral("Charlie")},
                       {60.0, 260.0, QStringLiteral("Delta")},
                       {60.0, 220.0, QStringLiteral("Echo")},
                       {60.0, 180.0, QStringLiteral("Foxtrot")},
                       {60.0, 140.0, QStringLiteral("Golf")}};

    const QByteArray oldBytes = buildFixturePdf({oldFirst, oldSecond});
    const QByteArray newBytes = buildFixturePdf({newFirst, newSecond});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    // 「Bravo」从第 1 页挪到了第 2 页：必须是移动，不能是删除 + 新增。
    const LayoutDifference* bravo = findDifferenceByText(result, QStringLiteral("Bravo"));
    QVERIFY(bravo != nullptr);
    QCOMPARE(bravo->kind, LayoutDiffKind::BlockMoved);
    QCOMPARE(bravo->oldPageIndex, 0);
    QCOMPARE(bravo->newPageIndex, 1);
    // 只有一条提到它，不能又报一遍删除或新增。
    int bravoCount = 0;
    for (const LayoutDifference& difference : result.differences) {
        if (difference.oldText.contains(QStringLiteral("Bravo"))
            || difference.newText.contains(QStringLiteral("Bravo"))) {
            ++bravoCount;
        }
    }
    QCOMPARE(bravoCount, 1);
}

// 页面尺寸不同时，坐标归一化之后仍然要能配上。
void LayoutDiffTests::blocksMatchAcrossDifferentPageSizes() {
    FixturePage oldPage;
    oldPage.width = 612.0;
    oldPage.height = 400.0;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Bravo")}};

    FixturePage newPage;
    newPage.width = 800.0;
    newPage.height = 600.0;
    // 同样的相对位置：x = 60/612×800，y = 340/400×600。
    newPage.texts = {{60.0 / 612.0 * 800.0, 340.0 / 400.0 * 600.0, QStringLiteral("Alpha")},
                      {60.0 / 612.0 * 800.0, 300.0 / 400.0 * 600.0, QStringLiteral("Bravo")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    // 配上了：不能被报成「删掉 + 新增」。
    QCOMPARE(result.deletedBlockCount, 0);
    QCOMPARE(result.insertedBlockCount, 0);
    QCOMPARE(result.matchedBlockCount, 2);
    QCOMPARE(countKind(result, LayoutDiffKind::BlockDeleted), 0);
    QCOMPARE(countKind(result, LayoutDiffKind::BlockInserted), 0);
    // 相对位置一样就不该报「挪动」。
    //
    // 这一条把「坐标归一化」钉死：页面尺寸不同时，同一段文字的绝对坐标
    // 差了上百点，只有归一化之后才谈得上「还在原来的位置」。
    // 不归一化的话，两块会被判成「从 (60,340) 挪到了 (78,510)」。
    QCOMPARE(countKind(result, LayoutDiffKind::BlockMoved), 0);
    if (!result.differences.isEmpty()) {
        QFAIL(qPrintable(QStringLiteral("layout diff reported %1 differences: %2")
                             .arg(result.differences.size())
                             .arg(layoutDiffKindName(result.differences.first().kind))));
    }
}

// 一边多一块、一边少一块：报「块新增 / 块删除」。
void LayoutDiffTests::insertedAndDeletedBlocksAreReported() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Bravo")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Charlie")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error));

    QCOMPARE(countKind(result, LayoutDiffKind::BlockDeleted), 1);
    QCOMPARE(countKind(result, LayoutDiffKind::BlockInserted), 1);
    const LayoutDifference* deleted = findDifference(result, LayoutDiffKind::BlockDeleted);
    QVERIFY(deleted != nullptr);
    QCOMPARE(deleted->oldText, QStringLiteral("Bravo"));
    QCOMPARE(deleted->newPageIndex, -1);
    const LayoutDifference* inserted = findDifference(result, LayoutDiffKind::BlockInserted);
    QVERIFY(inserted != nullptr);
    QCOMPARE(inserted->newText, QStringLiteral("Charlie"));
    QCOMPARE(inserted->oldPageIndex, -1);
}

// 同一块文字被改过：报「文本改写」，并给出相似度。
void LayoutDiffTests::rewrittenTextIsReported() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Total 100")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("Total 120")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY(aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error));

    QCOMPARE(countKind(result, LayoutDiffKind::TextRewritten), 1);
    const LayoutDifference* rewritten = findDifference(result, LayoutDiffKind::TextRewritten);
    QVERIFY(rewritten != nullptr);
    QCOMPARE(rewritten->oldText, QStringLiteral("Total 100"));
    QCOMPARE(rewritten->newText, QStringLiteral("Total 120"));
    QVERIFY(rewritten->similarity > 0.5);
    QVERIFY(rewritten->similarity < 1.0);
}

// 接进 compareDocuments：结果里要能拿到版面差异。
void LayoutDiffTests::compareDocumentsExposesLayoutDifferences() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {60.0, 300.0, QStringLiteral("Bravo")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")},
                      {240.0, 160.0, QStringLiteral("Bravo")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    DocumentCompareOptions options;
    DocumentCompareResult result;
    QString error;
    QVERIFY2(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error),
             qPrintable(error));

    QVERIFY(result.layoutDiff.analyzed);
    QCOMPARE(countKind(result.layoutDiff, LayoutDiffKind::BlockMoved), 1);
    // 文字层差异不受版面分析影响：一个字都没改，文字层本来就不该报差异——
    // 「块被挪走了」只有版面分析看得出来，这正是两条通路分开的理由。
    QVERIFY2(result.differences.isEmpty(),
             qPrintable(QStringLiteral("text layer reported %1 differences")
                            .arg(result.differences.size())));
    QCOMPARE(result.layoutDiff.movedBlockCount, 1);
}

// 开关：关掉之后不再做版面分析。
void LayoutDiffTests::layoutAnalysisCanBeTurnedOff() {
    FixturePage oldPage;
    oldPage.texts = {{60.0, 340.0, QStringLiteral("Alpha")}};
    FixturePage newPage;
    newPage.texts = {{60.0, 340.0, QStringLiteral("Bravo")}};

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    DocumentCompareOptions options;
    options.compareLayout = false;
    DocumentCompareResult result;
    QString error;
    QVERIFY(aipdf::pdf::compareDocuments(oldBytes, newBytes, options, &result, &error));
    QVERIFY(!result.layoutDiff.analyzed);
    QVERIFY(result.layoutDiff.differences.isEmpty());
}

// 旋转页上的坐标同源性。
//
// 这一条是版面分析的坐标前提：FPDFPageObj_GetBounds（对象框）与
// FPDFText_GetCharBox（字符框）必须落在**同一个**坐标系里，
// 否则文字块与图片块没法放在同一张版面上比，图片块的框也会报错位置。
//
// PDFium 的字符框是未旋转的用户空间（左下原点，y 向上），
// 渲染出来的图却已经应用过 /Rotate。对象框到底属于哪一个，
// 仓库里一直没有实测过，这里钉一条。
void LayoutDiffTests::rotatedPageKeepsObjectBoundsInUserSpace() {
    FixturePage page;
    page.width = 200.0;
    page.height = 100.0;
    page.rotation = 90;
    page.texts = {{20.0, 60.0, QStringLiteral("Hi")}};
    page.images = {{10.0, 10.0, 40.0, 20.0, solidImagePixels('\xff', '\x00', '\x00')}};

    const QByteArray bytes = buildFixturePdf({page});
    const aipdf::pdf::PdfOpenResult opened = aipdf::pdf::openDocumentFromBytes(bytes);
    QVERIFY(opened.document.isValid());
    const std::optional<aipdf::pdf::PdfPage> handle = opened.document.page(0);
    QVERIFY(handle.has_value());
    QCOMPARE(handle->rotationDegrees(), 90);

    LayoutBlockOptions options;
    QVector<LayoutBlock> blocks;
    QString reason;
    QVERIFY2(aipdf::pdf::extractLayoutBlocks(*handle, 0, options, &blocks, &reason),
             qPrintable(reason));

    const LayoutBlock* imageBlock = nullptr;
    const LayoutBlock* textBlock = nullptr;
    for (const LayoutBlock& block : blocks) {
        if (block.kind == LayoutBlockKind::Image && imageBlock == nullptr) {
            imageBlock = &block;
        }
        if (block.kind == LayoutBlockKind::Text && textBlock == nullptr) {
            textBlock = &block;
        }
    }
    QVERIFY(imageBlock != nullptr);
    QVERIFY(textBlock != nullptr);

    // 图片框：未旋转的用户空间，(10,10) 起、40×20。
    QVERIFY2(qAbs(imageBlock->bounds.left() - 10.0) <= 1.0
                 && qAbs(imageBlock->bounds.top() - 10.0) <= 1.0
                 && qAbs(imageBlock->bounds.width() - 40.0) <= 1.0
                 && qAbs(imageBlock->bounds.height() - 20.0) <= 1.0,
             qPrintable(QStringLiteral("image bounds: %1,%2 %3x%4")
                            .arg(imageBlock->bounds.left())
                            .arg(imageBlock->bounds.top())
                            .arg(imageBlock->bounds.width())
                            .arg(imageBlock->bounds.height())));
    // 字符框：同样是未旋转的用户空间——基线在 y=60，字框在它上方。
    //
    // 为什么不用「框子必须跨过基线」来判：PDFium 的字符框是从基线往上量的
    // （下沿正好是基线），这一点与「框子跨过基线」的直觉不一样，
    // 但不影响「框在哪个坐标系里」这个要验的事。
    QVERIFY2(textBlock->bounds.left() >= 19.0 && textBlock->bounds.left() <= 23.0
                 && textBlock->bounds.top() >= 55.0 && textBlock->bounds.bottom() <= 75.0,
             qPrintable(QStringLiteral("text bounds: %1,%2 %3x%4")
                            .arg(textBlock->bounds.left())
                            .arg(textBlock->bounds.top())
                            .arg(textBlock->bounds.width())
                            .arg(textBlock->bounds.height())));
    // 旋转页上的像素指纹要算得出来：换算表要把旋转考虑进去。
    QVERIFY(imageBlock->pixelFingerprintValid);
}

// 两份都是扫描件（没有文字也没有图形）：不能报「没有差异」。
//
// 「比不了」与「一样」是两回事：把比不了说成一样，用户会以为两份合同
// 真的逐字相同。
void LayoutDiffTests::documentsWithoutAnalyzableContentAreNotCalledEqual() {
    FixturePage oldPage;
    oldPage.width = 200.0;
    oldPage.height = 200.0;
    FixturePage newPage = oldPage;

    const QByteArray oldBytes = buildFixturePdf({oldPage});
    const QByteArray newBytes = buildFixturePdf({newPage});

    LayoutDiffOptions options;
    LayoutDiffResult result;
    QString error;
    QVERIFY(!aipdf::pdf::compareDocumentLayout(oldBytes, newBytes, options, &result, &error));
    QVERIFY(!result.analyzed);
    QVERIFY2(!result.error.isEmpty(), "必须说清楚「比不了」，不能留空");
    QVERIFY(result.differences.isEmpty());
}

QTEST_MAIN(LayoutDiffTests)
#include "LayoutDiffTests.moc"