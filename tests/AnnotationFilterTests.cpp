#include "pdf/annot/AnnotationFilter.h"

#include <QTest>

using namespace aipdf::pdf;

namespace {

// 造一条批注：只填筛选用得到的字段。
Annotation makeAnnotation(quint64 id, AnnotationType type, int pageIndex, const QString& contents,
                          const QString& author = QString(), quint64 replyToId = 0) {
    Annotation annotation;
    annotation.id = id;
    annotation.type = type;
    annotation.pageIndex = pageIndex;
    annotation.contents = contents;
    annotation.author = author;
    annotation.replyToId = replyToId;
    return annotation;
}

QVector<Annotation> sampleAnnotations() {
    QVector<Annotation> list;
    list.append(makeAnnotation(1, AnnotationType::Highlight, 0, QStringLiteral("重点段落")));
    list.append(makeAnnotation(2, AnnotationType::Note, 0, QStringLiteral("李四的便签"),
                               QStringLiteral("李四")));
    list.append(makeAnnotation(3, AnnotationType::Highlight, 1, QStringLiteral("review later"),
                               QStringLiteral("Alice")));
    list.append(makeAnnotation(4, AnnotationType::Note, 1, QString(), QStringLiteral("王五"), 2));
    return list;
}

}  // namespace

class AnnotationFilterTests final : public QObject {
    Q_OBJECT

private slots:
    void emptyFilterKeepsEverything();
    void filterByTypeKeepsOnlyThatType();
    void filterByKeywordMatchesContents();
    void filterByKeywordMatchesAuthor();
    void filterByKeywordIsCaseInsensitive();
    void filterByPageKeepsOnlyThatPage();
    void filterCanExcludeReplies();
    void filterCanShowOnlyReplies();
    void filterCanRequireContents();
    void filterKeepsOriginalOrder();
    void filterWithoutMatchReturnsEmpty();
    void filterIsEmptyDetectsMissingConditions();
    void filterMatchesSingleAnnotationDirectly();
};

void AnnotationFilterTests::emptyFilterKeepsEverything() {
    const QVector<Annotation> list = sampleAnnotations();
    const QVector<Annotation> result = filterAnnotations(list, AnnotationFilter());
    QCOMPARE(result.size(), list.size());
}

void AnnotationFilterTests::filterByTypeKeepsOnlyThatType() {
    AnnotationFilter filter;
    filter.types.append(AnnotationType::Highlight);
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.at(0).id, quint64(1));
    QCOMPARE(result.at(1).id, quint64(3));
}

void AnnotationFilterTests::filterByKeywordMatchesContents() {
    AnnotationFilter filter;
    filter.keyword = QStringLiteral("便签");
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, quint64(2));
}

void AnnotationFilterTests::filterByKeywordMatchesAuthor() {
    AnnotationFilter filter;
    filter.keyword = QStringLiteral("李四");
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, quint64(2));
}

void AnnotationFilterTests::filterByKeywordIsCaseInsensitive() {
    AnnotationFilter filter;
    filter.keyword = QStringLiteral("REVIEW");
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, quint64(3));
}

void AnnotationFilterTests::filterByPageKeepsOnlyThatPage() {
    AnnotationFilter filter;
    filter.pageIndex = 1;
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.at(0).id, quint64(3));
    QCOMPARE(result.at(1).id, quint64(4));
}

void AnnotationFilterTests::filterCanExcludeReplies() {
    AnnotationFilter filter;
    filter.includeReplies = false;
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 3);
    for (const Annotation& annotation : result) {
        QVERIFY(annotation.replyToId == 0);
    }
}

void AnnotationFilterTests::filterCanShowOnlyReplies() {
    AnnotationFilter filter;
    filter.onlyReplies = true;
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, quint64(4));
    QCOMPARE(result.at(0).replyToId, quint64(2));
}

void AnnotationFilterTests::filterCanRequireContents() {
    AnnotationFilter filter;
    filter.requireContents = true;
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 3);
    for (const Annotation& annotation : result) {
        QVERIFY(!annotation.contents.trimmed().isEmpty());
    }
}

void AnnotationFilterTests::filterKeepsOriginalOrder() {
    AnnotationFilter filter;
    filter.types.append(AnnotationType::Note);
    filter.types.append(AnnotationType::Highlight);
    const QVector<Annotation> result = filterAnnotations(sampleAnnotations(), filter);
    QCOMPARE(result.size(), 4);
    QCOMPARE(result.at(0).id, quint64(1));
    QCOMPARE(result.at(1).id, quint64(2));
    QCOMPARE(result.at(2).id, quint64(3));
    QCOMPARE(result.at(3).id, quint64(4));
}

void AnnotationFilterTests::filterWithoutMatchReturnsEmpty() {
    AnnotationFilter filter;
    filter.keyword = QStringLiteral("根本不存在的字");
    QVERIFY(filterAnnotations(sampleAnnotations(), filter).isEmpty());
}

void AnnotationFilterTests::filterIsEmptyDetectsMissingConditions() {
    AnnotationFilter filter;
    QVERIFY(filter.isEmpty());
    filter.types.append(AnnotationType::Note);
    QVERIFY(!filter.isEmpty());
    AnnotationFilter other;
    other.keyword = QStringLiteral("  ");
    // 只有空白的关键字不算条件。
    QVERIFY(other.isEmpty());
}

void AnnotationFilterTests::filterMatchesSingleAnnotationDirectly() {
    const Annotation annotation = makeAnnotation(9, AnnotationType::Square, 2,
                                                 QStringLiteral("框"), QStringLiteral("赵六"));
    AnnotationFilter filter;
    filter.types.append(AnnotationType::Square);
    filter.pageIndex = 2;
    filter.keyword = QStringLiteral("赵六");
    QVERIFY(matchesAnnotationFilter(annotation, filter));

    filter.pageIndex = 3;
    QVERIFY(!matchesAnnotationFilter(annotation, filter));
}

QTEST_GUILESS_MAIN(AnnotationFilterTests)
#include "AnnotationFilterTests.moc"
