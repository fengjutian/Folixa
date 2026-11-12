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
QToolBar#commandBar { min-height: 42px; padding: 4px 12px; background: #FCFCFD; }
QToolBar#commandBar QToolButton { border: 0; background: transparent; padding: 0 13px; color: #465267; font-weight: 600; }
QToolBar#commandBar QToolButton:hover { color: #3555B7; background: #F1F4FF; }
QToolBar#commandBar QToolButton:checked { color: #3555B7; background: #EAF0FF; border-bottom: 2px solid #5277E8; border-radius: 8px; }
QToolBar#commandBar QToolButton:disabled { color: #AAB2C0; background: transparent; }
QLabel#brandMark { min-width: 30px; min-height: 30px; color: white; background: #5277E8; border-radius: 9px; font-size: 16px; font-weight: 700; }
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
QFrame#navigationRail { background: #F7F8FB; border-right: 1px solid #E3E7EE; }
QToolButton#railButton { min-width: 38px; max-width: 38px; min-height: 38px; padding: 0; border: 0; background: transparent; color: #465267; font-size: 19px; }
QToolButton#railButton:hover { background: #EAF0FF; color: #3555B7; }
QToolButton#railButton:checked { background: #E4EAFF; color: #3555B7; border-left: 3px solid #5277E8; }
QFrame#aiPanel { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #F3FBFF,stop:0.55 #F8F7FF,stop:1 #F4F0FF); }
QLabel#aiTitle { color: #3555B7; font-size: 19px; font-weight: 700; padding-top: 4px; }
QLabel#aiSubtitle { color: #6C7890; font-size: 12px; }
QPushButton#aiChip { min-height: 28px; padding: 0 10px; color: #634CB4; background: rgba(255,255,255,190); border: 1px solid #D9CEF7; border-radius: 14px; }
QPushButton#aiChip:disabled { color: #8C80B0; background: rgba(255,255,255,150); }
QFrame#aiPanel QTextBrowser { background: rgba(255,255,255,210); border: 1px solid #E2DDF2; border-radius: 12px; padding: 8px; }
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
    font.setPointSize(10);
    application.setFont(font);
    application.setStyleSheet(lightStyleSheet());
}

}  // namespace aipdf::app::theme
