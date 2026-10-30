#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QJsonObject;
class QSize;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace cd::live2d {
class Session : public QObject {
public:
    enum State { Empty, Loading, Ready, Failed };

    explicit Session(QObject* parent = nullptr);
    ~Session() override;
    void configure(const QString& root, const QJsonObject& options);
    void pointer(double x, double y, bool tap);
    bool performMotion(const QString& name, unsigned priority);
    ID3D11ShaderResourceView* texture(ID3D11Device* device, ID3D11DeviceContext* context, QSize size,
                                      bool* changed = nullptr);
    void stop();

    State state() const;

    QString error() const;

    QJsonObject information() const;

    quint64 frames() const;

    QSize outputSize() const;

    qint64 processId() const;

    double lastCpuMs() const;

    double lastGpuMs() const;

    double lastInputLatencyMs() const;

    quint64 measuredInputSequence() const;

    std::function<void()> onChanged;
    // Diagnostic harness selects a separately built fault worker. Production never sets these.
    QString workerOverride;
    QString injectedFault;
    int loadTimeoutMs = 10000;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cd::live2d
