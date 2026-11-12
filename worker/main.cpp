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
    return application.exec();
}

