#include "app/AiPanel.h"

#include "app/AiSettingsDialog.h"
#include "app/DocumentQaController.h"
#include "app/IndexController.h"
#include "app/Language.h"
#include "app/LastWaveBridge.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "app/UploadPreviewDialog.h"
#include "ai/AuditLog.h"
#include "ai/ChatTypes.h"
#include "ai/CredentialStore.h"
#include "ai/OfflineQa.h"
#include "ai/ProviderConfig.h"
#include "ai/ProviderFactory.h"
#include "ai/UploadPreview.h"
#include "ipc/WorkerClient.h"
#include "i18n/Translate.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace aipdf::app {

namespace {

// 验收探针用的轨迹文件。
void trace(const QString& message) {
    const QString path = qEnvironmentVariable("AIPDF_TRACE_FILE");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " +
                    message + "\n")
                       .toUtf8());
    }
}

// 一次最多送多少字。
//
// 送整页已经够模型回答了；再多的内容对当前这四个任务没有帮助，
// 只会让用户多花钱。超出就截断，并说明截断了。
constexpr int kMaximumMaterialCharacters = 4000;
constexpr int kMaximumHistoryMessages = 12;
constexpr int kMaximumHistoryCharacters = 12000;

QString formatCost(double cost) {
    if (cost <= 0.0) {
        return QStringLiteral("—");
    }
    return QStringLiteral("$%1").arg(cost, 0, 'f', 4);
}

}  // namespace

AiPanel::AiPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    // 切换语言后常驻文案就地重译，状态行按缓存重算；对话记录保持原样。
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &AiPanel::retranslateUi);
}

