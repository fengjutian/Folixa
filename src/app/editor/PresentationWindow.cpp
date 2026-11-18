#include "app/PresentationWindow.h"

#include "app/DocumentOutput.h"
#include "app/Language.h"
#include "app/PresentationLoop.h"
#include "app/ViewFit.h"
#include "i18n/Translate.h"
#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <QCloseEvent>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>

namespace aipdf::app {

namespace {

// 验收探针用的轨迹文件（每个用到它的 .cpp 各留一份，保持零依赖）。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + message + "\n")
                       .toUtf8());
    }
}

}  // namespace

PresentationWindow::PresentationWindow(pdf::PdfDocument document, QWidget* parent)
    : QWidget(parent), document_(std::move(document)) {
    texts_.bind([this] { setWindowTitle(presentationTitle()); });
    setWindowFlag(Qt::Window, true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background: #111111;"));

    // 放映期间也可以切换语言：标题跟着新语言走，页码与画面不受影响。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &PresentationWindow::retranslateUi);

    // 放映要看到批注：走和打印/导出同一条路（先补 /AP 再渲染）。
    QString warning;
    const pdf::PdfDocument prepared = documentForOutput(document_, &warning);
    if (prepared.isValid()) {
        document_ = prepared;
    }
    pageCount_ = document_.isValid() ? document_.pageCount() : 0;
    page_ = 0;
}

void PresentationWindow::retranslateUi() {
    texts_.retranslate();
}

QString PresentationWindow::presentationTitle() const {
    // 循环打开时标题要让人看得出来：否则用户以为放映停在最后一页了，
    // 还以为程序没反应。关着时沿用窗口自己的那条「放映」。
    if (loop_) return presentationLoopTitle();
    return tr("放映");
}

void PresentationWindow::setLoop(bool enabled) {
    if (loop_ == enabled) return;
    loop_ = enabled;
    // 标题里的「（循环）」要跟着变。
    retranslateUi();
}

void PresentationWindow::start() {
    trace(QStringLiteral("presentation open pages=%1").arg(pageCount_));
    showFullScreen();
    setFocus();
    renderCurrent();
    update();
}

void PresentationWindow::step(int delta) {
    // 翻页规则放在纯函数里：关掉循环时「到头就停」的行为与从前逐字一致，
    // 打开循环时越界绕回另一头。
    const int next = nextPresentationPage(page_, pageCount_, delta, loop_);
    if (next == page_) {
        return;  // 到头就停住，不环绕
    }
    page_ = next;
    trace(QStringLiteral("presentation page=%1").arg(page_ + 1));
    renderCurrent();
    update();
}

void PresentationWindow::renderCurrent() {
    image_ = QImage();
    if (!document_.isValid() || pageCount_ == 0) {
        return;
    }
    const auto page = document_.page(page_);
    if (!page.has_value()) {
        return;
    }
    const double pageWidth = page->displayWidthPoints();
    const double pageHeight = page->displayHeightPoints();
    if (pageWidth <= 0.0 || pageHeight <= 0.0) {
        return;
    }
    // 用统一的适配计算：它同时管住"铺满屏幕"和"别超过适配层的像素预算"。
    const FitResult fit = fitPageToViewport(QSizeF(pageWidth, pageHeight), size());
    image_ = page->render(fit.zoom);
}

void PresentationWindow::closeEvent(QCloseEvent* event) {
    // 验收脚本用 trace 判断"放映确实退出过"，而不是只看窗口消失。
    trace(QStringLiteral("presentation closed page=%1 of %2").arg(page_ + 1).arg(pageCount_));
    QWidget::closeEvent(event);
}

void PresentationWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    renderCurrent();
    update();
}

void PresentationWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x11, 0x11, 0x11));
    if (image_.isNull()) {
        return;
    }
    // 居中，按当前窗口尺寸等比摆放。
    const double scale = std::min(static_cast<double>(width()) / image_.width(),
                                  static_cast<double>(height()) / image_.height());
    const QSizeF target(image_.width() * scale, image_.height() * scale);
    const QRectF box((width() - target.width()) / 2.0, (height() - target.height()) / 2.0,
                     target.width(), target.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(box, image_);
}

void PresentationWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            close();
            return;
        case Qt::Key_Right:
        case Qt::Key_Down:
        case Qt::Key_PageDown:
        case Qt::Key_Space:
            step(1);
            return;
        case Qt::Key_Left:
        case Qt::Key_Up:
        case Qt::Key_PageUp:
        case Qt::Key_Backspace:
            step(-1);
            return;
        case Qt::Key_Home:
            page_ = 0;
            renderCurrent();
            update();
            return;
        case Qt::Key_End:
            page_ = std::max(0, pageCount_ - 1);
            renderCurrent();
            update();
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace aipdf::app
