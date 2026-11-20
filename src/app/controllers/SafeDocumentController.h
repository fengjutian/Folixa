#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

class QEventLoop;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

// 用隔离进程打开文档的控制端。
//
// 为什么要有它：PDF 的解析（尤其是畸形文件触发的解析器缺陷）不该发生在主进程里。
// 这里让另一个进程去开、去渲染、去编辑、去保存，主进程只拿到图片、页数与表单域清单。
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

    // 当前打开的文档路径（保存回原文件时用）。
    QString path() const;
    // 打开时是否带了口令（界面提示「保存会不会丢加密」要用）。
    bool openedWithPassword() const;
    // 文档里的表单域清单：{name, type, value, readOnly, options, pageIndex}。
    QJsonArray formFields() const;
    // 某一页的显示尺寸（点）。主进程不自己算，问解析进程。
    QSizeF pageSizePt(int pageIndex) const;

    // 执行一条编辑指令（结构化 JSON，见 worker 的 pdf.edit）。
    bool edit(const QJsonObject& instruction, QString* error = nullptr);
    // 执行一批编辑指令（一次往返）。
    bool edit(const QJsonArray& instructions, QString* error = nullptr);
    // 按字段名填表单域。
    bool setFormFieldValue(const QString& fieldName, const QString& value, QString* error = nullptr);
    // 加一条批注：subtype 是 "text"（便签）或 "highlight"（高亮），
    // rect 用 PDF 用户坐标（左下角 + 右上角）。
    bool addAnnotation(int pageIndex,
                       const QString& subtype,
                       const QRectF& rect,
                       const QString& text,
                       QString* error = nullptr);

    // 把解析进程里那份文档写回磁盘（路径由调用方给）。
    // 成功返回 true；warnings 里是「这次保存丢了什么」的稳定代码。
    bool save(const QString& targetPath, QString* error = nullptr, QStringList* warnings = nullptr);
    // 保存之后解析进程里还有没有可编辑的文档（覆盖保存后它会重新打开）。
    bool documentStillOpen() const;

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
    // 当前打开的文档路径与「打开时是否带了口令」。
    QString path_;
    bool openedWithPassword_ = false;
    // 文档里的表单域清单（解析进程给的）。
    QJsonArray fields_;
    // 早于本地监听器收到的失败（例如请求发出时 socket 还没连上），
    // 这里先记下来，避免等超时才发现请求早就失败了。
    QHash<QString, QString> pendingFailures_;
};

}  // namespace aipdf::app
