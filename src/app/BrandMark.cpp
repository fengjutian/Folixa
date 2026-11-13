#include "app/BrandMark.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace aipdf::app {

BrandMark::BrandMark(int size, QWidget* parent) : QWidget(parent), size_(size) {
    setFixedSize(size_, size_);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

QSize BrandMark::sizeHint() const {
    return {size_, size_};
}

void BrandMark::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = std::min(width(), height());
    const QRectF tile(0, 0, s, s);
    const qreal radius = s * 0.29;

    // 底色：Prism Blue → Intelligence Violet 的对角渐变。
    QLinearGradient base(tile.topLeft(), tile.bottomRight());
    base.setColorAt(0.0, QColor(0x5B, 0x8D, 0xEF));
    base.setColorAt(0.55, QColor(0x62, 0x7C, 0xE8));
    base.setColorAt(1.0, QColor(0x7C, 0x63, 0xDC));
    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawRoundedRect(tile, radius, radius);

    // 顶部一层很轻的高光，避免纯色块看起来像占位图。
    QLinearGradient sheen(tile.topLeft(), tile.bottomLeft());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 60));
    sheen.setColorAt(0.6, QColor(255, 255, 255, 0));
    painter.setBrush(sheen);
    painter.drawRoundedRect(tile, radius, radius);

    // 一页纸。
    const qreal pageWidth = s * 0.30;
    const qreal pageHeight = s * 0.40;
    const QRectF page(s * 0.5 - pageWidth * 0.60, s * 0.5 - pageHeight * 0.50, pageWidth,
                      pageHeight);
    painter.setBrush(QColor(255, 255, 255, 243));
    painter.drawRoundedRect(page, s * 0.05, s * 0.05);

    QPen line(QColor(0x54, 0x6F, 0xCE, 165));
    line.setWidthF(std::max(1.0, s * 0.032));
    line.setCapStyle(Qt::RoundCap);
    painter.setPen(line);
    const qreal inset = pageWidth * 0.18;
    for (int i = 0; i < 3; ++i) {
        const qreal y = page.top() + pageHeight * (0.30 + 0.20 * i);
        const qreal right = page.right() - inset - (i == 2 ? pageWidth * 0.26 : 0.0);
        painter.drawLine(QPointF(page.left() + inset, y), QPointF(right, y));
    }

    // 纸角的一点棱光。
    const QPointF sparkle(s * 0.705, s * 0.315);
    const qreal r = s * 0.135;
    QPainterPath star;
    star.moveTo(sparkle.x(), sparkle.y() - r);
    star.quadTo(sparkle.x() + r * 0.18, sparkle.y() - r * 0.18, sparkle.x() + r, sparkle.y());
    star.quadTo(sparkle.x() + r * 0.18, sparkle.y() + r * 0.18, sparkle.x(), sparkle.y() + r);
    star.quadTo(sparkle.x() - r * 0.18, sparkle.y() + r * 0.18, sparkle.x() - r, sparkle.y());
    star.quadTo(sparkle.x() - r * 0.18, sparkle.y() - r * 0.18, sparkle.x(), sparkle.y() - r);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawPath(star);
}

}  // namespace aipdf::app
