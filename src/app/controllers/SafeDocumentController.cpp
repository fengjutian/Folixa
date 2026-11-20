#include "app/controllers/SafeDocumentController.h"

#include "ipc/WorkerClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

namespace aipdf::app {
namespace {

// 等解析进程连上的上限：拉起进程 + 建立本地 socket 要时间，但也不能无限等。
constexpr int kConnectTimeoutMs = 10'000;
// 单次请求的上限。渲染一页是毫秒级，超时基本等于进程卡住了。
constexpr int kRequestTimeoutMs = 15'000;
// 事件循环的兜底时限：比请求超时多留 2 秒，
// 用来接住「请求超时信号还没来，但进程已经没响应了」的情况。
constexpr int kEventLoopTimeoutMs = kRequestTimeoutMs + 2'000;

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

SafeDocumentController::SafeDocumentController(QObject* parent)
    : QObject(parent) {
    client_ = new ipc::WorkerClient(this);
    client_->setWorkerExecutable(defaultWorkerExecutable());
    // 解析进程与 AI Worker 是两个进程：这里用的是 PdfParseWorker，
    // 它不持有任何凭据，也不发网络请求。
    client_->setServerName(QStringLiteral("aipdf-parse-worker-") +
                           QUuid::createUuid().toString(QUuid::WithoutBraces));
    connect(client_, &ipc::WorkerClient::stateChanged,
            this, &SafeDocumentController::handleStateChanged);
    connect(client_, &ipc::WorkerClient::requestFailed,
            this, &SafeDocumentController::handleRequestFailed);
}

SafeDocumentController::~SafeDocumentController() {
    shutdownWorker();
}

void SafeDocumentController::setWorkerExecutable(const QString& path) {
    workerExecutable_ = path;
    if (client_ != nullptr) {
        client_->setWorkerExecutable(path);
    }
}

bool SafeDocumentController::open(const QString& path,
                                  const QString& password,
                                  QString* error) {
    if (path.isEmpty()) {
        setError(error, QStringLiteral("没有指定要打开的文件。"));
        return false;
    }
    if (!ensureWorkerReady(error)) {
        return false;
    }

    // 换文档时直接再发一次 pdf.open：解析进程内部会先把旧文档关掉，
    // 不必先发 pdf.close（那会让解析进程退出）。
    QJsonObject payload;
    payload.insert(QStringLiteral("path"), path);
    payload.insert(QStringLiteral("password"), password);

    QJsonObject result;
    if (!callSync(QStringLiteral("pdf.open"), payload, &result, error)) {
        return false;
    }

    pageCount_ = result.value(QStringLiteral("pageCount")).toInt();
    pages_ = result.value(QStringLiteral("pages")).toArray();
    fields_ = result.value(QStringLiteral("fields")).toArray();
    path_ = path;
    openedWithPassword_ = !password.isEmpty();
    open_ = true;
    return true;
}

int SafeDocumentController::pageCount() const {
    return pageCount_;
}

QString SafeDocumentController::path() const {
    return path_;
}

bool SafeDocumentController::openedWithPassword() const {
    return openedWithPassword_;
}

QJsonArray SafeDocumentController::formFields() const {
    return fields_;
}

QSizeF SafeDocumentController::pageSizePt(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= pages_.size()) {
        return {};
    }
    const QJsonObject page = pages_.at(pageIndex).toObject();
    return QSizeF(page.value(QStringLiteral("widthPt")).toDouble(),
                  page.value(QStringLiteral("heightPt")).toDouble());
}

QString SafeDocumentController::renderPage(int pageIndex, double scale, QString* error) {
    if (!open_) {
        setError(error, QStringLiteral("文档还没有打开，无法渲染。"));
        return {};
    }

    // 页码先在本地拦一道：省掉一次没必要的往返。
    const int knownPages = qMax(pageCount_, pages_.size());
    if (knownPages > 0 && (pageIndex < 0 || pageIndex >= knownPages)) {
        setError(error, QStringLiteral("页码超出文档范围（共 %1 页）。").arg(knownPages));
        return {};
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("pageIndex"), pageIndex);
    payload.insert(QStringLiteral("scale"), scale);

    QJsonObject result;
    if (!callSync(QStringLiteral("pdf.render"), payload, &result, error)) {
        return {};
    }
    return result.value(QStringLiteral("imagePath")).toString();
}

bool SafeDocumentController::edit(const QJsonObject& instruction, QString* error) {
    return edit(QJsonArray{instruction}, error);
}

bool SafeDocumentController::edit(const QJsonArray& instructions, QString* error) {
    if (!open_) {
        setError(error, QStringLiteral("文档还没有打开，无法编辑。"));
        return false;
    }
    if (instructions.isEmpty()) {
        setError(error, QStringLiteral("没有要执行的编辑指令。"));
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("instructions"), instructions);

    QJsonObject result;
    if (!callSync(QStringLiteral("pdf.edit"), payload, &result, error)) {
        return false;
    }
    // 解析进程回一份最新的字段清单：界面按它刷新。
    if (result.contains(QStringLiteral("fields"))) {
        fields_ = result.value(QStringLiteral("fields")).toArray();
    }
    return true;
}

bool SafeDocumentController::setFormFieldValue(const QString& fieldName,
                                                const QString& value,
                                                QString* error) {
    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("setFormFieldValue"));
    instruction.insert(QStringLiteral("field"), fieldName);
    instruction.insert(QStringLiteral("value"), value);
    return edit(instruction, error);
}