void AiPanel::buildUi() {
    setObjectName(QStringLiteral("aiPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("aiHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(14, 12, 12, 10);
    headerLayout->setSpacing(3);
    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    auto* title = new QLabel(QString(), this);
    title->setObjectName(QStringLiteral("aiTitle"));
    texts_.bind([this, title] { title->setText(tr("✦  Folixa AI")); });
    auto* subtitle = new QLabel(QString(), this);
    subtitle->setObjectName(QStringLiteral("aiSubtitle"));
    texts_.bind([this, subtitle] { subtitle->setText(tr("阅读、理解并定位当前文档内容")); });

    providerLabel_ = new QLabel(tr("AI 服务：正在启动"), this);
    providerLabel_->setObjectName(QStringLiteral("aiProviderLabel"));
    settingsButton_ = new QPushButton(QString(), this);
    settingsButton_->setObjectName(QStringLiteral("aiSettingsButton"));
    texts_.bind([this] { settingsButton_->setText(tr("设置")); });
    texts_.bind([this] { settingsButton_->setToolTip(tr("设置服务商与 API 密钥")); });
    newChatButton_ = new QPushButton(QString(), this);
    newChatButton_->setObjectName(QStringLiteral("aiNewChatButton"));
    texts_.bind([this] { newChatButton_->setText(tr("新对话")); });
    texts_.bind([this] { newChatButton_->setToolTip(tr("清空当前对话历史")); });
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(newChatButton_);
    titleRow->addWidget(settingsButton_);
    headerLayout->addLayout(titleRow);
    headerLayout->addWidget(subtitle);
    providerLabel_->setWordWrap(true);
    headerLayout->addWidget(providerLabel_);
    scopeLabel_ = new QLabel(QString(), this);
    scopeLabel_->setObjectName(QStringLiteral("aiScopeLabel"));
    scopeLabel_->setWordWrap(true);

    // 范围切换：两个可勾选的按钮，而不是下拉框。
    //
    // 为什么不用下拉框：范围是这个面板最要紧的一个选择（「送什么给模型」），
    // 用两个能同时看见当前状态的按钮，用户一眼就知道现在送的是哪半边；
    // 下拉框把当前选择藏起来，得点开才知道。
    scopePageButton_ = new QPushButton(QString(), this);
    scopePageButton_->setObjectName(QStringLiteral("aiScopePageButton"));
    scopePageButton_->setCheckable(true);
    scopePageButton_->setChecked(true);
    texts_.bind([this] { scopePageButton_->setText(tr("当前页")); });
    texts_.bind([this] { scopePageButton_->setToolTip(tr("只送当前页文本或当前选区")); });
    scopeDocumentButton_ = new QPushButton(QString(), this);
    scopeDocumentButton_->setObjectName(QStringLiteral("aiScopeDocumentButton"));
    scopeDocumentButton_->setCheckable(true);
    texts_.bind([this] { scopeDocumentButton_->setText(tr("整篇文档")); });
    texts_.bind([this] {
        scopeDocumentButton_->setToolTip(tr("检索整篇文档，只送最相关的几块，并给出可核对的引用"));
    });
    scopeGroup_ = new QButtonGroup(this);
    scopeGroup_->setExclusive(true);
    scopeGroup_->addButton(scopePageButton_);
    scopeGroup_->addButton(scopeDocumentButton_);

    auto* contextCard = new QFrame(this);
    contextCard->setObjectName(QStringLiteral("aiContextCard"));
    auto* contextLayout = new QVBoxLayout(contextCard);
    contextLayout->setContentsMargins(12, 10, 12, 10);
    contextLayout->setSpacing(8);
    auto* scopeRow = new QHBoxLayout();
    scopeRow->setSpacing(4);
    auto* scopeTitle = new QLabel(QString(), this);
    scopeTitle->setObjectName(QStringLiteral("aiSectionLabel"));
    texts_.bind([this, scopeTitle] { scopeTitle->setText(tr("回答范围")); });
    scopeRow->addWidget(scopeTitle);
    scopeRow->addStretch(1);

    auto* scopeSwitch = new QHBoxLayout();
    scopeSwitch->setSpacing(4);
    scopeSwitch->addWidget(scopePageButton_, 1);
    scopeSwitch->addWidget(scopeDocumentButton_, 1);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(6);
    summaryButton_ = new QPushButton(QString(), this);
    summaryButton_->setObjectName(QStringLiteral("aiSummaryButton"));
    texts_.bind([this] { summaryButton_->setText(tr("总结要点")); });
    texts_.bind([this] { summaryButton_->setToolTip(tr("总结当前页")); });
    explainButton_ = new QPushButton(QString(), this);
    explainButton_->setObjectName(QStringLiteral("aiExplainButton"));
    texts_.bind([this] { explainButton_->setText(tr("解释选区")); });
    texts_.bind([this] { explainButton_->setToolTip(tr("解释选中的文字")); });
    translateButton_ = new QPushButton(QString(), this);
    translateButton_->setObjectName(QStringLiteral("aiTranslateButton"));
    texts_.bind([this] { translateButton_->setText(tr("翻译选区")); });
    texts_.bind([this] { translateButton_->setToolTip(tr("翻译选中的文字")); });
    // 本地离线问答：不需要配 Provider，也不联网。
    offlineButton_ = new QPushButton(QString(), this);
    offlineButton_->setObjectName(QStringLiteral("aiOfflineButton"));
    texts_.bind([this] { offlineButton_->setText(tr("本地离线问答")); });
    texts_.bind([this] { offlineButton_->setToolTip(tr("在本地索引里检索并摘出答案（不联网、不调模型）")); });
    actions->addWidget(summaryButton_, 1);
    actions->addWidget(explainButton_, 1);
    actions->addWidget(translateButton_, 1);
    actions->addWidget(offlineButton_, 1);
    contextLayout->addLayout(scopeRow);
    contextLayout->addLayout(scopeSwitch);
    contextLayout->addWidget(scopeLabel_);
    contextLayout->addLayout(actions);

    conversation_ = new QTextBrowser(this);
    conversation_->setObjectName(QStringLiteral("aiConversation"));
    // 引用链接由应用自己处理：点了要跳到原文，不能让浏览器去打开。
    conversation_->setOpenLinks(false);
    conversation_->setOpenExternalLinks(false);
    conversation_->document()->setDefaultStyleSheet(QStringLiteral(
        "body{font-family:'Microsoft YaHei UI','Segoe UI';color:#253047;font-size:13px;}"
        ".welcome{background:#F7F9FC;border:1px solid #E5EAF1;padding:16px;margin:6px 2px;}"
        ".eyebrow{color:#4169C1;font-size:10px;font-weight:600;}"
        ".message{background:#F7F9FC;padding:12px;margin:8px 2px;}"
        ".user{background:#EAF1FF;padding:12px;margin:8px 2px;}"
        ".scope{color:#8792A3;font-size:11px;}"
        ".muted{color:#8792A3;font-size:11px;}"
        ".citations{background:#FFFFFF;border:1px solid #D9E3F2;padding:10px;margin:4px 2px;}"
        ".citation{font-size:12px;margin:3px 0;}"
        ".ok{color:#2E7D53;}"
        ".bad{color:#C0562F;}"
        "a{color:#4169C1;text-decoration:none;}"
        "a:hover{text-decoration:underline;}"));
    texts_.bind([this] {
        // 欢迎语是常驻说明；已经开聊后不能再覆盖对话记录。
        if (conversationStarted_) {
            return;
        }
        conversation_->setHtml(
            QStringLiteral("<div class='welcome'><div class='eyebrow'>FOLIXA AI</div>") +
            tr("<h3>从当前文档开始</h3>") +
            tr("<p>可以总结页面、解释或翻译选区，也可以直接询问文档内容。</p>") +
            tr("<p>切换到“整篇文档”后，我会检索相关内容并附上可核对的引用。</p>") +
            tr("<p class='muted'>文档内容只会按照上方显示的范围使用。</p>") +
            QStringLiteral("</div>"));
    });

    usageLabel_ = new QLabel(QString(), this);
    usageLabel_->setObjectName(QStringLiteral("aiUsageLabel"));
    // 引用核验状态：整篇问答之后，这一行告诉用户「这些引用查过没有」。
    citationStatusLabel_ = new QLabel(QString(), this);
    citationStatusLabel_->setObjectName(QStringLiteral("aiCitationStatus"));
    citationStatusLabel_->setWordWrap(true);
    // 引用清单：每条引用一行，带核验结果。
    citationList_ = new QListWidget(this);
    citationList_->setObjectName(QStringLiteral("aiCitationList"));
    citationList_->setMaximumHeight(132);
    citationList_->setVisible(false);
    texts_.bind([this] { citationList_->setToolTip(tr("点一条引用，跳到原文并高亮那一块")); });

    prompt_ = new QLineEdit(this);
    prompt_->setObjectName(QStringLiteral("aiPromptInput"));
    texts_.bind([this] { prompt_->setPlaceholderText(tr("询问当前文档…")); });
    texts_.bind([this] { prompt_->setAccessibleName(tr("向 AI 提问")); });
    sendButton_ = new QPushButton(QString(), this);
    sendButton_->setObjectName(QStringLiteral("aiButton"));
    sendButton_->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    sendButton_->setIconSize(QSize(18, 18));
    sendButton_->setText(QString());
    texts_.bind([this] { sendButton_->setToolTip(tr("发送")); });
    sendButton_->setEnabled(false);
    cancelButton_ = new QPushButton(QString(), this);
    cancelButton_->setObjectName(QStringLiteral("aiCancelButton"));
    texts_.bind([this] { cancelButton_->setText(tr("停止")); });
    texts_.bind([this] { cancelButton_->setToolTip(tr("停止这次生成")); });
    cancelButton_->setEnabled(false);

    auto* composerFrame = new QFrame(this);
    composerFrame->setObjectName(QStringLiteral("aiComposer"));
    auto* composer = new QHBoxLayout(composerFrame);
    composer->setContentsMargins(8, 6, 6, 6);
    composer->setSpacing(6);
    composer->addWidget(prompt_, 1);
    composer->addWidget(sendButton_);
    composer->addWidget(cancelButton_);

    layout->addWidget(header);
    layout->addWidget(contextCard);
    layout->addWidget(conversation_, 1);
    layout->addWidget(citationList_);
    layout->addWidget(citationStatusLabel_);
    layout->addWidget(usageLabel_);
    layout->addWidget(composerFrame);

    connect(summaryButton_, &QPushButton::clicked, this, &AiPanel::requestSummary);
    connect(explainButton_, &QPushButton::clicked, this, &AiPanel::requestExplain);
    connect(translateButton_, &QPushButton::clicked, this, &AiPanel::requestTranslate);
    connect(offlineButton_, &QPushButton::clicked, this, [this]() {
        requestOfflineQa(prompt_->text());
    });
    connect(sendButton_, &QPushButton::clicked, this, [this]() {
        requestAsk(prompt_->text());
    });
    connect(cancelButton_, &QPushButton::clicked, this, &AiPanel::cancelRequest);
    connect(settingsButton_, &QPushButton::clicked, this, &AiPanel::openSettings);
    connect(newChatButton_, &QPushButton::clicked, this, &AiPanel::startNewConversation);
    connect(prompt_, &QLineEdit::returnPressed, this, [this]() {
        requestAsk(prompt_->text());
    });
    // 范围切换：听 **toggled**，不听 clicked。
    //
    // QButtonGroup::buttonClicked 只在真实点击时发出；
    // 程序化 toggle（界面自动化的 TogglePattern 走的就是这条路）
    // 只发 toggled —— 听 clicked 的话，范围就切不过去，
    // 提问还是按当前页走。
    connect(scopeGroup_, &QButtonGroup::buttonToggled, this,
            [this](QAbstractButton* button, bool checked) {
                if (!checked) {
                    return;
                }
                if (button == scopeDocumentButton_) {
                    setScope(Scope::WholeDocument);
                } else {
                    setScope(Scope::CurrentPage);
                }
            });
    // 引用链接：点了跳到原文并高亮那一块。
    connect(conversation_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() != QLatin1String("citation")) {
            return;
        }
        // "citation://3" → host 是编号。
        int number = url.host().toInt();
        if (number <= 0) {
            number = url.path().toInt();
        }
        const ai::ContextBlock* block = contextByNumber(number);
        if (block == nullptr) {
            return;
        }
        trace(QStringLiteral("ai citation activated number=%1 page=%2")
                  .arg(number)
                  .arg(block->pageNumber));
        emit citationActivated(block->pageNumber - 1, block->chunkId);
    });
    // 引用清单：点一条跳到原文。
    connect(citationList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr) {
            return;
        }
        const int number = item->data(Qt::UserRole).toInt();
        const ai::ContextBlock* block = contextByNumber(number);
        if (block == nullptr) {
            return;
        }
        trace(QStringLiteral("ai citation activated number=%1 page=%2")
                  .arg(number)
                  .arg(block->pageNumber));
        emit citationActivated(block->pageNumber - 1, block->chunkId);
    });
}

