#include "app/dialogs/AiToolsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "app/controllers/AiToolService.h"
#include "ai/content/PptxExport.h"
#include "i18n/Translate.h"

namespace aipdf::app {

namespace {

}  // namespace

// ---------------------------------------------------------------- 文生图 ----

ImageGenerationDialog::ImageGenerationDialog(AiToolService* service, QWidget* parent)
    : QDialog(parent), service_(service) {
    setObjectName(QStringLiteral("ai.imageGenerationDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "AI 生成图片"));
    resize(640, 560);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    promptEdit_ = new QPlainTextEdit(this);
    promptEdit_->setObjectName(QStringLiteral("ai.imageGeneration.prompt"));
    promptEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "提示词"));
    promptEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "画什么，写清楚一点"));
    promptEdit_->setFixedHeight(90);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "提示词:"), promptEdit_);

    negativeEdit_ = new QLineEdit(this);
    negativeEdit_->setObjectName(QStringLiteral("ai.imageGeneration.negative"));
    negativeEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "反向提示词"));
    negativeEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "不要出现什么，可留空"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "反向提示词:"), negativeEdit_);

    styleEdit_ = new QLineEdit(this);
    styleEdit_->setObjectName(QStringLiteral("ai.imageGeneration.style"));
    styleEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "风格"));
    styleEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "例如「水彩插画」，可留空"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "风格:"), styleEdit_);

    sizeBox_ = new QComboBox(this);
    sizeBox_->setObjectName(QStringLiteral("ai.imageGeneration.size"));
    sizeBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "尺寸"));
    sizeBox_->addItems({QStringLiteral("1024x1024"), QStringLiteral("1024x1536"),
                        QStringLiteral("1536x1024"), QStringLiteral("512x512")});
    form->addRow(i18n::tr("aipdf::app::MainWindow", "尺寸:"), sizeBox_);

    countSpin_ = new QSpinBox(this);
    countSpin_->setObjectName(QStringLiteral("ai.imageGeneration.count"));
    countSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "张数"));
    countSpin_->setRange(1, 4);
    countSpin_->setValue(1);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "张数:"), countSpin_);

    layout->addLayout(form);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("ai.imageGeneration.preview"));
    preview_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "生成结果预览"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(180);
    preview_->setText(i18n::tr("aipdf::app::MainWindow", "还没有生成结果"));
    layout->addWidget(preview_);

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("ai.imageGeneration.results"));
    resultList_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "生成结果列表"));
    resultList_->setMaximumHeight(90);
    layout->addWidget(resultList_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("ai.imageGeneration.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "生成状态"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    generateButton_ = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "生成"), QDialogButtonBox::ActionRole);
    generateButton_->setObjectName(QStringLiteral("ai.imageGeneration.generate"));
    generateButton_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "生成"));
    auto* insertButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "插入到当前页"), QDialogButtonBox::ActionRole);
    insertButton->setObjectName(QStringLiteral("ai.imageGeneration.insert"));
    insertButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "插入到当前页"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(generateButton_, &QPushButton::clicked, this, [this] {
        QString reason;
        if (!canSend(&reason)) {
            statusLabel_->setText(reason);
            return;
        }
        if (service_ == nullptr) {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "AI 服务还没连上。"));
            return;
        }
        statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "正在生成…"));
        generateButton_->setEnabled(false);
        service_->requestImageGeneration(buildPayload());
    });
    connect(insertButton, &QPushButton::clicked, this, [this] {
        const QByteArray bytes = currentImageBytes();
        if (bytes.isEmpty()) {
            QMessageBox::information(this, i18n::tr("aipdf::app::MainWindow", "AI 生成图片"), i18n::tr("aipdf::app::MainWindow", "先生成一张图。"));
            return;
        }
        emit insertRequested(bytes);
    });
    connect(resultList_, &QListWidget::currentRowChanged, this,
            [this](int) { refreshPreview(); });

    if (service_ != nullptr) {
        connect(service_, &AiToolService::imageReady, this,
                [this](const QByteArray& bytes, const QString& mimeType, const QString&) {
                    addGeneratedImage(bytes, mimeType);
                });
        connect(service_, &AiToolService::failed, this,
                [this](const QString& reason) { statusLabel_->setText(reason); });
        connect(service_, &AiToolService::finished, this,
                [this] { generateButton_->setEnabled(true); });
    }
}

