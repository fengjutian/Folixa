#include "app/AiPanel.h"

#include "app/AiSettingsDialog.h"
#include "app/DocumentQaController.h"
#include "app/TextController.h"
#include "app/TextLayout.h"
#include "ai/ChatTypes.h"
#include "ai/CredentialStore.h"
#include "ai/ProviderConfig.h"
#include "ipc/WorkerClient.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QFile>
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

QString formatCost(double cost) {
    if (cost <= 0.0) {
        return QStringLiteral("—");
    }
    return QStringLiteral("$%1").arg(cost, 0, 'f', 4);
}

}  // namespace

AiPanel::AiPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void AiPanel::buildUi() {
    setObjectName(QStringLiteral("aiPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("✦  Folixa AI"), this);
    title->setObjectName(QStringLiteral("aiTitle"));
    auto* subtitle = new QLabel(tr("阅读、理解并定位当前文档内容"), this);
    subtitle->setObjectName(QStringLiteral("aiSubtitle"));

    providerLabel_ = new QLabel(tr("AI 服务：正在启动"), this);
    providerLabel_->setObjectName(QStringLiteral("aiProviderLabel"));
    settingsButton_ = new QPushButton(tr("设置"), this);
    settingsButton_->setObjectName(QStringLiteral("aiSettingsButton"));
    settingsButton_->setToolTip(tr("设置服务商与 API 密钥"));
    scopeLabel_ = new QLabel(QString(), this);
    scopeLabel_->setObjectName(QStringLiteral("aiScopeLabel"));
    scopeLabel_->setWordWrap(true);

    // 范围切换：两个可勾选的按钮，而不是下拉框。
    //
    // 为什么不用下拉框：范围是这个面板最要紧的一个选择（「送什么给模型」），
    // 用两个能同时看见当前状态的按钮，用户一眼就知道现在送的是哪半边；
    // 下拉框把当前选择藏起来，得点开才知道。
    scopePageButton_ = new QPushButton(tr("当前页"), this);
    scopePageButton_->setObjectName(QStringLiteral("aiScopePageButton"));
    scopePageButton_->setCheckable(true);
    scopePageButton_->setChecked(true);
    scopePageButton_->setToolTip(tr("只送当前页文本或当前选区"));
    scopeDocumentButton_ = new QPushButton(tr("整篇文档"), this);
    scopeDocumentButton_->setObjectName(QStringLiteral("aiScopeDocumentButton"));
    scopeDocumentButton_->setCheckable(true);
    scopeDocumentButton_->setToolTip(tr("检索整篇文档，只送最相关的几块，并给出可核对的引用"));
    scopeGroup_ = new QButtonGroup(this);
    scopeGroup_->setExclusive(true);
    scopeGroup_->addButton(scopePageButton_);
    scopeGroup_->addButton(scopeDocumentButton_);

    auto* scopeRow = new QHBoxLayout();
    scopeRow->setSpacing(6);
    scopeRow->addWidget(new QLabel(tr("范围"), this));
    scopeRow->addWidget(scopePageButton_);
    scopeRow->addWidget(scopeDocumentButton_);
    scopeRow->addStretch(1);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(6);
    summaryButton_ = new QPushButton(tr("总结"), this);
    summaryButton_->setObjectName(QStringLiteral("aiSummaryButton"));
    summaryButton_->setToolTip(tr("总结当前页"));
    explainButton_ = new QPushButton(tr("解释"), this);
    explainButton_->setObjectName(QStringLiteral("aiExplainButton"));
    explainButton_->setToolTip(tr("解释选中的文字"));
    translateButton_ = new QPushButton(tr("翻译"), this);
    translateButton_->setObjectName(QStringLiteral("aiTranslateButton"));
    translateButton_->setToolTip(tr("翻译选中的文字"));
    actions->addWidget(summaryButton_);
    actions->addWidget(explainButton_);
    actions->addWidget(translateButton_);
    actions->addStretch(1);

    conversation_ = new QTextBrowser(this);
    conversation_->setObjectName(QStringLiteral("aiConversation"));
    // 引用链接由应用自己处理：点了要跳到原文，不能让浏览器去打开。
    conversation_->setOpenLinks(false);
    conversation_->setOpenExternalLinks(false);
    conversation_->document()->setDefaultStyleSheet(QStringLiteral(
        "body{font-family:'Segoe UI','Microsoft YaHei UI';color:#202A3A;}"
        ".welcome{background:#FFFFFF;border:1px solid #E2DDF2;padding:14px;margin:4px;}"
        ".eyebrow{color:#765ECC;font-size:11px;font-weight:600;}"
        ".message{background:#FFFFFF;padding:10px;margin:8px 2px;}"
        ".user{background:#EAF0FF;padding:10px;margin:8px 2px;}"
        ".scope{color:#8792A3;font-size:11px;}"
        ".muted{color:#8792A3;font-size:11px;}"
        ".citations{background:#FFFFFF;border:1px solid #E2DDF2;padding:10px;margin:4px 2px;}"
        ".citation{font-size:12px;margin:3px 0;}"
        ".ok{color:#2E7D53;}"
        ".bad{color:#C0562F;}"
        "a{color:#765ECC;text-decoration:none;}"
        "a:hover{text-decoration:underline;}"));
    conversation_->setHtml(tr(
        "<div class='welcome'><div class='eyebrow'>FOLIXA AI</div>"
        "<h3>从当前文档开始</h3>"
        "<p>我可以帮你总结当前页、解释选中的文字、翻译选中的文字。</p>"
        "<p>切到「整篇文档」范围，我会先检索最相关的几块，再回答并给出<strong>可核对的引用</strong>。</p>"
        "<p class='muted'>每次只会送出当前页、当前选区或检索到的几块，发送前会先告诉你范围。</p></div>"));

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
    citationList_->setToolTip(tr("点一条引用，跳到原文并高亮那一块"));

    prompt_ = new QLineEdit(this);
    prompt_->setObjectName(QStringLiteral("aiPromptInput"));
    prompt_->setPlaceholderText(tr("就当前页提问…"));
    prompt_->setAccessibleName(tr("向 AI 提问"));
    sendButton_ = new QPushButton(tr("发送"), this);
    sendButton_->setObjectName(QStringLiteral("aiButton"));
    sendButton_->setIcon(QIcon(QStringLiteral(":/icons/send.svg")));
    sendButton_->setIconSize(QSize(18, 18));
    sendButton_->setToolTip(tr("发送"));
    sendButton_->setEnabled(false);
    cancelButton_ = new QPushButton(tr("停止"), this);
    cancelButton_->setObjectName(QStringLiteral("aiCancelButton"));
    cancelButton_->setToolTip(tr("停止这次生成"));
    cancelButton_->setEnabled(false);

    auto* composer = new QHBoxLayout();
    composer->setSpacing(6);
    composer->addWidget(prompt_, 1);
    composer->addWidget(sendButton_);
    composer->addWidget(cancelButton_);

    auto* providerRow = new QHBoxLayout();
    providerRow->setSpacing(6);
    providerRow->addWidget(providerLabel_, 1);
    providerRow->addWidget(settingsButton_);

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addLayout(providerRow);
    layout->addLayout(scopeRow);
    layout->addWidget(scopeLabel_);
    layout->addLayout(actions);
    layout->addWidget(conversation_, 1);
    layout->addWidget(citationList_);
    layout->addWidget(citationStatusLabel_);
    layout->addWidget(usageLabel_);
    layout->addLayout(composer);

    connect(summaryButton_, &QPushButton::clicked, this, &AiPanel::requestSummary);
    connect(explainButton_, &QPushButton::clicked, this, &AiPanel::requestExplain);
    connect(translateButton_, &QPushButton::clicked, this, &AiPanel::requestTranslate);
    connect(sendButton_, &QPushButton::clicked, this, [this]() {
        requestAsk(prompt_->text());
    });
    connect(cancelButton_, &QPushButton::clicked, this, &AiPanel::cancelRequest);
    connect(settingsButton_, &QPushButton::clicked, this, &AiPanel::openSettings);
    connect(prompt_, &QLineEdit::returnPressed, this, [this]() {
        requestAsk(prompt_->text());
    });
    connect(scopeGroup_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* button) {
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
                const QString label = state == QStringLiteral("ready") ? tr("已就绪")
                    : state == QStringLiteral("starting") ? tr("正在启动")
                    : state == QStringLiteral("stopped") ? tr("已停止")
                    : state;
                updateStatus(tr("AI 服务：%1").arg(label));
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
                        usageLabel_->setText(
                            tr("正在生成…已收到 %1 字").arg(lastAnswer_.size()));
                        return;
                    }
                    answerCursor_.insertText(text);
                    return;
                }
                if (event == QLatin1String("ai.stream.done")) {
                    const int inputTokens = payload.value("input_tokens").toInt();
                    const int outputTokens = payload.value("output_tokens").toInt();
                    const double cost = payload.value("estimated_cost").toDouble();
                    usageLabel_->setText(
                        tr("本次：输入 %1 tokens · 输出 %2 tokens · 约 %3")
                            .arg(inputTokens)
                            .arg(outputTokens)
                            .arg(formatCost(cost)));
                    trace(QStringLiteral("ai usage input=%1 output=%2 cost=%3")
                              .arg(inputTokens)
                              .arg(outputTokens)
                              .arg(cost, 0, 'f', 6));
                    emit usageReported(inputTokens, outputTokens, cost);
                    if (documentQaActive_) {
                        finishDocumentQa();
                    }
                    setBusy(false);
                    return;
                }
                if (event == QLatin1String("ai.stream.error")) {
                    appendMessage(tr("<b>错误：</b>") +
                                  payload.value("message").toString().toHtmlEscaped());
                    trace(QStringLiteral("ai error code=%1")
                              .arg(payload.value("code").toString()));
                    documentQaActive_ = false;
                    setBusy(false);
                    return;
                }
                if (event == QLatin1String("ai.stream.cancelled")) {
                    appendMessage(tr("<i>（已停止）</i>"));
                    trace(QStringLiteral("ai cancelled"));
                    documentQaActive_ = false;
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
                    const QString name = demo
                        ? tr("演示模式（未配置密钥）· 回答由本地拼出，不会发到网络")
                        : tr("%1 · %2")
                              .arg(payload.value("provider").toString(),
                                   payload.value("model").toString());
                    updateStatus(tr("AI 服务：%1").arg(name));
                    // 演示模式的说明放在服务状态行，不放在发送范围行：
                    // 范围行要留给用户看「这一趟送了什么」。
                    emit providerChanged(name, demo);
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
                setBusy(false);
            });
}

