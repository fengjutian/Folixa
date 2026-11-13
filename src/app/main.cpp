#include "app/MainWindow.h"
#include "app/Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSettings>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("AI PDF Editor");
    QCoreApplication::setOrganizationName("AI PDF Editor Project");
    QCoreApplication::setApplicationVersion(AIPDF_VERSION);

    // 验收探针模式（设了 AIPDF_TRACE_FILE）单独用一个设置文件目录。
    //
    // 验收脚本会把窗口挪到屏幕左上角来算点击坐标，退出时窗口布局会被写回
    // QSettings——默认写的是注册表，也就是**用户自己那份**：
    // 跑一次验收就把开发者的窗口位置、面板可见性全冲掉了。
    // 探针的设置落到临时目录里，与正式使用彻底隔开。
    if (qEnvironmentVariableIsSet("AIPDF_TRACE_FILE")) {
        const QString probeRoot = QDir::tempPath() + QStringLiteral("/ai-pdf-editor-probe");
        QDir().mkpath(probeRoot);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, probeRoot);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, probeRoot);
    }

    aipdf::app::theme::apply(application);

    // 窗口与任务栏图标：与标题栏左侧、安装包里的那份一致。
    // 矢量图标由 Qt 按需栅格化，16px 标题栏和高 DPI 任务栏都清楚。
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    aipdf::app::MainWindow window;
    window.show();

    // 命令行直接打开 PDF：AiPdfEditor.exe <file.pdf>
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1) {
        window.openPdfFile(arguments.at(1));
    }

    return application.exec();
}

