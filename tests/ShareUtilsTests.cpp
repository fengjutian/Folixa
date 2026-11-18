#include "app/util/ShareUtils.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace aipdf::app;

class ShareUtilsTests final : public QObject {
    Q_OBJECT

private slots:
    void fileLinkEncodesUnicodeAndSpaces();
    void emailUrlCarriesSubjectAndFileLink();
    void emptyPathHasNoFileLink();
};

void ShareUtilsTests::fileLinkEncodesUnicodeAndSpaces() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("审阅 文档.pdf"));
    const QString link = share::fileLink(path);
    const QUrl url(link);
    QCOMPARE(url.scheme(), QStringLiteral("file"));
    QCOMPARE(QDir::cleanPath(url.toLocalFile()), QDir::cleanPath(path));
}

void ShareUtilsTests::emailUrlCarriesSubjectAndFileLink() {
    const QString path = QDir::temp().filePath(QStringLiteral("quarterly review.pdf"));
    const QUrl url = share::emailUrl(path, QStringLiteral("Review: "),
                                     QStringLiteral("Open this file:\n"));
    QCOMPARE(url.scheme(), QStringLiteral("mailto"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("subject"), QUrl::FullyDecoded),
             QStringLiteral("Review: quarterly review.pdf"));
    QCOMPARE(query.queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded),
             QStringLiteral("Open this file:\n") + share::fileLink(path));
}

void ShareUtilsTests::emptyPathHasNoFileLink() {
    QVERIFY(share::fileLink(QString()).isEmpty());
    QVERIFY(share::fileLink(QStringLiteral("  ")).isEmpty());
}

QTEST_MAIN(ShareUtilsTests)
#include "ShareUtilsTests.moc"
