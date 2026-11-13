#include "app/WelcomePage.h"

#include "app/BrandMark.h"

#include <QEnterEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace aipdf::app {
namespace {

// 欢迎页的内容列宽：再宽，一行说明文字就会长得不像句子。
constexpr int kColumnWidth = 720;

// 文件路径可能很长，直接交给 QLabel 会被硬切成半个字。
// 这里在每次改宽度时按中间省略重算，只影响这一处，不改全局行为。
class ElidedLabel final : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void setFullText(const QString& text) {
        full_ = text;
        setToolTip(text);
        relayout();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        relayout();
    }

private:
    void relayout() {
        if (full_.isEmpty()) {
            return;
        }
        const int available = width();
        if (available < 24) {
            return;
        }
        setText(fontMetrics().elidedText(full_, Qt::ElideMiddle, available));
    }

    QString full_;
};

// 拖放区：虚线卡片，整块可点。
//
// 用自绘而不是 QSS 虚线边框：QSS 的 dashed 在圆角上会断成一截一截，
// 而且 hover 时没法同时改变图标底色。这里一次画完，状态切换只要 update。
class DropZone final : public QWidget {
public:
    explicit DropZone(QWidget* parent = nullptr) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }

    std::function<void()> onClick;

    void setHovered(bool hovered) {
        if (hovered_ == hovered) {
            return;
        }
        hovered_ = hovered;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal radius = 20.0;

        const QColor fill = hovered_ ? QColor(0xF2, 0xF7, 0xFF) : QColor(255, 255, 255, 224);
        painter.setBrush(fill);
        QPen border(hovered_ ? QColor(0x52, 0x77, 0xE8) : QColor(0xC8, 0xD4, 0xE8));
        border.setWidthF(2.0);
        border.setStyle(Qt::DashLine);
        border.setDashPattern({5.0, 4.0});
        painter.setPen(border);
        painter.drawRoundedRect(box, radius, radius);

        // 顶部的上传图标：浅色圆底 + 箭头。
        const QRectF badge(box.center().x() - 22, box.top() + 26, 44, 44);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hovered_ ? QColor(0xE1, 0xEA, 0xFF) : QColor(0xEF, 0xF3, 0xFF));
        painter.drawEllipse(badge);

        QPen arrow(hovered_ ? QColor(0x35, 0x55, 0xB7) : QColor(0x52, 0x77, 0xE8));
        arrow.setWidthF(2.0);
        arrow.setCapStyle(Qt::RoundCap);
        arrow.setJoinStyle(Qt::RoundJoin);
        painter.setPen(arrow);
        painter.setBrush(Qt::NoBrush);
        const QPointF tip(badge.center().x(), badge.center().y() - 8);
        const QPointF foot(badge.center().x(), badge.center().y() + 7);
        painter.drawLine(tip, foot);
        painter.drawLine(tip, tip + QPointF(-5, 5));
        painter.drawLine(tip, tip + QPointF(5, 5));
        painter.drawLine(foot + QPointF(-9, 0), foot + QPointF(9, 0));
    }

    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        setHovered(true);
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        setHovered(false);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClick) {
            onClick();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    bool hovered_ = false;
};

}  // namespace

