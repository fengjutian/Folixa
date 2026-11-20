#include "app/controllers/ExternalProcessTask.h"

namespace aipdf::app {

namespace {
constexpr qsizetype kMaximumOutputBytes = 256 * 1024;
}

ExternalProcessTask::ExternalProcessTask(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    timeout_.setSingleShot(true);

    connect(&process_, &QProcess::readyRead, this, &ExternalProcessTask::appendOutput);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            startError_ = process_.errorString();
            complete(QProcess::CrashExit, -1);
        }
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
                complete(status, exitCode);
            });
    connect(&timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_.kill();
        if (process_.state() == QProcess::NotRunning) {
            complete(process_.exitStatus(), process_.exitCode());
        }
    });
}

void ExternalProcessTask::start(const QString& program, const QStringList& arguments,
                                int timeoutMs) {
    if (completed_ || process_.state() != QProcess::NotRunning) {
        return;
    }
    process_.start(program, arguments);
    if (timeoutMs > 0) {
        timeout_.start(timeoutMs);
    }
}

void ExternalProcessTask::cancel() {
    if (completed_) {
        return;
    }
    canceled_ = true;
    timeout_.stop();
    process_.kill();
    if (process_.state() == QProcess::NotRunning) {
        complete(process_.exitStatus(), process_.exitCode());
    }
}

void ExternalProcessTask::appendOutput() {
    output_.append(process_.readAll());
    if (output_.size() > kMaximumOutputBytes) {
        output_.remove(0, output_.size() - kMaximumOutputBytes);
    }
}

void ExternalProcessTask::complete(QProcess::ExitStatus status, int exitCode) {
    if (completed_) {
        return;
    }
    completed_ = true;
    timeout_.stop();
    appendOutput();

    Result result;
    result.exitStatus = status;
    result.exitCode = exitCode;
    result.output = output_;
    result.errorString = startError_;
    result.canceled = canceled_;
    result.timedOut = timedOut_;
    emit finished(result);
}

}  // namespace aipdf::app
