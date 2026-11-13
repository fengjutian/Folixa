#include "pdf/PdfiumAdapter.h"

#include <QTest>
#include <QVector>

using aipdf::pdf::OutlineItem;
using aipdf::pdf::PageLink;
using aipdf::pdf::PdfDocument;
using aipdf::pdf::PdfOpenResult;

namespace {

QString fixturePath(const char* name) {
    return QString::fromUtf8(AIPDF_PDF_FIXTURES_DIR) + QLatin1Char('/') +
           QString::fromLatin1(name);
}

PdfDocument openFixture(const char* name) {
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath(name));
    if (!result.document.isValid()) {
        return {};
    }
    return result.document;
}

// 夹具里的链接矩形是 user space 的 [60 200 220 235]（左下原点）。
QRectF expectedInternalRect() {
    return QRectF(QPointF(60.0, 200.0), QPointF(220.0, 235.0));
}

QRectF expectedExternalRect() {
    return QRectF(QPointF(60.0, 150.0), QPointF(300.0, 180.0));
}

QRectF expectedOverlapRect() {
    return QRectF(QPointF(60.0, 100.0), QPointF(300.0, 140.0));
}

}  // namespace

class PdfNavigationTests : public QObject {
    Q_OBJECT

private slots:
    void navigationDocumentOpens();
    void outlineParsesHierarchy();
    void outlineResolvesDestAndActionTargets();
    void outlineDecodesUtf16Titles();
    void pageOneHasInternalLink();
    void pageTwoHasExternalLink();
    void linksAreReadPerPage();
    void overlappingLinksFollowZOrder();
    void documentWithoutOutlineIsEmpty();
    void invalidDocumentHasNoLinks();
};

// ------------------------------------------------------------------ 大纲 ----

void PdfNavigationTests::navigationDocumentOpens() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());
    QCOMPARE(document.pageCount(), 3);
}

void PdfNavigationTests::outlineParsesHierarchy() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    const std::vector<OutlineItem> outline = document.outline();
    QCOMPARE(outline.size(), size_t(2));
    QCOMPARE(outline.at(0).children.size(), size_t(2));
    QCOMPARE(outline.at(1).children.size(), size_t(0));
}

void PdfNavigationTests::outlineResolvesDestAndActionTargets() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    const std::vector<OutlineItem> outline = document.outline();
    // 顶层条目用 /Dest 指向目标页。
    QCOMPARE(outline.at(0).pageIndex, 0);
    // 子条目用 /A 动作（GoTo）指向目标页。
    QCOMPARE(outline.at(0).children.at(0).pageIndex, 1);
    QCOMPARE(outline.at(0).children.at(1).pageIndex, 2);
    QCOMPARE(outline.at(1).pageIndex, 2);
}

void PdfNavigationTests::outlineDecodesUtf16Titles() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    const std::vector<OutlineItem> outline = document.outline();
    QCOMPARE(outline.at(0).title, QStringLiteral("第一章 概述"));
    QCOMPARE(outline.at(0).children.at(0).title, QStringLiteral("第一节 背景"));
    QCOMPARE(outline.at(0).children.at(1).title, QStringLiteral("第二节 方法"));
    QCOMPARE(outline.at(1).title, QStringLiteral("第二章 结果"));
}

// ------------------------------------------------------------------ 链接 ----

void PdfNavigationTests::pageOneHasInternalLink() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    const auto page = document.page(0);
    QVERIFY(page.has_value());
    const QVector<PageLink> links = page->links();
    QCOMPARE(links.size(), 4);
    QCOMPARE(links.at(0).pageIndex, 1);
    QVERIFY(links.at(0).uri.isEmpty());
    QVERIFY(links.at(0).isInternal());
    QVERIFY(links.at(0).isUsable());
    QCOMPARE(links.at(0).rect, expectedInternalRect());
}

void PdfNavigationTests::pageTwoHasExternalLink() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    const auto page = document.page(1);
    QVERIFY(page.has_value());
    const QVector<PageLink> links = page->links();
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.at(0).uri, QStringLiteral("https://example.com/page2"));
    QVERIFY(links.at(0).isExternal());
    QVERIFY(links.at(0).isUsable());
    QCOMPARE(links.at(0).rect, expectedOverlapRect());
}

void PdfNavigationTests::linksAreReadPerPage() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    // 第 1 页上的外部链接只属于第 1 页。
    const auto first = document.page(0);
    QVERIFY(first.has_value());
    QCOMPARE(first->links().at(1).uri, QStringLiteral("https://example.com/folixa"));
    QCOMPARE(first->links().at(1).rect, expectedExternalRect());

    // 第 3 页没有链接：空白页不该凭空冒出热区。
    const auto third = document.page(2);
    QVERIFY(third.has_value());
    QVERIFY(third->links().isEmpty());
}

void PdfNavigationTests::overlappingLinksFollowZOrder() {
    const PdfDocument document = openFixture("navigation.pdf");
    QVERIFY(document.isValid());

    const auto page = document.page(0);
    QVERIFY(page.has_value());
    const QVector<PageLink> links = page->links();
    QCOMPARE(links.size(), 4);
    // 最后两个矩形完全重叠：先枚举的在下面，后枚举的（URI）在上层。
    QCOMPARE(links.at(2).pageIndex, 2);
    QCOMPARE(links.at(3).uri, QStringLiteral("https://example.com/overlay"));
    QCOMPARE(links.at(2).rect, links.at(3).rect);
    QCOMPARE(links.at(2).rect, expectedOverlapRect());
}

void PdfNavigationTests::documentWithoutOutlineIsEmpty() {
    const PdfDocument document = openFixture("normal.pdf");
    QVERIFY(document.isValid());

    QVERIFY(document.outline().empty());
    const auto page = document.page(0);
    QVERIFY(page.has_value());
    QVERIFY(page->links().isEmpty());
}

void PdfNavigationTests::invalidDocumentHasNoLinks() {
    const PdfOpenResult result = aipdf::pdf::openDocument(fixturePath("corrupt.pdf"));
    QVERIFY(!result.document.isValid());
    QVERIFY(result.document.outline().empty());
    QVERIFY(!result.document.page(0).has_value());
}

QTEST_MAIN(PdfNavigationTests)
#include "PdfNavigationTests.moc"
