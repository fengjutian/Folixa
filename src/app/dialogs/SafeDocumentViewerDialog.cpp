#include "app/dialogs/SafeDocumentViewerDialog.h"

#include "app/controllers/SafeDocumentController.h"
#include "i18n/Translate.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
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

// 便签图标的大小（点）。
constexpr double kNoteSizePt = 20.0;
// 高亮的高度（点）。
constexpr double kHighlightHeightPt = 12.0;
// 高亮的宽度（点）。
constexpr double kHighlightWidthPt = 120.0;

}  // namespace

SafeDocumentViewerDialog::SafeDocumentViewerDialog(const QString& path, QWidget* parent)
    : QDialog(parent), path_(path) {
    setObjectName(QStringLiteral("safeDocument.viewerDialog"));
    setWindowTitle(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "安全文档（隔离解析与编辑）"));
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

QJsonArray SafeDocumentViewerDialog::formFields() const {
    return controller_ != nullptr ? controller_->formFields() : QJsonArray();
}

QString SafeDocumentViewerDialog::formFieldValue(const QString& name) const {
    const QWidget* editor = fieldEditors_.value(name, nullptr);
    return editor != nullptr ? editorValue(editor) : QString();
}

QString SafeDocumentViewerDialog::statusText() const {
    return status_->text();
}

void SafeDocumentViewerDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // 第一次显示时才打开：父窗口可以在 show 之前把解析进程路径设好。
    if (!openAttempted_) {
        openDocument();
    }
}

bool SafeDocumentViewerDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == pageImage_ && placing_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            handlePageClick(mouseEvent->position().toPoint());
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
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

    // 表单面板：按解析进程报出来的字段清单生成控件。
    formPanel_ = new QWidget(this);
    formPanel_->setObjectName(QStringLiteral("safeDocument.formPanel"));
    formLayout_ = new QFormLayout(formPanel_);
    formLayout_->setContentsMargins(0, 0, 0, 0);
    formPanel_->setVisible(false);
    layout->addWidget(formPanel_);

    // 工具条：加批注与保存。
    auto* tools = new QHBoxLayout;
    addNoteButton_ = new QPushButton(this);
    addNoteButton_->setObjectName(QStringLiteral("safeDocument.addNote"));
    tools->addWidget(addNoteButton_);
    addHighlightButton_ = new QPushButton(this);
    addHighlightButton_->setObjectName(QStringLiteral("safeDocument.addHighlight"));
    tools->addWidget(addHighlightButton_);
    tools->addStretch(1);
    saveButton_ = new QPushButton(this);
    saveButton_->setObjectName(QStringLiteral("safeDocument.save"));
    tools->addWidget(saveButton_);
    saveAsButton_ = new QPushButton(this);
    saveAsButton_->setObjectName(QStringLiteral("safeDocument.saveAs"));
    tools->addWidget(saveAsButton_);
    layout->addLayout(tools);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("safeDocument.scroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setAlignment(Qt::AlignCenter);
    pageImage_ = new QLabel(scroll_);
    pageImage_->setObjectName(QStringLiteral("safeDocument.pageImage"));
    pageImage_->setAlignment(Qt::AlignCenter);
    pageImage_->setBackgroundRole(QPalette::Base);
    pageImage_->installEventFilter(this);
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
    connect(addNoteButton_, &QPushButton::clicked, this,
            [this] { startPlacement(QStringLiteral("text")); });
    connect(addHighlightButton_, &QPushButton::clicked, this,
            [this] { startPlacement(QStringLiteral("highlight")); });
    connect(saveButton_, &QPushButton::clicked, this, [this] { doSave(false); });
    connect(saveAsButton_, &QPushButton::clicked, this, [this] { doSave(true); });
}

void SafeDocumentViewerDialog::retranslateUi() {
    banner_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                              "隔离进程中解析与编辑：本进程不打开这个 PDF，"
                              "也不持有它的字节；表单、批注与保存都由隔离进程完成。"));
    passwordEdit_->setPlaceholderText(
        i18n::tr("aipdf::app::SafeDocumentViewerDialog", "打开密码"));
    retryButton_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "用密码重试"));
    previous_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "上一页"));
    next_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "下一页"));
    pageCountLabel_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "共 %1 页")
                                 .arg(pageCount_));
    addNoteButton_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "加便签"));
    addHighlightButton_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "加高亮"));
    saveButton_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "保存"));
    saveAsButton_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "另存为…"));
    if (applyFormButton_ != nullptr) {
        applyFormButton_->setText(
            i18n::tr("aipdf::app::SafeDocumentViewerDialog", "应用表单"));
    }
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
    buildFormPanel();
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

