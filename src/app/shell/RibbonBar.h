#pragma once

#include <QIcon>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QMenuBar;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace aipdf::app {

// 顶部 Ribbon：一行标签页 + 每个标签页下的一组命令分组。
//
// 只负责布局与外观，不实现任何命令：按钮点击通过 QToolButton::clicked
// 由调用方接线。这样它可以在没有文档、没有 PDFium 的情况下单独构造。
//
// 结构对齐福昕：
//   ┌ 文件 │ 主页 │ 注释 │ 页面管理 │ 视图 │ 保护 │ AI 助手 ┐
//   │ [组1]            [组2]            [组3]              │
//   └──────────────────────────────────────────────────────┘
class RibbonBar final : public QWidget {
    Q_OBJECT

public:
    explicit RibbonBar(QWidget* parent = nullptr);

    // 追加一个标签页，返回它的下标。
    int addTab(const QString& title);
    // 在指定标签页里追加一个命令分组，返回可以继续添加按钮的布局。
    QHBoxLayout* addGroup(int tabIndex, const QString& title);
    // 往分组里加一个命令按钮。group 是 addGroup 返回的布局。
    QToolButton* addCommand(QHBoxLayout* group,
                            const QString& text,
                            const QIcon& icon,
                            const QString& toolTip,
                            bool enabled = true,
                            const QString& objectName = QString());
    // 一个分组只显示一个"大按钮"时用它：图标 + 文字上下排。
    QToolButton* addLargeCommand(QHBoxLayout* group,
                                 const QString& text,
                                 const QIcon& icon,
                                 const QString& toolTip,
                                 bool enabled = true,
                                 const QString& objectName = QString());

    void setCurrentTab(int index);
    int currentTab() const;
    int tabCount() const;

    // ------------------------------------------------------- 无障碍与助记符 ----
    //
    // 屏幕阅读器读的是 accessibleName，不是按钮文字：文字里可能带助记符标记，
    // 读出来会是「&打开」这种怪东西。所以无障碍名一律用去掉助记符后的文字。

    // 去掉助记符标记："&打开" → "打开"，"&&" → 字面量 "&"。
    static QString stripMnemonics(const QString& text);
    // 取助记键：单个 '&' 后面的那个字符；没有助记符时返回空 QChar。
    // "&&" 是字面量 '&'，不算助记符。
    static QChar mnemonicKey(const QString& text);
    // 在指定字符前插入助记符标记（该字符已经是文本里的一部分时用）。
    static QString insertMnemonic(const QString& text, QChar key);

    // 命令区里的所有命令按钮（按创建顺序）。
    QVector<QToolButton*> commandButtons() const;

    // 切换语言之后重算按钮文字里的助记符与无障碍名。
    //
    // 为什么需要单独调一次：按钮文字是 texts_.bind 重新翻译出来的，
    // 翻译会把之前插进去的 '&' 冲掉，助记符与无障碍名都要跟着重算。
    void refreshCommandLabels();
    // 欢迎页只保留标签导航；打开文档后恢复完整命令区。
    void setCompactMode(bool compact);
    // 按标题重排标签与对应内容页；未列出的标签保持在末尾。
    void setTabOrder(const QStringList& titles);
    // 把 Ribbon 命令投影为下拉菜单；命令仍由原按钮执行，不复制业务接线。
    // skippedTitles 用于保留主窗口已有的、更完整的菜单（例如“文件”“视图”）。
    void appendMenusTo(QMenuBar* menuBar, const QStringList& skippedTitles = {});

    // Ribbon 的内容宽度由所有标签页里最宽的一页决定；如果直接把它当最小宽度，
    // 主窗口会被撑得比屏幕还宽，PDF 画布也跟着被挤到屏幕外。
    // 这里只上报一个紧凑的宽度，内容超出时由标签页自己裁切。
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void tabChanged(int index);

private:
    int appendPage();
    // 给一个命令按钮补上助记符标记（已有显式助记符时不动）。
    void applyMnemonicTo(QToolButton* button);
    QTabBar* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    // QStackedWidget 的直接子项是滚动容器，不是 pageWidgets_ 中的内容页。
    // 重排时必须移动容器，否则内容页会被错误地从 QScrollArea 中拆走。
    QVector<QWidget*> pageContainers_;
    QVector<QWidget*> pageWidgets_;
    QVector<QHBoxLayout*> pageLayouts_;
    bool compactMode_ = false;
    // 命令按钮（按创建顺序）：助记符与无障碍名都靠它统一维护。
    QVector<QToolButton*> commandButtons_;
    // 已经用过的助记键：Alt+同一键对应多个按钮会让用户按到一半不知道按的是谁，
    // 所以助记键全 Ribbon 唯一。
    QSet<QChar> usedMnemonics_;
};

}  // namespace aipdf::app