ai::ImageGenerationRequest ImageGenerationDialog::buildRequest() const {
    const QString size = sizeBox_ != nullptr ? sizeBox_->currentText() : QString();
    return ai::buildImageGenerationRequest(promptEdit_ != nullptr ? promptEdit_->toPlainText() : QString(),
                                           negativeEdit_ != nullptr ? negativeEdit_->text() : QString(),
                                           styleEdit_ != nullptr ? styleEdit_->text() : QString(),
                                           size, countSpin_ != nullptr ? countSpin_->value() : 1);
}

QByteArray ImageGenerationDialog::buildPayload() const {
    return ai::buildImageGenerationPayload(buildRequest());
}

bool ImageGenerationDialog::canSend(QString* reason) const {
    const ai::ImageGenerationRequest request = buildRequest();
    if (!request.ok) {
        if (reason != nullptr) {
            *reason = request.error;
        }
        return false;
    }
    if (reason != nullptr) {
        *reason = request.promptTruncated
                      ? i18n::tr("aipdf::app::MainWindow", "提示词太长，已经截断到 %1 个字符。")
                            .arg(request.prompt.size())
                      : QString();
    }
    return true;
}

void ImageGenerationDialog::addGeneratedImage(const QByteArray& bytes, const QString& mimeType) {
    if (bytes.isEmpty()) {
        return;
    }
    images_.append(bytes);
    auto* item = new QListWidgetItem(i18n::tr("aipdf::app::MainWindow", "第 %1 张（%2）")
                                          .arg(images_.size())
                                          .arg(mimeType.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "图片") : mimeType),
                                      resultList_);
    item->setData(Qt::UserRole, images_.size() - 1);
    if (resultList_->count() == 1) {
        resultList_->setCurrentRow(0);
    }
    statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "已生成 %1 张。").arg(images_.size()));
    refreshPreview();
}

int ImageGenerationDialog::generatedCount() const {
    return images_.size();
}

QByteArray ImageGenerationDialog::currentImageBytes() const {
    if (resultList_ == nullptr || resultList_->currentItem() == nullptr) {
        return QByteArray();
    }
    const int index = resultList_->currentItem()->data(Qt::UserRole).toInt();
    if (index < 0 || index >= images_.size()) {
        return QByteArray();
    }
    return images_.at(index);
}

QString ImageGenerationDialog::statusText() const {
    return statusLabel_ != nullptr ? statusLabel_->text() : QString();
}

void ImageGenerationDialog::refreshPreview() {
    if (preview_ == nullptr) {
        return;
    }
    const QByteArray bytes = currentImageBytes();
    if (bytes.isEmpty()) {
        preview_->setPixmap(QPixmap());
        preview_->setText(i18n::tr("aipdf::app::MainWindow", "还没有生成结果"));
        return;
    }
    QPixmap pixmap;
    if (!pixmap.loadFromData(bytes)) {
        preview_->setPixmap(QPixmap());
        preview_->setText(i18n::tr("aipdf::app::MainWindow", "生成结果不是一张可显示的图片"));
        return;
    }
    preview_->setPixmap(pixmap.scaledToWidth(360, Qt::SmoothTransformation));
}

// ------------------------------------------------------------ 总结配图 ----

