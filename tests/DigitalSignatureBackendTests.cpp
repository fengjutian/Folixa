#include "app/DigitalSignatureBackend.h"

#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace aipdf::app;

class DigitalSignatureBackendTests final : public QObject {
    Q_OBJECT
private slots:
    void prefersBundledExecutable();
    void buildsPkcs12TimestampPlan();
    void rejectsInvalidTimestampUrl();
    void buildsVerificationPlan();
};

void DigitalSignatureBackendTests::prefersBundledExecutable() {
    const QString directory = QCoreApplication::applicationDirPath() + QStringLiteral("/pyhanko");
    QDir().mkpath(directory);
    const QString executable = directory + QStringLiteral("/pyhanko.exe");
    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QCOMPARE(QDir::fromNativeSeparators(DigitalSignatureBackend::findPyHanko()), executable);
    QVERIFY(QFile::remove(executable));
    QVERIFY(QDir().rmdir(directory));
}

QString createFile(QTemporaryDir& directory, const QString& name) {
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("x") != 1) return {};
    return path;
}

void DigitalSignatureBackendTests::buildsPkcs12TimestampPlan() {
    QTemporaryDir directory;
    const QString input = createFile(directory, QStringLiteral("input.pdf"));
    const QString pfx = createFile(directory, QStringLiteral("signer.pfx"));
    const QString pass = createFile(directory, QStringLiteral("pass.txt"));
    const SignatureCommand command = DigitalSignatureBackend::signPkcs12(
        input, directory.filePath(QStringLiteral("signed.pdf")), pfx, pass,
        QStringLiteral("https://tsa.example.com"), true, QStringLiteral("pyhanko.exe"));
    QVERIFY(command.isValid());
    QVERIFY(command.arguments.contains(QStringLiteral("--timestamp-url")));
    QVERIFY(command.arguments.contains(QStringLiteral("--with-validation-info")));
    QVERIFY(command.arguments.contains(QStringLiteral("--use-pades")));
    QVERIFY(command.arguments.contains(QStringLiteral("pkcs12")));
    QVERIFY(command.arguments.contains(QStringLiteral("--passfile")));
}

void DigitalSignatureBackendTests::rejectsInvalidTimestampUrl() {
    QTemporaryDir directory;
    const SignatureCommand command = DigitalSignatureBackend::signPkcs12(
        createFile(directory, QStringLiteral("input.pdf")),
        directory.filePath(QStringLiteral("signed.pdf")),
        createFile(directory, QStringLiteral("signer.pfx")),
        createFile(directory, QStringLiteral("pass.txt")), QStringLiteral("file:///tsa"), false,
        QStringLiteral("pyhanko.exe"));
    QVERIFY(!command.isValid());
}

void DigitalSignatureBackendTests::buildsVerificationPlan() {
    QTemporaryDir directory;
    const QString input = createFile(directory, QStringLiteral("signed.pdf"));
    const SignatureCommand command = DigitalSignatureBackend::verify(input, QStringLiteral("pyhanko.exe"));
    QVERIFY(command.isValid());
    QCOMPARE(command.arguments, QStringList({QStringLiteral("sign"), QStringLiteral("validate"),
                                             QStringLiteral("--pretty-print"), input}));
}

QTEST_GUILESS_MAIN(DigitalSignatureBackendTests)
#include "DigitalSignatureBackendTests.moc"