void AiPanel::setWorkerClient(ipc::WorkerClient* client) {
    if (workerClient_ == client) {
        return;
    }
    if (workerClient_ != nullptr) {
        disconnect(workerClient_, nullptr, this, nullptr);
    }
    workerClient_ = client;
    if (workerClient_ == nullptr) {
        return;
    }

    connect(workerClient_, &ipc::WorkerClient::stateChanged, this,
            [this](const QString& state) {
                // Worker 状态变了：之前拿到的 Provider 状态已经过期，先回到状态行。
                workerState_ = state;
                providerStatusKnown_ = false;
                refreshStatusLabel();
                const bool ready = state == QLatin1String("ready");
                workerReady_ = ready;
                sendButton_->setEnabled(ready && !busy_);
                summaryButton_->setEnabled(ready);
                explainButton_->setEnabled(ready);
                translateButton_->setEnabled(ready);
                if (ready) {
                    refreshProviderStatus();
                }
            });

    connect(workerClient_, &ipc::WorkerClient::eventReceived, this,
            [this](const QString& requestId, const QString& event,
                   const QJsonObject& payload) {
                if (requestId != activeRequestId_) {
                    return;
                }
                if (event == QLatin1String("ai.stream.delta")) {
                    const QString text = payload.value("text").toString();
                    if (text.isEmpty()) {
                        return;
                    }
                    lastAnswer_ += text;
                    if (documentQaActive_) {
                        // 整篇问答：先攒着，等回答收完再连同引用一起渲染。
                        refreshUsageLabel();
                        return;
                    }
                    answerCursor_.insertText(text);
                    return;
                }
                if (event == QLatin1String("ai.stream.done")) {
                    const int inputTokens = payload.value("input_tokens").toInt();
                    const int outputTokens = payload.value("output_tokens").toInt();
                    const double cost = payload.value("estimated_cost").toDouble();
                    lastInputTokens_ = inputTokens;
                    lastOutputTokens_ = outputTokens;
                    lastCost_ = cost;
                    usageKnown_ = true;
                    trace(QStringLiteral("ai usage input=%1 output=%2 cost=%3")
                              .arg(inputTokens)
                              .arg(outputTokens)
                              .arg(cost, 0, 'f', 6));
                    emit usageReported(inputTokens, outputTokens, cost);
                    if (documentQaActive_) {
                        finishDocumentQa();
                    }
                    commitPendingTurn();
                    setBusy(false);
                    // 收完后才把「正在生成…」换成费用行；顺序反了会停在进度文案上。
                    refreshUsageLabel();
                    return;
                }
                if (event == QLatin1String("ai.stream.error")) {
                    appendMessage(tr("<b>错误：</b>") +
                                  payload.value("message").toString().toHtmlEscaped());
                    trace(QStringLiteral("ai error code=%1")
                              .arg(payload.value("code").toString()));
                    documentQaActive_ = false;
                    pendingUserTurn_.clear();
                    setBusy(false);
                    return;
                }
                if (event == QLatin1String("ai.stream.cancelled")) {
                    appendMessage(tr("<i>（已停止）</i>"));
                    trace(QStringLiteral("ai cancelled"));
                    documentQaActive_ = false;
                    pendingUserTurn_.clear();
                    setBusy(false);
                    return;
                }
            });

    connect(workerClient_, &ipc::WorkerClient::responseReceived, this,
            [this](const QString& requestId, const QJsonObject& response) {
                const QJsonObject payload = response.value("payload").toObject();
                if (payload.value("status").toString() == QLatin1String("streaming")) {
                    // 「开始了」的回执：界面切到生成中。
                    setBusy(true);
                    return;
                }
                if (payload.contains("provider")) {
                    // Provider 状态：现在是哪个服务、是不是演示模式。
                    const bool demo = payload.value("demo").toBool();
                    const QString mode = payload.value("data_mode").toString();
                    // 缓存原始状态：切换语言时按同一份状态重算状态行，
                    // 不能把上一轮翻好的字符串再翻一遍。
                    providerStatusKnown_ = true;
                    providerName_ = payload.value("provider").toString();
                    providerModel_ = payload.value("model").toString();
                    providerMode_ = mode;
                    providerDemo_ = demo;
                    refreshStatusLabel();
                    trace(QStringLiteral("ai provider mode=%1 demo=%2")
                              .arg(mode.isEmpty() ? QStringLiteral("unknown") : mode)
                              .arg(demo ? 1 : 0));
                    // 演示模式的说明放在服务状态行，不放在发送范围行：
                    // 范围行要留给用户看「这一趟送了什么」。
                    emit providerChanged(providerDisplayName(), demo);
                    return;
                }
                Q_UNUSED(requestId);
            });

    connect(workerClient_, &ipc::WorkerClient::requestFailed, this,
            [this](const QString& requestId, const QString& message) {
                if (!activeRequestId_.isEmpty() && requestId != activeRequestId_) {
                    return;
                }
                appendMessage(tr("<b>错误：</b>") + message.toHtmlEscaped());
                pendingUserTurn_.clear();
                setBusy(false);
            });
}

void AiPanel::setTextSource(TextController* controller) {
    if (textController_ == controller) {
        return;
    }
    const bool switchingDocument = textController_ != nullptr;
    if (textController_ != nullptr) {
        disconnect(textController_, nullptr, this, nullptr);
    }
    textController_ = controller;
    if (switchingDocument) {
        startNewConversation();
    }
    if (textController_ != nullptr) {
        // 文本层到了：之前等着要发的任务可以发了。
        connect(textController_, &TextController::textPageReady, this,
                [this](int pageIndex) {
                    if (pageIndex != currentPage_ || pendingTask_.isEmpty()) {
                        return;
                    }
                    const QString task = pendingTask_;
                    const QString question = pendingQuestion_;
                    pendingTask_.clear();
                    pendingQuestion_.clear();
                    sendTask(task, question);
                });
    }
}

