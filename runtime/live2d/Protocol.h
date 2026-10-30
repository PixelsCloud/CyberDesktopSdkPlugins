#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <functional>

namespace cd::live2d {
constexpr int kProtocolVersion = 1;
constexpr int kMaxPacket = 64 * 1024;
constexpr int kMaxSurfaceEdge = 4096;
constexpr quint64 kMaxSurfacePixels = 8ull * 1024 * 1024;

bool validSurfaceSize(int width, int height);

QByteArray encode(const QJsonObject& message);
bool consume(QByteArray& buffer, const std::function<void(const QJsonObject&)>& callback);
}  // namespace cd::live2d
