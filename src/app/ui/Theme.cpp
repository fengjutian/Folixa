#include "app/ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>

#include <algorithm>

namespace aipdf::app::theme {

namespace {

// 最后一次应用的模式。
//
// 自绘控件（画布空状态、蒙层）要问「现在是深色还是浅色」，
// 但它们在绘制热路径上，不能每帧去读 QSettings：这里缓存一份。
ThemeMode g_activeMode = ThemeMode::Light;
bool g_activeModeKnown = false;

// 系统是不是深色（Windows 11 的深色模式）。
bool systemPrefersDark() {
    if (auto* hints = QGuiApplication::styleHints()) {
        return hints->colorScheme() == Qt::ColorScheme::Dark;
    }
    return false;
}

// 当前生效的是不是暗色。
//
// 优先用「最后一次应用的模式」：自绘控件在绘制热路径上，
// 不能每帧去读 QSettings；应用还没跑过 apply() 时才回退去读设置。
bool darkActive() {
    if (!g_activeModeKnown) {
        return currentMode() == ThemeMode::Dark;
    }
    if (g_activeMode == ThemeMode::Dark) {
        return true;
    }
    if (g_activeMode == ThemeMode::System) {
        return systemPrefersDark();
    }
    return false;
}

// 告诉平台「这套界面是深色的」。
//
// Windows 11 的标题栏、原生下拉弹出层听的是系统的配色方案，
// 不是我们的样式表：不设置的话，自绘成深色的界面外面会挂着一根白标题栏。
//
// setColorScheme() 是 Qt 6.8 才有的；6.5~6.7 上这份代码不生效，
// 但界面本身仍然是深色的，只是标题栏跟着系统走。
void applySystemColorScheme(bool dark) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (auto* hints = QGuiApplication::styleHints()) {
        hints->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    }
#else
    Q_UNUSED(dark);
#endif
}

}  // namespace

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
    min-height: 34px; max-height: 34px; padding: 0 6px; spacing: 2px;
    background: #FAFAFB; border: 0; border-bottom: 1px solid #E4E4E7;
}
QToolBar::separator { background: #E8ECF3; width: 1px; margin: 11px 9px; }
QToolBar#commandBar QToolButton {
    min-height: 28px; padding: 0 11px; border: 1px solid transparent;
    border-radius: 9px; background: transparent; color: #4A576E; font-weight: 600;
}
QToolBar#commandBar QToolButton:hover { color: #2F4FA8; background: #F2F6FD; }
QToolBar#commandBar QToolButton:pressed { background: #E8EFFB; }
QToolBar#commandBar QToolButton:checked {
    color: #3555B7; background: #EEF3FF; border-color: #D9E3FA;
}
QToolBar#commandBar QToolButton:disabled { color: #B4BDCB; background: transparent; }
QToolBar#commandBar QToolButton#workspaceButton {
    min-width: 104px; max-width: 104px; min-height: 28px; max-height: 28px;
    padding: 0 10px; border: 1px solid transparent; border-radius: 6px;
    background: transparent; color: #4A576E; font-size: 14px; font-weight: 700;
}
QToolBar#commandBar QToolButton#workspaceButton:hover { background: #F2F6FD; color: #2F4FA8; }
/* 只有停在工作台页时才点亮：未选中保持安静，不和 Ribbon 的选中页签抢注意力。 */
QToolBar#commandBar QToolButton#workspaceButton:checked {
    border-color: #365AA8; background: #4169C1; color: #FFFFFF;
}
QToolBar#commandBar QToolButton#workspaceButton:checked:hover { background: #365AA8; }
/* 主菜单只剩一个图标：去掉内边距，让三条横线居中。 */
QToolBar#commandBar QToolButton#appMenuButton {
    min-width: 30px; max-width: 30px; min-height: 28px; max-height: 28px;
    padding: 0; border: 1px solid transparent; border-radius: 6px;
    background: transparent; color: #4A576E;
}
QToolBar#commandBar QToolButton#appMenuButton:hover { background: #F2F6FD; }
QToolBar#commandBar QToolButton#appMenuButton:pressed { background: #E8EFFB; }
QLineEdit#globalSearch {
    min-height: 26px; max-height: 26px; padding: 0 12px;
    border: 1px solid #E4E4E7; border-radius: 7px;
    background: #F0F0F2; color: #27272A; selection-background-color: #DCE7F7;
}
QLineEdit#globalSearch:hover { background: #ECECEF; border-color: #D4D4D8; }
QLineEdit#globalSearch:focus { background: #FFFFFF; border-color: #4169C1; }
QToolBar#commandBar QToolButton#aiCommandButton {
    color: #365AA8; background: #F2F6FC; border-color: #D9E3F2; padding: 0 15px;
}
QToolBar#commandBar QToolButton#aiCommandButton:hover { background: #E8F0FB; border-color: #C4D3EA; }
QLabel#brandWordmark { font-size: 13px; font-weight: 700; color: #1B2432; padding: 0 4px 0 9px; }

