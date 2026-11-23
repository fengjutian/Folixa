#include "app/shell/WelcomePage.h"

#include "app/ui/Language.h"

#include "i18n/Translate.h"

#include <QEnterEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
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
constexpr int kColumnWidth = 1120;

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
        updateGeometry();
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
        const qreal radius = 14.0;

        const QColor fill = hovered_ ? QColor(0xF5, 0xF8, 0xFF) : QColor(255, 255, 255);
        painter.setBrush(fill);
        QPen border(hovered_ ? QColor(0x41, 0x69, 0xC1) : QColor(0xE4, 0xE4, 0xE7));
        border.setWidthF(1.0);
        painter.setPen(border);
        painter.drawRoundedRect(box, radius, radius);

        // 顶部的上传图标：浅色圆底 + 箭头。
        const QRectF badge(box.center().x() - 22, box.top() + 26, 44, 44);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hovered_ ? QColor(0xE5, 0xED, 0xFC) : QColor(0xF0, 0xF4, 0xFC));
        painter.drawEllipse(badge);

        QPen arrow(hovered_ ? QColor(0x31, 0x59, 0xB8) : QColor(0x41, 0x69, 0xC1));
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
    contentLayout->setContentsMargins(48, 42, 48, 30);
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

    auto* brandRow = new QHBoxLayout();
    brandRow->setContentsMargins(0, 0, 0, 0);
    brandRow->setSpacing(14);
    auto* mark = new QLabel(column);
    mark->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(QSize(52, 52)));
    mark->setFixedSize(52, 52);
    brandRow->addWidget(mark, 0, Qt::AlignVCenter);
    auto* brandText = new QVBoxLayout();
    brandText->setContentsMargins(0, 0, 0, 0);
    brandText->setSpacing(0);
    auto* wordmark = new QLabel(QStringLiteral("Folixa"), column);
    wordmark->setObjectName(QStringLiteral("welcomeWordmark"));
    brandText->addWidget(wordmark);

    auto* eyebrow = new QLabel(QStringLiteral("AI PDF EDITOR"), column);
    eyebrow->setObjectName(QStringLiteral("welcomeEyebrow"));
    QFont eyebrowFont = eyebrow->font();
    eyebrowFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    eyebrow->setFont(eyebrowFont);
    brandText->addWidget(eyebrow);
    brandRow->addLayout(brandText);
    brandRow->addStretch(1);
    columnLayout->addLayout(brandRow);
    columnLayout->addSpacing(18);

    auto* tagline = new QLabel(QString(), column);
    tagline->setObjectName(QStringLiteral("welcomeTagline"));
    tagline->setAlignment(Qt::AlignLeft);
    tagline->setWordWrap(true);
    texts_.bind(
        [this, tagline] { tagline->setText(tr("专注阅读，也为文档智能留出恰到好处的空间")); });
    columnLayout->addWidget(tagline);
    columnLayout->addSpacing(24);

    auto* workspace = new QHBoxLayout();
    workspace->setContentsMargins(0, 0, 0, 0);
    workspace->setSpacing(24);
    auto* openPanel = new QWidget(column);
    auto* openLayout = new QVBoxLayout(openPanel);
    openLayout->setContentsMargins(0, 0, 0, 0);
    auto* recentPanel = new QWidget(column);
    auto* recentLayout = new QVBoxLayout(recentPanel);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(0);

    auto* dropZone = new DropZone(column);
    dropZone->setObjectName(QStringLiteral("dropZone"));
    dropZone->setFixedHeight(286);
    dropZone->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    dropZone->onClick = [this] { emit openPdfRequested(); };
    {
        auto* zoneLayout = new QVBoxLayout(dropZone);
        zoneLayout->setContentsMargins(24, 80, 24, 20);
        zoneLayout->setSpacing(0);
        auto* title = new QLabel(QString(), dropZone);
        title->setObjectName(QStringLiteral("dropZoneTitle"));
        title->setAlignment(Qt::AlignCenter);
        zoneLayout->addWidget(title);
        zoneLayout->addSpacing(4);
        auto* hint = new QLabel(QString(), dropZone);
        hint->setObjectName(QStringLiteral("dropZoneHint"));
        hint->setAlignment(Qt::AlignCenter);
        zoneLayout->addWidget(hint);
        zoneLayout->addSpacing(14);
        auto* openButton = new QPushButton(QString(), dropZone);
        openButton->setObjectName(QStringLiteral("primaryButton"));
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setMinimumSize(148, 42);
        connect(openButton, &QPushButton::clicked, this, &WelcomePage::openPdfRequested);
        zoneLayout->addWidget(openButton, 0, Qt::AlignHCenter);
        texts_.bind([this, title, hint, openButton] {
            title->setText(tr("把 PDF 拖到这里"));
            hint->setText(tr("也可以点击下方按钮从本机选择"));
            openButton->setText(tr("打开 PDF"));
        });
    }
    openLayout->addWidget(dropZone);
    workspace->addWidget(openPanel, 5);

    auto* recentHeader = new QHBoxLayout();
    recentHeader->setContentsMargins(4, 0, 4, 0);
    auto* recentTitle = new QLabel(QString(), column);
    recentTitle->setObjectName(QStringLiteral("welcomeSectionTitle"));
    texts_.bind([this, recentTitle] { recentTitle->setText(tr("最近打开")); });
    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch(1);
    // 计数不登记到 texts_：它跟着列表走，由 rebuildRecentCard() 直接重写，
    // 登记进来只会在列表重建后留一个指向已删控件的悬空 lambda。
    recentCount_ = new QLabel(QString(), column);
    recentCount_->setObjectName(QStringLiteral("welcomeSectionMeta"));
    recentHeader->addWidget(recentCount_);
    recentLayout->addLayout(recentHeader);
    recentLayout->addSpacing(8);
    // 列表本体放在固定容器里：新文件进来只换容器内容，标题、间距和
    // 下面的弹性空白都不动，页面的滚动位置也就不会跳。
    recentCardHost_ = new QWidget(column);
    recentCardHost_->setObjectName(QStringLiteral("recentCardHost"));
    auto* recentCardLayout = new QVBoxLayout(recentCardHost_);
    recentCardLayout->setContentsMargins(0, 0, 0, 0);
    recentCardLayout->setSpacing(0);
    recentLayout->addWidget(recentCardHost_);
    recentLayout->addStretch(1);
    recentLayout_ = recentLayout;
    workspace->addWidget(recentPanel, 6);
    columnLayout->addLayout(workspace);
    columnLayout->addSpacing(18);

    auto* footnote = new QLabel(QString(), column);
    footnote->setObjectName(QStringLiteral("welcomeFootnote"));
    footnote->setAlignment(Qt::AlignCenter);
    texts_.bind(
        [this, footnote] { footnote->setText(tr("Ctrl+O 打开  ·  Ctrl+F 查找  ·  支持多选与拖放")); });
    columnLayout->addWidget(footnote);

    scroll->setWidget(content);

    // 首次铺列表：之后每次打开文档由 setRecentFiles() 换内容。
    setRecentFiles(recentFiles);

    // 切换语言后登记过的常驻文案就地重译；列表行是建行时写死的，随重建一起换语言。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &WelcomePage::retranslateUi);
}

