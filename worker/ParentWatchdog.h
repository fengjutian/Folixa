#pragma once

#include <QObject>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace aipdf::worker {

/**
 * 让 AI worker 跟随启动它的编辑器进程一起退出。
 *
 * 编辑器被强制结束（任务管理器、断电、验收脚本 Kill）时，QProcess 的析构函数
 * 不会执行，WorkerClient 来不及 terminate/kill，worker 就成了永久孤儿进程：
 * 它继续占用内存，并且锁住 PdfAiWorker.exe，导致后续构建链接时报
 * "cannot open output file ... Permission denied"。
 *
 * 看门狗每秒检查一次父进程是否还活着，一旦父进程消失就退出自身。
 */
class ParentWatchdog final : public QObject {
    Q_OBJECT

public:
    explicit ParentWatchdog(QObject* parent = nullptr);
    ~ParentWatchdog() override;

    /**
     * @param parentPid 启动 worker 的进程 PID，非正数表示不监控
     *        （例如手工启动 worker 做调试）。
     * @param parentStartTimeTicks 父进程创建时间（Windows 100ns 刻度），用于
     *        识别 PID 复用；0 表示跳过该校验。
     */
    void start(qint64 parentPid, quint64 parentStartTimeTicks);

    /** start() 之后父进程是否已经不存在（worker 应当直接退出）。 */
    bool parentGone() const { return parentGone_; }

private:
    void checkParent();
    bool isParentAlive() const;

    QTimer timer_;
    qint64 watchedPid_ = 0;
    bool parentGone_ = false;
#ifdef Q_OS_WIN
    HANDLE parentHandle_ = nullptr;
#else
    qint64 posixParentPid_ = 0;
#endif
};

}  // namespace aipdf::worker
