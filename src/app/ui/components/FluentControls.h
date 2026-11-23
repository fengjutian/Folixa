#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

namespace aipdf::app {

enum class FluentButtonAppearance {
    Secondary,
    Primary,
    Subtle,
    Danger,
};

enum class FluentControlSize {
    Small,
    Medium,
    Large,
};

class FluentButton final : public QPushButton {
public:
    explicit FluentButton(const QString& text = {}, QWidget* parent = nullptr);

    void setAppearance(FluentButtonAppearance appearance);
    FluentButtonAppearance appearance() const { return appearance_; }
    void setControlSize(FluentControlSize size);
    FluentControlSize controlSize() const { return size_; }

private:
    void refreshProperties();

    FluentButtonAppearance appearance_ = FluentButtonAppearance::Secondary;
    FluentControlSize size_ = FluentControlSize::Medium;
};

class FluentLineEdit final : public QLineEdit {
public:
    explicit FluentLineEdit(QWidget* parent = nullptr);

    void setControlSize(FluentControlSize size);
    FluentControlSize controlSize() const { return size_; }

private:
    FluentControlSize size_ = FluentControlSize::Medium;
};

class FluentComboBox final : public QComboBox {
public:
    explicit FluentComboBox(QWidget* parent = nullptr);

    void setControlSize(FluentControlSize size);
    FluentControlSize controlSize() const { return size_; }
    void setPopupMinimumWidth(int width);
    int popupMinimumWidth() const;
    void setVisibleItemLimit(int count);

private:
    FluentControlSize size_ = FluentControlSize::Medium;
};

}  // namespace aipdf::app
