#include "app/dialogs/SafeDocumentViewerDialog.h"

#include "app/controllers/SafeDocumentController.h"
#include "i18n/Translate.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

// 可选的缩放倍率。
const double kZoomLevels[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0};

}  // namespace

SafeDocumentViewerDialog::SafeDocumentViewerDialog(const QString& path, QWidget* parent)
    : QDialog(parent), path_(path) {
    setObjectName(QStringLiteral("safeDocument.viewerDialog"));
    setWindowTitle(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "安全文档（隔离解析）"));
    resize(900, 1000);

    controller_ = new SafeDocumentController(this);

    buildUi();
    retranslateUi();
}

SafeDocumentViewerDialog::~SafeDocumentViewerDialog() = default;

void SafeDocumentViewerDialog::setWorkerExecutable(const QString& path) {
    if (controller_ != nullptr) {
        controller_->setWorkerExecutable(path);
    }
}

bool SafeDocumentViewerDialog::openDocument() {
    openAttempted_ = true;
    const QString password = passwordEdit_ != nullptr ? passwordEdit_->text() : QString();
    return attemptOpen(password);
}

bool SafeDocumentViewerDialog::documentOpened() const {
    return controller_ != nullptr && controller_->isOpen();
}

int SafeDocumentViewerDialog::currentPage() const {
    return currentPage_;
}

int SafeDocumentViewerDialog::pageCount() const {
    return pageCount_;
}

QString SafeDocumentViewerDialog::lastError() const {
    return lastError_;
}

QString SafeDocumentViewerDialog::bannerText() const {
    return banner_->text();
}

void SafeDocumentViewerDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // 第一次显示时才打开：父窗口可以在 show 之前把解析进程路径设好。
    if (!openAttempted_) {
        openDocument();
    }
}

void SafeDocumentViewerDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    banner_ = new QLabel(this);
    banner_->setObjectName(QStringLiteral("safeDocument.banner"));
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::PlainText);
    layout->addWidget(banner_);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("safeDocument.status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    // 密码重试行：只在打开失败时露出来。
    passwordRow_ = new QWidget(this);
    passwordRow_->setObjectName(QStringLiteral("safeDocument.passwordRow"));
    auto* passwordLayout = new QHBoxLayout(passwordRow_);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    passwordEdit_ = new QLineEdit(passwordRow_);
    passwordEdit_->setObjectName(QStringLiteral("safeDocument.password"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordLayout->addWidget(passwordEdit_, 1);
    retryButton_ = new QPushButton(passwordRow_);
    retryButton_->setObjectName(QStringLiteral("safeDocument.retry"));
    passwordLayout->addWidget(retryButton_);
    passwordRow_->setVisible(false);
    layout->addWidget(passwordRow_);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("safeDocument.scroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setAlignment(Qt::AlignCenter);
    pageImage_ = new QLabel(scroll_);
    pageImage_->setObjectName(QStringLiteral("safeDocument.pageImage"));
    pageImage_->setAlignment(Qt::AlignCenter);
    pageImage_->setBackgroundRole(QPalette::Base);
    scroll_->setWidget(pageImage_);
    layout->addWidget(scroll_, 1);

    auto* navigation = new QHBoxLayout;
    previous_ = new QPushButton(this);
    previous_->setObjectName(QStringLiteral("safeDocument.previous"));
    navigation->addWidget(previous_);
    pageBox_ = new QSpinBox(this);
    pageBox_->setObjectName(QStringLiteral("safeDocument.pageNumber"));
    pageBox_->setRange(1, 1);
    navigation->addWidget(pageBox_);
    next_ = new QPushButton(this);
    next_->setObjectName(QStringLiteral("safeDocument.next"));
    navigation->addWidget(next_);
    pageCountLabel_ = new QLabel(this);
    pageCountLabel_->setObjectName(QStringLiteral("safeDocument.pageCount"));
    navigation->addWidget(pageCountLabel_);
    navigation->addStretch(1);
    zoomBox_ = new QComboBox(this);
    zoomBox_->setObjectName(QStringLiteral("safeDocument.zoom"));
    for (const double level : kZoomLevels) {
        zoomBox_->addItem(QStringLiteral("%1%").arg(level * 100.0, 0, 'f', 0), level);
    }
    zoomBox_->setCurrentIndex(2);  // 100%
    navigation->addWidget(zoomBox_);
    layout->addLayout(navigation);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(previous_, &QPushButton::clicked, this, [this] { gotoPage(currentPage_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { gotoPage(currentPage_ + 1); });
    connect(retryButton_, &QPushButton::clicked, this, [this] { openDocument(); });
    connect(passwordEdit_, &QLineEdit::returnPressed, this, [this] { openDocument(); });
    connect(pageBox_, &QSpinBox::valueChanged, this, [this](int page) {
        if (loading_) {
            return;
        }
        gotoPage(page - 1);
    });
    connect(zoomBox_, &QComboBox::currentIndexChanged, this, [this](int) { applyZoom(); });
}

void SafeDocumentViewerDialog::retranslateUi() {
    banner_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                              "隔离进程中解析：本进程不打开这个 PDF；"
                              "页面由独立进程渲染。当前为只读，不能编辑。"));
    passwordEdit_->setPlaceholderText(
        i18n::tr("aipdf::app::SafeDocumentViewerDialog", "打开密码"));
    retryButton_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "用密码重试"));
    previous_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "上一页"));
    next_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "下一页"));
    pageCountLabel_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "共 %1 页")
                                 .arg(pageCount_));
}

