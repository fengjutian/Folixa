#include "app/UploadPreviewDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

QString modeLabel(ai::DataMode mode) {
    switch (mode) {
    case ai::DataMode::LocalOnly:
        return QObject::tr("本地（不发送）");
    case ai::DataMode::SelectedChunks:
        return QObject::tr("片段云端");
    case ai::DataMode::SelectedPages:
        return QObject::tr("选定页面");
    }
    return QObject::tr("本地（不发送）");
}

}  // namespace

UploadPreviewDialog::UploadPreviewDialog(const ai::UploadPreview& preview, QWidget* parent)
    : QDialog(parent), preview_(preview) {
    setObjectName(QStringLiteral("uploadPreviewDialog"));
    setWindowTitle(tr("发送到云端之前"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("这次请求会离开本机"), this);
    title->setObjectName(QStringLiteral("uploadPreviewTitle"));
    layout->addWidget(title);

    auto* target = new QLabel(tr("目标服务：%1").arg(
                                  preview_.host.isEmpty() ? tr("（未配置）") : preview_.host),
                              this);
    target->setObjectName(QStringLiteral("uploadPreviewTarget"));
    target->setWordWrap(true);
    layout->addWidget(target);

    layout->addWidget(new QLabel(tr("数据模式：%1").arg(modeLabel(preview_.mode)), this));

    layout->addWidget(new QLabel(tr("内容：%1 块 · %2 字")
                                     .arg(preview_.chunkCount)
                                     .arg(preview_.charCount),
                                 this));

    if (!preview_.pages.isEmpty()) {
        QStringList pageTexts;
        for (const int page : preview_.pages) {
            pageTexts.append(QString::number(page));
        }
        auto* pages = new QLabel(tr("涉及页：第 %1 页").arg(pageTexts.join(QStringLiteral("、"))), this);
        pages->setObjectName(QStringLiteral("uploadPreviewPages"));
        pages->setWordWrap(true);
        layout->addWidget(pages);
    }

    auto* hint = new QLabel(tr("只发送上面列出的内容。点「取消」不会发出任何请求。"), this);
    hint->setObjectName(QStringLiteral("uploadPreviewHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* send = buttons->addButton(tr("发送"), QDialogButtonBox::AcceptRole);
    send->setObjectName(QStringLiteral("uploadPreviewSend"));
    // 取消是默认按钮：手快回车时不应该把文档发出去。
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("uploadPreviewCancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

}  // namespace aipdf::app
