#pragma once

#include <QWidget>

namespace aipdf::app {

// Folixa 的品牌标记：渐变圆角方块里的一页纸 + 一点棱光。
//
// 用自绘而不是 SVG：同一个标记要出现在 60px 的欢迎页和 24px 的工具栏里，
// 自绘能按尺寸调整线宽与留白，缩到 24px 也不会糊成一团。
class BrandMark final : public QWidget {
    Q_OBJECT

public:
    explicit BrandMark(int size, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int size_ = 24;
};

}  // namespace aipdf::app