bool SafeDocumentController::addAnnotation(int pageIndex,
                                           const QString& subtype,
                                           const QRectF& rect,
                                           const QString& text,
                                           QString* error) {
    QJsonObject instruction;
    instruction.insert(QStringLiteral("type"), QStringLiteral("addAnnotation"));
    instruction.insert(QStringLiteral("pageIndex"), pageIndex);
    instruction.insert(QStringLiteral("subtype"), subtype);
    // 矩形用 PDF 用户坐标：left, bottom, right, top。
    instruction.insert(QStringLiteral("rect"),
                       QJsonArray{rect.left(), rect.bottom(), rect.right(), rect.top()});
    instruction.insert(QStringLiteral("text"), text);
    return edit(instruction, error);
}

bool SafeDocumentController::save(const QString& targetPath,
                                  QString* error,
                                  QStringList* warnings) {
    if (!open_) {
        setError(error, QStringLiteral("文档还没有打开，无法保存。"));
        return false;
    }
    if (targetPath.isEmpty()) {
        setError(error, QStringLiteral("没有指定保存到哪里。"));
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("path"), targetPath);

    QJsonObject result;
    if (!callSync(QStringLiteral("pdf.save"), payload, &result, error)) {
        return false;
    }

    if (warnings != nullptr) {
        warnings->clear();
        const QJsonArray codes = result.value(QStringLiteral("warnings")).toArray();
        for (const QJsonValue& code : codes) {
            warnings->append(code.toString());
        }
    }
    // 覆盖保存时解析进程会重新打开文档：它没打开成功就不能再当「可编辑」用。
    if (!result.value(QStringLiteral("documentOpen")).toBool(true)) {
        open_ = false;
        setError(error, QStringLiteral("文件已经保存，但解析进程没能重新打开它："
                                       "请重新打开文档才能继续编辑。"));
        return false;
    }
    return true;
}

bool SafeDocumentController::documentStillOpen() const {
    return open_;
}

void SafeDocumentController::close() {
    if (client_ != nullptr && connected_) {
        // 先让解析进程释放文档句柄与临时图片；它回完执会自己退出。
        callSync(QStringLiteral("pdf.close"), {}, nullptr, nullptr);
    }
    shutdownWorker();
}

bool SafeDocumentController::isOpen() const {
    return open_;
}