void WelcomePage::setRecentFiles(const QStringList& files) {
    if (files == recentFiles_ && recentCardHost_ != nullptr && recentCardHost_->layout() != nullptr &&
        recentCardHost_->layout()->count() > 0) {
        // 同样的六份文档被反复通知（每打开一次都要过一遍）：不重建，省一次闪烁。
        return;
    }
    recentFiles_ = files;
    rebuildRecentCard();
}

void WelcomePage::rebuildRecentCard() {
    if (recentCardHost_ == nullptr || recentCardHost_->layout() == nullptr) {
        return;
    }
    auto* hostLayout = recentCardHost_->layout();
    // 旧卡片挂到 deleteLater：立刻 delete 会踩到正在分发的点击/布局事件。
    while (QLayoutItem* item = hostLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    hostLayout->addWidget(createRecentCard(recentFiles_));

    if (recentCount_ != nullptr) {
        recentCount_->setVisible(!recentFiles_.isEmpty());
        recentCount_->setText(tr("%1 份文档").arg(recentFiles_.size()));
    }
}

QWidget* WelcomePage::createRecentCard(const QStringList& files) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("recentCard"));
    // 和拖放区同宽：默认卡片按 sizeHint 缩到内容宽度，右边会多出一条缝。
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (files.isEmpty()) {
        auto* empty = new QWidget(card);
        auto* emptyLayout = new QVBoxLayout(empty);
        emptyLayout->setContentsMargins(24, 22, 24, 22);
        emptyLayout->setSpacing(3);
        auto* title = new QLabel(QString(), empty);
        title->setObjectName(QStringLiteral("recentEmptyTitle"));
        title->setAlignment(Qt::AlignCenter);
        auto* hint = new QLabel(QString(), empty);
        hint->setObjectName(QStringLiteral("recentEmptyHint"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setWordWrap(true);
        // 不登记到 texts_：这张卡片会被 rebuildRecentCard() 整个换掉，
        // 登进去的 lambda 会指着已删的 label，切换语言时就是一次野指针调用。
        title->setText(tr("暂无最近文档"));
        hint->setText(tr("打开过的 PDF 会列在这里，方便下次接着读"));
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
    // 固定行高：行内标签的 sizeHint 依赖当前文本宽度，交给布局算会压成一条。
    row->setFixedHeight(58);

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

void WelcomePage::retranslateUi() {
    texts_.retranslate();
    // 列表行（含空状态）的文案在创建时写死，切语言只能重建。
    rebuildRecentCard();
}

void WelcomePage::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = rect();

    painter.fillRect(area, QColor(0xF7, 0xF7, 0xF8));
}

}  // namespace aipdf::app
