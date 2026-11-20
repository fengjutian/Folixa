#include "app/controllers/ExternalProcessTask.h"

#include <QSignalSpy>
#include <QtTest>

using aipdf::app::ExternalProcessTask;

class ExternalProcessTaskTests final : public QObject {
    Q_OBJECT

private slots:
    void capturesOutputWithoutBlocking();
    void reportsTimeout();
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

QTEST_GUILESS_MAIN(ExternalProcessTaskTests)
#include "ExternalProcessTaskTests.moc"
