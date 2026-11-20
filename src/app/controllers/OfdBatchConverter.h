#pragma once

#include <QObject>
#include <QStringList>

namespace aipdf::app {

class ExternalProcessTask;

// Serializes OFD conversions so a batch cannot spawn an unbounded number of
// converter processes. UI code observes progress and owns no process details.
class OfdBatchConverter final : public QObject {
    Q_OBJECT

public:
    explicit OfdBatchConverter(QObject* parent = nullptr);

    void start(const QString& converter, const QStringList& inputs);
    void cancel();

signals:
    void progress(int completed, int total, const QString& inputFileName);
    void finished(int converted, const QStringList& failures, bool canceled);

private:
    void startNext();

    QString converter_;
    QStringList inputs_;
    QStringList failures_;
    int index_ = 0;
    int converted_ = 0;
    bool canceled_ = false;
    bool running_ = false;
    ExternalProcessTask* currentTask_ = nullptr;
};

}  // namespace aipdf::app
