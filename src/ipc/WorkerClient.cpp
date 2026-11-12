#include "ipc/WorkerClient.h"

#include "ipc/IpcProtocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStringList>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace aipdf::ipc {
namespace {

/**
 * 当前进程的创建时间（Windows 100ns 刻度）。
 *
 * 传给 worker 用于识别 PID 复用：worker 拿到 --parent-pid 后会按 PID 打开父进程，
 * 如果这个 PID 已经被别的进程占用，说明原父进程早就没了，worker 应当立即退出。
 */
quint64 currentProcessStartTicks() {
#ifdef Q_OS_WIN
    FILETIME creation{};
    FILETIME exitTime{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        return 0;
    }
    ULARGE_INTEGER ticks{};
    ticks.LowPart = creation.dwLowDateTime;
    ticks.HighPart = creation.dwHighDateTime;
    return ticks.QuadPart;
#else
    return 0;
#endif
}

}  // namespace

WorkerClient::WorkerClient(QObject* parent)
    : QObject(parent),
      serverName_("aipdf-worker-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    connect(&socket_, &QLocalSocket::readyRead, this, &WorkerClient::readFrames);
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        connectionAttempts_ = 0;
        emit stateChanged("ready");
    });
    connect(&socket_, &QLocalSocket::errorOccurred,
            this, &WorkerClient::handleSocketError);
    connect(&workerProcess_, &QProcess::finished,
            this, &WorkerClient::handleWorkerFinished);
}

WorkerClient::~WorkerClient() {
    stopping_ = true;
    failPendingRequests("AI Worker is shutting down");
    socket_.abort();
    if (workerProcess_.state() != QProcess::NotRunning) {
        workerProcess_.terminate();
        if (!workerProcess_.waitForFinished(1500)) {
            workerProcess_.kill();
            workerProcess_.waitForFinished(1000);
        }
    }
}

void WorkerClient::start() {
    if (workerProcess_.state() != QProcess::NotRunning) {
        return;
    }

    emit stateChanged("starting");

    if (autoLaunchWorker_) {
        const QString executable = workerExecutablePath();
        if (!QFileInfo::exists(executable)) {
            emit stateChanged("unavailable");
            emit requestFailed({}, "AI Worker executable not found: " + executable);
            return;
        }

        workerProcess_.setProgram(executable);
        // 把父进程信息传给 worker：编辑器被强杀时 worker 靠自己退出，
        // 否则会成为孤儿进程一直锁住 PdfAiWorker.exe。
        workerProcess_.setArguments({"--server-name", serverName_,
                                     "--parent-pid", QString::number(QCoreApplication::applicationPid()),
                                     "--parent-start-time", QString::number(currentProcessStartTicks())});
        workerProcess_.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        workerProcess_.start();

        if (!workerProcess_.waitForStarted(3000)) {
            emit stateChanged("unavailable");
            emit requestFailed({}, "Unable to start PdfAiWorker: " + workerProcess_.errorString());
            return;
        }
    }

    connectionAttempts_ = 0;
    QTimer::singleShot(50, this, &WorkerClient::connectToWorker);
}

void WorkerClient::setServerName(const QString& serverName) {
    if (!serverName.isEmpty()) {
        serverName_ = serverName;
    }
}

void WorkerClient::setWorkerExecutable(const QString& executablePath) {
    workerExecutableOverride_ = executablePath;
}

void WorkerClient::setAutoLaunchWorker(bool enabled) {
    autoLaunchWorker_ = enabled;
}

QString WorkerClient::sendRequest(const QString& operation,
                                  const QJsonObject& payload,
                                  int timeoutMs) {
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (socket_.state() != QLocalSocket::ConnectedState) {
        emit requestFailed(requestId, "AI Worker is not connected");
        return requestId;
    }

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, requestId, timer] {
        pendingTimers_.remove(requestId);
        timer->deleteLater();
        emit requestFailed(requestId, "AI Worker request timed out");
    });
    pendingTimers_.insert(requestId, timer);
    timer->start(timeoutMs);

    socket_.write(encodeFrame(makeRequest(requestId, operation, payload)));
    return requestId;
}

void WorkerClient::connectToWorker() {
    if (stopping_ || socket_.state() == QLocalSocket::ConnectedState) {
        return;
    }
    if (connectionAttempts_++ >= 40) {
        emit stateChanged("unavailable");
        emit requestFailed({}, "Timed out connecting to PdfAiWorker");
        return;
    }
    socket_.abort();
    socket_.connectToServer(serverName_);
}

void WorkerClient::readFrames() {
    receiveBuffer_.append(socket_.readAll());
    if (receiveBuffer_.size() > kMaximumFrameBytes) {
        receiveBuffer_.clear();
        socket_.abort();
        failPendingRequests("AI Worker returned an oversized frame");
        return;
    }

    qsizetype newline = -1;
    while ((newline = receiveBuffer_.indexOf('\n')) >= 0) {
        const QByteArray frame = receiveBuffer_.left(newline);
        receiveBuffer_.remove(0, newline + 1);
        if (frame.isEmpty()) {
            continue;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            failPendingRequests("AI Worker returned invalid JSON");
            continue;
        }

        const QJsonObject response = document.object();
        const QString requestId = response.value("request_id").toString();
        if (QTimer* timer = pendingTimers_.take(requestId)) {
            timer->stop();
            timer->deleteLater();
        }
        emit responseReceived(requestId, response);
    }
}

void WorkerClient::handleSocketError(QLocalSocket::LocalSocketError error) {
    if (stopping_) {
        return;
    }
    if (error == QLocalSocket::ServerNotFoundError ||
        error == QLocalSocket::ConnectionRefusedError) {
        QTimer::singleShot(100, this, &WorkerClient::connectToWorker);
        return;
    }
    emit stateChanged("unavailable");
    failPendingRequests(socket_.errorString());
}

void WorkerClient::handleWorkerFinished(int exitCode, QProcess::ExitStatus status) {
    if (stopping_) {
        return;
    }
    emit stateChanged("unavailable");
    const QString reason = status == QProcess::CrashExit
        ? "AI Worker crashed"
        : QString("AI Worker exited with code %1").arg(exitCode);
    failPendingRequests(reason);
}

void WorkerClient::failPendingRequests(const QString& message) {
    const auto requestIds = pendingTimers_.keys();
    for (const QString& requestId : requestIds) {
        if (QTimer* timer = pendingTimers_.take(requestId)) {
            timer->stop();
            timer->deleteLater();
        }
        emit requestFailed(requestId, message);
    }
}

QString WorkerClient::workerExecutablePath() const {
    if (!workerExecutableOverride_.isEmpty()) {
        return workerExecutableOverride_;
    }

#ifdef Q_OS_WIN
    constexpr auto workerName = "PdfAiWorker.exe";
#else
    constexpr auto workerName = "PdfAiWorker";
#endif

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidateDirectories = {
        applicationDir,
        QDir(applicationDir).filePath(QStringLiteral("../worker")),
    };

    for (const QString& directory : candidateDirectories) {
        const QString candidate = QDir(directory).filePath(workerName);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir(applicationDir).filePath(workerName);
}

}  // namespace aipdf::ipc