/* -------------------------------------------------------------- Ribbon -- */
QWidget#ribbonBar { background: #FFFFFF; border: 0; border-bottom: 1px solid #D9DEE7; }
QTabBar#ribbonTabs { background: transparent; qproperty-drawBase: 0; }
/* 必须显式清掉全局 QTabBar::tab 的 min-height/margin：
   它们叠加后标签高 54px，而标签条只有 34px，选中指示条和下半截文字会被裁掉。 */
QTabBar#ribbonTabs::tab {
    background: transparent; color: #596579; padding: 0 14px; margin: 0;
    min-height: 34px; border: 0; border-bottom: 2px solid transparent;
    font-size: 13px; font-weight: 500;
}
QTabBar#ribbonTabs::tab:hover:!selected { color: #3159B8; background: #F7F9FC; }
/* 底部蓝条由 RibbonTabBar 自绘：QSS 的 border-bottom 会被 QTabBar 裁掉。 */
QTabBar#ribbonTabs::tab:selected { color: #24499D; font-weight: 600; }
QStackedWidget#ribbonPages { background: #FFFFFF; }
/* 滚动区的 viewport 会按窗口底色自绘，必须一起点成白色，否则 Ribbon 下半截发灰。 */
QScrollArea#ribbonPageScroll,
QScrollArea#ribbonPageScroll > QWidget#qt_scrollarea_viewport { background: #FFFFFF; }
QScrollArea#ribbonPageScroll QScrollBar:horizontal { height: 6px; margin: 0; background: #F5F7FA; }
QScrollArea#ribbonPageScroll QScrollBar::handle:horizontal { background: #D5DCE8; border-radius: 3px; min-width: 40px; }
QFrame#ribbonGroup { border: 0; border-right: 1px solid #E8EBF0; background: transparent; }
QLabel#ribbonGroupTitle { color: #98A2B3; font-size: 10px; font-weight: 400; padding-top: 2px; }
QToolButton[ribbonCommand="true"] {
    min-height: 60px; padding: 3px 5px 2px 5px; border: 1px solid transparent;
    border-radius: 6px; background: transparent; color: #465267;
    font-size: 12px; font-weight: 400;
}
QToolButton[ribbonCommand="true"]:hover { background: #F2F6FC; border-color: #DCE5F2; color: #3159B8; }
QToolButton[ribbonCommand="true"]:pressed { background: #E7EEF9; border-color: #CBD8EA; }
QToolButton[ribbonCommand="true"]:disabled { color: #B8C0CC; background: transparent; }

/* -------------------------------------------------------------- 按钮 -- */
/* InstantPopup 下整块按钮都能点开菜单，右边那个箭头纯属多余；
   留着它还会在 32px 的按钮里挤出一块空白。 */
QToolBar#commandBar QToolButton#appMenuButton::menu-indicator { image: none; width: 0; height: 0; }
QPushButton, QToolButton {
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
    color: #FFFFFF; background: #4169C1; border: 1px solid #365AA8;
    border-radius: 8px; font-weight: 600; font-size: 14px;
    min-height: 40px; padding: 0 22px;
}
QPushButton#primaryButton:hover { background: #365AA8; border-color: #2E4E93; }
QPushButton#primaryButton:pressed { background: #2E4E93; }
QPushButton#aiButton { color: #365AA8; background: #EEF3FC; border-color: #B8CAE6; font-weight: 600; }

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
    color: #27272A; background: #EEF3FC; border-color: #D9E3F2; font-weight: 600;
}
QTabBar::tab:hover:!selected { background: #F7F7F8; color: #365AA8; }
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
/* 面板内容区：与画布之间留一道细边，目录树才不会看起来「飘」在页面上。 */
QWidget#navigationPageArea { background: #FBFBFD; border-right: 1px solid #E4E4E7; }
/* 目录树：对齐参考阅读器的书签面板——浅底、行高松一些、选中/悬停用蓝色。 */
QTreeWidget#outlineTree {
    background: #FBFBFD; border: 0; outline: 0; padding: 6px 4px;
    show-decoration-selected: 1;
}
QTreeWidget#outlineTree::item {
    min-height: 26px; padding: 3px 4px; border-radius: 6px; color: #3F4B5E;
}
QTreeWidget#outlineTree::item:hover { background: #F1F4FF; color: #365AA8; }
QTreeWidget#outlineTree::item:selected { background: #EEF3FC; color: #3555B7; }
QTreeWidget#outlineTree::branch { background: transparent; }
/* 导航轨按钮：扁平图标，选中只换图标颜色（QIcon::On 的那份重染色），
   不再画整块浅蓝底 + 左侧竖条——那套边框在圆角下会弯成一只「括号」，
   看着像残缺的控件。 */
QToolButton[railButton="true"] {
    min-width: 36px; max-width: 36px; min-height: 36px;
    padding: 0; border: 0; border-radius: 8px; background: transparent;
}
QToolButton[railButton="true"]:hover,
QToolButton[railButton="true"]:checked:hover { background: #EFF2F8; }
QToolButton[railButton="true"]:pressed { background: #E4EAF4; }
QToolButton[railButton="true"]:checked { background: transparent; }

/* ---------------------------------------------------------------- AI -- */
QWidget#aiPanel { background: #F6F8FB; }
QFrame#aiHeader, QFrame#aiContextCard {
    background: #FFFFFF; border: 1px solid #E3E8F0; border-radius: 12px;
}
QLabel#aiTitle { color: #24499D; font-size: 18px; font-weight: 700; }
QLabel#aiSubtitle { color: #67748A; font-size: 12px; }
QLabel#aiProviderLabel { color: #8A95A8; font-size: 11px; padding-top: 4px; }
QLabel#aiSectionLabel { color: #465267; font-size: 12px; font-weight: 600; }
QLabel#aiScopeLabel, QLabel#aiUsageLabel, QLabel#aiCitationStatus {
    color: #7C8799; font-size: 11px;
}
QPushButton#aiSettingsButton, QPushButton#aiNewChatButton {
    min-height: 28px; padding: 0 10px; border: 1px solid #E0E5ED;
    border-radius: 7px; background: #F8F9FB; color: #596579;
}
QPushButton#aiSettingsButton:hover, QPushButton#aiNewChatButton:hover { background: #EEF3FC; color: #3159B8; border-color: #CAD7EA; }
QPushButton#aiScopePageButton, QPushButton#aiScopeDocumentButton {
    min-height: 32px; padding: 0 12px; border-radius: 7px;
    background: #F5F7FA; border: 1px solid #E3E8F0; color: #596579;
}
QPushButton#aiScopePageButton:checked, QPushButton#aiScopeDocumentButton:checked {
    color: #24499D; background: #EAF1FF; border-color: #B9CBEA; font-weight: 600;
}
QPushButton#aiSummaryButton, QPushButton#aiExplainButton, QPushButton#aiTranslateButton {
    min-height: 34px; padding: 0 8px; border-radius: 8px;
    background: #FFFFFF; border: 1px solid #DCE3EC; color: #39475D;
}
QPushButton#aiSummaryButton:hover, QPushButton#aiExplainButton:hover, QPushButton#aiTranslateButton:hover {
    color: #3159B8; background: #F1F5FC; border-color: #B9CBEA;
}
QWidget#aiPanel QTextBrowser#aiConversation {
    background: #FFFFFF; border: 1px solid #E3E8F0; border-radius: 12px; padding: 8px;
}
QListWidget#aiCitationList { background: #FFFFFF; border: 1px solid #E3E8F0; border-radius: 10px; padding: 4px; }
QFrame#aiComposer { background: #FFFFFF; border: 1px solid #C9D5E6; border-radius: 12px; }
QFrame#aiComposer:focus-within { border-color: #5277E8; }
QFrame#aiComposer QLineEdit { min-height: 36px; padding: 0 6px; border: 0; background: transparent; }
QFrame#aiComposer QLineEdit:focus { border: 0; }
QFrame#aiComposer QPushButton#aiButton { min-width: 38px; max-width: 38px; min-height: 38px; max-height: 38px; padding: 0; border-radius: 9px; color: white; background: #4169C1; border: 0; }
QFrame#aiComposer QPushButton#aiButton:hover { background: #3159B8; }
QFrame#aiComposer QPushButton#aiCancelButton { min-height: 34px; padding: 0 10px; border: 0; background: transparent; color: #7C8799; }
QFrame#aiComposer QPushButton#aiCancelButton:hover { color: #C0562F; background: #FFF3F0; }
QFrame#aiQuickCard { background: rgba(255,255,255,252); border: 1px solid #D9E3F2; border-radius: 14px; }
QLabel#aiQuickTitle { color: #3555B7; font-size: 15px; font-weight: 700; padding: 1px 2px 0 2px; }
QLabel#aiQuickSubtitle { color: #6C7890; font-size: 11px; padding: 2px 2px 6px 2px; }
QPushButton#aiQuickAction { min-height: 34px; text-align: left; padding: 0 12px; background: #FFFFFF; border: 1px solid #DFE6F0; border-radius: 10px; color: #344156; }
QPushButton#aiQuickAction[primary="true"] { color: #2E4E93; background: #EEF3FC; border-color: #C4D3EA; font-weight: 600; }
QPushButton#aiQuickAction[primary="true"]:hover { background: #E5EDF9; border-color: #B8CAE6; }
QPushButton#aiQuickAction:hover { color: #365AA8; background: #F3F7FC; border-color: #B8CAE6; }
QPushButton#aiQuickAction:disabled { color: #9AA3B4; background: #FAFAFD; border-color: #EEECF6; }
QPushButton#aiFloatingButton { color: white; background: #4169C1; border: 0; border-radius: 22px; font-size: 19px; font-weight: 700; }
QPushButton#aiFloatingButton:hover { background: #365AA8; }

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
QStatusBar QToolButton#statusSettingsButton:hover { background: #EEF3FC; color: #365AA8; }
QStatusBar QToolButton#statusSettingsButton:pressed,
QStatusBar QToolButton#statusSettingsButton:checked { background: #DCE7F7; color: #2E4E93; }
QStatusBar QToolButton#statusSettingsButton::menu-indicator { image: none; width: 0; height: 0; }

