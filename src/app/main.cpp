#include "app/MainWindow.h"
#include "app/Theme.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("AI PDF Editor");
    QCoreApplication::setOrganizationName("AI PDF Editor Project");
    QCoreApplication::setApplicationVersion(AIPDF_VERSION);
    aipdf::app::theme::apply(application);

    aipdf::app::MainWindow window;
    window.show();

    // 命令行直接打开 PDF：AiPdfEditor.exe <file.pdf>
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1) {
        window.openPdfFile(arguments.at(1));
    }

    return application.exec();
}

