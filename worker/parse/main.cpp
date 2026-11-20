#include "PdfParseServer.h"

#include "ParentWatchdog.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>

// 隔离解析进程：PDF 的解析与渲染都发生在这里，编辑器主进程不碰这个文件。
//
// 参数约定与 AI Worker 一致（--server-name / --parent-pid / --parent-start-time）：
// 父进程被强杀时，靠 ParentWatchdog 自己退出，不留孤儿进程。
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("PdfParseWorker");
    QCoreApplication::setApplicationVersion(AIPDF_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("AI PDF Editor isolated PDF parse worker");
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

    aipdf::worker::parse::PdfParseServer server(serverName);
    if (!server.start()) {
        QTextStream(stderr) << "Unable to listen on local IPC server\n";
        return 3;
    }

    aipdf::worker::ParentWatchdog watchdog;
    watchdog.start(parser.value(parentPidOption).toLongLong(),
                   parser.value(parentStartTimeOption).toULongLong());
    if (watchdog.parentGone()) {
        QTextStream(stderr) << "Parent process already gone, exiting\n";
        return 0;
    }

    return application.exec();
}
