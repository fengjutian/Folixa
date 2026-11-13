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
QMenuBar { background: #FFFFFF; border-bottom: 1px solid #E6EAF2; padding: 2px 8px; }
QMenuBar::item { padding: 6px 10px; border-radius: 6px; }
QMenuBar::item:selected, QMenu::item:selected { background: #F1F4FF; color: #3555B7; }
QMenu { background: #FFFFFF; border: 1px solid #E3E7EE; padding: 6px; }
QMenu::item { padding: 7px 28px 7px 10px; border-radius: 6px; }

/* ---------------------------------------------------------- 顶部命令栏 -- */
QToolBar#commandBar {
    min-height: 50px; padding: 0 12px; spacing: 2px;
    background: #FFFFFF; border: 0; border-bottom: 1px solid #E8ECF3;
}
QToolBar::separator { background: #E8ECF3; width: 1px; margin: 11px 9px; }
QToolBar#commandBar QToolButton {
    min-height: 32px; padding: 0 13px; border: 1px solid transparent;
    border-radius: 9px; background: transparent; color: #4A576E; font-weight: 600;
}
QToolBar#commandBar QToolButton:hover { color: #2F4FA8; background: #F2F6FD; }
QToolBar#commandBar QToolButton:pressed { background: #E8EFFB; }
QToolBar#commandBar QToolButton:checked {
    color: #3555B7; background: #EEF3FF; border-color: #D9E3FA;
}
QToolBar#commandBar QToolButton:disabled { color: #B4BDCB; background: transparent; }
QToolBar#commandBar QToolButton#appMenuButton {
    min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px;
    padding: 0; border: 1px solid transparent; border-radius: 9px;
    color: #54627A; font-size: 15px; font-weight: 400;
}
QToolBar#commandBar QToolButton#appMenuButton:hover { background: #F2F6FD; color: #2F4FA8; }
QToolBar#commandBar QToolButton#appMenuButton:pressed { background: #E8EFFB; }
QToolBar#commandBar QToolButton#aiCommandButton {
    color: #634CB4; background: #F7F3FF; border-color: #E4DAFB; padding: 0 15px;
}
QToolBar#commandBar QToolButton#aiCommandButton:hover { background: #EFE8FF; border-color: #D2C2F6; }
QLabel#brandWordmark { font-size: 13px; font-weight: 700; color: #1B2432; padding: 0 4px 0 9px; }

/* -------------------------------------------------------------- 按钮 -- */
/* InstantPopup 下整块按钮都能点开菜单，右边那个箭头纯属多余；
   留着它还会在 32px 的按钮里挤出一块空白。 */
