#include "ParentWatchdog.h"
#include "WorkerServer.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("PdfAiWorker");
    QCoreApplication::setApplicationVersion(AIPDF_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("AI PDF Editor isolated AI Worker");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption serverNameOption(
        "server-name", "Local IPC server name", "name");
    parser.addOption(serverNameOption);
    QCommandLineOption parentPidOption(
        "parent-pid", "PID of the process that launched this worker", "pid");
    parser.addOption(parentPidOption);
    QCommandLineOption parentStartTimeOption(
        "parent-start-time",
        "Creation time of the parent process (Windows 100ns ticks), used to detect PID reuse",
        "ticks");
    parser.addOption(parentStartTimeOption);
    parser.process(application);

    const QString serverName = parser.value(serverNameOption);
    if (serverName.isEmpty()) {
        QTextStream(stderr) << "Missing required --server-name argument\n";
        return 2;
    }

    aipdf::worker::WorkerServer server(serverName);
    if (!server.start()) {
        QTextStream(stderr) << "Unable to listen on local IPC server\n";
        return 3;
    }

    // 启动方被强制结束时，QProcess 析构函数不会执行，这里负责收尾。
    aipdf::worker::ParentWatchdog watchdog;
    watchdog.start(parser.value(parentPidOption).toLongLong(),
                   parser.value(parentStartTimeOption).toULongLong());
    if (watchdog.parentGone()) {
        QTextStream(stderr) << "Parent process already gone, exiting\n";
        return 0;
    }

    return application.exec();
}