void AiPanel::setTextSource(TextController* controller) {
    if (textController_ == controller) {
        return;
    }
    if (textController_ != nullptr) {
        disconnect(textController_, nullptr, this, nullptr);
    }
    textController_ = controller;
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
    const QVector<ai::ContextBlock> context = qaController_->retrieve(question, 6);
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
    const QString scope = tr("整篇文档 · 检索到 %1 块 · %2 字")
                              .arg(context.size())
                              .arg(characters);
    lastDataScope_ = scope;
    scopeLabel_->setText(tr("本次发送：%1").arg(scope));
    appendMessage(tr("<div class='scope'>▸ 本次发送：%1</div>").arg(scope.toHtmlEscaped()));
    trace(QStringLiteral("ai qa scope blocks=%1 chars=%2").arg(context.size()).arg(characters));

    appendMessage(tr("<div class='user'>%1</div>").arg(question.toHtmlEscaped()));
    prompt_->clear();

    // 提示词：系统提示词要求带 [n] 标记与「引用：」段；
    // 用户消息里是带编号的上下文块，编号就是模型写 [n] 时要对上的号。
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), ai::qaSystemPrompt());
    messages.append(system);
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"),
                QStringLiteral("材料：\n\n%1\n\n问题：%2")
                    .arg(ai::formatContextBlocks(context), question));
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
    lastCitationReport_ = doc::CitationReport();
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
    const QString scope = tr("第 %1 页 · %2 字%3")
                              .arg(currentPage_ + 1)
                              .arg(material.size())
                              .arg(truncated ? tr("（已截断）") : QString());
    lastDataScope_ = scope;
    scopeLabel_->setText(tr("本次发送：%1").arg(scope));
    appendMessage(tr("<div class='scope'>▸ 本次发送：%1</div>").arg(scope.toHtmlEscaped()));
    trace(QStringLiteral("ai scope page=%1 chars=%2 task=%3")
              .arg(currentPage_ + 1)
              .arg(material.size())
              .arg(task));

    // 提示词由共享的构造器生成（app 与 Worker 用同一套）。
    QJsonArray messages;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), ai::systemPromptForTask(task));
    messages.append(system);
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
    answerCursor_ = conversation_->textCursor();
    answerCursor_.movePosition(QTextCursor::End);
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

