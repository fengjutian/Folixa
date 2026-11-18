#include "app/dialogs/SignatureDialog.h"

#include <QBuffer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace aipdf::app {
namespace {

QString signatureDirectory() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(root);
    dir.mkpath(QStringLiteral("signatures"));
    return dir.filePath(QStringLiteral("signatures"));
}

QString safeFileName(QString name) {
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    return name.isEmpty() ? QStringLiteral("签章") : name;
}

QString uniqueSignaturePath(const QString& requestedName) {
    QDir dir(signatureDirectory());
    const QString base = safeFileName(requestedName);
    QString path = dir.filePath(base + QStringLiteral(".png"));
    for (int suffix = 2; QFileInfo::exists(path); ++suffix) {
        path = dir.filePath(QStringLiteral("%1 (%2).png").arg(base).arg(suffix));
    }
    return path;
}

class SignaturePad final : public QWidget {
public:
    explicit SignaturePad(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(520, 220);
        setCursor(Qt::CrossCursor);
        setAutoFillBackground(true);
    }

    bool isEmpty() const { return strokes_.isEmpty(); }
    void clear() { strokes_.clear(); update(); }

    QImage image() const {
        QImage result(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
        result.setDevicePixelRatio(devicePixelRatioF());
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(25, 25, 25), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawStrokes(painter);
        return result;
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        strokes_.append({event->position()});
        drawing_ = true;
        update();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!drawing_ || strokes_.isEmpty()) return;
        strokes_.last().append(event->position());
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) drawing_ = false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.setPen(QPen(QColor(215, 220, 228), 1, Qt::DashLine));
        painter.drawLine(18, height() - 38, width() - 18, height() - 38);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(25, 25, 25), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawStrokes(painter);
    }

private:
    void drawStrokes(QPainter& painter) const {
        for (const QVector<QPointF>& stroke : strokes_) {
            if (stroke.size() == 1) {
                painter.drawPoint(stroke.first());
                continue;
            }
            for (int i = 1; i < stroke.size(); ++i) painter.drawLine(stroke.at(i - 1), stroke.at(i));
        }
    }

    QVector<QVector<QPointF>> strokes_;
    bool drawing_ = false;
};

QByteArray pngBytes(const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (buffer.open(QIODevice::WriteOnly)) image.save(&buffer, "PNG");
    return bytes;
}

}  // namespace

SignatureDialog::SignatureDialog(bool selectionMode, QWidget* parent)
    : QDialog(parent), selectionMode_(selectionMode) {
    setWindowTitle(selectionMode ? tr("选择签章") : tr("管理签章"));
    resize(620, 410);

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("签章仅保存在本机。可导入透明 PNG/JPEG，或直接手写创建。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(150, 72));
    list_->setResizeMode(QListView::Adjust);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list_, 1);

    auto* actions = new QHBoxLayout;
    auto* importButton = new QPushButton(tr("导入图片…"), this);
    auto* drawButton = new QPushButton(tr("手写签名…"), this);
    removeButton_ = new QPushButton(tr("删除"), this);
    actions->addWidget(importButton);
    actions->addWidget(drawButton);
    actions->addWidget(removeButton_);
    actions->addStretch();
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (selectionMode_) {
        useButton_ = buttons->addButton(tr("使用此签章"), QDialogButtonBox::AcceptRole);
    }
    layout->addWidget(buttons);

    connect(importButton, &QPushButton::clicked, this, &SignatureDialog::importImage);
    connect(drawButton, &QPushButton::clicked, this, &SignatureDialog::createHandwritten);
    connect(removeButton_, &QPushButton::clicked, this, &SignatureDialog::removeSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (useButton_) connect(useButton_, &QPushButton::clicked, this, &SignatureDialog::acceptSelected);
    connect(list_, &QListWidget::itemSelectionChanged, this, &SignatureDialog::updateActions);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        if (selectionMode_) acceptSelected();
    });
    reload();
}

StoredSignature SignatureDialog::selectedSignature() const { return selected_; }

void SignatureDialog::reload() {
    list_->clear();
    const QFileInfoList files = QDir(signatureDirectory()).entryInfoList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files) {
        QImage image(info.absoluteFilePath());
        if (image.isNull()) continue;
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(image)), info.completeBaseName(), list_);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(info.absoluteFilePath());
    }
    if (list_->count() > 0) list_->setCurrentRow(0);
    updateActions();
}

void SignatureDialog::importImage() {
    const QString source = QFileDialog::getOpenFileName(
        this, tr("导入签章图片"), QString(), tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (source.isEmpty()) return;
    QImage image(source);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("导入签章"), tr("无法识别这张图片。"));
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("签章名称"), tr("名称"), QLineEdit::Normal,
                                               QFileInfo(source).completeBaseName(), &accepted);
    if (!accepted) return;
    if (!image.save(uniqueSignaturePath(name), "PNG")) {
        QMessageBox::warning(this, tr("导入签章"), tr("无法保存签章图片。"));
    }
    reload();
}

void SignatureDialog::createHandwritten() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("手写签名"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("按住鼠标或触控笔书写签名："), &dialog));
    auto* pad = new SignaturePad(&dialog);
    layout->addWidget(pad);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* clearButton = buttons->addButton(tr("清空"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(clearButton, &QPushButton::clicked, pad, &SignaturePad::clear);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, pad] {
        if (!pad->isEmpty()) dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;

    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("签章名称"), tr("名称"), QLineEdit::Normal,
                                               tr("我的签名"), &accepted);
    if (!accepted) return;
    const QByteArray bytes = pngBytes(pad->image());
    QFile file(uniqueSignaturePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        QMessageBox::warning(this, tr("手写签名"), tr("无法保存签名。"));
    }
    reload();
}

void SignatureDialog::removeSelected() {
    QListWidgetItem* item = list_->currentItem();
    if (!item) return;
    if (QMessageBox::question(this, tr("删除签章"), tr("确定删除“%1”吗？").arg(item->text()))
        != QMessageBox::Yes) return;
    QFile::remove(item->data(Qt::UserRole).toString());
    reload();
}

void SignatureDialog::acceptSelected() {
    QListWidgetItem* item = list_->currentItem();
    if (!item) return;
    QFile file(item->data(Qt::UserRole).toString());
    if (!file.open(QIODevice::ReadOnly)) return;
    selected_ = {item->text(), file.readAll()};
    if (selected_.isValid()) accept();
}

void SignatureDialog::updateActions() {
    const bool selected = list_->currentItem() != nullptr;
    removeButton_->setEnabled(selected);
    if (useButton_) useButton_->setEnabled(selected);
}

}  // namespace aipdf::app