QDialog#settingsDialog { background: #FFFFFF; }
QListWidget#settingsNavigation {
    background: #F7F7F8; border: 0; border-right: 1px solid #E4E4E7;
    padding: 12px 8px; outline: 0;
}
QListWidget#settingsNavigation::item { min-height: 40px; padding: 0 12px; border-radius: 7px; }
QListWidget#settingsNavigation::item:selected { background: #EEF3FC; color: #365AA8; font-weight: 600; }
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

// 暗色对照表：文字色。
//
// 只管 `color:` 后面的色值。同样是蓝，深底上的蓝字要提亮，
// 不然 #365AA8 那种深蓝直接糊在深色底上看不见。
//
// 色阶取自 docs/UI_DESIGN_SYSTEM.md §5.3 的 Neutral 表：
// text.primary #EDF2F8、text.secondary #B5C0D0、text.muted #8491A5、
// text.disabled #667287。
QMap<QString, QString> darkTextColors() {
    return {
        // —— 正文与标题：浅色下的深字，在深色下反过来 ——
        {"#202A3A", "#EDF2F8"},      // 正文（text.primary）
        {"#27272A", "#E2E9F2"},      // 选中标签、输入框文字
        {"#18181B", "#F4F8FC"},      // 标题类文字
        {"#1B2432", "#EDF2F8"},      // 品牌字
        {"#39475D", "#C3CEDD"},      // AI 快捷动作文字
        {"#344156", "#C3CEDD"},      // AI 快捷动作文字
        {"#465267", "#B5C0D0"},      // 次级文字（text.secondary）
        {"#3F4B5E", "#C3CEDD"},      // 大纲树文字
        {"#4A576E", "#A9B4C4"},      // 命令栏文字
        {"#5A6779", "#A9B4C4"},      // 标签文字
        {"#596579", "#A9B4C4"},      // AI 按钮文字
        {"#52525B", "#AFBAC9"},      // 状态栏文字
        {"#67748A", "#8491A5"},      // AI 副标题（text.muted）
        {"#6C7890", "#8491A5"},      // AI 副标题
        {"#7C8799", "#8491A5"},      // AI 说明文字
        {"#7C8AA5", "#8491A5"},      // 欢迎页副标题
        {"#8792A3", "#8491A5"},      // 次要说明文字
        {"#8A95A8", "#7A8699"},      // AI 服务商标签
        {"#94A0B4", "#7A8699"},      // 最近文档路径
        {"#98A2B3", "#6F7B8F"},      // Ribbon 分组标题、欢迎页分组说明
        {"#9AA3B4", "#68748A"},      // 空状态提示
        {"#9AA5B8", "#68748A"},      // 欢迎页脚注
        {"#AAB2C0", "#667287"},      // 禁用文字（text.disabled）
        {"#A1A1AA", "#667287"},      // 纯图标按钮禁用文字
        {"#B4BDCB", "#6E7B90"},      // 禁用文字
        {"#B7C1D2", "#77839A"},      // 最近文档箭头
        {"#B8C0CC", "#6E7B90"},      // Ribbon 禁用文字
        // —— 主色文字：深底上的蓝字要提亮 ——
        {"#365AA8", "#8FB4FF"},
        {"#3555B7", "#8FB4FF"},
        {"#3159B8", "#8FB4FF"},
        {"#2F4FA8", "#8FB4FF"},
        {"#24499D", "#8FB4FF"},
        {"#2E4E93", "#8FB4FF"},
        // —— 状态色 ——
        {"#D1434F", "#FF7A85"},      // 危险标签文字
        {"#C0562F", "#E0805A"},      // 取消按钮文字
        // —— 白字：主色按钮上的白字在暗色下也必须是白的 ——
        {"#FFFFFF", "#FFFFFF"},
    };
}

