#include "app/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("AI PDF Editor");
    QCoreApplication::setOrganizationName("AI PDF Editor Project");
    QCoreApplication::setApplicationVersion(AIPDF_VERSION);

    aipdf::app::MainWindow window;
    window.show();
    return application.exec();
}