// --------------------------------------------------------------- 表单 ----

void SafeDocumentViewerDialog::buildFormPanel() {
    fieldEditors_.clear();
    // 先把上一轮的面板拆掉重建：字段清单可能变了。
    while (formLayout_->rowCount() > 0) {
        formLayout_->removeRow(0);
    }
    if (controller_ == nullptr) {
        formPanel_->setVisible(false);
        return;
    }

    const QJsonArray fields = controller_->formFields();
    for (const QJsonValue& value : fields) {
        const QJsonObject field = value.toObject();
        const QString name = field.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }
        QWidget* editor = createFieldEditor(field);
        fieldEditors_.insert(name, editor);
        formLayout_->addRow(name, editor);
    }

    applyFormButton_ = new QPushButton(formPanel_);
    applyFormButton_->setObjectName(QStringLiteral("safeDocument.applyForm"));
    formLayout_->addRow(applyFormButton_);
    connect(applyFormButton_, &QPushButton::clicked, this,
            &SafeDocumentViewerDialog::applyFormValues);

    retranslateUi();
    formPanel_->setVisible(!fieldEditors_.isEmpty());
}

QWidget* SafeDocumentViewerDialog::createFieldEditor(const QJsonObject& field) {
    const QString type = field.value(QStringLiteral("type")).toString();
    const QString current = field.value(QStringLiteral("value")).toString();
    const bool readOnly = field.value(QStringLiteral("readOnly")).toBool();

    if (type == QStringLiteral("text")) {
        auto* edit = new QLineEdit(formPanel_);
        edit->setObjectName(QStringLiteral("safeDocument.field.") +
                            field.value(QStringLiteral("name")).toString());
        edit->setText(current);
        if (readOnly) {
            edit->setReadOnly(true);
            edit->setToolTip(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                      "这个字段是只读的，改不了。"));
        }
        return edit;
    }

    if (type == QStringLiteral("combo") || type == QStringLiteral("list")) {
        const QJsonArray options = field.value(QStringLiteral("options")).toArray();
        if (!options.isEmpty()) {
            auto* box = new QComboBox(formPanel_);
            box->setObjectName(QStringLiteral("safeDocument.field.") +
                               field.value(QStringLiteral("name")).toString());
            for (const QJsonValue& option : options) {
                box->addItem(option.toString());
            }
            const int index = box->findText(current);
            if (index >= 0) {
                box->setCurrentIndex(index);
            }
            box->setEnabled(!readOnly);
            return box;
        }
        auto* edit = new QLineEdit(formPanel_);
        edit->setObjectName(QStringLiteral("safeDocument.field.") +
                            field.value(QStringLiteral("name")).toString());
        edit->setText(current);
        edit->setReadOnly(readOnly);
        return edit;
    }

    if (type == QStringLiteral("checkbox") || type == QStringLiteral("radio")) {
        auto* box = new QCheckBox(formPanel_);
        box->setObjectName(QStringLiteral("safeDocument.field.") +
                           field.value(QStringLiteral("name")).toString());
        box->setChecked(!current.isEmpty() && current != QStringLiteral("Off"));
        box->setEnabled(!readOnly);
        // 如实说明：解析器只能把勾选状态写成字符串，别的阅读器可能不显示勾号。
        box->setToolTip(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                 "勾选状态由解析器按字符串写入，"
                                 "本程序里有效，别的阅读器可能不显示勾号。"));
        return box;
    }

    // 按钮、签名域这类：现在改不了，就只显示当前值。
    auto* edit = new QLineEdit(formPanel_);
    edit->setObjectName(QStringLiteral("safeDocument.field.") +
                        field.value(QStringLiteral("name")).toString());
    edit->setText(current);
    edit->setReadOnly(true);
    edit->setToolTip(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                              "这一类字段现在还不能编辑。"));
    return edit;
}

QString SafeDocumentViewerDialog::editorValue(const QWidget* editor) const {
    if (editor == nullptr) {
        return {};
    }
    if (const auto* edit = qobject_cast<const QLineEdit*>(editor)) {
        return edit->text();
    }
    if (const auto* box = qobject_cast<const QComboBox*>(editor)) {
        return box->currentText();
    }
    if (const auto* check = qobject_cast<const QCheckBox*>(editor)) {
        return check->isChecked() ? QStringLiteral("Yes") : QStringLiteral("Off");
    }
    return {};
}