SummaryImageDialog::SummaryImageDialog(AiToolService* service, QWidget* parent)
    : QDialog(parent), service_(service) {
    setObjectName(QStringLiteral("ai.summaryImageDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "AI 总结配图"));
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "要总结的正文:"), this));
    textEdit_ = new QPlainTextEdit(this);
    textEdit_->setObjectName(QStringLiteral("ai.summaryImage.text"));
    textEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "要总结的正文"));
    layout->addWidget(textEdit_, 1);

    auto* form = new QFormLayout;
    instructionEdit_ = new QLineEdit(this);
    instructionEdit_->setObjectName(QStringLiteral("ai.summaryImage.instruction"));
    instructionEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "画面要求"));
    instructionEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "例如「偏商务风格」，可留空"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "画面要求:"), instructionEdit_);
    layout->addLayout(form);

    layout->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "配图计划:"), this));
    pointList_ = new QListWidget(this);
    pointList_->setObjectName(QStringLiteral("ai.summaryImage.points"));
    pointList_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "配图计划"));
    layout->addWidget(pointList_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("ai.summaryImage.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "配图状态"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* planButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "生成配图计划"), QDialogButtonBox::ActionRole);
    planButton->setObjectName(QStringLiteral("ai.summaryImage.plan"));
    planButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "生成配图计划"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(planButton, &QPushButton::clicked, this, [this] {
        const ai::SummaryImagePlan plan = buildPlan();
        if (!plan.ok) {
            statusLabel_->setText(plan.error);
            return;
        }
        setPlan(plan);
    });
}

QString SummaryImageDialog::documentText() const {
    return textEdit_ != nullptr ? textEdit_->toPlainText() : QString();
}

void SummaryImageDialog::setDocumentText(const QString& text) {
    if (textEdit_ != nullptr) {
        textEdit_->setPlainText(text);
    }
}

QString SummaryImageDialog::instruction() const {
    return instructionEdit_ != nullptr ? instructionEdit_->text() : QString();
}

void SummaryImageDialog::setInstruction(const QString& value) {
    if (instructionEdit_ != nullptr) {
        instructionEdit_->setText(value);
    }
}

ai::SummaryImagePlan SummaryImageDialog::buildPlan() const {
    return ai::planSummaryImages(documentText(), instruction());
}

void SummaryImageDialog::setPlan(const ai::SummaryImagePlan& plan) {
    plan_ = plan;
    if (pointList_ != nullptr) {
        pointList_->clear();
        for (int index = 0; index < plan.keyPoints.size(); ++index) {
            const QString prompt = index < plan.imagePrompts.size() ? plan.imagePrompts.at(index)
                                                                   : QString();
            auto* item = new QListWidgetItem(
                i18n::tr("aipdf::app::MainWindow", "%1. %2").arg(index + 1).arg(plan.keyPoints.at(index)), pointList_);
            item->setToolTip(prompt);
        }
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(plan.ok ? i18n::tr("aipdf::app::MainWindow", "要点 %1 条，可以逐条生成配图。")
                                       .arg(plan.keyPoints.size())
                                       : plan.error);
    }
}

QStringList SummaryImageDialog::keyPoints() const {
    return plan_.keyPoints;
}

QStringList SummaryImageDialog::imagePrompts() const {
    return plan_.imagePrompts;
}

QString SummaryImageDialog::statusText() const {
    return statusLabel_ != nullptr ? statusLabel_->text() : QString();
}

// -------------------------------------------------------------- 生成 PPT ----

PptGenerationDialog::PptGenerationDialog(AiToolService* service, QWidget* parent)
    : QDialog(parent), service_(service) {
    setObjectName(QStringLiteral("ai.pptDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "AI 生成 PPT"));
    resize(720, 620);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    sourceBox_ = new QComboBox(this);
    sourceBox_->setObjectName(QStringLiteral("ai.ppt.source"));
    sourceBox_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "内容来源"));
    sourceBox_->addItem(i18n::tr("aipdf::app::MainWindow", "按主题生成"), static_cast<int>(ai::PptSource::Topic));
    sourceBox_->addItem(i18n::tr("aipdf::app::MainWindow", "按文档生成"), static_cast<int>(ai::PptSource::Document));
    sourceBox_->addItem(i18n::tr("aipdf::app::MainWindow", "按大纲生成"), static_cast<int>(ai::PptSource::Outline));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "来源:"), sourceBox_);

    topicEdit_ = new QLineEdit(this);
    topicEdit_->setObjectName(QStringLiteral("ai.ppt.topic"));
    topicEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "主题"));
    topicEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "这次 PPT 讲什么"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "主题:"), topicEdit_);

    instructionEdit_ = new QLineEdit(this);
    instructionEdit_->setObjectName(QStringLiteral("ai.ppt.instruction"));
    instructionEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "附加要求"));
    instructionEdit_->setPlaceholderText(i18n::tr("aipdf::app::MainWindow", "例如「面向新手」，可留空"));
    form->addRow(i18n::tr("aipdf::app::MainWindow", "附加要求:"), instructionEdit_);

    slideSpin_ = new QSpinBox(this);
    slideSpin_->setObjectName(QStringLiteral("ai.ppt.slides"));
    slideSpin_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "最多页数"));
    slideSpin_->setRange(ai::kMinDeckSlides, ai::kMaxDeckSlides);
    slideSpin_->setValue(12);
    form->addRow(i18n::tr("aipdf::app::MainWindow", "最多页数:"), slideSpin_);

    layout->addLayout(form);

    layout->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "文档正文（按文档生成时用）:"), this));
    documentEdit_ = new QPlainTextEdit(this);
    documentEdit_->setObjectName(QStringLiteral("ai.ppt.document"));
    documentEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "文档正文"));
    documentEdit_->setFixedHeight(90);
    layout->addWidget(documentEdit_);

    layout->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "大纲（按大纲生成时用，顶格行是页标题，缩进行是要点）:"), this));
    outlineEdit_ = new QPlainTextEdit(this);
    outlineEdit_->setObjectName(QStringLiteral("ai.ppt.outline"));
    outlineEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "大纲"));
    outlineEdit_->setFixedHeight(90);
    layout->addWidget(outlineEdit_);

    slideList_ = new QListWidget(this);
    slideList_->setObjectName(QStringLiteral("ai.ppt.slidesList"));
    slideList_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "幻灯片列表"));
    slideList_->setMaximumHeight(120);
    layout->addWidget(slideList_);

    preview_ = new QTextBrowser(this);
    preview_->setObjectName(QStringLiteral("ai.ppt.preview"));
    preview_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "PPT 预览"));
    layout->addWidget(preview_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("ai.ppt.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "PPT 状态"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* generateButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "生成"), QDialogButtonBox::ActionRole);
    generateButton->setObjectName(QStringLiteral("ai.ppt.generate"));
    generateButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "生成"));
    // 主按钮是「存为 PPTX」：命令叫「生成 PPT」，产出的就得是 PPT。
    // Markdown 留作次要选项（有人要的是纯文本大纲，不是演示文稿）。
    auto* savePptxButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "存为 PPTX"), QDialogButtonBox::ActionRole);
    savePptxButton->setObjectName(QStringLiteral("ai.ppt.savePptx"));
    savePptxButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "存为 PPTX"));
    auto* saveButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "存为 Markdown"), QDialogButtonBox::ActionRole);
    saveButton->setObjectName(QStringLiteral("ai.ppt.save"));
    saveButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "存为 Markdown"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(generateButton, &QPushButton::clicked, this, [this] { generateDeck(); });
    connect(savePptxButton, &QPushButton::clicked, this, [this] {
        if (!deck_.isValid()) {
            QMessageBox::information(this, i18n::tr("aipdf::app::MainWindow", "AI 生成 PPT"),
                                     i18n::tr("aipdf::app::MainWindow", "先生成一套 PPT。"));
            return;
        }
        const QByteArray bytes = ai::deckToPptx(deck_);
        if (bytes.isEmpty()) {
            QMessageBox::warning(this, i18n::tr("aipdf::app::MainWindow", "AI 生成 PPT"),
                                 i18n::tr("aipdf::app::MainWindow", "导出 PPTX 失败：这一套 PPT 没有可导出的内容。"));
            return;
        }
        emit savePptxRequested(bytes);
    });
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (markdown().isEmpty()) {
            QMessageBox::information(this, i18n::tr("aipdf::app::MainWindow", "AI 生成 PPT"), i18n::tr("aipdf::app::MainWindow", "先生成一套 PPT。"));
            return;
        }
        emit saveMarkdownRequested(markdown());
    });
    connect(slideList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (preview_ == nullptr || row < 0 || row >= deck_.slides.size()) {
            return;
        }
        const ai::Slide& slide = deck_.slides.at(row);
        QString html = QStringLiteral("<h3>%1</h3>").arg(slide.title.toHtmlEscaped());
        if (!slide.subtitle.isEmpty()) {
            html += QStringLiteral("<p><i>%1</i></p>").arg(slide.subtitle.toHtmlEscaped());
        }
        html += QStringLiteral("<ul>");
        for (const QString& bullet : slide.bullets) {
            html += QStringLiteral("<li>%1</li>").arg(bullet.toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
        if (!slide.notes.isEmpty()) {
            html += QStringLiteral("<blockquote>%1</blockquote>").arg(slide.notes.toHtmlEscaped());
        }
        preview_->setHtml(html);
    });
}

