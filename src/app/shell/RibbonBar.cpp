#include "app/shell/RibbonBar.h"
#include "app/ui/Theme.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QSizePolicy>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 选中标签的底部蓝条自己画。
//
// QSS 的 `::tab:selected { border-bottom: ... }` 在 QTabBar 上会被标签矩形裁掉，
// 实测只在文字颜色上生效，当前页看起来和别的页没区别。这里在标签矩形底部
// 补一条 2px 的指示条，宽度跟着文字收一点，和福昕的样式一致。
class RibbonTabBar final : public QTabBar {
public:
    explicit RibbonTabBar(QWidget* parent = nullptr) : QTabBar(parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QTabBar::paintEvent(event);
        const int index = currentIndex();
        if (index < 0) {
            return;
        }
        const QRect tab = tabRect(index);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(QRect(tab.left() + 12, tab.bottom() - 1, qMax(0, tab.width() - 24), 2),
                         QColor(0x41, 0x69, 0xC1));
    }
};

// 命令按钮的统一样式：图标在上、文字在下，和福昕 Ribbon 一致。
void styleCommand(QToolButton* button) {
    button->setObjectName(button->objectName().isEmpty()
                              ? QStringLiteral("ribbonCommand")
                              : button->objectName());
    button->setProperty("ribbonCommand", true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    // 稍小的图标把注意力留给文档，同时仍保持清晰的命令辨识度。
    button->setIconSize(QSize(22, 22));
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumWidth(50);
    button->setStyleSheet(QString());
}

}  // namespace

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbonBar"));
    // 普通 QWidget 子类默认不画 QSS 背景，白底会透出父窗口的灰。
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    tabs_ = new RibbonTabBar(this);
    tabs_->setObjectName(QStringLiteral("ribbonTabs"));
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    tabs_->setMovable(false);
    // 固定标签条高度：否则 QVBoxLayout 会把多余高度分给它，命令区被压成一条缝。
    tabs_->setFixedHeight(32);
    layout->addWidget(tabs_);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("ribbonPages"));
    // 每个标签页各自包一层横向滚动区，而不是把整个 pages_ 塞进一个滚动区。
    //
    // 后者的滚动范围由最宽的那一页（注释 / 页面管理）决定：停在「文件」页时
    // 也会常驻一条横向滚动条，白吃掉十几像素，把分组标题压成 0 高。
    // 拆开之后只有真的放不下的当前页才出现滚动条，也不参与主窗口最小宽度。
    layout->addWidget(pages_, 1);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        pages_->setCurrentIndex(index);
        emit tabChanged(index);
    });
}

int RibbonBar::appendPage() {
    auto* page = new QWidget;
    auto* pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(8, 2, 8, 2);
    pageLayout->setSpacing(0);
    // 末尾的弹簧吸收多余宽度，命令组保持自然宽度。
    pageLayout->addStretch(1);

    auto* scroll = new QScrollArea(pages_);
    scroll->setObjectName(QStringLiteral("ribbonPageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    // 滚动区的 viewport 默认按 QPalette::Base 自绘灰底，会盖住 Ribbon 的白底。
    // 必须在 setWidget() 之后再设：setWidget 会重置 viewport 的调色板。
    // 底色取当前主题的面板色：写死白色的话，暗色主题下这里会亮一块。
    page->setAutoFillBackground(false);
    scroll->viewport()->setBackgroundRole(QPalette::Base);
    QPalette viewportPalette = scroll->viewport()->palette();
    viewportPalette.setColor(QPalette::Base, theme::surfaceColor());
    viewportPalette.setColor(QPalette::Window, theme::surfaceColor());
    scroll->viewport()->setPalette(viewportPalette);
    scroll->viewport()->setAutoFillBackground(true);

    pages_->addWidget(scroll);
    pageContainers_.append(scroll);
    pageWidgets_.append(page);
    pageLayouts_.append(pageLayout);
    return pageWidgets_.size() - 1;
}

int RibbonBar::addTab(const QString& title) {
    const int index = appendPage();
    tabs_->addTab(title);
    if (tabs_->count() == 1) {
        tabs_->setCurrentIndex(0);
    }
    return index;
}

QHBoxLayout* RibbonBar::addGroup(int tabIndex, const QString& title) {
    if (tabIndex < 0 || tabIndex >= pageLayouts_.size()) {
        return nullptr;
    }
    auto* frame = new QFrame(pageWidgets_.at(tabIndex));
    frame->setObjectName(QStringLiteral("ribbonGroup"));
    frame->setProperty("ribbonGroup", true);
    // 分组名只保留给辅助功能，不再占用一整行显示空间。
    frame->setAccessibleName(title);
    frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* groupLayout = new QVBoxLayout(frame);
    groupLayout->setContentsMargins(7, 0, 7, 0);
    groupLayout->setSpacing(0);

    auto* content = new QWidget(frame);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(4);
    groupLayout->addWidget(content, 1);

    // 插到弹簧之前。
    pageLayouts_.at(tabIndex)->insertWidget(pageLayouts_.at(tabIndex)->count() - 1, frame);
    return contentLayout;
}

QToolButton* RibbonBar::addCommand(QHBoxLayout* group,
                                   const QString& text,
                                   const QIcon& icon,
                                   const QString& toolTip,
                                   bool enabled,
                                   const QString& objectName) {
    if (group == nullptr) {
        return nullptr;
    }
    auto* button = new QToolButton(group->parentWidget());
    button->setText(text);
    button->setIcon(icon);
    button->setToolTip(toolTip.isEmpty() ? text : toolTip);
    button->setEnabled(enabled);
    if (!objectName.isEmpty()) {
        button->setObjectName(objectName);
    }
    styleCommand(button);
    group->addWidget(button);
    // 无障碍名一开始就要有：屏幕阅读器读的是它，不是按钮文字。
    button->setAccessibleName(stripMnemonics(text));
    commandButtons_.append(button);
    applyMnemonicTo(button);
    return button;
}

QToolButton* RibbonBar::addLargeCommand(QHBoxLayout* group,
                                        const QString& text,
                                        const QIcon& icon,
                                        const QString& toolTip,
                                        bool enabled,
                                        const QString& objectName) {
    QToolButton* button = addCommand(group, text, icon, toolTip, enabled, objectName);
    if (button != nullptr) {
        button->setMinimumWidth(72);
    }
    return button;
}

// ------------------------------------------------------------- 无障碍与助记符 ----

QString RibbonBar::stripMnemonics(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch != QLatin1Char('&')) {
            result.append(ch);
            continue;
        }
        // "&&" 是字面量 '&'：保留一个 &，不当作助记符标记。
        if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('&')) {
            result.append(QLatin1Char('&'));
            ++index;
            continue;
        }
        // 单个 '&' 是助记符标记：标记本身不显示。
    }
    return result;
}