void AiPanel::setQaController(DocumentQaController* controller) {
    if (qaController_ == controller) {
        return;
    }
    if (qaController_ != nullptr) {
        disconnect(qaController_, nullptr, this, nullptr);
    }
    qaController_ = controller;
    if (qaController_ != nullptr) {
        // 控制器换了（换文档、重建索引）就重新指一次。
        connect(qaController_, &DocumentQaController::vectorIndexReady, this,
                [this](int) {
                    if (!pendingQaQuestion_.isEmpty()) {
                        const QString question = pendingQaQuestion_;
                        pendingQaQuestion_.clear();
                        sendDocumentQa(question);
                    }
                });
        // 控制器被销毁时（换文档会重建它）要把指针清掉：
        // 留着就是野指针，用户下一次提问会踩到。
        connect(qaController_, &QObject::destroyed, this, [this]() {
            qaController_ = nullptr;
        });
        // 向量索引建不出来：排队的问题不能一直挂着。
        // 关键词那半边仍然能用，但要说清楚为什么没答。
        connect(qaController_, &DocumentQaController::vectorIndexFailed, this,
                [this](const QString& message) {
                    if (!pendingQaQuestion_.isEmpty()) {
                        pendingQaQuestion_.clear();
                        appendMessage(tr("<b>提示：</b>向量索引建好了吗？%1。请重建索引后再问。")
                                          .arg(message.toHtmlEscaped()));
                    }
                });
    }
}

void AiPanel::setCurrentPage(int pageIndex) {
    currentPage_ = pageIndex;
}

void AiPanel::setIndexController(IndexController* controller) {
    indexController_ = controller;
}

void AiPanel::setOfflineAnswerProvider(OfflineAnswerProvider provider) {
    offlineAnswerProvider_ = std::move(provider);
}

// 本地离线问答：不联网、不调模型，答案从本地索引里**摘**出来。
//
// 找不到就说找不到：这时不显示任何答案正文，只列出最接近的几个片段，
// 让人自己判断——编一句「看起来像答案」的话比不给答案坏得多。
void AiPanel::requestOfflineQa(const QString& question) {
    lastOfflineAnswer_.clear();
    lastOfflineNotice_.clear();
    lastOfflineCitationLines_.clear();
    lastOfflineClosestLines_.clear();
    lastOfflineNoMatch_ = false;

    const QString asked = question.trimmed();
    if (asked.isEmpty()) {
        lastOfflineNotice_ = tr("先输入一个问题。");
        appendMessage(tr("<b>本地离线问答：</b>%1").arg(lastOfflineNotice_.toHtmlEscaped()));
        return;
    }
    if (indexController_ == nullptr) {
        lastOfflineNotice_ = tr("当前文档还没有索引：先在「索引」面板里建立索引，再问。");
        appendMessage(tr("<b>本地离线问答：</b>%1").arg(lastOfflineNotice_.toHtmlEscaped()));
        return;
    }
    const doc::LocalIndex& index = indexController_->index();
    if (index.isEmpty()) {
        lastOfflineNotice_ = tr("本地索引里还没有内容：先建立索引，再问。");
        appendMessage(tr("<b>本地离线问答：</b>%1").arg(lastOfflineNotice_.toHtmlEscaped()));
        return;
    }

    if (!offlineAnswerProvider_) {
        lastOfflineNotice_ = tr("本地离线问答还没有接上：当前文档没有可检索的索引。");
        appendMessage(tr("<b>本地离线问答：</b>%1").arg(lastOfflineNotice_.toHtmlEscaped()));
        return;
    }

    const ai::OfflineAnswer answer = offlineAnswerProvider_(asked);    lastOfflineNoMatch_ = answer.isNoMatch();
    lastOfflineAnswer_ = offlineAnswerText(answer);
    lastOfflineNotice_ = offlineNoMatchText(answer);
    lastOfflineCitationLines_ = offlineCitationLines(answer);
    lastOfflineClosestLines_ = offlineClosestLines(answer);

    QString html = QStringLiteral("<div class='message'><b>本地离线问答</b><br/>");
    if (lastOfflineNoMatch_) {
        html += QStringLiteral("<span class='bad'>%1</span><br/>")
                    .arg(lastOfflineNotice_.toHtmlEscaped());
        html += QStringLiteral("<span class='muted'>最接近的几个片段（请自己判断）：</span><br/>");
        for (const QString& line : lastOfflineClosestLines_) {
            html += QStringLiteral("· %1<br/>").arg(line.toHtmlEscaped());
        }
    } else {
        html += QStringLiteral("%1<br/>").arg(lastOfflineAnswer_.toHtmlEscaped());
        if (!lastOfflineCitationLines_.isEmpty()) {
            html += QStringLiteral("<div class='citations'><span class='muted'>引用：</span><br/>");
            for (const QString& line : lastOfflineCitationLines_) {
                html += QStringLiteral("· %1<br/>").arg(line.toHtmlEscaped());
            }
            html += QStringLiteral("</div>");
        }
    }
    html += QStringLiteral("</div>");
    appendMessage(html);
}

QString AiPanel::lastOfflineAnswer() const {
    return lastOfflineAnswer_;
}

QString AiPanel::lastOfflineNotice() const {
    return lastOfflineNotice_;
}

QStringList AiPanel::lastOfflineCitationLines() const {
    return lastOfflineCitationLines_;
}

QStringList AiPanel::lastOfflineClosestLines() const {
    return lastOfflineClosestLines_;
}

bool AiPanel::lastOfflineNoMatch() const {
    return lastOfflineNoMatch_;
}

QString AiPanel::currentPageText() const {
    if (textController_ == nullptr) {
        return QString();
    }
    const TextLayout* layout = textController_->layout(currentPage_);
    if (layout == nullptr || layout->isEmpty()) {
        return QString();
    }
    return layout->textForRange(0, layout->readingOrder().size());
}

QString AiPanel::currentSelection() const {
    if (textController_ == nullptr) {
        return QString();
    }
    return textController_->selectedText();
}

void AiPanel::requestSummary() {
    ensureTextThenRun(ai::Task::Summary, QString());
}

void AiPanel::requestExplain() {
    const QString selection = currentSelection().trimmed();
    if (selection.isEmpty()) {
        appendMessage(tr("<b>提示：</b>先在页面上选中一段文字，再点「解释」。"));
        trace(QStringLiteral("ai explain skipped reason=no_selection"));
        return;
    }
    sendTask(ai::Task::Explain, QString());
}

void AiPanel::requestTranslate() {
    const QString selection = currentSelection().trimmed();
    if (selection.isEmpty()) {
        appendMessage(tr("<b>提示：</b>先在页面上选中一段文字，再点「翻译」。"));
        trace(QStringLiteral("ai translate skipped reason=no_selection"));
        return;
    }
    sendTask(ai::Task::Translate, QString());
}

void AiPanel::requestAsk(const QString& question) {
    // 范围是「整篇文档」时，提问走检索那一条路。
    if (scope_ == Scope::WholeDocument) {
        requestDocumentQa(question);
        return;
    }
    ensureTextThenRun(ai::Task::Chat, question);
}

