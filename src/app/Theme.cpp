#include "app/Theme.h"

#include <QApplication>
#include <QFont>
#include <QSettings>

#include <algorithm>

namespace aipdf::app::theme {

QString lightStyleSheet() {
    return QStringLiteral(R"(
QWidget {
    color: #202A3A;
    outline: 0;
}
QAbstractItemView, QPushButton, QToolButton, QCheckBox, QRadioButton,
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QSlider { outline: 0; }
QMainWindow, QDialog { background: #F3F4F6; }
QMenuBar { background: #FFFFFF; border-bottom: 1px solid #E6EAF2; padding: 2px 8px; }
QMenuBar::item { padding: 6px 10px; border-radius: 6px; }
QMenuBar::item:selected, QMenu::item:selected { background: #F1F4FF; color: #3555B7; }
QMenu { background: #FFFFFF; border: 1px solid #E3E7EE; padding: 6px; }
QMenu::item { padding: 7px 28px 7px 10px; border-radius: 6px; }

/* ---------------------------------------------------------- 顶部命令栏 -- */
QToolBar#commandBar {
    min-height: 42px; max-height: 42px; padding: 0 8px; spacing: 2px;
    background: #FAFAFB; border: 0; border-bottom: 1px solid #E4E4E7;
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
    min-width: 112px; max-width: 112px; min-height: 30px; max-height: 30px;
    padding: 0 12px; border: 1px solid #6D28D9; border-radius: 6px;
    background: #7C3AED; color: #FFFFFF; font-size: 14px; font-weight: 700;
}
QToolBar#commandBar QToolButton#appMenuButton:hover { background: #6D28D9; color: #FFFFFF; }
QToolBar#commandBar QToolButton#appMenuButton:pressed { background: #5B21B6; }
QToolBar#commandBar QToolButton[titleQuick="true"] {
    min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px;
    padding: 0; border: 0; border-radius: 5px; color: #52525B; background: transparent;
}
QToolBar#commandBar QToolButton[titleQuick="true"]:hover { background: #EEEFF2; }
QToolBar#commandBar QToolButton[titleQuick="true"]:disabled { background: transparent; color: #A1A1AA; }
QLineEdit#globalSearch {
    min-height: 30px; max-height: 30px; padding: 0 14px;
    border: 1px solid #E4E4E7; border-radius: 7px;
    background: #F0F0F2; color: #27272A; selection-background-color: #DDD6FE;
}
QLineEdit#globalSearch:hover { background: #ECECEF; border-color: #D4D4D8; }
QLineEdit#globalSearch:focus { background: #FFFFFF; border-color: #8B5CF6; }
QToolBar#commandBar QToolButton[titleAccount="true"] {
    min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 0;
    border: 0; border-radius: 14px;
    background: #7C3AED; color: #FFFFFF; font-weight: 800;
}
QToolBar#commandBar QToolButton#aiCommandButton {
    color: #634CB4; background: #F7F3FF; border-color: #E4DAFB; padding: 0 15px;
}
QToolBar#commandBar QToolButton#aiCommandButton:hover { background: #EFE8FF; border-color: #D2C2F6; }
QLabel#brandWordmark { font-size: 13px; font-weight: 700; color: #1B2432; padding: 0 4px 0 9px; }

/* -------------------------------------------------------------- Ribbon -- */
QWidget#ribbonBar { background: #FFFFFF; border: 0; border-bottom: 1px solid #D9DEE7; }
QTabBar#ribbonTabs { background: transparent; qproperty-drawBase: 0; }
/* 必须显式清掉全局 QTabBar::tab 的 min-height/margin：
   它们叠加后标签高 54px，而标签条只有 34px，选中指示条和下半截文字会被裁掉。 */
QTabBar#ribbonTabs::tab {
    background: transparent; color: #30394A; padding: 0 15px; margin: 0;
    min-height: 34px; border: 0; border-bottom: 2px solid transparent; font-weight: 600;
}
QTabBar#ribbonTabs::tab:hover:!selected { color: #6D28D9; background: #F7F5FA; }
/* 底部蓝条由 RibbonTabBar 自绘：QSS 的 border-bottom 会被 QTabBar 裁掉。 */
QTabBar#ribbonTabs::tab:selected { color: #202A3A; }
QStackedWidget#ribbonPages { background: #FFFFFF; }
/* 滚动区的 viewport 会按窗口底色自绘，必须一起点成白色，否则 Ribbon 下半截发灰。 */
QScrollArea#ribbonPageScroll,
QScrollArea#ribbonPageScroll > QWidget#qt_scrollarea_viewport { background: #FFFFFF; }
QScrollArea#ribbonPageScroll QScrollBar:horizontal { height: 6px; margin: 0; background: #F5F7FA; }
QScrollArea#ribbonPageScroll QScrollBar::handle:horizontal { background: #D5DCE8; border-radius: 3px; min-width: 40px; }
QFrame#ribbonGroup { border: 0; border-right: 1px solid #E1E5EC; background: transparent; }
QLabel#ribbonGroupTitle { color: #8A92A0; font-size: 10px; padding-top: 1px; }
QToolButton[ribbonCommand="true"] {
    min-height: 62px; padding: 3px 7px 2px 7px; border: 1px solid transparent;
    border-radius: 3px; background: transparent; color: #30394A; font-weight: 400;
}
QToolButton[ribbonCommand="true"]:hover { background: #F5F3F7; border-color: #E8E4EB; color: #6D28D9; }
QToolButton[ribbonCommand="true"]:pressed { background: #E9EBF0; }
QToolButton[ribbonCommand="true"]:disabled { color: #AEB4BF; background: transparent; }

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
/* 纯图标按钮（搜索面板的上一条 / 下一条）：去掉文字内边距，压成 32px 见方。
   30 + 左右各 1px 边框 = 32，比邻的文字按钮一样高，行里不会一高一低。 */
QPushButton[iconOnly="true"] {
    padding: 0; min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px;
}
QPushButton[iconOnly="true"]:disabled { background: #F7F8FA; }
QPushButton#primaryButton {
    color: #FFFFFF; background: #7C3AED; border: 1px solid #6D28D9;
    border-radius: 8px; font-weight: 600; font-size: 14px;
    min-height: 40px; padding: 0 22px;
}
QPushButton#primaryButton:hover { background: #6D28D9; border-color: #5B21B6; }
QPushButton#primaryButton:pressed { background: #5B21B6; }
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
QTabWidget::pane { border: 0; background: #F3F4F6; }
QTabBar { background: #FFFFFF; }
QTabBar::tab {
    min-height: 30px; margin: 5px 2px 0 2px; padding: 0 16px;
    color: #5A6779; background: transparent;
    border: 1px solid transparent; border-radius: 6px 6px 0 0;
}
QTabBar::tab:selected {
    color: #27272A; background: #F5F3FF; border-color: #E9E5F5; font-weight: 600;
}
QTabBar::tab:hover:!selected { background: #F7F7F8; color: #6D28D9; }
QTabBar::close-button { image: url(:/icons/close.svg); subcontrol-position: right; margin-left: 4px; padding: 2px; border-radius: 4px; }
QTabBar::close-button:hover { background: #E4EAF5; }

/* -------------------------------------------------------------- 面板 -- */
QDockWidget { color: #202A3A; font-weight: 600; }
QDockWidget::title { background: #FBFCFE; border-bottom: 1px solid #E3E7EE; padding: 11px 12px; }
QLabel#navigationHeader {
    padding: 10px 12px 6px 12px; font-weight: 600; font-size: 13px;
    color: #202A3A; background: #FFFFFF; border-bottom: 1px solid #EDF0F5;
}
QFrame#navigationRail { background: #FAFAFB; border-right: 1px solid #E4E4E7; }
QToolButton[railButton="true"] { min-width: 36px; max-width: 36px; min-height: 36px; padding: 0; border: 0; background: transparent; color: #465267; font-size: 18px; }
QToolButton[railButton="true"]:hover { background: #F3F0FA; color: #6D28D9; }
QToolButton[railButton="true"]:checked { background: #EDE9FE; color: #6D28D9; border-left: 3px solid #7C3AED; }

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
QPushButton#aiFloatingButton { color: white; background: #7C3AED; border: 0; border-radius: 22px; font-size: 19px; font-weight: 700; }
QPushButton#aiFloatingButton:hover { background: #6D28D9; }

/* ------------------------------------------------------ 导航 / 缩略图 -- */
QListWidget#thumbnailList { background: #F7F8FB; border: 0; padding: 10px 4px; outline: 0; }
QListWidget#thumbnailList::item { color: #465267; background: transparent; border: 2px solid transparent; border-radius: 10px; padding: 8px; }
QListWidget#thumbnailList::item:hover { background: #F1F4FF; }
QListWidget#thumbnailList::item:selected { color: #3555B7; background: #EEF2FF; border-color: #5277E8; }
QListWidget, QTextBrowser { background: #FFFFFF; border: 0; }
QStatusBar {
    min-height: 36px; max-height: 36px; padding-left: 34px;
    background: #FAFAFB; color: transparent; border-top: 1px solid #E4E4E7;
}
QStatusBar::item { border: 0; }
QStatusBar QLabel { color: #52525B; }
QStatusBar QLabel#statusMessageLabel { color: #52525B; background: transparent; }
QWidget#statusDocumentControls { background: transparent; }
QWidget#statusDocumentControls QLineEdit {
    min-height: 26px; max-height: 26px; border-radius: 6px; padding: 0 6px;
}
QWidget#statusDocumentControls QToolButton {
    min-width: 28px; max-width: 28px; min-height: 26px; max-height: 26px;
    padding: 0; border-radius: 6px;
}
QStatusBar QToolButton#statusSettingsButton {
    min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px;
    margin: 1px 2px; padding: 0; border: 0; border-radius: 5px;
    background: transparent; color: #52525B; font-size: 15px;
}
QStatusBar QToolButton#statusSettingsButton:hover { background: #EDE9FE; color: #6D28D9; }
QStatusBar QToolButton#statusSettingsButton:pressed,
QStatusBar QToolButton#statusSettingsButton:checked { background: #DDD6FE; color: #5B21B6; }
QStatusBar QToolButton#statusSettingsButton::menu-indicator { image: none; width: 0; height: 0; }

QDialog#settingsDialog { background: #FFFFFF; }
QListWidget#settingsNavigation {
    background: #F7F7F8; border: 0; border-right: 1px solid #E4E4E7;
    padding: 12px 8px; outline: 0;
}
QListWidget#settingsNavigation::item { min-height: 40px; padding: 0 12px; border-radius: 7px; }
QListWidget#settingsNavigation::item:selected { background: #EDE9FE; color: #6D28D9; font-weight: 600; }
QLabel#settingsPageTitle { color: #18181B; font-size: 20px; font-weight: 700; }
QFrame#settingsCard {
    background: #FAFAFB; border: 1px solid #E4E4E7; border-radius: 10px;
}
QLabel#settingsSectionTitle { color: #27272A; font-size: 14px; font-weight: 700; }
QLabel#settingsProductName { color: #18181B; font-size: 24px; font-weight: 750; }
QLabel#settingsFontPreview {
    background: #FFFFFF; border: 1px solid #E4E4E7; border-radius: 8px; padding: 10px;
}
QDialog#settingsDialog QComboBox, QDialog#settingsDialog QFontComboBox,
QDialog#settingsDialog QSpinBox { min-width: 220px; }

/* ------------------------------------------------------------ 欢迎页 -- */
QScrollArea#welcomeScroll, QScrollArea#welcomeScroll > QWidget { background: transparent; border: 0; }
QWidget#welcomeContent { background: transparent; }
QLabel#welcomeWordmark { font-size: 25px; font-weight: 650; color: #18181B; }
QLabel#welcomeEyebrow { font-size: 11px; font-weight: 600; color: #7C8AA5; }
QLabel#welcomeTagline { font-size: 15px; color: #52525B; }
QLabel#dropZoneTitle { font-size: 16px; font-weight: 600; color: #1B2432; }
QLabel#dropZoneHint { font-size: 12px; color: #8792A3; }
QLabel#welcomeSectionTitle { font-size: 13px; font-weight: 600; color: #465267; }
QLabel#welcomeSectionMeta { font-size: 12px; color: #98A2B3; }
QLabel#welcomeFootnote { font-size: 11px; color: #9AA5B8; }
QFrame#recentCard { background: #FFFFFF; border: 1px solid #E4E4E7; border-radius: 12px; }
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
    QSettings settings;
    const QString family = settings.value(QStringLiteral("appearance/fontFamily"),
                                          QStringLiteral("Segoe UI Variable")).toString();
    const int pointSize = settings.value(QStringLiteral("appearance/fontPointSize"), 10).toInt();
    QFont font(family);
    font.setPointSize(std::clamp(pointSize, 8, 24));
    application.setFont(font);
    application.setStyleSheet(lightStyleSheet());
}

}  // namespace aipdf::app::theme
