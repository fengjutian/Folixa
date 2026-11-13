#pragma once

#include "ai/ChatTypes.h"
#include "ai/DataPolicy.h"
#include "doc/CitationVerifier.h"

#include <QObject>
#include <QString>
#include <QTextCursor>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextBrowser;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class DocumentQaController;
class TextController;

// AI 面板：三个受限的 AI 功能 + 流式显示 + 整篇问答。
//
// 「受限」是这个面板的底线：
//   - 送出去的内容要么是当前页文本/当前选区，要么是**检索出来的**几块；
//   - 每次发送前先把「本次发送范围」显示出来，用户看得见送了什么；
//   - 任务各有固定系统提示词，模型不会越出这个范围。
//
// 面板自己不碰网络：它把请求交给 Worker（独立进程），
// 由 Worker 里的 Provider 去调模型，增量文本以事件帧流回来。
class AiPanel final : public QWidget {
    Q_OBJECT

public:
    // 这次送出去的范围。
    enum class Scope {
        CurrentPage,     // 当前页 / 当前选区
        WholeDocument,   // 整篇文档（检索出来的几块）
    };
    Q_ENUM(Scope)

    explicit AiPanel(QWidget* parent = nullptr);

    // 绑定 Worker 客户端。
    void setWorkerClient(ipc::WorkerClient* client);
    // 绑定当前文档的文本控制器（拿当前页文本与选区）。
    void setTextSource(TextController* controller);
    // 绑定整篇问答控制器（检索 + 引用核验）。
    void setQaController(DocumentQaController* controller);
    // 当前页变了。
    void setCurrentPage(int pageIndex);

    // 三个受限功能 + 就当前页提问。界面按钮与验收脚本都走这几个入口。
    void requestSummary();
    void requestExplain();
    void requestTranslate();
    void requestAsk(const QString& question);
    // 问整篇文档（与范围选择器无关，脚本与快捷入口都用它）。
    void requestDocumentQa(const QString& question);

    // 取消当前这次生成。
    void cancelRequest();

    // 打开「AI 服务设置」：选服务商、填地址与模型、存 API 密钥。
    void openSettings();

    // 把焦点放到输入框（欢迎页的「开始聊天」按钮用）。
    void focusPrompt();

    // 范围。
    Scope scope() const;
    void setScope(Scope scope);

    // 状态文字（Worker 状态 / 生成状态）。
    QString statusText() const;
    // 正在进行的请求 id（没有则为空）。
    QString activeRequestId() const;
    // 最近一次回答的全文（验收与测试用）。
    QString lastAnswer() const;
    // 本次发送范围的最后一次记录。
    QString lastDataScope() const;
    // 最近一次引用核验报告。
    doc::CitationReport lastCitationReport() const;
    // 引用核验状态文字（界面上显示的那一行）。
    QString citationStatusText() const;
    // 本次问答用到的上下文块（编号 → 页码 / 块 id）。
    QVector<ai::ContextBlock> lastContext() const;

signals:
    // Provider 变了（名字与是不是演示模式）。
    void providerChanged(const QString& providerName, bool demo);
    // 一次请求结束，带 token 用量与估算费用。
    void usageReported(int inputTokens, int outputTokens, double estimatedCost);
    // 用户点了某条引用：跳到那一页并高亮那一块。
    void citationActivated(int pageIndex, const QString& chunkId);

private:
    void buildUi();
    // 文本还没抽出来时：记下要做的任务，等 textPageReady 到了再发。
    void ensureTextThenRun(const QString& task, const QString& question);
    void sendTask(const QString& task, const QString& question);
    // 云端请求前的隐私确认：跑策略、必要时弹预览、写审计。
    // 返回 false 表示这次不发送（被策略拒绝，或用户点了取消）。
    bool confirmCloudUpload(const ai::DataScope& scope,
                            const QVector<int>& pages,
                            const QString& task);
    // 整篇问答：检索 → 拼上下文 → 发请求。
    void sendDocumentQa(const QString& question);
    // 回答收完：核验引用并渲染（带可点击的引用）。
    void finishDocumentQa();
    QString currentPageText() const;
    QString currentSelection() const;
    void appendMessage(const QString& html);
    void updateStatus(const QString& text);
    void setBusy(bool busy);
    void refreshProviderStatus();
    // 把正文里的 [n] 变成可点击的引用链接。
    QString linkifyMarkers(const QString& text) const;
    // 渲染带引用清单的回答。
    QString renderQaAnswer() const;
    // 引用清单里的一行（第 n 号引用）。
    QString citationListText(const doc::ParsedCitation& citation) const;
    const ai::ContextBlock* contextByNumber(int number) const;

    ipc::WorkerClient* workerClient_ = nullptr;
    TextController* textController_ = nullptr;
    DocumentQaController* qaController_ = nullptr;
    int currentPage_ = 0;
    Scope scope_ = Scope::CurrentPage;

    QLabel* providerLabel_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QLabel* usageLabel_ = nullptr;
    QLabel* citationStatusLabel_ = nullptr;
    // 引用清单：每条引用一行，带核验结果，点一行跳到原文。
    //
    // 为什么是真正的控件而不是一段 HTML：引用是这个功能的**产物**，
    // 用户要能一眼看到每条引用核过没有、点哪一条跳哪一页。
    QListWidget* citationList_ = nullptr;
    QTextBrowser* conversation_ = nullptr;
    QLineEdit* prompt_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* summaryButton_ = nullptr;
    QPushButton* explainButton_ = nullptr;
    QPushButton* translateButton_ = nullptr;
    QPushButton* settingsButton_ = nullptr;
    QPushButton* scopePageButton_ = nullptr;
    QPushButton* scopeDocumentButton_ = nullptr;
    QButtonGroup* scopeGroup_ = nullptr;

    // 正在进行的流式请求。
    QString activeRequestId_;
    // 回答的插入位置（流式增量往这里塞）。
    QTextCursor answerCursor_;
    QString lastAnswer_;
    QString lastDataScope_;
    // 本次问答的上下文块（引用编号 → 页码 / 块 id）。
    QVector<ai::ContextBlock> qaContext_;
    // 最近一次的引用核验报告。
    doc::CitationReport lastCitationReport_;
    // 文本还没准备好时记下的任务。
    QString pendingTask_;
    QString pendingQuestion_;
    // 索引还没建好时排队的「问整篇」问题。
    QString pendingQaQuestion_;
    bool busy_ = false;
    // 这一轮是不是「整篇问答」。
    bool documentQaActive_ = false;
    // Worker 在不在：Worker 掉线后不能把「发送」按钮点亮。
    bool workerReady_ = false;
};

}  // namespace aipdf::app
