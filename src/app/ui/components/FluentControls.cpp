#include "app/ui/components/FluentControls.h"

#include "app/ui/FluentTokens.h"

#include <QAbstractItemView>
#include <QListView>
#include <QStyle>

namespace aipdf::app {
namespace {

int controlHeight(FluentControlSize size) {
    using fluent::MetricRole;
    switch (size) {
        case FluentControlSize::Small: return fluent::metric(MetricRole::ControlSmall);
        case FluentControlSize::Large: return fluent::metric(MetricRole::ControlLarge);
        case FluentControlSize::Medium: break;
    }
    return fluent::metric(MetricRole::ControlMedium);
}

const char* sizeName(FluentControlSize size) {
    switch (size) {
        case FluentControlSize::Small: return "small";
        case FluentControlSize::Large: return "large";
        case FluentControlSize::Medium: break;
    }
    return "medium";
}

void refreshDynamicStyle(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

}  // namespace

FluentButton::FluentButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
    setProperty("fluentControl", true);
    refreshProperties();
}

void FluentButton::setAppearance(FluentButtonAppearance appearance) {
    if (appearance_ == appearance) return;
    appearance_ = appearance;
    refreshProperties();
}

void FluentButton::setControlSize(FluentControlSize size) {
    if (size_ == size) return;
    size_ = size;
    refreshProperties();
}

void FluentButton::refreshProperties() {
    const char* appearance = "secondary";
    switch (appearance_) {
        case FluentButtonAppearance::Primary: appearance = "primary"; break;
        case FluentButtonAppearance::Subtle: appearance = "subtle"; break;
        case FluentButtonAppearance::Danger: appearance = "danger"; break;
        case FluentButtonAppearance::Secondary: break;
    }
    setProperty("fluentAppearance", appearance);
    setProperty("fluentSize", sizeName(size_));
    setFixedHeight(controlHeight(size_));
    refreshDynamicStyle(this);
}

FluentLineEdit::FluentLineEdit(QWidget* parent) : QLineEdit(parent) {
    setProperty("fluentControl", true);
    setControlSize(FluentControlSize::Medium);
}

void FluentLineEdit::setControlSize(FluentControlSize size) {
    size_ = size;
    setProperty("fluentSize", sizeName(size_));
    setFixedHeight(controlHeight(size_));
    refreshDynamicStyle(this);
}

FluentComboBox::FluentComboBox(QWidget* parent) : QComboBox(parent) {
    setProperty("fluentControl", true);
    auto* popup = new QListView(this);
    popup->setObjectName(QStringLiteral("fluentComboPopup"));
    popup->setProperty("fluentPopup", true);
    popup->setUniformItemSizes(true);
    popup->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setView(popup);
    setVisibleItemLimit(9);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setControlSize(FluentControlSize::Medium);
}

void FluentComboBox::setControlSize(FluentControlSize size) {
    size_ = size;
    setProperty("fluentSize", sizeName(size_));
    setFixedHeight(controlHeight(size_));
    refreshDynamicStyle(this);
}

void FluentComboBox::setPopupMinimumWidth(int width) {
    if (view() != nullptr) view()->setMinimumWidth(width);
}

int FluentComboBox::popupMinimumWidth() const {
    return view() != nullptr ? view()->minimumWidth() : 0;
}

void FluentComboBox::setVisibleItemLimit(int count) {
    setMaxVisibleItems(qMax(1, count));
}

}  // namespace aipdf::app