void AiPanel::requestDocumentQa(const QString& question) {
    if (workerClient_ == nullptr) {
        appendMessage(tr("<b>错误：</b>AI 服务还没连上。"));
        return;
    }
    if (busy_) {
        appendMessage(tr("<b>提示：</b>上一次还在生成，先点「停止」或等它结束。"));
        return;
    }
    if (qaController_ == nullptr) {
        appendMessage(tr("<b>错误：</b>整篇问答还没接上索引。"));
        return;
    }
    const QString asked = question.trimmed().isEmpty()
                              ? tr("这份文档主要讲了什么？")
                              : question.trimmed();

    // 没有索引就没法检索：先把索引建起来，建好了再问。
    if (!qaController_->isIndexReady()) {
        pendingQaQuestion_ = asked;
        appendMessage(tr("<i>正在建立索引，稍后自动回答…</i>"));
        trace(QStringLiteral("ai qa waiting for index"));
        qaController_->requestIndexing();
        return;
    }

    sendDocumentQa(asked);
}

void AiPanel::sendDocumentQa(const QString& question) {
    if (workerClient_ == nullptr || qaController_ == nullptr) {
        return;
    }

    // 检索：关键词 + 向量，RRF 融合。
    // 把最近的问题并入检索词，使“继续解释上一点”这类省略主语的追问
    // 仍能召回上一轮相关的文档块；真正显示给用户和模型的当前问题保持原样。
    const QVector<ai::ContextBlock> context =
        qaController_->retrieve(retrievalQuestion(question), 6);
    if (context.isEmpty()) {
        appendMessage(tr("<b>提示：</b>没有检索到相关材料，换个说法再问试试。"));
        trace(QStringLiteral("ai qa skipped reason=no_hits"));
        return;
    }
    qaContext_ = context;

    int characters = 0;
    for (const ai::ContextBlock& block : context) {
        characters += block.text.size();
    }
    sentBlockCount_ = static_cast<int>(context.size());
    sentCharCount_ = characters;
    scopeLabelState_ = ScopeLabelState::SentDocument;
    refreshScopeLabel();
    const QString scope = lastDataScope_;
    appendMessage(tr("<div class='scope'>▸ 本次发送：%1</div>").arg(scope.toHtmlEscaped()));
    trace(QStringLiteral("ai qa scope blocks=%1 chars=%2").arg(context.size()).arg(characters));

    // 云端请求前的隐私确认：本地模式在这里就被拒，片段/页面模式弹预览。
    ai::DataScope policyScope;
    policyScope.chunkCount = context.size();
    policyScope.charCount = characters;
    policyScope.hasExplicitPageSelection = true;
    QVector<int> pages;
    for (const ai::ContextBlock& block : context) {
        pages.append(block.pageNumber);
    }
    if (!confirmCloudUpload(policyScope, pages, QStringLiteral("qa"))) {
        return;
    }

    appendMessage(tr("<div class='user'>%1</div>").arg(question.toHtmlEscaped()));
    prompt_->clear();

    // 提示词：系统提示词要求带 [n] 标记与「引用：」段；
    // 用户消息里是带编号的上下文块，编号就是模型写 [n] 时要对上的号。
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), ai::qaSystemPrompt());
    messages.append(system);
    appendConversationHistory(messages);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"),
                QStringLiteral("材料：\n\n%1\n\n问题：%2")
                    .arg(ai::wrapMaterial(ai::formatContextBlocks(context)), question));
    messages.append(user);

    // 结构化的一份也带上：演示模式要靠它引用**真实的**块。
    QJsonArray contextArray;
    for (const ai::ContextBlock& block : context) {
        QJsonObject item;
        item.insert(QStringLiteral("number"), block.number);
        item.insert(QStringLiteral("page"), block.pageNumber);
        item.insert(QStringLiteral("text"), block.text);
        item.insert(QStringLiteral("chunk_id"), block.chunkId);
        contextArray.append(item);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("task"), ai::Task::DocumentQa);
    payload.insert(QStringLiteral("data_scope"), scope);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("context"), contextArray);

    lastAnswer_.clear();
    pendingUserTurn_ = question;
    lastCitationReport_ = doc::CitationReport();
    citationStatusVisible_ = false;
    citationStatusLabel_->clear();
    citationList_->clear();
    documentQaActive_ = true;
    // 整篇问答的回答**先攒着**，收完再连同引用核验结果一起渲染：
    // 边流边显示的话，引用链接要等回答结束才知道该链到哪一块。
    answerCursor_ = conversation_->textCursor();
    answerCursor_.movePosition(QTextCursor::End);
    activeRequestId_ = workerClient_->sendStreamRequest(
        QStringLiteral("ai.chat.stream"), payload);
    trace(QStringLiteral("ai qa request id=%1 blocks=%2")
              .arg(activeRequestId_)
              .arg(context.size()));
    setBusy(true);
}

bool AiPanel::confirmCloudUpload(const ai::DataScope& scope,
                                 const QVector<int>& pages,
                                 const QString& task) {
    const ai::ProviderConfig config = ai::ProviderConfig::load();
    const ai::DataMode mode = config.effectiveDataMode();

    // 演示 Provider 根本不联网：不需要预览，也不该写一条「云端请求」的审计。
    //
    // 用 resolveProviderKind 判断，而不是再手写一遍「有密钥且是 openai」：
    // 本地模式那条短路必须只有一处实现，否则界面预览与实际 Provider 迟早对不上。
    const bool cloud = ai::resolveProviderKind(config, config.hasCredential()) ==
                       ai::ProviderKind::OpenAi;
    if (!cloud) {
        if (!ai::mayUseCloud(mode)) {
            // 本地模式不是「没配好」，是用户明确选的策略：留一条能核对的轨迹。
            trace(QStringLiteral("ai upload skipped mode=%1 reason=local")
                      .arg(ai::dataModeId(mode)));
        }
        return true;
    }

    const ai::UploadPreview preview = ai::buildUploadPreview(config, scope, pages);
    bool allowed = preview.decision.allowed;
    if (!allowed) {
        appendMessage(tr("<b>没有发送：</b>%1").arg(preview.decision.reason.toHtmlEscaped()));
        trace(QStringLiteral("ai upload blocked mode=%1 reason=%2")
                  .arg(ai::dataModeId(mode), preview.decision.reason));
    } else {
        UploadPreviewDialog dialog(preview, this);
        allowed = dialog.exec() == QDialog::Accepted;
        trace(QStringLiteral("ai upload %1 mode=%2 host=%3 chunks=%4 chars=%5")
                  .arg(allowed ? QStringLiteral("confirmed") : QStringLiteral("cancelled"),
                       ai::dataModeId(mode),
                       preview.host)
                  .arg(scope.chunkCount)
                  .arg(scope.charCount));
    }

    // 审计只记元数据：谁在什么模式下、发了多少、发到哪、允不允许。
    ai::AuditEntry entry;
    entry.time = QDateTime::currentDateTimeUtc();
    entry.mode = ai::dataModeId(mode);
    entry.task = task;
    entry.chunkCount = scope.chunkCount;
    entry.charCount = scope.charCount;
    entry.host = preview.host;
    entry.allowed = allowed;
    ai::appendAudit(entry);
    return allowed;
}

