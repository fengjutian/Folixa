#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace aipdf::app {

// Runs one external command without blocking or re-entering the UI event loop.
// Output is drained continuously and capped so verbose tools cannot deadlock the
// pipe or grow the editor process without bound.
class ExternalProcessTask final : public QObject {
    Q_OBJECT

public:
    struct Result {
        QProcess::ExitStatus exitStatus = QProcess::NormalExit;
        int exitCode = -1;
        QByteArray output;
        QString errorString;
        bool canceled = false;
        bool timedOut = false;

        [[nodiscard]] bool succeeded() const {
            return !canceled && !timedOut && errorString.isEmpty() &&
                   exitStatus == QProcess::NormalExit && exitCode == 0;
        }
    };

    explicit ExternalProcessTask(QObject* parent = nullptr);

    void start(const QString& program, const QStringList& arguments, int timeoutMs);
    void cancel();

signals:
    void finished(const aipdf::app::ExternalProcessTask::Result& result);

private:
    void appendOutput();
    void complete(QProcess::ExitStatus status, int exitCode);

    QProcess process_;
    QTimer timeout_;
    QByteArray output_;
    QString startError_;
    bool canceled_ = false;
    bool timedOut_ = false;
    bool completed_ = false;
};

}  // namespace aipdf::app

Q_DECLARE_METATYPE(aipdf::app::ExternalProcessTask::Result)
