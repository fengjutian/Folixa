#include "app/RibbonBar.h"

#include <QHBoxLayout>
#include <QLabel>
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
                         QColor(0x7C, 0x3A, 0xED));
    }
};

// 命令按钮的统一样式：图标在上、文字在下，和福昕 Ribbon 一致。
void styleCommand(QToolButton* button) {
    button->setObjectName(button->objectName().isEmpty()
                              ? QStringLiteral("ribbonCommand")
                              : button->objectName());
    button->setProperty("ribbonCommand", true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(28, 28));
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumWidth(58);
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
    tabs_->setFixedHeight(36);
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
    page->setAutoFillBackground(false);
    scroll->viewport()->setBackgroundRole(QPalette::Base);
    QPalette viewportPalette = scroll->viewport()->palette();
    viewportPalette.setColor(QPalette::Base, QColor(0xFF, 0xFF, 0xFF));
    viewportPalette.setColor(QPalette::Window, QColor(0xFF, 0xFF, 0xFF));
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
    frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* groupLayout = new QVBoxLayout(frame);
    groupLayout->setContentsMargins(7, 1, 7, 0);
    groupLayout->setSpacing(0);

    auto* content = new QWidget(frame);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(2);
    groupLayout->addWidget(content, 1);

    auto* label = new QLabel(title, frame);
    label->setObjectName(QStringLiteral("ribbonGroupTitle"));
    label->setAlignment(Qt::AlignHCenter);
    groupLayout->addWidget(label);

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
    setFixedHeight(compact ? tabs_->height() : 112);
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