void AiPanel::ensureTextThenRun(const QString& task, const QString& question) {
    if (workerClient_ == nullptr) {
        appendMessage(tr("<b>错误：</b>AI 服务还没连上。"));
        return;
    }
    if (busy_) {
        appendMessage(tr("<b>提示：</b>上一次还在生成，先点「停止」或等它结束。"));
        return;
    }
    // 解释与翻译要的是选区，不等文本层。
    if (task != QLatin1String("summary") && task != QLatin1String("chat")) {
        sendTask(task, question);
        return;
    }
    if (!currentPageText().isEmpty()) {
        sendTask(task, question);
        return;
    }
    // 这一页的文本还没抽出来：先要它，来了再发。
    pendingTask_ = task;
    pendingQuestion_ = question;
    if (textController_ != nullptr) {
        textController_->requestTextPage(currentPage_);
        if (pendingTask_.isEmpty()) {
            // requestTextPage 里同步补好了排版，任务已经发出去了。
            return;
        }
        if (textController_->hasTextPage(currentPage_)) {
            // 文本层已经在手边却是空的：这一页根本没有文字层。
            // 再等下去也不会有结果，面板会一直停在「正在提取…」。
            pendingTask_.clear();
            pendingQuestion_.clear();
            appendMessage(tr("<b>提示：</b>这一页没有可用的文本层。"));
            trace(QStringLiteral("ai skipped reason=no_text"));
            return;
        }
    }
    appendMessage(tr("<i>正在提取第 %1 页的文本…</i>").arg(currentPage_ + 1));
}

void AiPanel::sendTask(const QString& task, const QString& question) {
    if (workerClient_ == nullptr) {
        return;
    }
    // 材料：摘要与提问用当前页，解释与翻译用当前选区。
    QString material;
    if (task == QLatin1String("explain") || task == QLatin1String("translate")) {
        material = currentSelection();
    } else {
        material = currentPageText();
    }
    material = material.trimmed();
    if (material.isEmpty()) {
        appendMessage(tr("<b>提示：</b>这一页没有可用的文本层。"));
        trace(QStringLiteral("ai skipped reason=no_text"));
        return;
    }

    bool truncated = false;
    if (material.size() > kMaximumMaterialCharacters) {
        material = material.left(kMaximumMaterialCharacters);
        truncated = true;
    }

    // 用户可见的数据范围：送了什么、送了多少。
    sentPageNumber_ = currentPage_ + 1;
    sentCharCount_ = static_cast<int>(material.size());
    sentTruncated_ = truncated;
    scopeLabelState_ = ScopeLabelState::SentPage;
    refreshScopeLabel();
    const QString scope = lastDataScope_;
    appendMessage(tr("<div class='scope'>▸ 本次发送：%1</div>").arg(scope.toHtmlEscaped()));
    trace(QStringLiteral("ai scope page=%1 chars=%2 task=%3")
              .arg(currentPage_ + 1)
              .arg(material.size())
              .arg(task));

    // 云端请求前的隐私确认：本地模式在这里就被拒，片段/页面模式弹预览。
    ai::DataScope policyScope;
    policyScope.chunkCount = 1;  // 一次发送一个文本块（当前页或当前选区）
    policyScope.charCount = material.size();
    policyScope.hasExplicitPageSelection = true;
    if (!confirmCloudUpload(policyScope, QVector<int>{currentPage_ + 1}, task)) {
        return;
    }

    // 提示词由共享的构造器生成（app 与 Worker 用同一套）。
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), ai::systemPromptForTask(task));
    messages.append(system);
    appendConversationHistory(messages);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"),
                ai::userPromptForTask(task, material, question));
    messages.append(user);

    QJsonObject payload;
    payload.insert(QStringLiteral("task"), task);
    payload.insert(QStringLiteral("data_scope"), scope);
    payload.insert(QStringLiteral("messages"), messages);

    if (!question.trimmed().isEmpty()) {
        appendMessage(tr("<div class='user'>%1</div>").arg(question.trimmed().toHtmlEscaped()));
    }
    prompt_->clear();

    lastAnswer_.clear();
    if (!question.trimmed().isEmpty()) {
        pendingUserTurn_ = question.trimmed();
    } else if (task == QLatin1String("summary")) {
        pendingUserTurn_ = tr("请总结第 %1 页的要点").arg(currentPage_ + 1);
    } else if (task == QLatin1String("explain")) {
        pendingUserTurn_ = tr("请解释我选中的内容");
    } else if (task == QLatin1String("translate")) {
        pendingUserTurn_ = tr("请翻译我选中的内容");
    }
    answerCursor_ = conversation_->textCursor();
    answerCursor_.movePosition(QTextCursor::End);
    conversationStarted_ = true;
    answerCursor_.insertText(QStringLiteral("\n"));
    activeRequestId_ = workerClient_->sendStreamRequest(
        QStringLiteral("ai.chat.stream"), payload);
    trace(QStringLiteral("ai request id=%1 task=%2").arg(activeRequestId_).arg(task));
    setBusy(true);
}

void AiPanel::cancelRequest() {
    if (workerClient_ == nullptr || activeRequestId_.isEmpty()) {
        return;
    }
    trace(QStringLiteral("ai cancel requested id=%1").arg(activeRequestId_));
    workerClient_->cancelStreamRequest(activeRequestId_);
}

void AiPanel::focusPrompt() {
    if (prompt_ != nullptr) {
        prompt_->setFocus();
    }
}

void AiPanel::startNewConversation() {
    if (busy_) {
        cancelRequest();
        // 新会话不接收上一轮迟到的流事件；requestId 不再匹配后会被忽略。
        setBusy(false);
    }
    documentQaActive_ = false;
    conversationHistory_.clear();
    pendingUserTurn_.clear();
    lastAnswer_.clear();
    qaContext_.clear();
    lastCitationReport_ = doc::CitationReport();
    citationStatusVisible_ = false;
    citationList_->clear();
    citationList_->setVisible(false);
    citationStatusLabel_->clear();
    usageKnown_ = false;
    usageLabel_->clear();
    conversationStarted_ = false;
    conversation_->clear();
    // 欢迎内容由常驻文案绑定生成；这样切换语言后仍只有一套文案来源。
    retranslateUi();
    focusPrompt();
}

