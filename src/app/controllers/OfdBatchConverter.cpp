#include "app/controllers/OfdBatchConverter.h"

#include "app/controllers/ExternalProcessTask.h"
#include "app/controllers/OfdConverters.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

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
    emit progress(index_, inputs_.size(), inputInfo.fileName());

    // 在**沙箱**里跑：转换器碰不到用户自己的目录。
    //
    // 转换器是用户自备的可执行文件，命令行约定各异，不沙箱就等于让它往用户
    // 目录里写文件（只收输入的那种会写同名 .pdf 覆盖掉用户那份）。
    // 沙箱里跑完，产物再由我们搬到挑好的目的地（那个名字保证不存在）。
    auto sandbox = std::make_unique<QTemporaryDir>();
    if (!sandbox->isValid()) {
        failures_.append(inputInfo.fileName());
        ++index_;
        startNext();
        return;
    }

    const OfdConversionSession session = prepareOfdConversion(converter_, input, sandbox->path());
    if (!session.isValid()) {
        failures_.append(inputInfo.fileName());
        ++index_;
        startNext();
        return;
    }

    currentTask_ = new ExternalProcessTask(this);
    connect(currentTask_, &ExternalProcessTask::finished, this,
            [this, input, session, sandbox = std::move(sandbox)](
                const ExternalProcessTask::Result& result) {
                currentTask_->deleteLater();
                currentTask_ = nullptr;
                const bool produced = QFileInfo(session.plan.expectedOutput).isFile() &&
                                      QFileInfo(session.plan.expectedOutput).size() > 0;
                if (!result.canceled && result.succeeded() && produced) {
                    // 产物在沙箱里，搬到用户指定的位置（挑的是不存在的名字，
                    // 落盘时再挑一次：期间被别人占了就换名，不覆盖也不删）。
                    if (copyOfdResultTo(session.plan.expectedOutput, session.destination).isEmpty()) {
                        failures_.append(QFileInfo(input).fileName());
                        ++index_;
                        startNext();
                        return;
                    }
                    ++converted_;
                } else if (!result.canceled) {
                    failures_.append(QFileInfo(input).fileName());
                }
                ++index_;
                startNext();
            });
    currentTask_->start(session.plan.program, session.plan.arguments, 120000);
}

}  // namespace aipdf::app
