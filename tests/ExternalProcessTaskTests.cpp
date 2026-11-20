#include "app/controllers/ExternalProcessTask.h"
#include "app/controllers/OfdBatchConverter.h"

#include <QSignalSpy>
#include <QtTest>

using aipdf::app::ExternalProcessTask;
using aipdf::app::OfdBatchConverter;

class ExternalProcessTaskTests final : public QObject {
    Q_OBJECT

private slots:
    void capturesOutputWithoutBlocking();
    void reportsTimeout();
    void runsBatchSequentially();
};

void ExternalProcessTaskTests::capturesOutputWithoutBlocking() {
    ExternalProcessTask task;
    QSignalSpy finished(&task, &ExternalProcessTask::finished);

    task.start(QStringLiteral(AIPDF_CMAKE_COMMAND),
               {QStringLiteral("-E"), QStringLiteral("echo"), QStringLiteral("hello")},
               5000);

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    const auto result = qvariant_cast<ExternalProcessTask::Result>(finished.first().first());
    QVERIFY(result.succeeded());
    QVERIFY(result.output.contains("hello"));
}

void ExternalProcessTaskTests::reportsTimeout() {
    ExternalProcessTask task;
    QSignalSpy finished(&task, &ExternalProcessTask::finished);

    task.start(QStringLiteral(AIPDF_CMAKE_COMMAND),
               {QStringLiteral("-E"), QStringLiteral("sleep"), QStringLiteral("2")}, 50);

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    const auto result = qvariant_cast<ExternalProcessTask::Result>(finished.first().first());
    QVERIFY(result.timedOut);
    QVERIFY(!result.succeeded());
}

void ExternalProcessTaskTests::runsBatchSequentially() {
    OfdBatchConverter batch;
    QSignalSpy finished(&batch, &OfdBatchConverter::finished);

    batch.start(QStringLiteral(AIPDF_CMAKE_COMMAND),
                {QStringLiteral("first.ofd"), QStringLiteral("second.ofd")});

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    const QList<QVariant> arguments = finished.first();
    QCOMPARE(arguments.at(0).toInt(), 0);
    QCOMPARE(arguments.at(1).toStringList(),
             QStringList({QStringLiteral("first.ofd"), QStringLiteral("second.ofd")}));
    QCOMPARE(arguments.at(2).toBool(), false);
}

QTEST_GUILESS_MAIN(ExternalProcessTaskTests)
#include "ExternalProcessTaskTests.moc"