WelcomePage::WelcomePage(const QStringList& recentFiles, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("welcomePage"));
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("welcomeScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 视图口必须透出下面的渐变。不要给 viewport 设样式表：Qt 会把控件级样式表
    // 原样传播给全部子控件，一个 `background: transparent` 会把页面里所有按钮的
    // 底色一起清掉（主按钮变成只有描边的空框）。这里只关掉自动填背景。
    scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);

    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("welcomeContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(48, 40, 48, 30);
    contentLayout->addStretch(1);

    // 内容列：最大 720px，窗口变窄时自动收缩，横向居中。
    auto* column = new QWidget(content);
    column->setObjectName(QStringLiteral("welcomeColumn"));
    column->setMaximumWidth(kColumnWidth);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);
    row->addWidget(column, 10);
    row->addStretch(1);
    contentLayout->addLayout(row);
    contentLayout->addStretch(1);

    auto* mark = new BrandMark(60, column);
    columnLayout->addWidget(mark, 0, Qt::AlignHCenter);
    columnLayout->addSpacing(18);

    auto* wordmark = new QLabel(QStringLiteral("Folixa"), column);
    wordmark->setObjectName(QStringLiteral("welcomeWordmark"));
    wordmark->setAlignment(Qt::AlignCenter);
    columnLayout->addWidget(wordmark);
    columnLayout->addSpacing(6);

    auto* eyebrow = new QLabel(QStringLiteral("AI PDF EDITOR"), column);
    eyebrow->setObjectName(QStringLiteral("welcomeEyebrow"));
    eyebrow->setAlignment(Qt::AlignCenter);
    QFont eyebrowFont = eyebrow->font();
    eyebrowFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    eyebrow->setFont(eyebrowFont);
    columnLayout->addWidget(eyebrow);
    columnLayout->addSpacing(26);

    auto* tagline = new QLabel(tr("专注阅读，也为文档智能留出恰到好处的空间"), column);
    tagline->setObjectName(QStringLiteral("welcomeTagline"));
    tagline->setAlignment(Qt::AlignCenter);
    tagline->setWordWrap(true);
    columnLayout->addWidget(tagline);
    columnLayout->addSpacing(30);

    auto* dropZone = new DropZone(column);
    dropZone->setObjectName(QStringLiteral("dropZone"));
    dropZone->setFixedHeight(196);
    dropZone->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    dropZone->onClick = [this] { emit openPdfRequested(); };
    {
        auto* zoneLayout = new QVBoxLayout(dropZone);
        zoneLayout->setContentsMargins(24, 92, 24, 26);
        zoneLayout->setSpacing(0);
        auto* title = new QLabel(tr("把 PDF 拖到这里"), dropZone);
        title->setObjectName(QStringLiteral("dropZoneTitle"));
        title->setAlignment(Qt::AlignCenter);
        zoneLayout->addWidget(title);
        zoneLayout->addSpacing(4);
        auto* hint = new QLabel(tr("也可以点击下方按钮从本机选择"), dropZone);
        hint->setObjectName(QStringLiteral("dropZoneHint"));
        hint->setAlignment(Qt::AlignCenter);
        zoneLayout->addWidget(hint);
        zoneLayout->addSpacing(14);
        auto* openButton = new QPushButton(tr("打开 PDF"), dropZone);
        openButton->setObjectName(QStringLiteral("primaryButton"));
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setMinimumSize(148, 42);
        connect(openButton, &QPushButton::clicked, this, &WelcomePage::openPdfRequested);
        zoneLayout->addWidget(openButton, 0, Qt::AlignHCenter);
    }
    columnLayout->addWidget(dropZone);
    columnLayout->addSpacing(34);

    auto* recentHeader = new QHBoxLayout();
    recentHeader->setContentsMargins(4, 0, 4, 0);
    auto* recentTitle = new QLabel(tr("最近打开"), column);
    recentTitle->setObjectName(QStringLiteral("welcomeSectionTitle"));
    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch(1);
    if (!recentFiles.isEmpty()) {
        auto* recentCount = new QLabel(tr("%1 份文档").arg(recentFiles.size()), column);
        recentCount->setObjectName(QStringLiteral("welcomeSectionMeta"));
        recentHeader->addWidget(recentCount);
    }
    columnLayout->addLayout(recentHeader);
    columnLayout->addSpacing(10);
    columnLayout->addWidget(createRecentCard(recentFiles));
    columnLayout->addSpacing(24);

    auto* footnote = new QLabel(tr("Ctrl+O 打开  ·  Ctrl+F 查找  ·  支持多选与拖放"), column);
    footnote->setObjectName(QStringLiteral("welcomeFootnote"));
    footnote->setAlignment(Qt::AlignCenter);
    columnLayout->addWidget(footnote);

    scroll->setWidget(content);
}