// 暗色对照表：表面色。
//
// 管背景、描边、滚动条这些「不是文字」的色值。
//
// 色阶取自 docs/UI_DESIGN_SYSTEM.md §5.3：
// foundation #111827（应用背景）、surface.primary #1B2432（主表面）、
// surface.secondary #222D3D（次级表面）、surface.tertiary #2A3648（hover）、
// border.default #344156、border.strong #4A5870。
//
// 分组按「这张颜色在界面里扮演什么角色」，不按色相：
// 同一个蓝色可能既是选中底也是 hover 底，角色不同，给法就不同。
//
// 主色在这张表里是**压深**的（#4169C1 → #3E63BE）：
// 深色主题里按钮底本来就暗，再用亮蓝就压不住上面的白字了。
QMap<QString, QString> darkSurfaceColors() {
    return {
        // —— 表面：浅色下的白与浅灰，在暗色下换成三档深灰 ——
        {"#FFFFFF", "#1B2432"},      // 面板、菜单、输入框、卡片（surface.primary）
        {"#FAFAFB", "#161F2E"},      // 命令栏、状态栏、导航轨、设置卡片
        {"#F7F7F8", "#151D2B"},      // 设置导航列表
        {"#F8F9FB", "#222D3D"},      // AI 次级按钮（surface.secondary）
        {"#FBFCFE", "#1E2837"},      // 停靠面板标题
        {"#FBFBFD", "#1E2837"},      // 导航页容器底
        {"#FAFAFD", "#161F2E"},      // AI 快捷动作禁用底
        {"#F7F8FB", "#151D2B"},      // 缩略图列表底
        {"#F6F8FB", "#151D2B"},      // AI 面板底
        {"#F5F7FA", "#222D3D"},      // AI 范围按钮底
        {"#F3F4F6", "#111827"},      // 窗口底（foundation）
        {"#EFF2F8", "#26334A"},      // 最近文档分隔线
        {"#F0F0F2", "#222D3D"},      // 搜索框底
        {"#ECECEF", "#2A3648"},      // 搜索框 hover 底（surface.tertiary）
        {"#F5F8FD", "#2A3648"},      // 按钮 hover 底
        {"#F7F8FA", "#1A2333"},      // 按钮禁用底
        {"#FAFBFD", "#1A2333"},      // 工具栏按钮禁用底
        {"#EEEFF2", "#2A3648"},      // 纯图标按钮 hover 底
        {"#EAF0F8", "rgba(120,140,180,0.14)"},  // 导航轨 hover
        {"#F7F9FC", "rgba(120,140,180,0.12)"},  // Ribbon 标签 hover
        {"#F2F6FD", "rgba(62,99,190,0.16)"},     // AI 命令按钮底
        {"#F1F4FF", "rgba(62,99,190,0.18)"},     // 菜单选中
        {"#F2F6FC", "rgba(62,99,190,0.16)"},     // AI 命令按钮 hover
        {"#F1F5FC", "rgba(62,99,190,0.20)"},     // AI 摘要按钮 hover
        {"#F3F7FC", "rgba(62,99,190,0.20)"},     // AI 快捷动作 hover
        {"#EEF3FC", "rgba(62,99,190,0.18)"},     // 主色淡底
        {"#EDF2FC", "rgba(62,99,190,0.20)"},     // 最近文档行 pressed
        {"#E8F0FB", "rgba(62,99,190,0.16)"},     // AI 命令按钮 hover
        {"#EAF1FF", "rgba(62,99,190,0.22)"},     // AI 范围按钮选中
        {"#EEF3FF", "rgba(62,99,190,0.26)"},     // 工具栏 / 缩略图选中
        {"#EEF2FF", "rgba(62,99,190,0.26)"},     // 缩略图选中
        {"#E9F0FB", "rgba(62,99,190,0.28)"},     // 按钮 pressed
        {"#E8EFFB", "rgba(62,99,190,0.28)"},     // 命令栏按钮 pressed
        {"#E4EAF4", "rgba(62,99,190,0.26)"},     // 导航轨按钮 pressed
        {"#E7EEF9", "rgba(62,99,190,0.28)"},     // Ribbon 命令 pressed
        {"#E5EDF9", "rgba(62,99,190,0.30)"},     // AI 快捷动作 primary hover

        // —— 描边：浅灰描边在深色下要比底色亮一档，否则边界消失 ——
        {"#E4E4E7", "#344156"},      // 通用细描边（border.default）
        {"#E3E7EE", "#344156"},      // 控件描边
        {"#E3E8F0", "#344156"},      // AI 卡片描边
        {"#E0E5ED", "#38455C"},      // AI 设置按钮描边
        {"#DCE3EC", "#38455C"},      // AI 摘要按钮描边
        {"#DFE6F0", "#344156"},      // AI 快捷动作描边
        {"#C9D5E6", "#46566F"},      // AI 输入区描边
        {"#CBD2DE", "#4A5870"},      // 输入框描边、滚动条滑块（border.strong）
        {"#D4D4D8", "#3E4C63"},      // 搜索框 hover 描边
        {"#E6EAF2", "#2B374B"},      // 菜单栏描边
        {"#E8ECF3", "#2E3B50"},      // 文档工具栏描边
        {"#E8EBF0", "#2B374B"},      // Ribbon 分组描边
        {"#EDF0F5", "#2E3B50"},      // 导航面板描边
        {"#E4EAF5", "#344156"},      // 标签关闭按钮 hover
        {"#EEECF6", "#2A3648"},      // AI 快捷动作禁用描边
        {"#EEF1F6", "#2E3B50"},      // 工具栏按钮禁用描边
        {"#D9DEE7", "#2F3C52"},      // Ribbon 描边
        {"#D5DCE8", "#4A5870"},      // Ribbon 滚动条滑块
        {"#D9E3F2", "#3C4C68"},      // 选中态描边
        {"#D9E3FA", "#3C4C68"},      // 选中态描边
        {"#DCE5F2", "#3C4C68"},      // Ribbon 命令 hover 描边
        {"#DCE7F7", "#3C4C68"},      // 状态栏按钮 hover
        {"#CBD8EA", "#46566F"},      // Ribbon 命令 pressed 描边
        {"#C3D0F3", "#46566F"},      // 按钮 hover 描边
        {"#9FB4EF", "#5A7096"},      // 按钮 pressed 描边
        {"#B9CBEA", "rgba(62,99,190,0.45)"},   // AI 按钮描边
        {"#C4D3EA", "rgba(62,99,190,0.45)"},   // AI 按钮 hover 描边
        {"#CAD7EA", "rgba(62,99,190,0.45)"},   // AI 按钮 hover 描边
        {"#B8CAE6", "rgba(62,99,190,0.52)"},   // AI primary hover 描边
        {"#DCE7FF", "rgba(62,99,190,0.40)"},   // 输入框选中文本底

        // —— 主色（当底色用）：深色主题里要压深，白字才压得住 ——
        {"#4169C1", "#3E63BE"},      // 主色底
        {"#365AA8", "#4A72CC"},      // 主色 hover 底、描边
        {"#3159B8", "#4A72CC"},      // 主色 hover 底
        {"#2E4E93", "#2F4F9E"},      // 主色 pressed 底
        {"#5277E8", "#6C93F0"},      // 聚焦描边

        // —— 状态色 ——
        {"#FDECEE", "rgba(255,122,133,0.16)"},   // 危险标签底
        {"#FFF3F0", "rgba(224,128,90,0.14)"},    // 取消按钮底

        // —— 滚动条 hover 滑块（同时也是一处文字色，两张表里都有） ——
        {"#8792A3", "#4A5870"},      // 滚动条滑块 hover

        // —— 半透明叠加：AI 快捷卡片 ——
        {"rgba(255,255,255,252)", "rgba(27,36,50,0.94)"},
    };
}