ai::PptSource PptGenerationDialog::source() const {
    if (sourceBox_ == nullptr || sourceBox_->currentIndex() < 0) {
        return ai::PptSource::Topic;
    }
    return static_cast<ai::PptSource>(sourceBox_->currentData().toInt());
}

void PptGenerationDialog::setSource(ai::PptSource source) {
    if (sourceBox_ == nullptr) {
        return;
    }
    const int index = sourceBox_->findData(static_cast<int>(source));
    if (index >= 0) {
        sourceBox_->setCurrentIndex(index);
    }
}

QString PptGenerationDialog::topic() const {
    return topicEdit_ != nullptr ? topicEdit_->text() : QString();
}

void PptGenerationDialog::setTopic(const QString& value) {
    if (topicEdit_ != nullptr) {
        topicEdit_->setText(value);
    }
}

QString PptGenerationDialog::documentText() const {
    return documentEdit_ != nullptr ? documentEdit_->toPlainText() : QString();
}

void PptGenerationDialog::setDocumentText(const QString& text) {
    if (documentEdit_ != nullptr) {
        documentEdit_->setPlainText(text);
    }
}

QString PptGenerationDialog::outlineText() const {
    return outlineEdit_ != nullptr ? outlineEdit_->toPlainText() : QString();
}

