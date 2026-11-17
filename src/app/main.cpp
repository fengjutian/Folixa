#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/PdfView.h"
#include "app/Theme.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // 必须在 QApplication 之前：否则 Windows 按 125% 放大窗口，
    // 1440×900 会变成 1800×1125，状态栏被顶到屏幕外。
    SetProcessDPIAware();
#endif
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

    // 界面语言要在建窗口之前定下来：窗口构造时读到的就是最终语言。
    // 设置里没存过就跟随系统（中文系统→中文，其他→英文）。
    aipdf::i18n::LanguageManager::instance().loadSavedPreference();

    aipdf::app::theme::apply(application);
    // 阅读配色（夜间 / 护眼 / 高对比）：只改阅读区底色，界面保持浅色。
    aipdf::app::theme::applyScheme(application, aipdf::app::theme::currentScheme());

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

    // 验收专用：--diagnostic-action=<name> 在窗口显示后触发一个动作，然后退出。
    // 走这条路而不是模拟按键——按键会受前台切换与焦点影响，验收会变得不稳定。
    QString diagnostic;
    for (const QString& argument : arguments) {
        if (argument.startsWith(QStringLiteral("--diagnostic-action="))) {
            diagnostic = argument.section(QLatin1Char('='), 1);
        }
    }
    if (!diagnostic.isEmpty()) {
        QTimer::singleShot(700, &window, [&window, diagnostic]() {
            auto triggered = std::make_shared<bool>(false);
            // 模态对话框会挡住后面的退出：安排一个反复关闭的定时器。
            // 动作里可能连开多个对话框（先输入文字、再确认），只关一次会卡住。
            auto* closer = new QTimer(&window);
            QObject::connect(closer, &QTimer::timeout, []() {
                if (QWidget* modal = QApplication::activeModalWidget()) {
                    modal->close();
                }
            });
            closer->start(400);

            // 退出定时器必须在动作**之前**排好：动作会进入模态事件循环，
            // 排在其后的代码要等对话框关掉才执行。
            QTimer::singleShot(2500, [triggered]() {
                QCoreApplication::exit(*triggered ? 0 : 3);
            });

            // 直接找当前标签页里的 PdfView：不动 MainWindow 的接口，
            // 免得和正在并行的编辑互相覆盖。
            aipdf::app::PdfView* view = window.findChild<aipdf::app::PdfView*>();
            if (view != nullptr) {
                if (diagnostic == QStringLiteral("watermark")) {
                    view->applyWatermarkDialog();
                    *triggered = true;
                } else if (diagnostic == QStringLiteral("headerFooter")) {
                    view->applyHeaderFooterDialog();
                    *triggered = true;
                } else if (diagnostic == QStringLiteral("pageNumbers")) {
                    view->applyPageNumbersDialog();
                    *triggered = true;
                } else if (diagnostic == QStringLiteral("merge")) {
                    view->mergeDocumentDialog();
                    *triggered = true;
                } else if (diagnostic == QStringLiteral("extract")) {
                    view->extractCurrentPageDialog();
                    *triggered = true;
                }
            }
        });
    }
    return application.exec();
}

