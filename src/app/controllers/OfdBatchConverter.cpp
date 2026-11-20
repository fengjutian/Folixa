#include "app/controllers/OfdBatchConverter.h"

#include "app/controllers/ExternalProcessTask.h"

#include <QDir>
#include <QFileInfo>

namespace aipdf::app {

OfdBatchConverter::OfdBatchConverter(QObject* parent) : QObject(parent) {}

void OfdBatchConverter::start(const QString& converter, const QStringList& inputs) {
    if (running_) return;
    converter_ = converter;
    inputs_ = inputs;
    failures_.clear();
    index_ = 0;
    converted_ = 0;
    canceled_ = false;
    running_ = true;
    startNext();
}

void OfdBatchConverter::cancel() {
    if (!running_) return;
    canceled_ = true;
    if (currentTask_ != nullptr) currentTask_->cancel();
}

void OfdBatchConverter::startNext() {
    if (canceled_ || index_ >= inputs_.size()) {
        running_ = false;
        emit progress(inputs_.size(), inputs_.size(), QString());
        emit finished(converted_, failures_, canceled_);
        return;
    }

    const QString input = inputs_.at(index_);
    const QFileInfo inputInfo(input);
    QString output = inputInfo.dir().filePath(inputInfo.completeBaseName() +
                                              QStringLiteral(".pdf"));
    if (QFileInfo::exists(output)) {
        output = inputInfo.dir().filePath(inputInfo.completeBaseName() +
                                          QStringLiteral("-converted.pdf"));
    }
    emit progress(index_, inputs_.size(), inputInfo.fileName());

    currentTask_ = new ExternalProcessTask(this);
    connect(currentTask_, &ExternalProcessTask::finished, this,
            [this, input, output](const ExternalProcessTask::Result& result) {
                currentTask_->deleteLater();
                currentTask_ = nullptr;
                if (!result.canceled && result.succeeded() &&
                    QFileInfo(output).isFile() && QFileInfo(output).size() > 0) {
                    ++converted_;
                } else if (!result.canceled) {
                    failures_.append(QFileInfo(input).fileName());
                }
                ++index_;
                startNext();
            });
    currentTask_->start(converter_, {input, output}, 120000);
}

}  // namespace aipdf::app