void PptGenerationDialog::setOutlineText(const QString& text) {
    if (outlineEdit_ != nullptr) {
        outlineEdit_->setPlainText(text);
    }
}

QString PptGenerationDialog::instruction() const {
    return instructionEdit_ != nullptr ? instructionEdit_->text() : QString();
}

int PptGenerationDialog::maxSlides() const {
    return slideSpin_ != nullptr ? slideSpin_->value() : 12;
}

void PptGenerationDialog::setMaxSlides(int count) {
    if (slideSpin_ != nullptr) {
        slideSpin_->setValue(qBound(ai::kMinDeckSlides, count, ai::kMaxDeckSlides));
    }
}

ai::PptDeck PptGenerationDialog::deck() const {
    return deck_;
}

void PptGenerationDialog::setDeck(const ai::PptDeck& deck) {
    deck_ = deck;
    if (slideList_ != nullptr) {
        slideList_->clear();
        for (const ai::Slide& slide : deck.slides) {
            slideList_->addItem(i18n::tr("aipdf::app::MainWindow", "第 %1 页：%2").arg(slide.index).arg(slide.title));
        }
        if (slideList_->count() > 0) {
            slideList_->setCurrentRow(0);
        }
    }
    if (preview_ != nullptr) {
        preview_->setMarkdown(ai::deckToMarkdown(deck));
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(deck.isValid() ? i18n::tr("aipdf::app::MainWindow", "共 %1 页。").arg(deck.slides.size())
                                              : deck.error);
    }
}

QString PptGenerationDialog::markdown() const {
    return ai::deckToMarkdown(deck_);
}

QString PptGenerationDialog::statusText() const {
    return statusLabel_ != nullptr ? statusLabel_->text() : QString();
}

void PptGenerationDialog::generateDeck() {
    if (service_ == nullptr) {
        statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "AI 服务还没连上。"));
        return;
    }
    // 模型调用要的是一个**同步**回调（领域层的 DeckModelCall 就是这个形状），
    // 所以这里用阻塞式的问法；界面在等待期间是活的（事件循环还在跑）。
    ai::DeckModelCall modelCall = [this](const QString& prompt, QString* error) {
        QString reason;
        const QByteArray answer = service_->askModelBlocking(
            i18n::tr("aipdf::app::MainWindow", "你是一位专业的演示文稿作者。只输出 JSON，不要任何解释文字。"), prompt, &reason);
        if (answer.isEmpty()) {
            if (error != nullptr) {
                *error = reason.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "模型没有返回内容。") : reason;
            }
            return QByteArray();
        }
        return answer;
    };

    ai::PptDeck deck;
    switch (source()) {
    case ai::PptSource::Document:
        deck = ai::generateDeckFromDocument(documentText(), topic(), maxSlides(), modelCall);
        break;
    case ai::PptSource::Outline:
        deck = ai::generateDeckFromOutline(outlineText(), topic(), maxSlides());
        break;
    case ai::PptSource::Topic:
    default:
        deck = ai::generateDeckFromTopic(topic(), instruction(), maxSlides(), modelCall);
        break;
    }
    setDeck(deck);
}