namespace {

// 把 text 里所有命中 pattern 的色值按 map 替换。
//
// 命中但 map 里没有的色值原样保留——这正是「漏登记」的兜底：
// 宁可留一条亮边，也不要把颜色换成一个不相干的色值。
QString replaceMappedColors(const QString& text, const QRegularExpression& pattern,
                            const QMap<QString, QString>& map) {
    QRegularExpressionMatchIterator matches = pattern.globalMatch(text);
    QStringList chunks;
    int last = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        chunks.append(text.mid(last, match.capturedStart() - last));
        chunks.append(map.value(match.captured(), match.captured()));
        last = match.capturedEnd();
    }
    chunks.append(text.mid(last));
    return chunks.join(QString());
}

}  // namespace

QString darkStyleSheet() {
    // 文字色先换成占位符。
    //
    // 必须占位：文字色的暗色值（比如保持白色的 #FFFFFF）会被下一步的
    // 表面色替换再换一遍，白字就变成了深色字。
    static const QRegularExpression textColorPattern(
        QStringLiteral("(?<![\\w-])(color:\\s*)(#[0-9A-Fa-f]{6})"));
    static const QRegularExpression hexPattern(QStringLiteral("#[0-9A-Fa-f]{6}"));
    static const QRegularExpression rgbaPattern(QStringLiteral("rgba\\([^)]*\\)"));

    const QMap<QString, QString> textColors = darkTextColors();
    const QMap<QString, QString> surfaceColors = darkSurfaceColors();

    QString sheet = lightStyleSheet();

    // 第一步：文字色 → 占位符。
    QStringList placeholders;
    {
        QRegularExpressionMatchIterator matches = textColorPattern.globalMatch(sheet);
        QStringList chunks;
        int last = 0;
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            chunks.append(sheet.mid(last, match.capturedStart() - last));
            const QString hex = match.captured(2);
            const QString placeholder = QStringLiteral("\x01%1\x02").arg(placeholders.size());
            placeholders.append(textColors.value(hex, hex));
            chunks.append(match.captured(1) + placeholder);
            last = match.capturedEnd();
        }
        chunks.append(sheet.mid(last));
        sheet = chunks.join(QString());
    }

    // 第二步：表面色（背景、描边、滚动条）。
    sheet = replaceMappedColors(sheet, hexPattern, surfaceColors);
    sheet = replaceMappedColors(sheet, rgbaPattern, surfaceColors);

    // 第三步：文字色归位。
    for (int index = 0; index < placeholders.size(); ++index) {
        sheet.replace(QStringLiteral("\x01%1\x02").arg(index), placeholders.at(index));
    }
    return sheet;
}

