#pragma once

#include <QObject>
#include <QString>
#include <QTextCursor>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextBrowser;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

class TextController;

// AI 面板：三个受限的 AI 功能 + 流式显示。
//
// 「受限」是这个面板的底线：
//   - 送出去的内容只有当前页文本或当前选区，绝不默认送整份文档；
//   - 每次发送前先把「本次发送范围」显示出来，用户看得见送了什么；
//   - 四个任务各有固定系统提示词，模型不会越出这个范围。
//
// 面板自己不碰网络：它把请求交给 Worker（独立进程），
// 由 Worker 里的 Provider 去调模型，增量文本以事件帧流回来。
class AiPanel final : public QWidget {
    Q_OBJECT

public:
    explicit AiPanel(QWidget* parent = nullptr);

    // 绑定 Worker 客户端。
    void setWorkerClient(ipc::WorkerClient* client);
    // 绑定当前文档的文本控制器（拿当前页文本与选区）。
    void setTextSource(TextController* controller);
    // 当前页变了。
    void setCurrentPage(int pageIndex);

    // 三个受限功能 + 就当前页提问。界面按钮与验收脚本都走这几个入口。
    void requestSummary();
    void requestExplain();
    void requestTranslate();
    void requestAsk(const QString& question);

    // 取消当前这次生成。
    void cancelRequest();

    // 打开「AI 服务设置」：选服务商、填地址与模型、存 API 密钥。
    void openSettings();

    // 把焦点放到输入框（欢迎页的「开始聊天」按钮用）。
    void focusPrompt();

    // 状态文字（Worker 状态 / 生成状态）。
    QString statusText() const;
    // 正在进行的请求 id（没有则为空）。
    QString activeRequestId() const;
    // 最近一次回答的全文（验收与测试用）。
    QString lastAnswer() const;
    // 本次发送范围的最后一次记录。
    QString lastDataScope() const;

signals:
    // Provider 变了（名字与是不是演示模式）。
    void providerChanged(const QString& providerName, bool demo);
    // 一次请求结束，带 token 用量与估算费用。
    void usageReported(int inputTokens, int outputTokens, double estimatedCost);

private:
    void buildUi();
    // 文本还没抽出来时：记下要做的任务，等 textPageReady 到了再发。
    void ensureTextThenRun(const QString& task, const QString& question);
    void sendTask(const QString& task, const QString& question);
    QString currentPageText() const;
    QString currentSelection() const;
    void appendMessage(const QString& html);
    void updateStatus(const QString& text);
    void setBusy(bool busy);
    void refreshProviderStatus();

    ipc::WorkerClient* workerClient_ = nullptr;
    TextController* textController_ = nullptr;
    int currentPage_ = 0;

    QLabel* providerLabel_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QLabel* usageLabel_ = nullptr;
    QTextBrowser* conversation_ = nullptr;
    QLineEdit* prompt_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* summaryButton_ = nullptr;
    QPushButton* explainButton_ = nullptr;
    QPushButton* translateButton_ = nullptr;
    QPushButton* settingsButton_ = nullptr;

    // 正在进行的流式请求。
    QString activeRequestId_;
    // 回答的插入位置（流式增量往这里塞）。
    QTextCursor answerCursor_;
    QString lastAnswer_;
    QString lastDataScope_;
    // 文本还没准备好时记下的任务。
    QString pendingTask_;
    QString pendingQuestion_;
    bool busy_ = false;
};

}  // namespace aipdf::app