// ------------------------------------------------------------ 内容检测 ----

DetectionDialog::DetectionDialog(AiToolService* service, QWidget* parent)
    : QDialog(parent), service_(service) {
    setObjectName(QStringLiteral("ai.detectionDialog"));
    setWindowTitle(i18n::tr("aipdf::app::MainWindow", "AI 内容检测"));
    resize(620, 460);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(i18n::tr("aipdf::app::MainWindow", "要检测的文本:"), this));
    textEdit_ = new QPlainTextEdit(this);
    textEdit_->setObjectName(QStringLiteral("ai.detection.text"));
    textEdit_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "要检测的文本"));
    layout->addWidget(textEdit_, 1);

    verdictLabel_ = new QLabel(this);
    verdictLabel_->setObjectName(QStringLiteral("ai.detection.verdict"));
    verdictLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "检测判定"));
    verdictLabel_->setWordWrap(true);
    layout->addWidget(verdictLabel_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("ai.detection.status"));
    statusLabel_->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "检测状态"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* detectButton = buttons->addButton(i18n::tr("aipdf::app::MainWindow", "检测"), QDialogButtonBox::ActionRole);
    detectButton->setObjectName(QStringLiteral("ai.detection.run"));
    detectButton->setAccessibleName(i18n::tr("aipdf::app::MainWindow", "检测"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(detectButton, &QPushButton::clicked, this, [this, detectButton] {
        if (service_ == nullptr) {
            statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "AI 服务还没连上。"));
            return;
        }
        QString reason;
        const QByteArray prompt = ai::buildDetectionPrompt(sourceText(), &reason);
        if (prompt.isEmpty()) {
            statusLabel_->setText(reason.isEmpty() ? i18n::tr("aipdf::app::MainWindow", "没有可检测的文本。") : reason);
            return;
        }
        statusLabel_->setText(i18n::tr("aipdf::app::MainWindow", "正在检测…"));
        detectButton->setEnabled(false);
        service_->requestCompletion(
            i18n::tr("aipdf::app::MainWindow", "你是一位文本来源鉴定专家。只输出 JSON，不要任何解释文字。"),
            QString::fromUtf8(prompt));
    });

    if (service_ != nullptr) {
        connect(service_, &AiToolService::completionReady, this, [this](const QString& answer) {
            // 解析交给领域层：它认得两种 JSON 形状，也知道「解析不出来」是什么。
            setVerdict(ai::parseDetectionResponse(answer.toUtf8()));
        });
        connect(service_, &AiToolService::failed, this,
                [this, detectButton](const QString& reason) {
                    statusLabel_->setText(reason);
                    detectButton->setEnabled(true);
                });
        connect(service_, &AiToolService::finished, this,
                [this, detectButton] { detectButton->setEnabled(true); });
    }
}

QString DetectionDialog::sourceText() const {
    return textEdit_ != nullptr ? textEdit_->toPlainText() : QString();
}

void DetectionDialog::setSourceText(const QString& text) {
    if (textEdit_ != nullptr) {
        textEdit_->setPlainText(text);
    }
}

ai::DetectionVerdict DetectionDialog::verdict() const {
    return verdict_;
}

void DetectionDialog::setVerdict(const ai::DetectionVerdict& verdict) {
    verdict_ = verdict;
    if (verdictLabel_ != nullptr) {
        verdictLabel_->setText(verdictText());
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->setText(verdict.ok ? i18n::tr("aipdf::app::MainWindow", "检测完成。") : verdict.reason);
    }
}

QString DetectionDialog::verdictText() const {
    // 解析不出来就是「无法判断」，不能写成「完全人工」：
    // 后者会被当成一个确定结论，用户据此下判断就会出错。
    if (!verdict_.ok) {
        return i18n::tr("aipdf::app::MainWindow", "判定：无法判断 —— %1").arg(verdict_.reason);
    }
    return i18n::tr("aipdf::app::MainWindow", "判定：%1 · 置信度 %2% —— %3")
        .arg(verdict_.category)
        .arg(verdict_.confidence)
        .arg(verdict_.reason);
}

QString DetectionDialog::statusText() const {
    return statusLabel_ != nullptr ? statusLabel_->text() : QString();
}

}  // namespace aipdf::app