void AiPanel::openSettings() {
    const ai::ProviderConfig current = ai::ProviderConfig::load();

    AiSettingsDialog dialog(this);
    dialog.applyConfig(current.providerId,
                       current.baseUrl,
                       current.model,
                       current.hasCredential(),
                       current.inputPricePerMillion,
                       current.outputPricePerMillion,
                       current.timeoutMs,
                       current.dataMode);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    ai::ProviderConfig updated = current;
    updated.providerId = dialog.providerId();
    updated.baseUrl = dialog.baseUrl();
    updated.model = dialog.model();
    updated.inputPricePerMillion = dialog.inputPricePerMillion();
    updated.outputPricePerMillion = dialog.outputPricePerMillion();
    updated.timeoutMs = dialog.timeoutMs();
    // 数据策略：未知值在这里就被归一成 local（dataModeFromId 的兜底）。
    updated.dataMode = ai::dataModeId(ai::dataModeFromId(dialog.dataMode()));

    // 密钥单独处理：只进凭据管理器，不进配置文件、不进日志。
    const QString target =
        ai::CredentialStore::targetNameFor(updated.providerId);
    if (dialog.clearCredential()) {
        QString error;
        if (!ai::CredentialStore::remove(target, &error)) {
            QMessageBox::warning(this, tr("删除密钥失败"), error);
            return;
        }
        trace(QStringLiteral("ai credential removed provider=%1")
                  .arg(updated.providerId));
    }
    const QString key = dialog.apiKey();
    if (!key.isEmpty()) {
        QString error;
        if (!ai::CredentialStore::store(target, key, &error)) {
            QMessageBox::warning(this, tr("保存密钥失败"), error);
            return;
        }
        trace(QStringLiteral("ai credential stored provider=%1")
                  .arg(updated.providerId));
    }

    if (!updated.save()) {
        QMessageBox::warning(
            this,
            tr("保存设置失败"),
            tr("配置文件写不进去，检查 %1 是否可写。")
                .arg(ai::ProviderConfig::configFilePath()));
        return;
    }

    // 设置变了：Worker 下次建 Provider 时会重新读配置与凭据，
    // 这里立刻把新的服务商状态拉回来显示。
    refreshProviderStatus();
    appendMessage(tr("<div class='scope'>▸ 已更新 AI 服务设置：%1</div>")
                      .arg(updated.providerId.toHtmlEscaped()));
}

void AiPanel::finishDocumentQa() {
    documentQaActive_ = false;
    if (qaController_ == nullptr) {
        appendMessage(lastAnswer_.toHtmlEscaped());
        return;
    }

    // 核验：编号存在性 + 引文逐字核对 + 页码一致。全部离线、确定性。
    lastCitationReport_ = qaController_->verify(lastAnswer_, qaContext_);
    citationStatusVisible_ = true;
    refreshCitationView();
    const doc::CitationReport& report = lastCitationReport_;
    trace(QStringLiteral("ai citation check valid=%1 invalid=%2 unsupported=%3")
              .arg(report.validCount)
              .arg(report.invalidCount)
              .arg(report.hasUnsupportedMarker ? 1 : 0));

    appendMessage(renderQaAnswer());
}

QString AiPanel::citationListText(const doc::ParsedCitation& citation) const {
    const ai::ContextBlock* block = contextByNumber(citation.number);
    // 页码显示**实际的**那一页：模型写错了页，引用就该指到真的地方。
    const int page = block != nullptr ? block->pageNumber : citation.pageNumber;
    if (citation.valid) {
        return tr("[%1] 第 %2 页 · 已核实").arg(citation.number).arg(page);
    }
    return tr("[%1] 第 %2 页 · 无法核实（%3）")
        .arg(citation.number)
        .arg(page)
        .arg(citation.reason);
}

QString AiPanel::renderQaAnswer() const {
    QString html = QStringLiteral("<div class='message'>");
    html += linkifyMarkers(doc::CitationVerifier::answerBody(lastAnswer_));
    html += QStringLiteral("</div>");

    if (!lastCitationReport_.citations.isEmpty()) {
        html += QStringLiteral("<div class='citations'><div class='eyebrow'>") + tr("引用") +
                QStringLiteral("</div>");
        for (const doc::ParsedCitation& citation : lastCitationReport_.citations) {
            const ai::ContextBlock* block = contextByNumber(citation.number);
            // 页码显示**实际的**那一页：模型写错了页，引用就该指到真的地方。
            const int page = block != nullptr ? block->pageNumber : citation.pageNumber;
            const QString label =
                citation.valid
                    ? QStringLiteral("<span class='ok'>") + tr("已核实") +
                          QStringLiteral("</span>")
                    : QStringLiteral("<span class='bad'>") +
                          tr("无法核实：%1").arg(citation.reason.toHtmlEscaped()) +
                          QStringLiteral("</span>");
            // 只有核验通过的引用才可点：点一个查不到出处的引用没有意义。
            const QString link =
                citation.valid
                    ? QStringLiteral("<a href=\"citation://%1\">[%1]</a>").arg(citation.number)
                    : QStringLiteral("[%1]").arg(citation.number);
            html += QStringLiteral("<div class='citation'>") +
                    tr("%1 第 %2 页 · %3").arg(link, QString::number(page), label) +
                    QStringLiteral("</div>");
        }
        html += QStringLiteral("</div>");
    }
    return html;
}

QString AiPanel::linkifyMarkers(const QString& text) const {
    static const QRegularExpression pattern(QStringLiteral("\\[(\\d+)\\]"));
    QString result;
    result.reserve(text.size() + 32);
    int position = 0;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        result.append(text.mid(position, match.capturedStart() - position).toHtmlEscaped());
        const int number = match.captured(1).toInt();
        if (contextByNumber(number) != nullptr) {
            result.append(QStringLiteral("<a href=\"citation://%1\">[%1]</a>").arg(number));
        } else {
            // 不在这次材料里的编号不能链：点了跳不过去。
            result.append(QStringLiteral("[%1]").arg(number));
        }
        position = match.capturedEnd();
    }
    result.append(text.mid(position).toHtmlEscaped());
    return result;
}

const ai::ContextBlock* AiPanel::contextByNumber(int number) const {
    for (const ai::ContextBlock& block : qaContext_) {
        if (block.number == number) {
            return &block;
        }
    }
    return nullptr;
}

void AiPanel::setScope(Scope scope) {
    scope_ = scope;
    scopePageButton_->setChecked(scope == Scope::CurrentPage);
    scopeDocumentButton_->setChecked(scope == Scope::WholeDocument);
    citationStatusVisible_ = false;
    citationStatusLabel_->clear();
    scopeLabelState_ = scope == Scope::WholeDocument ? ScopeLabelState::DocumentHint
                                                    : ScopeLabelState::Empty;
    refreshScopeLabel();
}

void AiPanel::appendMessage(const QString& html) {
    conversationStarted_ = true;
    conversation_->append(html);
    conversation_->verticalScrollBar()->setValue(
        conversation_->verticalScrollBar()->maximum());
}

void AiPanel::appendConversationHistory(QJsonArray& messages) const {
    for (const ai::ChatMessage& turn : conversationHistory_) {
        QJsonObject message;
        message.insert(QStringLiteral("role"), turn.roleName());
        message.insert(QStringLiteral("content"), turn.content);
        messages.append(message);
    }
}

void AiPanel::commitPendingTurn() {
    const QString user = pendingUserTurn_.trimmed();
    const QString assistant = lastAnswer_.trimmed();
    pendingUserTurn_.clear();
    if (user.isEmpty() || assistant.isEmpty()) {
        return;
    }

    conversationHistory_.append(ai::ChatMessage::user(user));
    conversationHistory_.append(ai::ChatMessage::assistant(assistant));

    // 同时限制轮数和字符数。始终成对删除，避免历史从一条孤立的 assistant 开始。
    auto historyCharacters = [this]() {
        int total = 0;
        for (const ai::ChatMessage& message : conversationHistory_) {
            total += message.content.size();
        }
        return total;
    };
    while (conversationHistory_.size() > kMaximumHistoryMessages ||
           historyCharacters() > kMaximumHistoryCharacters) {
        if (!conversationHistory_.isEmpty()) {
            conversationHistory_.removeFirst();
        }
        if (!conversationHistory_.isEmpty()) {
            conversationHistory_.removeFirst();
        }
    }
}