QPalette lightPalette() {
    QPalette palette = QApplication::style()->standardPalette();
    // 明确钉住与样式表一致的颜色。
    //
    // 不钉住的话：系统是深色时 standardPalette() 给的是深色底，
    // 浅色界面就会「白底黑控件」地花掉；钉住之后浅色主题在任何系统下都一样。
    palette.setColor(QPalette::Window, QColor(0xF3, 0xF4, 0xF6));
    palette.setColor(QPalette::WindowText, QColor(0x20, 0x2A, 0x3A));
    palette.setColor(QPalette::Base, QColor(0xFF, 0xFF, 0xFF));
    palette.setColor(QPalette::AlternateBase, QColor(0xF7, 0xF8, 0xFA));
    palette.setColor(QPalette::Text, QColor(0x20, 0x2A, 0x3A));
    palette.setColor(QPalette::Button, QColor(0xFF, 0xFF, 0xFF));
    palette.setColor(QPalette::ButtonText, QColor(0x20, 0x2A, 0x3A));
    palette.setColor(QPalette::Highlight, QColor(0x41, 0x69, 0xC1));
    palette.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    palette.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xFF));
    palette.setColor(QPalette::ToolTipText, QColor(0x20, 0x2A, 0x3A));
    palette.setColor(QPalette::Link, QColor(0x36, 0x5A, 0xA8));
    palette.setColor(QPalette::PlaceholderText, QColor(0x98, 0xA2, 0xB3));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0xAA, 0xB2, 0xC0));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xAA, 0xB2, 0xC0));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xAA, 0xB2, 0xC0));
    return palette;
}