QJsonObject SafeDocumentViewerDialog::fieldByName(const QString& name) const {
    if (controller_ == nullptr) {
        return {};
    }
    const QJsonArray fields = controller_->formFields();
    for (const QJsonValue& value : fields) {
        const QJsonObject field = value.toObject();
        if (field.value(QStringLiteral("name")).toString() == name) {
            return field;
        }
    }
    return {};
}

void SafeDocumentViewerDialog::applyFormValues() {
    if (controller_ == nullptr) {
        return;
    }
    QJsonArray instructions;
    for (auto it = fieldEditors_.constBegin(); it != fieldEditors_.constEnd(); ++it) {
        const QJsonObject field = fieldByName(it.key());
        if (field.value(QStringLiteral("readOnly")).toBool()) {
            continue;
        }
        const QString value = editorValue(it.value());
        if (value == field.value(QStringLiteral("value")).toString()) {
            // 没改过的字段不发指令：少一次往返，也少一次风险。
            continue;
        }
        QJsonObject instruction;
        instruction.insert(QStringLiteral("type"), QStringLiteral("setFormFieldValue"));
        instruction.insert(QStringLiteral("field"), it.key());
        instruction.insert(QStringLiteral("value"), value);
        instructions.append(instruction);
    }

    if (instructions.isEmpty()) {
        status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "表单没有改动。"));
        return;
    }

    QString error;
    if (!controller_->edit(instructions, &error)) {
        status_->setText(error.isEmpty()
                             ? i18n::tr("aipdf::app::SafeDocumentViewerDialog", "表单没能填上。")
                             : error);
        QMessageBox::warning(this,
                             i18n::tr("aipdf::app::SafeDocumentViewerDialog", "表单没能填上"),
                             error.isEmpty()
                                 ? i18n::tr("aipdf::app::SafeDocumentViewerDialog", "表单没能填上。")
                                 : error);
        return;
    }

    // 字段值变了，控件里的旧值要换成解析进程回报的新值。
    buildFormPanel();
    status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                              "表单已填上（改动在隔离进程里，保存后才会写进文件）。"));
}

// --------------------------------------------------------------- 批注 ----

void SafeDocumentViewerDialog::startPlacement(const QString& subtype) {
    if (controller_ == nullptr || !controller_->isOpen()) {
        return;
    }
    placing_ = true;
    placementSubtype_ = subtype;
    updatePlacementUi();
}

void SafeDocumentViewerDialog::updatePlacementUi() {
    if (placing_) {
        status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                  "在页面上点一个位置来放这条批注。"));
        return;
    }
    if (controller_ != nullptr && controller_->isOpen()) {
        status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                  "已在隔离进程中打开，共 %1 页。").arg(pageCount_));
    }
}

bool SafeDocumentViewerDialog::handlePageClick(const QPoint& position) {
    if (!placing_ || controller_ == nullptr) {
        return false;
    }
    const QPixmap pixmap = pageImage_->pixmap();
    if (pixmap.isNull()) {
        return false;
    }
    // 图片是居中放在标签里的：先把点击位置换算成图片里的像素。
    const int offsetX = (pageImage_->width() - pixmap.width()) / 2;
    const int offsetY = (pageImage_->height() - pixmap.height()) / 2;
    const int pixelX = position.x() - offsetX;
    const int pixelY = position.y() - offsetY;
    if (pixelX < 0 || pixelY < 0 || pixelX >= pixmap.width() || pixelY >= pixmap.height()) {
        return false;
    }

    const QSizeF pageSize = controller_->pageSizePt(currentPage_);
    if (pageSize.isEmpty()) {
        return false;
    }
    // 像素 → 点（渲染时的倍率就是 zoom_）；PDF 的 y 轴朝上，要翻一下。
    const double x = pixelX / zoom_;
    const double y = pageSize.height() - pixelY / zoom_;

    placing_ = false;
    updatePlacementUi();

    QString text;
    if (placementSubtype_ == QStringLiteral("text")) {
        bool accepted = false;
        text = QInputDialog::getText(
            this,
            i18n::tr("aipdf::app::SafeDocumentViewerDialog", "加便签"),
            i18n::tr("aipdf::app::SafeDocumentViewerDialog", "便签内容："),
            QLineEdit::Normal,
            QString(),
            &accepted);
        if (!accepted) {
            return true;
        }
    }

    QRectF rect;
    if (placementSubtype_ == QStringLiteral("highlight")) {
        rect = QRectF(x, y - kHighlightHeightPt / 2.0, kHighlightWidthPt, kHighlightHeightPt);
    } else {
        rect = QRectF(x - kNoteSizePt / 2.0,
                      y - kNoteSizePt / 2.0,
                      kNoteSizePt,
                      kNoteSizePt);
    }

    QString error;
    if (!controller_->addAnnotation(currentPage_, placementSubtype_, rect, text, &error)) {
        status_->setText(error.isEmpty()
                             ? i18n::tr("aipdf::app::SafeDocumentViewerDialog", "批注没加上。")
                             : error);
        return true;
    }

    // 页面已经变了：缓存作废，重画这一页。
    cache_.clear();
    loadPage(currentPage_);
    status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                              "批注已加上（改动在隔离进程里，保存后才会写进文件）。"));
    return true;
}