QString AiPanel::retrievalQuestion(const QString& question) const {
    QStringList parts;
    int userTurns = 0;
    for (int index = conversationHistory_.size() - 1; index >= 0 && userTurns < 3; --index) {
        const ai::ChatMessage& message = conversationHistory_.at(index);
        if (message.role != ai::ChatMessage::Role::User) {
            continue;
        }
        parts.prepend(message.content.left(300));
        ++userTurns;
    }
    parts.append(question.trimmed());
    return parts.join(QLatin1Char('\n'));
}

QString AiPanel::providerDisplayName() const {
    if (providerMode_ == QLatin1String("local")) {
        // 本地模式比「没配密钥」更强：这里不是降级，而是策略。
        // 只写「演示模式」会让用户以为配了密钥就能发出去。
        return tr("本地模式 · 文档内容不出本机（回答由本地演示模型生成）");
    }
    if (providerDemo_) {
        return tr("演示模式（未配置密钥）· 回答由本地拼出，不会发到网络");
    }
    return tr("%1 · %2").arg(providerName_, providerModel_);
}

void AiPanel::refreshStatusLabel() {
    if (providerLabel_ == nullptr) {
        return;
    }
    if (providerStatusKnown_) {
        providerLabel_->setText(tr("AI 服务：%1").arg(providerDisplayName()));
        return;
    }
    if (workerState_.isEmpty()) {
        providerLabel_->setText(tr("AI 服务：正在启动"));
        return;
    }
    const QString label = workerState_ == QStringLiteral("ready") ? tr("已就绪")
        : workerState_ == QStringLiteral("starting") ? tr("正在启动")
        : workerState_ == QStringLiteral("stopped") ? tr("已停止")
        : workerState_;
    providerLabel_->setText(tr("AI 服务：%1").arg(label));
}

void AiPanel::refreshUsageLabel() {
    if (usageLabel_ == nullptr) {
        return;
    }
    if (documentQaActive_ && busy_) {
        usageLabel_->setText(tr("正在生成…已收到 %1 字").arg(lastAnswer_.size()));
        return;
    }
    if (!usageKnown_) {
        usageLabel_->clear();
        return;
    }
    usageLabel_->setText(tr("本次：输入 %1 tokens · 输出 %2 tokens · 约 %3")
                             .arg(lastInputTokens_)
                             .arg(lastOutputTokens_)
                             .arg(formatCost(lastCost_)));
}

void AiPanel::refreshScopeLabel() {
    if (scopeLabel_ == nullptr) {
        return;
    }
    switch (scopeLabelState_) {
    case ScopeLabelState::Empty:
        scopeLabel_->clear();
        break;
    case ScopeLabelState::DocumentHint:
        scopeLabel_->setText(
            tr("本次将问整篇文档：先检索最相关的几块，回答里带可核对的引用。"));
        break;
    case ScopeLabelState::SentPage:
        lastDataScope_ = tr("第 %1 页 · %2 字%3")
                             .arg(sentPageNumber_)
                             .arg(sentCharCount_)
                             .arg(sentTruncated_ ? tr("（已截断）") : QString());
        scopeLabel_->setText(tr("本次发送：%1").arg(lastDataScope_));
        break;
    case ScopeLabelState::SentDocument:
        lastDataScope_ = tr("整篇文档 · 检索到 %1 块 · %2 字")
                             .arg(sentBlockCount_)
                             .arg(sentCharCount_);
        scopeLabel_->setText(tr("本次发送：%1").arg(lastDataScope_));
        break;
    }
}

void AiPanel::refreshCitationView() {
    if (citationStatusLabel_ == nullptr || citationList_ == nullptr) {
        return;
    }
    const doc::CitationReport& report = lastCitationReport_;
    if (!citationStatusVisible_) {
        citationStatusLabel_->clear();
    } else {
        QString status;
        if (report.citations.isEmpty()) {
            status = tr("引用核验：回答里没有给出引用");
        } else if (report.validCount == 0) {
            status = tr("引用核验：%1 条引用全部无法核实").arg(report.citations.size());
        } else {
            status = tr("引用核验：%1 条中 %2 条已核实")
                         .arg(report.citations.size())
                         .arg(report.validCount);
            if (report.invalidCount > 0) {
                status += tr("，%1 条无法核实").arg(report.invalidCount);
            }
        }
        if (report.hasUnsupportedMarker) {
            status += tr("；有标记指向无效引用");
        }
        citationStatusLabel_->setText(status);
    }

    // 引用清单：每条引用一行，带核验结果。
    citationList_->clear();
    for (const doc::ParsedCitation& citation : report.citations) {
        auto* item = new QListWidgetItem(citationListText(citation), citationList_);
        item->setData(Qt::UserRole, citation.number);
        // 核验不通过的引用不给跳：点了也跳不出一个可信的出处。
        if (!citation.valid) {
            item->setToolTip(citation.reason);
            item->setForeground(QColor(QStringLiteral("#C0562F")));
        } else {
            const ai::ContextBlock* block = contextByNumber(citation.number);
            item->setToolTip(block != nullptr
                                 ? tr("跳到第 %1 页").arg(block->pageNumber)
                                 : QString());
        }
    }
    citationList_->setVisible(!report.citations.isEmpty());
}

void AiPanel::retranslateUi() {
    texts_.retranslate();
    // 随状态变化的文案按缓存的原始状态重算，不把已翻译过的字符串再翻一遍。
    refreshStatusLabel();
    refreshScopeLabel();
    refreshUsageLabel();
    refreshCitationView();
}

void AiPanel::setBusy(bool busy) {
    busy_ = busy;
    // Worker 掉线时不能把「发送」点亮：点了也只会换来一句「未连接」。
    sendButton_->setEnabled(!busy && workerReady_);
    cancelButton_->setEnabled(busy);
    if (!busy) {
        activeRequestId_.clear();
    }
}

void AiPanel::refreshProviderStatus() {
    if (workerClient_ == nullptr) {
        return;
    }
    workerClient_->sendRequest(QStringLiteral("ai.provider.status"));
}

QString AiPanel::statusText() const {
    return providerLabel_->text();
}

QString AiPanel::activeRequestId() const {
    return activeRequestId_;
}

QString AiPanel::lastAnswer() const {
    return lastAnswer_;
}

QString AiPanel::lastDataScope() const {
    return lastDataScope_;
}

AiPanel::Scope AiPanel::scope() const {
    return scope_;
}

doc::CitationReport AiPanel::lastCitationReport() const {
    return lastCitationReport_;
}

QString AiPanel::citationStatusText() const {
    return citationStatusLabel_ != nullptr ? citationStatusLabel_->text() : QString();
}

QVector<ai::ContextBlock> AiPanel::lastContext() const {
    return qaContext_;
}

}  // namespace aipdf::app
