#include "app/Theme.h"

#include <QApplication>
#include <QFont>

namespace aipdf::app::theme {

QString lightStyleSheet() {
    return QStringLiteral(R"(
QWidget {
    color: #202A3A;
    font-family: "Segoe UI Variable", "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 13px;
}
QMainWindow, QDialog { background: #F5F7FA; }
QMenuBar { background: #FFFFFF; border-bottom: 1px solid #E3E7EE; padding: 2px 8px; }
QMenuBar::item { padding: 6px 10px; border-radius: 6px; }
QMenuBar::item:selected, QMenu::item:selected { background: #F1F4FF; color: #3555B7; }
QMenu { background: #FFFFFF; border: 1px solid #E3E7EE; padding: 6px; }
QMenu::item { padding: 7px 28px 7px 10px; border-radius: 6px; }
QToolBar { background: #FFFFFF; border: 0; border-bottom: 1px solid #E3E7EE; spacing: 4px; padding: 6px 10px; }
QToolBar::separator { background: #E3E7EE; width: 1px; margin: 5px 6px; }
QPushButton, QToolButton {
    min-height: 30px; padding: 0 11px; border: 1px solid #E3E7EE;
    border-radius: 8px; background: #FFFFFF;
}
QPushButton:hover, QToolButton:hover { background: #F1F4FF; border-color: #AABAF9; }
QPushButton:pressed, QToolButton:pressed { background: #E4EAFF; border-color: #5277E8; }
QPushButton:focus, QToolButton:focus, QLineEdit:focus { border: 2px solid #5277E8; }
QPushButton:disabled { color: #AAB2C0; background: #F7F8FA; }
QPushButton#primaryButton { color: #FFFFFF; background: #5277E8; border-color: #5277E8; font-weight: 600; }
QPushButton#primaryButton:hover { background: #4267D8; }
QPushButton#aiButton { color: #634CB4; background: #F6F2FF; border-color: #C6B6F4; font-weight: 600; }
QLineEdit {
    min-height: 32px; padding: 0 10px; background: #FFFFFF;
    border: 1px solid #CBD2DE; border-radius: 8px; selection-background-color: #DCE7FF;
}
QTabWidget::pane { border: 0; background: #F5F7FA; }
QTabBar { background: #F5F7FA; }
QTabBar::tab { min-height: 34px; padding: 0 14px; color: #465267; background: #F5F7FA; border: 0; border-right: 1px solid #E3E7EE; }
QTabBar::tab:selected { color: #202A3A; background: #FFFFFF; font-weight: 600; border-top: 2px solid #5277E8; }
QTabBar::tab:hover:!selected { background: #F1F4FF; }
QDockWidget { color: #202A3A; font-weight: 600; }
QDockWidget::title { background: #FFFFFF; border-bottom: 1px solid #E3E7EE; padding: 10px 12px; }
QListWidget, QTextBrowser { background: #FFFFFF; border: 0; }
QStatusBar { background: #FFFFFF; color: #465267; border-top: 1px solid #E3E7EE; }
QStatusBar::item { border: 0; }
QLabel#emptyTitle { font-size: 22px; font-weight: 600; color: #202A3A; }
QLabel#emptyDescription { color: #8792A3; font-size: 13px; }
QLabel#secondaryText { color: #8792A3; }
QFrame#emptyCard { background: #FFFFFF; border: 1px solid #E3E7EE; border-radius: 14px; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { min-height: 30px; background: #CBD2DE; border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: #8792A3; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)");
}

void apply(QApplication& application) {
    QFont font(QStringLiteral("Segoe UI Variable"));
    font.setFallbackFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    font.setPointSize(10);
    application.setFont(font);
    application.setStyleSheet(lightStyleSheet());
}

}  // namespace aipdf::app::theme
