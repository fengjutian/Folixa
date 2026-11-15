#include "app/UploadPreviewDialog.h"

#include "app/Language.h"
#include "i18n/Translate.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 数据模式的源文案：切换语言时按同一份源文案再翻一次。
const char* modeLabelSource(ai::DataMode mode) {
    switch (mode) {
    case ai::DataMode::LocalOnly:
        return QT_TRANSLATE_NOOP("aipdf::app::UploadPreviewDialog", "本地（不发送）");
    case ai::DataMode::SelectedChunks:
        return QT_TRANSLATE_NOOP("aipdf::app::UploadPreviewDialog", "片段云端");
    case ai::DataMode::SelectedPages:
        return QT_TRANSLATE_NOOP("aipdf::app::UploadPreviewDialog", "选定页面");
    }
    return QT_TRANSLATE_NOOP("aipdf::app::UploadPreviewDialog", "本地（不发送）");
}

}  // namespace

UploadPreviewDialog::UploadPreviewDialog(const ai::UploadPreview& preview, QWidget* parent)
    : QDialog(parent), preview_(preview) {
    setObjectName(QStringLiteral("uploadPreviewDialog"));
    texts_.bind([this] { setWindowTitle(tr("发送到云端之前")); });
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(10);

    auto* title = new QLabel(this);
    title->setObjectName(QStringLiteral("uploadPreviewTitle"));
    texts_.bind([this, title] { title->setText(tr("这次请求会离开本机")); });
    layout->addWidget(title);

    auto* target = new QLabel(this);
    target->setObjectName(QStringLiteral("uploadPreviewTarget"));
    target->setWordWrap(true);
    texts_.bind([this, target] {
        target->setText(tr("目标服务：%1").arg(
            preview_.host.isEmpty() ? tr("（未配置）") : preview_.host));
    });
    layout->addWidget(target);

    // 数据模式的名称也是常驻文案：源文案由辅助函数带下来，这里用 tr(source) 现翻。
    auto* mode = new QLabel(this);
    const char* modeSource = modeLabelSource(preview_.mode);
    texts_.bind([this, mode, modeSource] {
        mode->setText(tr("数据模式：%1").arg(tr(modeSource)));
    });
    layout->addWidget(mode);

    auto* content = new QLabel(this);
    content->setObjectName(QStringLiteral("uploadPreviewContent"));
    texts_.bind([this, content] {
        content->setText(tr("内容：%1 块 · %2 字")
                             .arg(preview_.chunkCount)
                             .arg(preview_.charCount));
    });
    layout->addWidget(content);

    if (!preview_.pages.isEmpty()) {
        auto* pages = new QLabel(this);
        pages->setObjectName(QStringLiteral("uploadPreviewPages"));
        pages->setWordWrap(true);
        texts_.bind([this, pages] {
            QStringList pageTexts;
            for (const int page : preview_.pages) {
                pageTexts.append(QString::number(page));
            }
            pages->setText(tr("涉及页：第 %1 页").arg(pageTexts.join(tr("、"))));
        });
        layout->addWidget(pages);
    }

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("uploadPreviewHint"));
    hint->setWordWrap(true);
    texts_.bind([this, hint] {
        hint->setText(tr("只发送上面列出的内容。点「取消」不会发出任何请求。"));
    });
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* send = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    send->setObjectName(QStringLiteral("uploadPreviewSend"));
    texts_.bind([this, send] { send->setText(tr("发送")); });
    // 取消是默认按钮：手快回车时不应该把文档发出去。
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("uploadPreviewCancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // 切换语言后就地重译。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &UploadPreviewDialog::retranslateUi);
}

void UploadPreviewDialog::retranslateUi() {
    texts_.retranslate();
}

}  // namespace aipdf::app