QToolBar#commandBar QToolButton#appMenuButton::menu-indicator { image: none; width: 0; height: 0; }QPushButton, QToolButton {
    min-height: 30px; padding: 0 12px; border: 1px solid #E3E7EE;
    border-radius: 8px; background: #FFFFFF;
}
QPushButton:hover, QToolButton:hover { background: #F5F8FD; border-color: #C3D0F3; }
QPushButton:pressed, QToolButton:pressed { background: #E9F0FB; border-color: #9FB4EF; }
QPushButton:focus, QToolButton:focus, QLineEdit:focus { border-color: #5277E8; }
QPushButton:disabled { color: #AAB2C0; background: #F7F8FA; }
QPushButton#primaryButton {
    color: #FFFFFF; background: #5277E8; border: 1px solid #4A6DDE;
    border-radius: 10px; font-weight: 600; font-size: 14px;
    min-height: 40px; padding: 0 22px;
}
QPushButton#primaryButton:hover { background: #476BDF; border-color: #3F63D6; }
QPushButton#primaryButton:pressed { background: #3B5ECB; }
QPushButton#aiButton { color: #634CB4; background: #F6F2FF; border-color: #C6B6F4; font-weight: 600; }

QLineEdit {
    min-height: 32px; padding: 0 10px; background: #FFFFFF;
    border: 1px solid #CBD2DE; border-radius: 8px; selection-background-color: #DCE7FF;
}
QLineEdit:focus { border-color: #5277E8; }

/* ---------------------------------------------- 文档工具栏 / 批注工具栏 -- */
QToolBar#documentToolBar, QToolBar#annotationToolBar {
    background: #FFFFFF; border: 0; border-bottom: 1px solid #E8ECF3;
    spacing: 6px; padding: 6px 12px;
}
QToolBar#documentToolBar QToolButton, QToolBar#annotationToolBar QToolButton { padding: 0 10px; }
/* 缩放与旋转是纯图标按钮：压成 32px 见方，不占多余横向空间。 */
QToolBar#documentToolBar QToolButton[iconOnly="true"] { padding: 0; min-width: 32px; max-width: 32px; }
QToolBar#documentToolBar QToolButton:checked, QToolBar#annotationToolBar QToolButton:checked {
    color: #3555B7; background: #EEF3FF; border-color: #D9E3FA;
}
QToolBar#documentToolBar QToolButton:disabled, QToolBar#annotationToolBar QToolButton:disabled {
    color: #B4BDCB; background: #FAFBFD; border-color: #EEF1F6;
}

/* -------------------------------------------------------------- 标签页 -- */
QTabWidget::pane { border: 0; background: #F5F7FA; }
QTabBar { background: #FFFFFF; }
QTabBar::tab {
    min-height: 32px; margin: 6px 3px 0 3px; padding: 0 16px;
    color: #5A6779; background: transparent;
    border: 1px solid transparent; border-radius: 9px;
}
QTabBar::tab:selected {
    color: #202A3A; background: #F3F6FD; border-color: #E4EAF5; font-weight: 600;
}
QTabBar::tab:hover:!selected { background: #F6F8FC; color: #3555B7; }
QTabBar::close-button { image: url(:/icons/close.svg); subcontrol-position: right; margin-left: 4px; padding: 2px; border-radius: 4px; }
QTabBar::close-button:hover { background: #E4EAF5; }

/* -------------------------------------------------------------- 面板 -- */
QDockWidget { color: #202A3A; font-weight: 600; }
QDockWidget::title { background: #FBFCFE; border-bottom: 1px solid #E3E7EE; padding: 11px 12px; }
QFrame#navigationRail { background: #F7F8FB; border-right: 1px solid #E3E7EE; }
QToolButton[railButton="true"] { min-width: 38px; max-width: 38px; min-height: 38px; padding: 0; border: 0; background: transparent; color: #465267; font-size: 19px; }
QToolButton[railButton="true"]:hover { background: #EAF0FF; color: #3555B7; }
QToolButton[railButton="true"]:checked { background: #E4EAFF; color: #3555B7; border-left: 3px solid #5277E8; }

/* ---------------------------------------------------------------- AI -- */
QFrame#aiPanel { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #F3FBFF,stop:0.55 #F8F7FF,stop:1 #F4F0FF); }
QLabel#aiTitle { color: #3555B7; font-size: 19px; font-weight: 700; padding-top: 4px; }
QLabel#aiSubtitle { color: #6C7890; font-size: 12px; }
QPushButton#aiChip { min-height: 28px; padding: 0 10px; color: #634CB4; background: rgba(255,255,255,190); border: 1px solid #D9CEF7; border-radius: 14px; }
QPushButton#aiChip:disabled { color: #8C80B0; background: rgba(255,255,255,150); }
QFrame#aiPanel QTextBrowser { background: rgba(255,255,255,210); border: 1px solid #E2DDF2; border-radius: 12px; padding: 8px; }
QFrame#aiComposer { background: rgba(255,255,255,220); border: 1px solid #D9CEF7; border-radius: 12px; }
QFrame#aiComposer QLineEdit { min-height: 34px; padding: 0 6px; border: 0; background: transparent; }
QFrame#aiComposer QLineEdit:focus { border: 0; }
QFrame#aiComposer QPushButton#aiButton { min-width: 38px; max-width: 38px; min-height: 38px; max-height: 38px; padding: 0; border-radius: 9px; color: white; background: #765ECC; border: 0; }
QFrame#aiComposer QPushButton#aiButton:hover { background: #634CB4; }
QFrame#aiQuickCard { background: rgba(255,255,255,252); border: 1px solid #E2DCF2; border-radius: 14px; }
QLabel#aiQuickTitle { color: #3555B7; font-size: 15px; font-weight: 700; padding: 1px 2px 0 2px; }
QLabel#aiQuickSubtitle { color: #6C7890; font-size: 11px; padding: 2px 2px 6px 2px; }
QPushButton#aiQuickAction { min-height: 34px; text-align: left; padding: 0 12px; background: #FFFFFF; border: 1px solid #E6E1F5; border-radius: 10px; color: #344156; }
QPushButton#aiQuickAction[primary="true"] { color: #4B3A8F; background: #F6F2FF; border-color: #D5C8F6; font-weight: 600; }
QPushButton#aiQuickAction[primary="true"]:hover { background: #EFE9FF; border-color: #C6B6F4; }
QPushButton#aiQuickAction:hover { color: #634CB4; background: #F8F5FF; border-color: #C6B6F4; }
QPushButton#aiQuickAction:disabled { color: #9AA3B4; background: #FAFAFD; border-color: #EEECF6; }
QPushButton#aiFloatingButton { color: white; background: #5277E8; border: 0; border-radius: 22px; font-size: 19px; font-weight: 700; }
QPushButton#aiFloatingButton:hover { background: #4267D8; }

/* ------------------------------------------------------ 导航 / 缩略图 -- */
QListWidget#thumbnailList { background: #F7F8FB; border: 0; padding: 10px 4px; outline: 0; }
QListWidget#thumbnailList::item { color: #465267; background: transparent; border: 2px solid transparent; border-radius: 10px; padding: 8px; }
QListWidget#thumbnailList::item:hover { background: #F1F4FF; }
QListWidget#thumbnailList::item:selected { color: #3555B7; background: #EEF2FF; border-color: #5277E8; }
QListWidget, QTextBrowser { background: #FFFFFF; border: 0; }
QStatusBar { background: #FFFFFF; color: #465267; border-top: 1px solid #E3E7EE; }
QStatusBar::item { border: 0; }

/* ------------------------------------------------------------ 欢迎页 -- */
QScrollArea#welcomeScroll, QScrollArea#welcomeScroll > QWidget { background: transparent; border: 0; }
QWidget#welcomeContent { background: transparent; }
QLabel#welcomeWordmark { font-size: 30px; font-weight: 600; color: #1B2432; }
QLabel#welcomeEyebrow { font-size: 11px; font-weight: 600; color: #7C8AA5; }
QLabel#welcomeTagline { font-size: 14px; color: #66748C; }
QLabel#dropZoneTitle { font-size: 16px; font-weight: 600; color: #1B2432; }
QLabel#dropZoneHint { font-size: 12px; color: #8792A3; }
QLabel#welcomeSectionTitle { font-size: 13px; font-weight: 600; color: #465267; }
QLabel#welcomeSectionMeta { font-size: 12px; color: #98A2B3; }
QLabel#welcomeFootnote { font-size: 11px; color: #9AA5B8; }
QFrame#recentCard { background: #FFFFFF; border: 1px solid #E6EAF2; border-radius: 16px; }
QPushButton#recentRow { background: transparent; border: 0; border-radius: 12px; padding: 0; min-height: 56px; text-align: left; }
QPushButton#recentRow:hover { background: #F5F8FD; }
QPushButton#recentRow:pressed { background: #EDF2FC; }
QLabel#pdfBadge { background: #FDECEE; color: #D1434F; font-size: 9px; font-weight: 700; border-radius: 10px; }
QLabel#recentName { font-size: 13px; font-weight: 500; color: #202A3A; }
QLabel#recentPath { font-size: 11px; color: #94A0B4; }
QLabel#recentChevron { font-size: 18px; color: #B7C1D2; }
QFrame#recentSeparator { background: #EFF2F8; border: 0; }
QLabel#recentEmptyTitle { font-size: 13px; color: #465267; }
QLabel#recentEmptyHint { font-size: 12px; color: #98A2B3; }

/* ---------------------------------------------------------- 其他状态 -- */
QLabel#emptyTitle { font-size: 22px; font-weight: 600; color: #202A3A; }
QLabel#emptyDescription { color: #8792A3; font-size: 13px; }
QLabel#secondaryText { color: #8792A3; }
QLabel#sectionLabel { color: #8792A3; font-size: 12px; font-weight: 600; padding-left: 2px; }

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