QPalette darkPalette() {
    QPalette palette = QApplication::style()->standardPalette();
    // 色阶取自 docs/UI_DESIGN_SYSTEM.md §5.3 的 Neutral 表。
    palette.setColor(QPalette::Window, QColor(0x11, 0x18, 0x27));       // foundation
    palette.setColor(QPalette::WindowText, QColor(0xED, 0xF2, 0xF8));  // text.primary
    palette.setColor(QPalette::Base, QColor(0x22, 0x2D, 0x3D));        // surface.secondary
    palette.setColor(QPalette::AlternateBase, QColor(0x1B, 0x24, 0x32));// surface.primary
    palette.setColor(QPalette::Text, QColor(0xED, 0xF2, 0xF8));
    palette.setColor(QPalette::Button, QColor(0x1B, 0x24, 0x32));
    palette.setColor(QPalette::ButtonText, QColor(0xED, 0xF2, 0xF8));
    palette.setColor(QPalette::BrightText, QColor(0xFF, 0x7A, 0x85));
    palette.setColor(QPalette::Light, QColor(0x2A, 0x36, 0x48));       // surface.tertiary
    palette.setColor(QPalette::Midlight, QColor(0x26, 0x33, 0x4A));
    palette.setColor(QPalette::Mid, QColor(0x16, 0x1F, 0x2E));
    palette.setColor(QPalette::Dark, QColor(0x0D, 0x13, 0x20));
    palette.setColor(QPalette::Shadow, QColor(0x08, 0x0C, 0x14));
    palette.setColor(QPalette::Highlight, QColor(0x3C, 0x4C, 0x68));
    palette.setColor(QPalette::HighlightedText, QColor(0xF4, 0xF8, 0xFC));
    palette.setColor(QPalette::ToolTipBase, QColor(0x26, 0x33, 0x4A));
    palette.setColor(QPalette::ToolTipText, QColor(0xED, 0xF2, 0xF8));
    palette.setColor(QPalette::Link, QColor(0x8F, 0xB4, 0xFF));
    palette.setColor(QPalette::LinkVisited, QColor(0xB3, 0x9D, 0xFF));
    palette.setColor(QPalette::PlaceholderText, QColor(0x7A, 0x86, 0x99));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x66, 0x72, 0x87));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x66, 0x72, 0x87));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x66, 0x72, 0x87));
    return palette;
}

