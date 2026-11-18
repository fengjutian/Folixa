#include "app/util/BatchRename.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::batchRenameFiles;

namespace {
void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}  // namespace

class BatchRenameTests final : public QObject {
    Q_OBJECT

private slots:
    void keepsExtensionsAndInputOrder();
    void handlesTargetsOccupiedByOtherSources();
    void rejectsExternalCollisionWithoutChangingFiles();
};

void BatchRenameTests::keepsExtensionsAndInputOrder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("alpha.pdf"));
    const QString second = directory.filePath(QStringLiteral("beta.OFD"));
    writeFile(first, "alpha");
    writeFile(second, "beta");

    const auto result = batchRenameFiles({first, second}, QStringLiteral("材料"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.renamedPaths.size(), 2);
    QCOMPARE(readFile(directory.filePath(QStringLiteral("材料_001.pdf"))), QByteArray("alpha"));
    QCOMPARE(readFile(directory.filePath(QStringLiteral("材料_002.OFD"))), QByteArray("beta"));
    QVERIFY(!QFileInfo::exists(first));
    QVERIFY(!QFileInfo::exists(second));
}

void BatchRenameTests::handlesTargetsOccupiedByOtherSources() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString one = directory.filePath(QStringLiteral("doc_001.pdf"));
    const QString two = directory.filePath(QStringLiteral("doc_002.pdf"));
    writeFile(one, "one");
    writeFile(two, "two");

    const auto result = batchRenameFiles({two, one}, QStringLiteral("doc"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(readFile(one), QByteArray("two"));
    QCOMPARE(readFile(two), QByteArray("one"));
}

void BatchRenameTests::rejectsExternalCollisionWithoutChangingFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.pdf"));
    const QString collision = directory.filePath(QStringLiteral("doc_001.pdf"));
    writeFile(source, "source");
    writeFile(collision, "existing");

    const auto result = batchRenameFiles({source}, QStringLiteral("doc"));
    QVERIFY(!result.ok);
    QCOMPARE(readFile(source), QByteArray("source"));
    QCOMPARE(readFile(collision), QByteArray("existing"));
}

QTEST_MAIN(BatchRenameTests)
#include "BatchRenameTests.moc"
