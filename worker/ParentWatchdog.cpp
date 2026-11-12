#include "ParentWatchdog.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace aipdf::worker {
namespace {

constexpr int kCheckIntervalMs = 1000;

#ifdef Q_OS_WIN
quint64 fileTimeToTicks(const FILETIME& fileTime) {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    return ticks.QuadPart;
}
#endif

}  // namespace

ParentWatchdog::ParentWatchdog(QObject* parent)
    : QObject(parent) {
    timer_.setInterval(kCheckIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &ParentWatchdog::checkParent);
}

ParentWatchdog::~ParentWatchdog() {
#ifdef Q_OS_WIN
    if (parentHandle_ != nullptr) {
        CloseHandle(parentHandle_);
        parentHandle_ = nullptr;
    }
#endif
}

void ParentWatchdog::start(qint64 parentPid, quint64 parentStartTimeTicks) {
    if (parentPid <= 0) {
        // 手工启动的 worker 没有父进程可跟随，保持常驻由使用者自行结束。
        return;
    }
    watchedPid_ = parentPid;

#ifdef Q_OS_WIN
    parentHandle_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE,
                                 static_cast<DWORD>(parentPid));
    if (parentHandle_ == nullptr) {
        const DWORD error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER) {
            // PID 不存在：父进程在 worker 起来之前就已经退出了。
            parentGone_ = true;
        }
        // 其余错误（例如权限不足）无法判断父进程状态，退化成不监控。
        return;
    }

    if (parentStartTimeTicks != 0) {
        FILETIME creation{};
        FILETIME exitTime{};
        FILETIME kernel{};
        FILETIME user{};
        if (GetProcessTimes(parentHandle_, &creation, &exitTime, &kernel, &user)) {
            if (fileTimeToTicks(creation) != parentStartTimeTicks) {
                // 同一个 PID 已经是另一个进程了，原父进程早已结束。
                parentGone_ = true;
                return;
            }
        }
    }
#else
    posixParentPid_ = parentPid;
#endif

    timer_.start();
}

void ParentWatchdog::checkParent() {
    if (!isParentAlive()) {
        QCoreApplication::exit(0);
    }
}

bool ParentWatchdog::isParentAlive() const {
#ifdef Q_OS_WIN
    if (parentHandle_ == nullptr) {
        return true;
    }
    // 进程句柄一旦被 signaled 就表示目标进程已终止，PID 复用不影响判断。
    return WaitForSingleObject(parentHandle_, 0) == WAIT_TIMEOUT;
#else
    if (posixParentPid_ <= 0) {
        return true;
    }
    // 父进程退出后子进程会被 init 收养，getppid() 随之改变。
    return ::getppid() == posixParentPid_;
#endif
}

}  // namespace aipdf::worker
