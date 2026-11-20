#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QEventLoop;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

// 用隔离进程打开文档的控制端。
//
// 为什么要有它：PDF 的解析（尤其是畸形文件触发的解析器缺陷）不该发生在主进程里。
// 这里让另一个进程去开、去渲染，主进程只拿到图片与页数。
//
// 安全边界：主进程只通过本地 socket 发请求，**从不读取这个 PDF 的字节**。
// 密码只出现在请求载荷里，不写日志、不进错误消息。
class SafeDocumentController final : public QObject {
    Q_OBJECT

public:
    explicit SafeDocumentController(QObject* parent = nullptr);
    ~SafeDocumentController() override;

    // 设置解析进程可执行文件路径（默认在应用同目录找 PdfParseWorker）。
    void setWorkerExecutable(const QString& path);

    // 打开文档。成功返回 true。
    bool open(const QString& path, const QString& password, QString* error = nullptr);
    int pageCount() const;
    // 渲染一页，返回 PNG 文件路径（空串表示失败）。
    QString renderPage(int pageIndex, double scale, QString* error = nullptr);
    void close();
    bool isOpen() const;

private:
    // 同步发一次请求：内部等结果，带超时；超时按失败处理并写明原因。
    bool callSync(const QString& operation,
                  const QJsonObject& payload,
                  QJsonObject* result,
                  QString* error);
    // 等解析进程连上（第一次调用时把它拉起来）。
    bool ensureWorkerReady(QString* error);
    // 结束解析进程：不留孤儿。
    void shutdownWorker();
    void handleStateChanged(const QString& state);
    void handleRequestFailed(const QString& requestId, const QString& message);
    static QString defaultWorkerExecutable();

    ipc::WorkerClient* client_ = nullptr;
    // 等解析进程连上时用的局部事件循环：连接状态一变就把它叫醒。
    // 放在成员里是为了让常驻的 stateChanged 回调知道该叫醒谁。
    QEventLoop* waitLoop_ = nullptr;
    // 每一页的显示尺寸与旋转（解析进程算好的，主进程不自己算）。
    QJsonArray pages_;
    int pageCount_ = 0;
    bool open_ = false;
    bool connected_ = false;
    QString workerExecutable_;
    // 早于本地监听器收到的失败（例如请求发出时 socket 还没连上），
    // 这里先记下来，避免等超时才发现请求早就失败了。
    QHash<QString, QString> pendingFailures_;
};

}  // namespace aipdf::app