void AiPanel::openSettings() {
    const ai::ProviderConfig current = ai::ProviderConfig::load();

    AiSettingsDialog dialog(this);
    dialog.applyConfig(current.providerId,
                       current.baseUrl,
                       current.model,
                       current.hasCredential(),
                       current.inputPricePerMillion,
                       current.outputPricePerMillion,
                       current.timeoutMs);
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
    const doc::CitationReport& report = lastCitationReport_;

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
    trace(QStringLiteral("ai citation check valid=%1 invalid=%2 unsupported=%3")
              .arg(report.validCount)
              .arg(report.invalidCount)
              .arg(report.hasUnsupportedMarker ? 1 : 0));

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
        html += QStringLiteral("<div class='citations'><div class='eyebrow'>引用</div>");
        for (const doc::ParsedCitation& citation : lastCitationReport_.citations) {
            const ai::ContextBlock* block = contextByNumber(citation.number);
            // 页码显示**实际的**那一页：模型写错了页，引用就该指到真的地方。
            const int page = block != nullptr ? block->pageNumber : citation.pageNumber;
            const QString label =
                citation.valid
                    ? QStringLiteral("<span class='ok'>已核实</span>")
                    : QStringLiteral("<span class='bad'>无法核实：%1</span>")
                          .arg(citation.reason.toHtmlEscaped());
            // 只有核验通过的引用才可点：点一个查不到出处的引用没有意义。
            const QString link =
                citation.valid
                    ? QStringLiteral("<a href=\"citation://%1\">[%1]</a>").arg(citation.number)
                    : QStringLiteral("[%1]").arg(citation.number);
            html += QStringLiteral("<div class='citation'>%1 第 %2 页 · %3</div>")
                        .arg(link, QString::number(page), label);
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
    citationStatusLabel_->clear();
    if (scope == Scope::WholeDocument) {
        scopeLabel_->setText(
            tr("本次将问整篇文档：先检索最相关的几块，回答里带可核对的引用。"));
    } else {
        scopeLabel_->clear();
    }
}

void AiPanel::appendMessage(const QString& html) {
    conversation_->append(html);
    conversation_->verticalScrollBar()->setValue(
        conversation_->verticalScrollBar()->maximum());
}

void AiPanel::updateStatus(const QString& text) {
    providerLabel_->setText(text);
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
