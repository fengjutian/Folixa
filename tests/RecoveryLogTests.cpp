// 恢复日志的测试。
//
// 恢复日志记的是命令序列：写进去、读回来、重放，工作就能恢复。
// 这一层不碰 PDFium：日志只是文件里的 JSON。

#include "doc/RecoveryLog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using aipdf::doc::RecoveryEntry;
using aipdf::doc::RecoveryLog;

class RecoveryLogTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void writeAndReadRoundTrip();
    void emptyCommandListWritesNothing();
    void listReturnsEntries();
    void removeDeletesEntry();
    void differentDocumentsHaveDifferentKeys();

private:
    QTemporaryDir directory_;
    QString previousDirectory_;
    bool hadPrevious_ = false;
};

void RecoveryLogTests::initTestCase() {
    QVERIFY(directory_.isValid());
    // 恢复目录用环境变量指到临时目录：不碰用户真实的恢复目录。
    hadPrevious_ = qEnvironmentVariableIsSet("AIPDF_RECOVERY_DIR");
    previousDirectory_ = qEnvironmentVariable("AIPDF_RECOVERY_DIR");
    qputenv("AIPDF_RECOVERY_DIR", directory_.path().toUtf8());
}

void RecoveryLogTests::cleanupTestCase() {
    if (hadPrevious_) {
        qputenv("AIPDF_RECOVERY_DIR", previousDirectory_.toUtf8());
    } else {
        qunsetenv("AIPDF_RECOVERY_DIR");
    }
}

void RecoveryLogTests::writeAndReadRoundTrip() {
    const QString documentPath = directory_.filePath(QStringLiteral("doc-a.pdf"));
    const QByteArray first = QByteArrayLiteral("{\"type\":\"page.insert\",\"index\":1}");
    const QByteArray second = QByteArrayLiteral("{\"type\":\"page.move\",\"from\":0,\"to\":2}");

    QVERIFY(RecoveryLog::write(documentPath, {first, second}));

    RecoveryEntry entry;
    QVERIFY(RecoveryLog::read(documentPath, &entry));
    QCOMPARE(entry.commands.size(), 2);
    // 命令以 JSON 对象存：比较解析后的内容，不比字节
    // （JSON 对象的键是排序过的，字节顺序不保证一致）。
    const QJsonDocument firstDocument = QJsonDocument::fromJson(entry.commands.first());
    const QJsonDocument secondDocument = QJsonDocument::fromJson(entry.commands.last());
    QCOMPARE(firstDocument.object().value(QStringLiteral("type")).toString(),
             QStringLiteral("page.insert"));
    QCOMPARE(firstDocument.object().value(QStringLiteral("index")).toInt(), 1);
    QCOMPARE(secondDocument.object().value(QStringLiteral("type")).toString(),
             QStringLiteral("page.move"));
    QCOMPARE(secondDocument.object().value(QStringLiteral("to")).toInt(), 2);
    QCOMPARE(QFileInfo(entry.documentPath).absoluteFilePath(),
             QFileInfo(documentPath).absoluteFilePath());
    QVERIFY(!entry.updatedAt.isEmpty());
}

void RecoveryLogTests::emptyCommandListWritesNothing() {
    const QString documentPath = directory_.filePath(QStringLiteral("doc-empty.pdf"));
    QVERIFY(!RecoveryLog::write(documentPath, {}));
    RecoveryEntry entry;
    QVERIFY(!RecoveryLog::read(documentPath, &entry));
}

void RecoveryLogTests::listReturnsEntries() {
    const QString documentPath = directory_.filePath(QStringLiteral("doc-b.pdf"));
    QVERIFY(RecoveryLog::write(documentPath, {QByteArrayLiteral("{\"type\":\"page.delete\"}")}));

    const QVector<RecoveryEntry> entries = RecoveryLog::list();
    bool found = false;
    for (const RecoveryEntry& entry : entries) {
        if (QFileInfo(entry.documentPath).fileName() == QStringLiteral("doc-b.pdf")) {
            found = true;
        }
    }
    QVERIFY(found);
}

void RecoveryLogTests::removeDeletesEntry() {
    const QString documentPath = directory_.filePath(QStringLiteral("doc-c.pdf"));
    QVERIFY(RecoveryLog::write(documentPath, {QByteArrayLiteral("{\"type\":\"page.delete\"}")}));
    RecoveryEntry entry;
    QVERIFY(RecoveryLog::read(documentPath, &entry));

    QVERIFY(RecoveryLog::remove(documentPath));
    QVERIFY(!RecoveryLog::read(documentPath, &entry));
}

void RecoveryLogTests::differentDocumentsHaveDifferentKeys() {
    const QString first = directory_.filePath(QStringLiteral("doc-x.pdf"));
    const QString second = directory_.filePath(QStringLiteral("doc-y.pdf"));
    QVERIFY(RecoveryLog::keyFor(first) != RecoveryLog::keyFor(second));
    // 同一个路径永远同一个键：重启之后要还能找到那份记录。
    QCOMPARE(RecoveryLog::keyFor(first), RecoveryLog::keyFor(first));
}

QTEST_MAIN(RecoveryLogTests)
#include "RecoveryLogTests.moc"
