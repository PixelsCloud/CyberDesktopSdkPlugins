#include "runtime/live2d/WorkerProcess.h"

#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QString>
#include <atomic>
#include <utility>

namespace cd::live2d {
struct WorkerProcess::Impl {
    using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
    QString program_, endpoint_, desktop_, error_;
    Handle job_{nullptr, &CloseHandle}, process_{nullptr, &CloseHandle};
    mutable QMutex mutex_;
    std::atomic<DWORD> pid_{0};
    std::atomic<DWORD> lastException_{0}, fatalExceptions_{0};
    std::atomic<DWORD> lastEvent_{0}, debugEvents_{0}, resumeResult_{DWORD(-1)};
    Impl(QString program, QString endpoint, QString desktop, HANDLE job);
    void terminate();
    qint64 processId() const;
    QString error() const;
    QJsonObject diagnostics() const;
    void run(WorkerProcess& owner);
};

WorkerProcess::Impl::Impl(QString program, QString endpoint, QString desktop, HANDLE job)
    : program_(std::move(program)), endpoint_(std::move(endpoint)), desktop_(std::move(desktop)) {
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        error_ = "Cannot retain model job";
    }
    job_.reset(duplicate);
}

void WorkerProcess::Impl::terminate() {
    QMutexLocker lock(&mutex_);
    if (process_) {
        TerminateProcess(process_.get(), 1);
    }
}

qint64 WorkerProcess::Impl::processId() const {
    return pid_.load();
}

QString WorkerProcess::Impl::error() const {
    QMutexLocker lock(&mutex_);
    return error_;
}

QJsonObject WorkerProcess::Impl::diagnostics() const {
    return {{"pid", double(pid_.load())},
            {"lastException", QString::number(lastException_.load(), 16)},
            {"fatalExceptions", double(fatalExceptions_.load())},
            {"lastDebugEvent", double(lastEvent_.load())},
            {"debugEvents", double(debugEvents_.load())},
            {"resumeResult", double(resumeResult_.load())}};
}

void WorkerProcess::Impl::run(WorkerProcess& owner) {
    if (!job_) {
        return;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = reinterpret_cast<LPWSTR>(desktop_.data());
    PROCESS_INFORMATION child{};
    auto command = QString("\"%1\" \"%2\"").arg(program_, endpoint_);
    // Exception supervision must not implicitly enable Windows' debug heap.
    // Its allocation checks make large motion JSONs orders of magnitude slower.
    // Set this only in the child's environment; workers start on background threads.
    auto variables = QProcessEnvironment::systemEnvironment();
    variables.insert("_NO_DEBUG_HEAP", "1");
    auto entries = variables.toStringList();
    entries.sort(Qt::CaseInsensitive);
    auto environment = entries.join(QChar(0));
    environment.append(QChar(0));
    environment.append(QChar(0));
    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | DEBUG_ONLY_THIS_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    const bool created =
        CreateProcessW(reinterpret_cast<LPCWSTR>(program_.utf16()), reinterpret_cast<LPWSTR>(command.data()), nullptr,
                       nullptr, FALSE, flags, environment.data(), nullptr, &startup, &child);
    if (!created) {
        QMutexLocker lock(&mutex_);
        error_ = QString("Cannot start model process (%1)").arg(GetLastError());
        return;
    }
    Handle thread(child.hThread, &CloseHandle);
    {
        QMutexLocker lock(&mutex_);
        process_.reset(child.hProcess);
        pid_ = child.dwProcessId;
        if (!AssignProcessToJobObject(job_.get(), child.hProcess) || owner.isInterruptionRequested()) {
            error_ = "Model process isolation failed or was cancelled";
            TerminateProcess(child.hProcess, 1);
        }
    }
    job_.reset();
    resumeResult_ = ResumeThread(thread.get());
    if (resumeResult_.load() == DWORD(-1)) {
        QMutexLocker lock(&mutex_);
        error_ = "Cannot resume isolated model process";
        TerminateProcess(child.hProcess, 1);
    }
    thread.reset();

    bool initialBreakpoint = false, exited = false;
    while (!exited) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, INFINITE)) {
            QMutexLocker lock(&mutex_);
            error_ = QString("Model exception supervision failed (%1)").arg(GetLastError());
            TerminateProcess(child.hProcess, 1);
            break;
        }
        DWORD status = DBG_CONTINUE;
        lastEvent_ = event.dwDebugEventCode;
        ++debugEvents_;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && event.u.CreateProcessInfo.hFile) {
            CloseHandle(event.u.CreateProcessInfo.hFile);
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile) {
            CloseHandle(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const auto code = event.u.Exception.ExceptionRecord.ExceptionCode;
            lastException_ = code;
            const bool first = event.u.Exception.dwFirstChance != 0;
            if (first && code == EXCEPTION_BREAKPOINT && !initialBreakpoint) {
                initialBreakpoint = true;
            } else {
                status = DBG_EXCEPTION_NOT_HANDLED;
                if (!first || code == 0xC0000602u || code == 0xC0000409u) {
                    ++fatalExceptions_;
                    {
                        QMutexLocker lock(&mutex_);
                        error_ = QString("Model process exception 0x%1").arg(code, 8, 16, QChar('0'));
                    }
                    TerminateProcess(child.hProcess, code);
                }
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            exited = true;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)) {
            QMutexLocker lock(&mutex_);
            error_ = "Cannot continue isolated model debug event";
            TerminateProcess(child.hProcess, 1);
            break;
        }
        // Windows closes debug process/thread handles on their continued exit
        // events. Only image/DLL file handles above belong to this event loop.
    }
    {
        QMutexLocker lock(&mutex_);
        process_.reset();
        pid_ = 0;
    }
}

WorkerProcess::WorkerProcess(QString program, QString endpoint, QString desktop, HANDLE job, QObject* parent)
    : QThread(parent),
      impl_(std::make_unique<Impl>(std::move(program), std::move(endpoint), std::move(desktop), job)) {}

WorkerProcess::~WorkerProcess() {
    stop();
    wait();
}

void WorkerProcess::stop() {
    requestInterruption();
    impl_->terminate();
}

qint64 WorkerProcess::processId() const {
    return impl_->processId();
}

QString WorkerProcess::error() const {
    return impl_->error();
}

QJsonObject WorkerProcess::diagnostics() const {
    return impl_->diagnostics();
}

void WorkerProcess::run() {
    impl_->run(*this);
}
}  // namespace cd::live2d
