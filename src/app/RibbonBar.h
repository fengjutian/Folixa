#pragma once

#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
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
    // 按标题重排标签与对应内容页；未列出的标签保持在末尾。
    void setTabOrder(const QStringList& titles);

    // Ribbon 的内容宽度由所有标签页里最宽的一页决定；如果直接把它当最小宽度，
    // 主窗口会被撑得比屏幕还宽，PDF 画布也跟着被挤到屏幕外。
    // 这里只上报一个紧凑的宽度，内容超出时由标签页自己裁切。
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void tabChanged(int index);

private:
    int appendPage();
    QTabBar* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    // QStackedWidget 的直接子项是滚动容器，不是 pageWidgets_ 中的内容页。
    // 重排时必须移动容器，否则内容页会被错误地从 QScrollArea 中拆走。
    QVector<QWidget*> pageContainers_;
    QVector<QWidget*> pageWidgets_;
    QVector<QHBoxLayout*> pageLayouts_;
};

}  // namespace aipdf::app