QColor interfaceTextColor() {
    return darkActive() ? QColor(0xED, 0xF2, 0xF8) : QColor(0x20, 0x2A, 0x3A);
}

QColor surfaceColor() {
    return darkActive() ? QColor(0x1B, 0x24, 0x32) : QColor(0xFF, 0xFF, 0xFF);
}

bool isDarkInterface() {
    return darkActive();
}

QString modeKey(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark:
            return QStringLiteral("dark");
        case ThemeMode::System:
            return QStringLiteral("system");
        case ThemeMode::Light:
            break;
    }
    return QStringLiteral("light");
}

ThemeMode modeFromKey(const QString& key) {
    if (key == QLatin1String("dark")) {
        return ThemeMode::Dark;
    }
    if (key == QLatin1String("system")) {
        return ThemeMode::System;
    }
    return ThemeMode::Light;
}

ThemeMode currentMode() {
    QSettings settings;
    return modeFromKey(
        settings.value(QStringLiteral("appearance/theme"), QStringLiteral("light")).toString());
}

void setCurrentMode(ThemeMode mode) {
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"), modeKey(mode));
}

void apply(QApplication& application) {
    QSettings settings;
    const QString family = settings.value(QStringLiteral("appearance/fontFamily"),
                                          QStringLiteral("Microsoft YaHei UI")).toString();
    const int pointSize = settings.value(QStringLiteral("appearance/fontPointSize"), 10).toInt();
    QFont font(family);
    font.setPointSize(std::clamp(pointSize, 8, 24));
    application.setFont(font);
    applyMode(application, currentMode());
}

void applyMode(QApplication& application, ThemeMode mode) {
    g_activeMode = mode;
    g_activeModeKnown = true;

    const bool dark = mode == ThemeMode::Dark ||
                      (mode == ThemeMode::System && systemPrefersDark());

    // 先告诉平台用哪套配色，再读调色板：
    // 顺序反了读到的还是系统原来那套。
    applySystemColorScheme(dark);

    QPalette palette = dark ? darkPalette() : lightPalette();
    // 画布周围的底色归「阅读配色」管（它写的是 QPalette::Mid），
    // 主题切换不能把它冲掉。
    palette.setColor(QPalette::Mid, application.palette().color(QPalette::Mid));
    application.setPalette(palette);
    application.setStyleSheet(dark ? darkStyleSheet() : lightStyleSheet());
}

}  // namespace aipdf::app::theme