bool SafeDocumentViewerDialog::attemptOpen(const QString& password) {
    lastError_.clear();
    QString error;
    if (controller_ == nullptr || !controller_->open(path_, password, &error)) {
        showFailure(error.isEmpty()
                        ? i18n::tr("aipdf::app::SafeDocumentViewerDialog", "隔离解析进程没能打开这个文件。")
                        : error);
        return false;
    }

    pageCount_ = controller_->pageCount();
    loading_ = true;
    pageBox_->setRange(1, qMax(1, pageCount_));
    pageBox_->setValue(1);
    loading_ = false;
    passwordRow_->setVisible(false);
    retranslateUi();
    status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                              "已在隔离进程中打开，共 %1 页。").arg(pageCount_));
    gotoPage(0);
    return true;
}

void SafeDocumentViewerDialog::showFailure(const QString& reason) {
    lastError_ = reason;
    status_->setText(reason);
    // 失败原因要看得见，也要能补救：密码不对就让人重新输一次。
    passwordRow_->setVisible(true);
    passwordEdit_->setFocus();
}

void SafeDocumentViewerDialog::gotoPage(int pageIndex) {
    if (pageCount_ <= 0) {
        return;
    }
    const int target = qBound(0, pageIndex, pageCount_ - 1);
    loading_ = true;
    pageBox_->setValue(target + 1);
    loading_ = false;
    loadPage(target);
}

void SafeDocumentViewerDialog::loadPage(int pageIndex) {
    if (cache_.contains(pageIndex)) {
        currentPage_ = pageIndex;
        pageImage_->setPixmap(cache_.value(pageIndex));
        updateNavigationState();
        return;
    }

    QString error;
    const QString imagePath = controller_->renderPage(pageIndex, zoom_, &error);
    if (imagePath.isEmpty()) {
        status_->setText(error.isEmpty()
                             ? i18n::tr("aipdf::app::SafeDocumentViewerDialog", "这一页渲染失败。")
                             : error);
        return;
    }

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        status_->setText(
            i18n::tr("aipdf::app::SafeDocumentViewerDialog", "渲染出来的图片读不出来。"));
        return;
    }

    currentPage_ = pageIndex;
    cache_.insert(pageIndex, pixmap);
    pageImage_->setPixmap(pixmap);
    updateNavigationState();

    // 相邻页提前渲染：翻页时就不用等往返。
    QTimer::singleShot(0, this, &SafeDocumentViewerDialog::prefetchNeighbours);
}

void SafeDocumentViewerDialog::prefetchNeighbours() {
    if (pageCount_ <= 0) {
        return;
    }
    for (const int neighbour : {currentPage_ - 1, currentPage_ + 1}) {
        if (neighbour < 0 || neighbour >= pageCount_ || cache_.contains(neighbour)) {
            continue;
        }
        QString error;
        const QString imagePath = controller_->renderPage(neighbour, zoom_, &error);
        if (imagePath.isEmpty()) {
            continue;
        }
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            cache_.insert(neighbour, pixmap);
        }
    }
}

void SafeDocumentViewerDialog::updateNavigationState() {
    previous_->setEnabled(currentPage_ > 0);
    next_->setEnabled(currentPage_ + 1 < pageCount_);
    pageCountLabel_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "共 %1 页")
                                 .arg(pageCount_));
}

void SafeDocumentViewerDialog::applyZoom() {
    zoom_ = zoomBox_->currentData().toDouble();
    // 倍率变了，缓存里的图就作废了。
    cache_.clear();
    if (pageCount_ > 0) {
        loadPage(currentPage_);
    }
}

}  // namespace aipdf::app
