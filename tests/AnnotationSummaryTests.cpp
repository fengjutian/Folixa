// 批注摘要（HTML）的单元测试。
//
// 摘要要贴进邮件、要打印，所以两件事必须钉死：正文里的 HTML 特殊字符要转义
// （否则表格结构被打断），以及按页排序（否则审阅顺序是乱的）。

#include "pdf/AnnotationSummary.h"

#include <QTest>

using aipdf::pdf::annotationsToHtmlSummary;
using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;

namespace {

Annotation make(AnnotationType type, int page) {
    Annotation annotation;
    annotation.pageIndex = page;
    annotation.type = type;
    annotation.color = QColor(0xff, 0xd6, 0x00);
    return annotation;
}

}  // namespace

class AnnotationSummaryTests : public QObject {
    Q_OBJECT

private slots:
    void emptyDocumentSaysSo();
    void rowsFollowAnnotationCount();
    void escapesUserText();
    void sortsByPage();
    void includesAuthorAndTime();
};

void AnnotationSummaryTests::emptyDocumentSaysSo() {
    const QString html = annotationsToHtmlSummary({}, 3, QStringLiteral("合同.pdf"));
    QVERIFY(html.contains(QStringLiteral("还没有批注")));
    QVERIFY(html.contains(QStringLiteral("共 0 条批注")));
}

void AnnotationSummaryTests::rowsFollowAnnotationCount() {
    QVector<Annotation> annotations{ make(AnnotationType::Highlight, 0),
                                     make(AnnotationType::Note, 2) };
    annotations[0].contents = QStringLiteral("第一条");
    annotations[1].contents = QStringLiteral("第二条");

    const QString html = annotationsToHtmlSummary(annotations, 5, QStringLiteral("报告"));
    // 表头那行带 style（"<tr style=..."），数据行是裸 <tr>：数数据行即可。
    QCOMPARE(html.count(QStringLiteral("<tr>")), 2);
    QVERIFY(html.contains(QStringLiteral("<thead>")));
    QVERIFY(html.contains(QStringLiteral("第一条")));
    QVERIFY(html.contains(QStringLiteral("高亮")));
}

void AnnotationSummaryTests::escapesUserText() {
    Annotation annotation = make(AnnotationType::Note, 0);
    annotation.contents = QStringLiteral("<script>alert(1)</script> & \"引号\"");
    const QString html = annotationsToHtmlSummary({ annotation }, 1, QStringLiteral("x"));
    QVERIFY(!html.contains(QStringLiteral("<script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(html.contains(QStringLiteral("&amp;")));
}

void AnnotationSummaryTests::sortsByPage() {
    Annotation late = make(AnnotationType::Highlight, 3);
    late.contents = QStringLiteral("后面的");
    Annotation early = make(AnnotationType::Highlight, 0);
    early.contents = QStringLiteral("前面的");

    const QString html = annotationsToHtmlSummary({ late, early }, 4, QStringLiteral("x"));
    QVERIFY(html.indexOf(QStringLiteral("前面的")) < html.indexOf(QStringLiteral("后面的")));
}

void AnnotationSummaryTests::includesAuthorAndTime() {
    Annotation annotation = make(AnnotationType::Highlight, 0);
    annotation.author = QStringLiteral("张三");
    annotation.createdAt = QDateTime(QDate(2026, 11, 15), QTime(10, 30), Qt::LocalTime);
    const QString html = annotationsToHtmlSummary({ annotation }, 1, QStringLiteral("x"));
    QVERIFY(html.contains(QStringLiteral("张三")));
    QVERIFY(html.contains(QStringLiteral("2026-11-15")));
}

QTEST_MAIN(AnnotationSummaryTests)
#include "AnnotationSummaryTests.moc"
