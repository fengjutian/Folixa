#include "app/RibbonBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 命令按钮的统一样式：图标在上、文字在下，和福昕 Ribbon 一致。
void styleCommand(QToolButton* button) {
    button->setObjectName(button->objectName().isEmpty()
                              ? QStringLiteral("ribbonCommand")
                              : button->objectName());
    button->setProperty("ribbonCommand", true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(16, 16));
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumWidth(58);
    button->setStyleSheet(QString());
}

}  // namespace

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbonBar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("ribbonTabs"));
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    tabs_->setMovable(false);
    // 固定标签条高度：否则 QVBoxLayout 会把多余高度分给它，命令区被压成一条缝。
    tabs_->setFixedHeight(34);
    layout->addWidget(tabs_);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("ribbonPages"));
    layout->addWidget(pages_, 1);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        pages_->setCurrentIndex(index);
        emit tabChanged(index);
    });
}

int RibbonBar::appendPage() {
    auto* page = new QWidget(pages_);
    auto* pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(10, 2, 10, 2);
    pageLayout->setSpacing(0);
    // 末尾的弹簧吸收多余宽度，命令组保持自然宽度。
    pageLayout->addStretch(1);
    pages_->addWidget(page);
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
    groupLayout->setContentsMargins(8, 2, 8, 0);
    groupLayout->setSpacing(0);

    auto* content = new QWidget(frame);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(4);
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

}  // namespace aipdf::app