QWidget* WelcomePage::createRecentCard(const QStringList& files) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("recentCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (files.isEmpty()) {
        auto* empty = new QWidget(card);
        auto* emptyLayout = new QVBoxLayout(empty);
        emptyLayout->setContentsMargins(24, 22, 24, 22);
        emptyLayout->setSpacing(3);
        auto* title = new QLabel(tr("暂无最近文档"), empty);
        title->setObjectName(QStringLiteral("recentEmptyTitle"));
        title->setAlignment(Qt::AlignCenter);
        auto* hint = new QLabel(tr("打开过的 PDF 会列在这里，方便下次接着读"), empty);
        hint->setObjectName(QStringLiteral("recentEmptyHint"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        emptyLayout->addWidget(title);
        emptyLayout->addWidget(hint);
        layout->addWidget(empty);
        return card;
    }

    for (int i = 0; i < files.size(); ++i) {
        layout->addWidget(createRecentRow(files.at(i), false));
        if (i != files.size() - 1) {
            auto* separator = new QFrame(card);
            separator->setObjectName(QStringLiteral("recentSeparator"));
            separator->setFixedHeight(1);
            layout->addWidget(separator);
        }
    }
    return card;
}

QWidget* WelcomePage::createRecentRow(const QString& path, bool isLast) {
    Q_UNUSED(isLast);
    const QFileInfo info(path);

    auto* row = new QPushButton(this);
    row->setObjectName(QStringLiteral("recentRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setToolTip(path);
    row->setMinimumHeight(58);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 9, 14, 9);
    layout->setSpacing(12);

    auto* badge = new QLabel(QStringLiteral("PDF"), row);
    badge->setObjectName(QStringLiteral("pdfBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(36, 36);
    layout->addWidget(badge);

    auto* texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    auto* name = new ElidedLabel(row);
    name->setObjectName(QStringLiteral("recentName"));
    name->setFullText(info.fileName());
    auto* location = new ElidedLabel(row);
    location->setObjectName(QStringLiteral("recentPath"));
    location->setFullText(info.absolutePath());
    texts->addWidget(name);
    texts->addWidget(location);
    layout->addLayout(texts, 1);

    auto* chevron = new QLabel(QStringLiteral("›"), row);
    chevron->setObjectName(QStringLiteral("recentChevron"));
    chevron->setAlignment(Qt::AlignCenter);
    layout->addWidget(chevron);

    // 行内的标签不接鼠标事件，点哪儿都算点这一行。
    const auto children = row->findChildren<QWidget*>();
    for (QWidget* child : children) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    connect(row, &QPushButton::clicked, this, [this, path] { emit recentFileRequested(path); });
    return row;
}

void WelcomePage::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = rect();

    QLinearGradient base(area.topLeft(), area.bottomLeft());
    base.setColorAt(0.0, QColor(0xFE, 0xFE, 0xFF));
    base.setColorAt(0.55, QColor(0xF8, 0xFA, 0xFD));
    base.setColorAt(1.0, QColor(0xF1, 0xF4, 0xFA));
    painter.fillRect(area, base);

    // 两团极淡的棱光：上面偏蓝，右下偏紫。不做动画，不抢文字。
    QRadialGradient topGlow(QPointF(area.width() * 0.5, area.top() - area.height() * 0.12),
                            area.width() * 0.55);
    topGlow.setColorAt(0.0, QColor(0x5B, 0x8D, 0xEF, 20));
    topGlow.setColorAt(1.0, QColor(0x5B, 0x8D, 0xEF, 0));
    painter.fillRect(area, topGlow);

    QRadialGradient violetGlow(
        QPointF(area.right() + area.width() * 0.08, area.bottom() + area.height() * 0.14),
        area.width() * 0.5);
    violetGlow.setColorAt(0.0, QColor(0x7C, 0x63, 0xDC, 14));
    violetGlow.setColorAt(1.0, QColor(0x7C, 0x63, 0xDC, 0));
    painter.fillRect(area, violetGlow);
}

}  // namespace aipdf::app
