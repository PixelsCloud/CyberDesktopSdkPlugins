#pragma once

#include <Windows.h>

#include <QThread>
#include <memory>

class QJsonObject;
class QString;

namespace cd::live2d {
// Creates and supervises one job-limited child on the same background thread.
// Windows requires debug event handling on the process-creation thread.
class WorkerProcess : public QThread {
public:
    WorkerProcess(QString program, QString endpoint, QString desktop, HANDLE job, QObject* parent);
    ~WorkerProcess() override;
    void stop();
    qint64 processId() const;
    QString error() const;
    QJsonObject diagnostics() const;

protected:
    void run() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cd::live2d