QChar RibbonBar::mnemonicKey(const QString& text) {
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) != QLatin1Char('&')) {
            continue;
        }
        if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('&')) {
            ++index;
            continue;
        }
        if (index + 1 < text.size()) {
            return text.at(index + 1);
        }
    }
    return QChar();
}

QString RibbonBar::insertMnemonic(const QString& text, QChar key) {
    if (key.isNull()) {
        return text;
    }
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) == key) {
            return text.left(index) + QLatin1Char('&') + text.mid(index);
        }
    }
    return text;
}

QVector<QToolButton*> RibbonBar::commandButtons() const {
    return commandButtons_;
}

void RibbonBar::applyMnemonicTo(QToolButton* button) {
    if (button == nullptr) {
        return;
    }
    const QString text = button->text();
    // 已经有显式助记符（"保存(&S)"、"&打开"）就不再自动加。
    if (!mnemonicKey(text).isNull()) {
        const QChar key = mnemonicKey(text);
        usedMnemonics_.insert(key.toLower());
        return;
    }
    // 自动挑一个还没被用过的助记键：
    // 先找 ASCII 字母（Alt+字母 真的按得出来），没有再退到第一个汉字。
    QChar chosen;
    for (int index = 0; index < text.size() && chosen.isNull(); ++index) {
        const QChar ch = text.at(index);
        if (ch.isLetter() && ch.unicode() < 128 && !usedMnemonics_.contains(ch.toLower())) {
            chosen = ch;
        }
    }
    if (chosen.isNull()) {
        // 没有 ASCII 字母：退到第一个还没用过的汉字。
        // Qt 允许 `&` 后面跟中文（显示时会把 & 去掉，Alt+该字仍可触发）。
        for (int index = 0; index < text.size() && chosen.isNull(); ++index) {
            const QChar ch = text.at(index);
            if (ch.unicode() >= 128 && ch.isLetter() && !usedMnemonics_.contains(ch)) {
                chosen = ch;
            }
        }
    }
    if (chosen.isNull()) {
        return;
    }
    usedMnemonics_.insert(chosen.toLower());
    button->setText(insertMnemonic(text, chosen));
}

void RibbonBar::refreshCommandLabels() {
    usedMnemonics_.clear();
    for (QToolButton* button : commandButtons_) {
        if (button == nullptr) {
            continue;
        }
        const QString current = button->text();
        const QChar existing = mnemonicKey(current);
        if (!existing.isNull()) {
            // 文字里已经有助记符（显式写的，或上一轮自动加的）：保留它。
            button->setAccessibleName(stripMnemonics(current));
            usedMnemonics_.insert(existing.toLower());
            continue;
        }
        // 没有助记符：挑一个还没被用过的插进去。
        const QString base = stripMnemonics(current);
        button->setText(base);
        button->setAccessibleName(base);
        applyMnemonicTo(button);
    }
}


QSize RibbonBar::sizeHint() const {
    const int height = QWidget::sizeHint().height();
    return QSize(640, height);
}

QSize RibbonBar::minimumSizeHint() const {
    return QSize(320, QWidget::sizeHint().height());
}

void RibbonBar::setCurrentTab(int index) {
    if (index >= 0 && index < tabs_->count()) {
        tabs_->setCurrentIndex(index);
    }
}

int RibbonBar::currentTab() const {
    return tabs_->currentIndex();
}

int RibbonBar::tabCount() const {
    return tabs_->count();
}

void RibbonBar::setCompactMode(bool compact) {
    if (compactMode_ == compact) {
        return;
    }
    compactMode_ = compact;
    pages_->setVisible(!compact);
    setFixedHeight(compact ? tabs_->height() : 90);
    updateGeometry();
}

void RibbonBar::setTabOrder(const QStringList& titles) {
    for (int target = 0; target < titles.size() && target < tabs_->count(); ++target) {
        int source = -1;
        for (int index = target; index < tabs_->count(); ++index) {
            if (tabs_->tabText(index) == titles.at(target)) {
                source = index;
                break;
            }
        }
        if (source < 0 || source == target) {
            continue;
        }
        tabs_->moveTab(source, target);
        QWidget* container = pageContainers_.takeAt(source);
        QWidget* page = pageWidgets_.takeAt(source);
        QHBoxLayout* pageLayout = pageLayouts_.takeAt(source);
        pageContainers_.insert(target, container);
        pageWidgets_.insert(target, page);
        pageLayouts_.insert(target, pageLayout);
        pages_->removeWidget(container);
        pages_->insertWidget(target, container);
    }
    pages_->setCurrentIndex(tabs_->currentIndex());
}

}  // namespace aipdf::app