bool SafeDocumentController::callSync(const QString& operation,
                                      const QJsonObject& payload,
                                      QJsonObject* result,
                                      QString* error) {
    if (client_ == nullptr || !connected_) {
        setError(error, QStringLiteral("隔离解析进程没有连上。"));
        return false;
    }

    bool handled = false;
    bool succeeded = false;
    QString failure;
    QJsonObject responsePayload;

    QEventLoop loop;
    const QString requestId = client_->sendRequest(operation, payload, kRequestTimeoutMs);

    // 请求刚发出就失败（socket 恰好断了）时，失败信号早于这里的监听器到达：
    // 常驻的 handleRequestFailed 已经把它记下来了，这里直接取用。
    if (pendingFailures_.contains(requestId)) {
        failure = pendingFailures_.take(requestId);
        setError(error, failure);
        return false;
    }

    const QMetaObject::Connection responseConnection = connect(
        client_, &ipc::WorkerClient::responseReceived, &loop,
        [&handled, &succeeded, &failure, &responsePayload, &requestId, &loop](
            const QString& id, const QJsonObject& response) {
            if (handled || (!id.isEmpty() && id != requestId)) {
                return;
            }
            handled = true;
            if (response.value(QStringLiteral("type")).toString() ==
                    QLatin1String("error") ||
                !response.value(QStringLiteral("ok")).toBool(true)) {
                failure = response.value(QStringLiteral("error"))
                              .toObject()
                              .value(QStringLiteral("message"))
                              .toString();
                if (failure.isEmpty()) {
                    failure = QStringLiteral("隔离解析进程返回了错误。");
                }
            } else {
                responsePayload = response.value(QStringLiteral("payload")).toObject();
                succeeded = true;
            }
            loop.quit();
        });

    const QMetaObject::Connection failureConnection = connect(
        client_, &ipc::WorkerClient::requestFailed, &loop,
        [&handled, &succeeded, &failure, &requestId, &loop](const QString& id,
                                                            const QString& message) {
            if (handled || (!id.isEmpty() && id != requestId)) {
                return;
            }
            handled = true;
            failure = message.isEmpty() ? QStringLiteral("隔离解析进程请求失败。") : message;
            loop.quit();
        });

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, [&handled, &failure, &loop]() {
        handled = true;
        failure = QStringLiteral("隔离解析进程超时未响应。");
        loop.quit();
    });
    timer.start(kEventLoopTimeoutMs);

    loop.exec();

    disconnect(responseConnection);
    disconnect(failureConnection);
    pendingFailures_.remove(requestId);

    if (!succeeded) {
        setError(error, failure.isEmpty() ? QStringLiteral("隔离解析进程没有返回结果。")
                                          : failure);
        return false;
    }
    if (result != nullptr) {
        *result = responsePayload;
    }
    return true;
}

bool SafeDocumentController::ensureWorkerReady(QString* error) {
    if (connected_) {
        return true;
    }
    if (client_ == nullptr) {
        setError(error, QStringLiteral("隔离解析进程没有启动。"));
        return false;
    }
    if (!workerExecutable_.isEmpty()) {
        client_->setWorkerExecutable(workerExecutable_);
    }

    QEventLoop loop;
    waitLoop_ = &loop;

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kConnectTimeoutMs);

    // start() 是幂等的：进程还在跑就直接返回，进程没了就重新拉起。
    client_->start();
    if (!connected_) {
        loop.exec();
    }
    waitLoop_ = nullptr;

    if (!connected_) {
        setError(error, QStringLiteral("隔离解析进程没有连上，无法打开文档。"));
        return false;
    }
    return true;
}

void SafeDocumentController::shutdownWorker() {
    if (client_ != nullptr) {
        // ~WorkerClient 会 terminate/kill 解析进程：不能留孤儿进程占着文件。
        delete client_;
        client_ = nullptr;
    }
    waitLoop_ = nullptr;
    connected_ = false;
    open_ = false;
    pageCount_ = 0;
    pages_ = QJsonArray();
    fields_ = QJsonArray();
    path_.clear();
    openedWithPassword_ = false;
    pendingFailures_.clear();
}

void SafeDocumentController::handleStateChanged(const QString& state) {
    connected_ = state == QLatin1String("ready");
    if (connected_ && waitLoop_ != nullptr) {
        waitLoop_->quit();
    }
}

void SafeDocumentController::handleRequestFailed(const QString& requestId,
                                                 const QString& message) {
    if (!requestId.isEmpty()) {
        pendingFailures_.insert(requestId, message);
    }
}

QString SafeDocumentController::defaultWorkerExecutable() {
#ifdef Q_OS_WIN
    constexpr auto workerName = "PdfParseWorker.exe";
#else
    constexpr auto workerName = "PdfParseWorker";
#endif

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        applicationDir,
        QDir(applicationDir).filePath(QStringLiteral("../worker")),
    };
    for (const QString& directory : candidates) {
        const QString candidate = QDir(directory).filePath(workerName);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QDir(applicationDir).filePath(workerName);
}

}  // namespace aipdf::app