// --------------------------------------------------------------- 保存 ----

void SafeDocumentViewerDialog::doSave(bool saveAs) {
    if (controller_ == nullptr || !controller_->isOpen()) {
        return;
    }

    QString target = path_;
    if (saveAs) {
        target = QFileDialog::getSaveFileName(
            this,
            i18n::tr("aipdf::app::SafeDocumentViewerDialog", "另存为"),
            path_,
            i18n::tr("aipdf::app::SafeDocumentViewerDialog", "PDF 文件 (*.pdf)"));
        if (target.isEmpty()) {
            return;
        }
    }

    // 保存前把「会丢什么」说清楚：解析进程是重写整个文件，不是改几个字节。
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "保存会重写整个文件"));
    confirm.setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                             "保存会让隔离解析进程重写整个文件：\n"
                             "· 原来签过的数字签名会失效（签名数据还在，但字节范围已经对不上）；\n"
                             "· 增量更新的历史版本不会保留；\n"
                             "· 文件结构会被重新序列化。\n"
                             "加密与口令按解析器实测为保留，保存后会告诉你实际结果。"));
    confirm.setInformativeText(
        i18n::tr("aipdf::app::SafeDocumentViewerDialog", "保存到：%1").arg(target));
    confirm.addButton(QMessageBox::Ok);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    if (confirm.exec() != QMessageBox::Ok) {
        return;
    }

    QStringList notes;
    QString error;
    if (!controller_->save(target, &error, &notes)) {
        const QString reason =
            error.isEmpty()
                ? i18n::tr("aipdf::app::SafeDocumentViewerDialog", "保存失败，原文件没有改动。")
                : error;
        status_->setText(reason);
        QMessageBox::warning(this,
                             i18n::tr("aipdf::app::SafeDocumentViewerDialog", "保存失败"),
                             reason);
        return;
    }

    // 保存会重排页面：缓存作废，重画当前页。
    cache_.clear();
    if (pageCount_ > 0) {
        loadPage(currentPage_);
    }
    const QString summary = saveSummary(notes);
    status_->setText(i18n::tr("aipdf::app::SafeDocumentViewerDialog", "已保存到 %1。%2")
                          .arg(target, summary));
}

QString SafeDocumentViewerDialog::saveSummary(const QStringList& notes) const {
    QStringList parts;
    for (const QString& note : notes) {
        if (note == QStringLiteral("encryption_dropped")) {
            parts.append(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                   "注意：加密丢了，保存后的文件不再需要口令。"));
        } else if (note == QStringLiteral("encryption_password_changed")) {
            parts.append(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                   "注意：文件仍然加密，但口令变了。"));
        } else if (note == QStringLiteral("encryption_preserved")) {
            parts.append(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                   "加密保留（同一个口令仍然有效）。"));
        } else if (note == QStringLiteral("signatures_invalidated")) {
            parts.append(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                   "原来的数字签名已失效。"));
        } else if (note == QStringLiteral("incremental_history_dropped")) {
            parts.append(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                   "增量更新的历史版本没有保留。"));
        } else if (note == QStringLiteral("structure_rewritten")) {
            parts.append(i18n::tr("aipdf::app::SafeDocumentViewerDialog",
                                   "文件结构已重写。"));
        }
    }
    return parts.join(QStringLiteral(" "));
}

}  // namespace aipdf::app
